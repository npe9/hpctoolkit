// SPDX-FileCopyrightText: 2026 Contributors to the HPCToolkit Project
//
// SPDX-License-Identifier: BSD-3-Clause

// POWER: socket energy charged to calling contexts (2026-10-28).
//
//   hpcrun -e CPUTIME -e POWER[@MS] ...
//
// An MI300A is one socket of CPU cores, GPU and HBM behind one power controller; amdgpu
// reports its power in /sys/class/drm/cardN/device/hwmon/hwmonM/power1_input (uW, ~1 W
// resolution), readable without privileges -- unlike RAPL
// (power/energy-pkg), which is CPU-wide and denied at perf_event_paranoid 1. POWER rides
// on the time samples of another source (blame shift, like CXI): at most once per
// period one sampled thread reads the local sockets' power, integrates it over the time
// since the previous read (trapezoid) and charges the energy to that sample's calling
// context -- under Lithe, the context that was running. Statistical attribution of a
// socket-wide quantity: per context it says where the run was when the socket drew
// what; the process total is the socket's energy over the run (every process on the
// socket included).
//
// Environment:
//   HPCRUN_POWER_SOCKETS  comma list of DRM card numbers, or "all". Default: the cards
//                         whose device/local_cpulist overlaps the affinity mask.
//   POWER@MS              read period in ms (default 100). A read is a query to the
//                         socket's power controller: ~180 us mean, up to 0.7 ms,
//                         on the sampled hart (2.7 us only when read back to back;
//                         .build_logs/hpctk/ibs/pwr_gap.c), so ~0.2 % of one hart
//                         at 100 ms.
// Metric POWER:ENERGY (uJ); totals and average watts in power-<pid>.txt.

#define _GNU_SOURCE

#include <dirent.h>
#include <fcntl.h>
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

#include "../hpcrun_options.h"
#include "../files.h"
#include "../metrics.h"
#include "../sample_sources_registered.h"
#include "blame-shift/blame-shift.h"
#include "../cct2metrics.h"
#include "../thread_data.h"
#include "../messages/messages.h"
#include "../utilities/tokenize.h"

#define POWER_MAX_SOCKETS 16

static int fds[POWER_MAX_SOCKETS], n_fds;
static int cards[POWER_MAX_SOCKETS];
static int metric_id;
static uint64_t period_ns = 100 * 1000000ull;
static _Atomic uint64_t next_due_ns;
static atomic_flag reading = ATOMIC_FLAG_INIT;
static bool active;
static uint64_t prev_uw, prev_ns, t_first_ns, n_reads, read_ns_total;
static double energy_uj;
static bs_fn_entry_t bs_entry;

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static uint64_t read_uw(void) {
  uint64_t sum = 0;
  for (int i = 0; i < n_fds; i++) {
    char b[32];
    long n = syscall(SYS_pread64, fds[i], b, sizeof(b) - 1, 0);
    uint64_t v = 0;
    for (long j = 0; j < n && b[j] >= '0' && b[j] <= '9'; j++)
      v = v * 10 + (uint64_t)(b[j] - '0');
    sum += v;
  }
  return sum;
}

// Integrate up to now; returns the energy (uJ) since the previous read. Caller holds
// `reading`.
static double integrate(uint64_t t) {
  uint64_t uw = read_uw();
  double uj = (double)(uw + prev_uw) * 0.5 * (double)(t - prev_ns) * 1e-9;
  prev_uw = uw;
  prev_ns = t;
  energy_uj += uj;
  return uj;
}

static void power_blame(void* arg, int mid, cct_node_t* node, int metric_incr) {
  (void)arg;
  (void)metric_incr;
  if (!active || node == NULL)
    return;
  metric_desc_t* md = hpcrun_id2metric(mid);
  if (md == NULL || !(md->properties.time | md->properties.cycles))
    return;
  uint64_t t = now_ns();
  uint64_t due = atomic_load_explicit(&next_due_ns, memory_order_relaxed);
  if (t < due)
    return;
  if (!atomic_compare_exchange_strong(&next_due_ns, &due, t + period_ns))
    return;
  if (atomic_flag_test_and_set_explicit(&reading, memory_order_acquire))
    return;
  double uj = integrate(t);
  uint64_t t1 = now_ns();
  n_reads++;
  read_ns_total += t1 - t;
  atomic_flag_clear_explicit(&reading, memory_order_release);
  if (uj >= 1.0)
    cct_metric_data_increment(metric_id, node, (cct_metric_data_t){.i = (uint64_t)uj});
}

static bool cpulist_overlaps(const char* path, const cpu_set_t* mine) {
  char buf[256];
  FILE* f = fopen(path, "r");
  if (f == NULL)
    return true;
  bool hit = false;
  if (fgets(buf, sizeof(buf), f)) {
    for (char* t = strtok(buf, ",\n"); t && !hit; t = strtok(NULL, ",\n")) {
      int a, b;
      if (sscanf(t, "%d-%d", &a, &b) != 2)
        a = b = atoi(t);
      for (int c = a; c <= b && !hit; c++)
        hit = c < CPU_SETSIZE && CPU_ISSET(c, mine);
    }
  }
  fclose(f);
  return hit;
}

static void open_card(int card) {
  char dir[96];
  snprintf(dir, sizeof(dir), "/sys/class/drm/card%d/device/hwmon", card);
  DIR* d = opendir(dir);
  if (d == NULL)
    return;
  struct dirent* e;
  while ((e = readdir(d)) != NULL && n_fds < POWER_MAX_SOCKETS) {
    if (strncmp(e->d_name, "hwmon", 5) != 0)
      continue;
    char path[192];
    snprintf(path, sizeof(path), "%s/%s/power1_input", dir, e->d_name);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
      cards[n_fds] = card;
      fds[n_fds++] = fd;
      break;
    }
  }
  closedir(d);
}

static void open_sockets(void) {
  const char* e = getenv("HPCRUN_POWER_SOCKETS");
  if (e && *e && strcmp(e, "all") != 0) {
    char* s = strdup(e);
    for (char* t = strtok(s, ","); t; t = strtok(NULL, ","))
      open_card(atoi(t));
    free(s);
    return;
  }
  bool all = e && strcmp(e, "all") == 0;
  cpu_set_t mine;
  if (sched_getaffinity(0, sizeof(mine), &mine) != 0)
    all = true;
  DIR* d = opendir("/sys/class/drm");
  if (d == NULL)
    return;
  struct dirent* de;
  while ((de = readdir(d)) != NULL) {
    int card;
    char tail;
    if (sscanf(de->d_name, "card%d%c", &card, &tail) != 1)
      continue;  // card0, not card0-DP-1 or renderD128
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/local_cpulist", card);
    if (all || cpulist_overlaps(path, &mine))
      open_card(card);
  }
  closedir(d);
}

static void write_totals(void) {
  if (!active)
    return;
  active = false;
  while (atomic_flag_test_and_set_explicit(&reading, memory_order_acquire))
    ;
  uint64_t t = now_ns();
  integrate(t);
  char path[512];
  snprintf(path, sizeof(path), "%s/power-%d.txt", hpcrun_files_output_directory(), (int)getpid());
  FILE* f = fopen(path, "w");
  if (f == NULL)
    return;
  double s = (t - t_first_ns) * 1e-9;
  fprintf(f, "# POWER: socket power (amdgpu hwmon power1_input) integrated; %lu reads, %.1f us/read; cards",
          (unsigned long)n_reads, n_reads ? read_ns_total / 1e3 / n_reads : 0.0);
  for (int i = 0; i < n_fds; i++)
    fprintf(f, " %d", cards[i]);
  fprintf(f, "\nseconds   %12.3f\nenergy_J  %12.3f\navg_W     %12.1f\n", s, energy_uj * 1e-6,
          s > 0 ? energy_uj * 1e-6 / s : 0.0);
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
    STDERR_MSG("HPCToolkit: POWER needs a time or cycles sample source to ride on "
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
  return hpcrun_ev_is(ev_str, "POWER");
}

static void METHOD_FN(process_event_list) {
  char* evlist = METHOD_CALL(self, get_event_str);
  char* ev = strstr(evlist, "POWER");
  if (ev && ev[5] == '@') {
    long ms = atol(ev + 6);
    if (ms > 0)
      period_ns = (uint64_t)ms * 1000000ull;
  }
  open_sockets();
  if (n_fds == 0)
    EEMSG("HPCToolkit: POWER: no amdgpu power1_input under /sys/class/drm; POWER metrics stay zero");

  kind_info_t* kind = hpcrun_metrics_new_kind();
  metric_id = hpcrun_set_new_metric_info(kind, "POWER:ENERGY (uJ)");
  hpcrun_close_kind(kind);

  prev_uw = read_uw();
  prev_ns = t_first_ns = now_ns();
  atomic_store(&next_due_ns, t_first_ns + period_ns);

  bs_entry.fn = power_blame;
  bs_entry.next = 0;
  blame_shift_register(&bs_entry);
  active = n_fds > 0;
}

static void METHOD_FN(finalize_event_list) {}

static void METHOD_FN(gen_event_set) {}

static void METHOD_FN(display_events) {
  printf("===========================================================================\n");
  printf("Socket energy (MI300A: CPU + GPU + HBM)\n");
  printf("===========================================================================\n");
  printf("POWER[@ms] Socket power integrated between reads and charged to the sampled\n"
         "          context (needs CPUTIME, REALTIME or cycles). HPCRUN_POWER_SOCKETS=\n"
         "          card,...|all (default: cards local to the affinity mask).\n");
  printf("\n");
}

/***************************************************************************
 * object
 ***************************************************************************/

#define ss_name power
#define ss_cls SS_SOFTWARE

#include "ss_obj.h"
