// SPDX-FileCopyrightText: 2026 Contributors to the HPCToolkit Project
//
// SPDX-License-Identifier: BSD-3-Clause

// UNCORE: AMD L3 (per CCD) and data-fabric counters as hpcrun metrics (2026-10-27).
//
//   hpcrun -e CPUTIME -e UNCORE[@MS] ...
//
// Uncore PMUs count for a whole CCD (amd_l3) or socket (amd_df), not for a thread, so
// perf only opens them CPU-wide (pid -1, one cpu per unit), which needs
// perf_event_paranoid <= 0 or CAP_PERFMON. Like CXI (cxi.c) the counts ride on another
// source's time samples: at most one thread per period reads them and charges the
// deltas to that sample's calling context -- under Lithe, the running context.
// Counted units: the CCDs that hold at least one CPU of the process's affinity mask
// (other jobs' cores on those CCDs count too, as on any shared L3).
//
// Environment:
//   HPCRUN_UNCORE_EVENTS  comma list of NAME=PMU:CONFIG (CONFIG in hex), e.g.
//                         L3_MISS=amd_l3:0x0300c00000000104. Default: L3 lookups and
//                         L3 misses (Zen 4 PPR: event 0x04, umask 0xff / 0x01, all
//                         cores, all slices, both threads).
//   UNCORE@MS             read period in ms (default 50).
// The MI300A data fabric (amd_df) has no named events in this kernel; give raw configs.

#define _GNU_SOURCE

#include <errno.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "simple_oo.h"
#include "sample_source_obj.h"
#include "common.h"

#include "../files.h"
#include "../metrics.h"
#include "../sample_sources_registered.h"
#include "blame-shift/blame-shift.h"
#include "../cct2metrics.h"
#include "../thread_data.h"
#include "../messages/messages.h"
#include "../utilities/tokenize.h"

// amd_l3 config: event 0-7, umask 8-15, enallslices 46, enallcores 47, threadmask 56-57
#define L3_ALL ((1ull << 46) | (1ull << 47) | (3ull << 56))
static const char* uncore_default_events =
    "L3_LOOKUP=amd_l3:0x4,L3_MISS=amd_l3:0x104";  // L3_ALL is or-ed into amd_l3 configs

#define UNC_MAX_EVENTS 16
#define UNC_MAX_FDS 512

typedef struct {
  char name[48];
  char pmu[16];
  uint64_t config;
  int metric_id;
  uint64_t prev, total;
} unc_event_t;

static unc_event_t events[UNC_MAX_EVENTS];
static int n_events;
static struct {
  int fd;
  int ev;
} fds[UNC_MAX_FDS];
static int n_fds;

static uint64_t period_ns = 50 * 1000000ull;
static _Atomic uint64_t next_due_ns;
static atomic_flag reading = ATOMIC_FLAG_INIT;
static bool active;
static uint64_t n_reads, read_ns_total, t_first_ns;
static bs_fn_entry_t bs_entry;

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static int pmu_type(const char* pmu) {
  char path[128];
  snprintf(path, sizeof(path), "/sys/bus/event_source/devices/%s/type", pmu);
  FILE* f = fopen(path, "r");
  int t = -1;
  if (f) {
    if (fscanf(f, "%d", &t) != 1)
      t = -1;
    fclose(f);
  }
  return t;
}

// The PMU's cpumask: one cpu per counted unit. For amd_l3, keep units whose CCD
// (cpu .. next unit cpu - 1) overlaps our affinity.
static int pmu_cpus(const char* pmu, int* cpus, int max) {
  char path[128], buf[1024];
  snprintf(path, sizeof(path), "/sys/bus/event_source/devices/%s/cpumask", pmu);
  FILE* f = fopen(path, "r");
  if (f == NULL)
    return 0;
  int all[256], na = 0;
  if (fgets(buf, sizeof(buf), f)) {
    for (char* t = strtok(buf, ",\n"); t && na < 256; t = strtok(NULL, ",\n")) {
      int a, b;
      if (sscanf(t, "%d-%d", &a, &b) == 2)
        for (int c = a; c <= b && na < 256; c++)
          all[na++] = c;
      else
        all[na++] = atoi(t);
    }
  }
  fclose(f);
  cpu_set_t mine;
  if (sched_getaffinity(0, sizeof(mine), &mine) != 0)
    CPU_ZERO(&mine);
  int n = 0;
  for (int i = 0; i < na && n < max; i++) {
    int lo = all[i], hi = (i + 1 < na) ? all[i + 1] : lo + (na > 1 ? all[1] - all[0] : 1);
    bool keep = strcmp(pmu, "amd_l3") != 0;  // socket-wide PMUs: every unit
    for (int c = lo; c < hi && !keep; c++)
      keep = CPU_ISSET(c, &mine);
    if (keep)
      cpus[n++] = all[i];
  }
  return n;
}

static void open_event(int e) {
  int type = pmu_type(events[e].pmu);
  if (type < 0) {
    EEMSG("HPCToolkit: UNCORE: no PMU %s", events[e].pmu);
    return;
  }
  int cpus[256];
  int nc = pmu_cpus(events[e].pmu, cpus, 256);
  for (int i = 0; i < nc && n_fds < UNC_MAX_FDS; i++) {
    struct perf_event_attr a;
    memset(&a, 0, sizeof(a));
    a.size = sizeof(a);
    a.type = type;
    a.config = events[e].config;
    int fd = syscall(SYS_perf_event_open, &a, -1, cpus[i], -1, PERF_FLAG_FD_CLOEXEC);
    if (fd < 0) {
      if (i == 0)
        EEMSG("HPCToolkit: UNCORE: %s on cpu %d: %s (CPU-wide events need "
              "perf_event_paranoid <= 0 or CAP_PERFMON)",
              events[e].name, cpus[i], strerror(errno));
      return;
    }
    fds[n_fds].fd = fd;
    fds[n_fds++].ev = e;
  }
}

static void read_all(uint64_t* sums) {
  for (int e = 0; e < n_events; e++)
    sums[e] = 0;
  for (int i = 0; i < n_fds; i++) {
    uint64_t v = 0;
    if (syscall(SYS_read, fds[i].fd, &v, sizeof(v)) == sizeof(v))
      sums[fds[i].ev] += v;
  }
}

static void unc_blame(void* arg, int metric_id, cct_node_t* node, int metric_incr) {
  (void)arg;
  (void)metric_incr;
  if (!active || node == NULL)
    return;
  metric_desc_t* md = hpcrun_id2metric(metric_id);
  if (md == NULL || !(md->properties.time | md->properties.cycles))
    return;
  uint64_t t = now_ns();
  uint64_t due = atomic_load_explicit(&next_due_ns, memory_order_relaxed);
  if (t < due || !atomic_compare_exchange_strong(&next_due_ns, &due, t + period_ns))
    return;
  if (atomic_flag_test_and_set_explicit(&reading, memory_order_acquire))
    return;
  uint64_t sums[UNC_MAX_EVENTS];
  read_all(sums);
  for (int e = 0; e < n_events; e++) {
    uint64_t d = sums[e] >= events[e].prev ? sums[e] - events[e].prev : 0;
    events[e].prev = sums[e];
    if (d) {
      events[e].total += d;
      cct_metric_data_increment(events[e].metric_id, node, (cct_metric_data_t){.i = d});
    }
  }
  n_reads++;
  read_ns_total += now_ns() - t;
  atomic_flag_clear_explicit(&reading, memory_order_release);
}

static void write_totals(void) {
  if (!active)
    return;
  active = false;
  while (atomic_flag_test_and_set_explicit(&reading, memory_order_acquire))
    ;
  uint64_t sums[UNC_MAX_EVENTS];
  read_all(sums);
  for (int e = 0; e < n_events; e++)
    if (sums[e] >= events[e].prev)
      events[e].total += sums[e] - events[e].prev;
  char path[512];
  snprintf(path, sizeof(path), "%s/uncore-%d.txt", hpcrun_files_output_directory(), (int)getpid());
  FILE* f = fopen(path, "w");
  if (f == NULL)
    return;
  fprintf(f, "# uncore totals over the counted units; %.3f s; %lu reads, %.1f us/read; %d fds\n",
          (now_ns() - t_first_ns) * 1e-9, (unsigned long)n_reads,
          n_reads ? read_ns_total / 1e3 / n_reads : 0.0, n_fds);
  for (int e = 0; e < n_events; e++)
    fprintf(f, "%-24s %-8s 0x%016lx %20lu\n", events[e].name, events[e].pmu,
            (unsigned long)events[e].config, (unsigned long)events[e].total);
  fclose(f);
}

static void METHOD_FN(init) {
  self->state = INIT;
}
static void METHOD_FN(thread_init) {}
static void METHOD_FN(thread_init_action) {}
static void METHOD_FN(start) {
  if (!blame_shift_source_available(bs_type_timer) &&
      !blame_shift_source_available(bs_type_cycles)) {
    STDERR_MSG("HPCToolkit: UNCORE needs a time or cycles sample source to ride on.");
    active = false;
  }
}
static void METHOD_FN(thread_fini_action) {}
static void METHOD_FN(stop) {}
static void METHOD_FN(shutdown) {
  write_totals();
  self->state = UNINIT;
}

static bool METHOD_FN(supports_event, const char* ev_str) {
  return hpcrun_ev_is(ev_str, "UNCORE");
}

static void METHOD_FN(process_event_list) {
  char* evlist = METHOD_CALL(self, get_event_str);
  char* ev = strstr(evlist, "UNCORE");
  if (ev && ev[6] == '@') {
    long ms = atol(ev + 7);
    if (ms > 0)
      period_ns = (uint64_t)ms * 1000000ull;
  }
  const char* list = getenv("HPCRUN_UNCORE_EVENTS");
  char* s = strdup(list && *list ? list : uncore_default_events);
  for (char* t = strtok(s, ","); t && n_events < UNC_MAX_EVENTS; t = strtok(NULL, ",")) {
    unc_event_t* u = &events[n_events];
    char* eq = strchr(t, '=');
    char* colon = eq ? strchr(eq, ':') : NULL;
    if (!eq || !colon) {
      EEMSG("HPCToolkit: UNCORE: bad event '%s' (want NAME=PMU:CONFIG)", t);
      continue;
    }
    *eq = *colon = 0;
    snprintf(u->name, sizeof(u->name), "%s", t);
    snprintf(u->pmu, sizeof(u->pmu), "%s", eq + 1);
    u->config = strtoull(colon + 1, NULL, 16);
    if (strcmp(u->pmu, "amd_l3") == 0)
      u->config |= L3_ALL;
    n_events++;
  }
  free(s);

  for (int e = 0; e < n_events; e++)
    open_event(e);

  kind_info_t* kind = hpcrun_metrics_new_kind();
  for (int e = 0; e < n_events; e++) {
    char m[64];
    snprintf(m, sizeof(m), "UNCORE:%s", events[e].name);
    events[e].metric_id = hpcrun_set_new_metric_info(kind, strdup(m));
  }
  hpcrun_close_kind(kind);

  uint64_t sums[UNC_MAX_EVENTS];
  read_all(sums);
  for (int e = 0; e < n_events; e++)
    events[e].prev = sums[e];
  t_first_ns = now_ns();
  atomic_store(&next_due_ns, t_first_ns + period_ns);
  bs_entry.fn = unc_blame;
  bs_entry.next = 0;
  blame_shift_register(&bs_entry);
  active = n_fds > 0;
}

static void METHOD_FN(finalize_event_list) {}
static void METHOD_FN(gen_event_set) {}
static void METHOD_FN(display_events) {
  printf("===========================================================================\n");
  printf("AMD uncore counters (CPU-wide; needs perf_event_paranoid <= 0)\n");
  printf("===========================================================================\n");
  printf("UNCORE[@ms]  L3/DF deltas charged to the sampled context. HPCRUN_UNCORE_EVENTS=\n"
         "             NAME=PMU:CONFIG,...  default: %s\n\n", uncore_default_events);
}

#define ss_name uncore
#define ss_cls SS_SOFTWARE

#include "ss_obj.h"
