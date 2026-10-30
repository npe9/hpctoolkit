// SPDX-FileCopyrightText: 2026 Contributors to the HPCToolkit Project
//
// SPDX-License-Identifier: BSD-3-Clause

// FREQ: effective core clock per calling context, from APERF/MPERF (2026-10-28).
//
//   hpcrun -e CPUTIME -e FREQ ...
//
// APERF counts at the actual core clock, MPERF at the nominal (P0) clock, both only
// while the core is in C0; APERF/MPERF x nominal is the clock the code really ran at
// (boost, power capping, thermal throttling). The kernel's `msr` PMU exposes both as
// per-thread counting events, open at perf_event_paranoid 1, unlike RAPL, IBS and the
// uncore PMUs (CPU-wide only). They cannot interrupt, so FREQ rides on the time samples
// of another source (blame shift, like CXI): on each CPUTIME/REALTIME/cycles sample the
// sampled OS thread reads its two counters and charges the deltas since its last read
// to the sample's calling context -- under Lithe, the context that was running on the
// hart (lithe/lithe-tool.c). Statistical, like the time metric it rides on.
//
// Counters are opened lazily, by each OS thread at its first sample (a raw syscall, in
// the handler): hart threads, and only those, that run sampled code pay for them. Their
// state is kept by kernel thread id, not __thread: under parlib a hart's TLS is the
// running context's.
//
// Metrics FREQ:APERF and FREQ:MPERF (cycles); the effective clock of a scope is
// APERF/MPERF x nominal. Totals to freq-<pid>.txt in the measurement directory.

#define _GNU_SOURCE

#include <linux/perf_event.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "simple_oo.h"
#include "sample_source_obj.h"
#include "common.h"

#include "../hpcrun_options.h"
#include "../files.h"
#include "../metrics.h"
#include "../sample_sources_registered.h"
#include "blame-shift/blame-shift.h"
#include "../cct2metrics.h"
#include "../thread_data.h"
#include "../messages/messages.h"
#include "../utilities/tokenize.h"

#define FREQ_SLOTS 4096  // OS threads that took a sample; probe by tid

typedef struct {
  _Atomic int tid;  // 0 free, else owner; never released
  int fd[2];        // APERF, MPERF; -1 when the open failed
  uint64_t prev[2];
} freq_slot_t;

static freq_slot_t slots[FREQ_SLOTS];
static int msr_type = -1;
static const unsigned long long msr_config[2] = {0x01, 0x02};  // msr PMU: aperf, mperf
static const char* msr_name[2] = {"APERF", "MPERF"};
static int metric_id[2];
static _Atomic uint64_t total[2];
static atomic_int n_threads, n_open_fail, n_slot_full;
static bool active;
static bs_fn_entry_t bs_entry;

static uint64_t read_fd(int fd) {
  uint64_t v = 0;
  if (syscall(SYS_read, fd, &v, sizeof(v)) != (long)sizeof(v))
    return 0;
  return v;
}

static int open_counter(int i) {
  struct perf_event_attr a;
  memset(&a, 0, sizeof(a));
  a.size = sizeof(a);
  a.type = msr_type;
  a.config = msr_config[i];
  return (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

// The calling OS thread's slot, claimed and opened on first use. Async-signal-safe.
static freq_slot_t* my_slot(void) {
  int tid = (int)syscall(SYS_gettid);
  unsigned h = ((unsigned)tid * 2654435761u) & (FREQ_SLOTS - 1);
  for (unsigned i = 0; i < FREQ_SLOTS; i++, h = (h + 1) & (FREQ_SLOTS - 1)) {
    freq_slot_t* s = &slots[h];
    int t = atomic_load_explicit(&s->tid, memory_order_acquire);
    if (t == tid)
      return s;
    if (t != 0)
      continue;
    // Claim with -1 while opening, so nobody else reads a half-made slot.
    if (!atomic_compare_exchange_strong(&s->tid, &t, -1))
      continue;
    for (int k = 0; k < 2; k++) {
      s->fd[k] = open_counter(k);
      s->prev[k] = s->fd[k] >= 0 ? read_fd(s->fd[k]) : 0;
      if (s->fd[k] < 0)
        atomic_fetch_add(&n_open_fail, 1);
    }
    atomic_fetch_add(&n_threads, 1);
    atomic_store_explicit(&s->tid, tid, memory_order_release);
    return NULL;  // baseline only: the first delta would cover the time before it
  }
  atomic_fetch_add(&n_slot_full, 1);
  return NULL;
}

static void freq_blame(void* arg, int mid, cct_node_t* node, int metric_incr) {
  (void)arg;
  (void)metric_incr;
  if (!active || node == NULL)
    return;
  metric_desc_t* md = hpcrun_id2metric(mid);
  if (md == NULL || !(md->properties.time | md->properties.cycles))
    return;
  freq_slot_t* s = my_slot();
  if (s == NULL)
    return;
  for (int k = 0; k < 2; k++) {
    if (s->fd[k] < 0)
      continue;
    uint64_t v = read_fd(s->fd[k]);
    uint64_t d = v >= s->prev[k] ? v - s->prev[k] : 0;
    s->prev[k] = v;
    if (d) {
      atomic_fetch_add_explicit(&total[k], d, memory_order_relaxed);
      cct_metric_data_increment(metric_id[k], node, (cct_metric_data_t){.i = d});
    }
  }
}

static long read_khz(const char* path) {
  FILE* f = fopen(path, "r");
  long best = 0, v;
  if (f == NULL)
    return 0;
  while (fscanf(f, "%ld", &v) == 1)
    if (v > best)
      best = v;
  fclose(f);
  return best;
}

static void write_totals(void) {
  if (!active)
    return;
  active = false;
  char path[512];
  snprintf(path, sizeof(path), "%s/freq-%d.txt", hpcrun_files_output_directory(), (int)getpid());
  FILE* f = fopen(path, "w");
  if (f == NULL)
    return;
  uint64_t a = atomic_load(&total[0]), m = atomic_load(&total[1]);
  // acpi-cpufreq lists P-states; with boost on, the top one is P0 (what MPERF counts).
  long p0 = read_khz("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies");
  fprintf(f, "# FREQ: APERF/MPERF over sampled threads; %d threads, %d open failures, %d without slot\n",
          atomic_load(&n_threads), atomic_load(&n_open_fail), atomic_load(&n_slot_full));
  fprintf(f, "APERF %20lu\nMPERF %20lu\nratio %20.4f\n", (unsigned long)a, (unsigned long)m,
          m ? (double)a / m : 0.0);
  if (p0 > 0 && m)
    fprintf(f, "nominal_MHz %14.0f\neffective_MHz %12.0f\n", p0 / 1e3, p0 / 1e3 * a / m);
  fclose(f);
}

/******************************************************************************
 * sample source interface
 *****************************************************************************/

static void METHOD_FN(init) {
  self->state = INIT;
}

static void METHOD_FN(thread_init) {}

static void METHOD_FN(thread_init_action) {}

static void METHOD_FN(start) {
  if (!blame_shift_source_available(bs_type_timer) &&
      !blame_shift_source_available(bs_type_cycles)) {
    STDERR_MSG("HPCToolkit: FREQ needs a time or cycles sample source to ride on "
               "(CPUTIME, REALTIME, or cycles).");
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
  return hpcrun_ev_is(ev_str, "FREQ");
}

static void METHOD_FN(process_event_list) {
  FILE* f = fopen("/sys/bus/event_source/devices/msr/type", "r");
  if (f == NULL || fscanf(f, "%d", &msr_type) != 1)
    msr_type = -1;
  if (f)
    fclose(f);
  if (msr_type < 0) {
    EEMSG("HPCToolkit: FREQ: no msr PMU (/sys/bus/event_source/devices/msr); FREQ metrics stay zero");
  } else {
    int fd = open_counter(0);  // fail now, with a message, rather than per thread
    if (fd < 0) {
      EEMSG("HPCToolkit: FREQ: cannot open the msr aperf counter; FREQ metrics stay zero");
      msr_type = -1;
    } else {
      close(fd);
    }
  }

  kind_info_t* kind = hpcrun_metrics_new_kind();
  for (int k = 0; k < 2; k++) {
    char mname[32];
    snprintf(mname, sizeof(mname), "FREQ:%s", msr_name[k]);
    metric_id[k] = hpcrun_set_new_metric_info(kind, strdup(mname));
  }
  hpcrun_close_kind(kind);

  bs_entry.fn = freq_blame;
  bs_entry.next = 0;
  blame_shift_register(&bs_entry);
  active = msr_type >= 0;
}

static void METHOD_FN(finalize_event_list) {}

static void METHOD_FN(gen_event_set) {}

static void METHOD_FN(display_events) {
  printf("===========================================================================\n");
  printf("Effective core clock (APERF/MPERF)\n");
  printf("===========================================================================\n");
  printf("FREQ      APERF and MPERF deltas of the sampled OS thread, charged to the\n"
         "          sampled context (needs CPUTIME, REALTIME or cycles). Effective\n"
         "          clock = APERF/MPERF x nominal; totals in freq-<pid>.txt.\n");
  printf("\n");
}

/***************************************************************************
 * object
 ***************************************************************************/

#define ss_name freq
#define ss_cls SS_SOFTWARE

#include "ss_obj.h"
