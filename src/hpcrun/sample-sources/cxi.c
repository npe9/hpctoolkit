// SPDX-FileCopyrightText: 2026 Contributors to the HPCToolkit Project
//
// SPDX-License-Identifier: BSD-3-Clause

// CXI: HPE Slingshot (Cassini NIC) counters as hpcrun metrics (2026-10-27).
//
//   hpcrun -e CPUTIME -e CXI[@MS] ...
//
// The Cassini NIC exports ~1900 counters per device in
// /sys/class/cxi/cxiN/device/telemetry/<name> as "value@sec.nsec". They are NIC-wide
// (every process and endpoint on that NIC), so there is no overflow interrupt to sample
// on. Instead CXI rides on the time samples of another source (blame shift, like IDLE):
// on a CPUTIME/REALTIME/cycles sample, at most one thread per period (claimed by CAS)
// reads the selected counters and charges each delta to that sample's calling context.
// Under Lithe (lithe/lithe-tool.c) that context belongs to the Lithe context that was
// running, so NIC activity lands on the logical rank whose sample saw it: statistical
// attribution, the same model as uncore counters. Process totals for the whole run go
// to cxi-<pid>.txt in the measurement directory with the measured read cost.
//
// Why sysfs and not libcxi: cxil_read_n_cntrs costs ~560 us per call on Tuolumne (it
// asks the driver for a fresh counter DMA and waits); a sysfs pread at offset 0 is a
// fresh value in ~3.3 us per counter (.build_logs/hpctk/cxi/cxi_bench.c).
//
// Environment:
//   HPCRUN_CXI_COUNTERS  comma list of telemetry names; "name_*" sums the indexed family
//                        (per traffic class / lane). Default: the set below.
//   HPCRUN_CXI_DEVICES   comma list of device numbers, or "all". Default: the NICs whose
//                        local_cpulist overlaps the process's affinity (on Tuolumne,
//                        one NIC per MI300A socket). Counters are summed over them.
//   CXI@MS               read period in ms (default 50). Cost per read is about
//                        3.3 us x files x devices, on whichever hart took the sample.

#define _GNU_SOURCE

#include <dirent.h>
#include <sched.h>
#include <fcntl.h>
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
#include "../libc-functions.h"
#include "../metrics.h"
#include "../sample_sources_registered.h"
#include "blame-shift/blame-shift.h"
#include "../cct2metrics.h"
#include "../thread_data.h"
#include "../messages/messages.h"
#include "../utilities/tokenize.h"

// The default set: what an MPI performance question about the NIC needs first.
static const char* cxi_default_counters =
    // traffic
    "hni_pkts_sent_by_tc_*,hni_pkts_recv_by_tc_*,cq_success_tx_cntr,ixe_rx_ptl_msgs,"
    // matching: puts, puts that found no posted receive (unexpected), rendezvous
    "lpe_put_cmds,lpe_event_put_overflow,lpe_rendezvous_put_cmds,"
    // congestion and back-pressure
    "hni_pause_recv_*,hni_pause_xoff_sent_*,cq_cycles_blocked_*,mst_stalled_waiting_for_lpe,"
    // reliability: retries, timeouts, NACKs, discards
    "pct_retry_srb_requests,pct_sct_timeouts,pct_tct_timeouts,pct_no_tct_nacks,hni_discard_cntr_*,"
    // NIC address translation (the MR cache's hardware side)
    "atu_cache_miss_oxe,atu_cache_miss_ixe,"
    // host interface: events written to EQs, DMA writes to host memory, PCIe writes/reads
    "ee_eqs_event_req_cntr,ixe_disp_dmawr_reqs,pi_pti_tarb_mwr_pkts,pi_pti_tarb_mrd_pkts";

#define CXI_MAX_COUNTERS 64
#define CXI_MAX_FILES 1024
#define CXI_MAX_DEVICES 16
#define CXI_TELEMETRY "/sys/class/cxi/cxi%d/device/telemetry"

typedef struct {
  char name[64];
  int metric_id;
  uint64_t prev;   // last summed value
  uint64_t total;  // sum of deltas since the first read
} cxi_counter_t;

static cxi_counter_t counters[CXI_MAX_COUNTERS];
static int n_counters;
static struct {
  int fd;
  int counter;
} files[CXI_MAX_FILES];
static int n_files;

static uint64_t period_ns = 50 * 1000000ull;
static _Atomic uint64_t next_due_ns;
static atomic_flag reading = ATOMIC_FLAG_INIT;
static bool active;
static uint64_t n_reads, read_ns_total, t_first_ns, t_last_ns;
static bs_fn_entry_t bs_entry;

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

// "value@sec.nsec" -> value. Async-signal-safe (raw syscall: hpcrun's IO source
// interposes read/write, and libc pread may be someone else's).
static uint64_t read_file(int fd) {
  char b[64];
  long n = syscall(SYS_pread64, fd, b, sizeof(b) - 1, 0);
  uint64_t v = 0;
  for (long i = 0; i < n && b[i] >= '0' && b[i] <= '9'; i++)
    v = v * 10 + (uint64_t)(b[i] - '0');
  return v;
}

// Read every counter into sums[]; one pass over the open files.
static void read_all(uint64_t* sums) {
  for (int c = 0; c < n_counters; c++)
    sums[c] = 0;
  for (int f = 0; f < n_files; f++)
    sums[files[f].counter] += read_file(files[f].fd);
}

static void open_counter_files(int c, int dev) {
  char dir[96];
  snprintf(dir, sizeof(dir), CXI_TELEMETRY, dev);
  const char* name = counters[c].name;
  size_t len = strlen(name);
  bool family = len > 1 && name[len - 1] == '*';
  if (!family) {
    char path[192];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0 && n_files < CXI_MAX_FILES) {
      files[n_files].fd = fd;
      files[n_files++].counter = c;
    } else if (fd < 0) {
      EEMSG("HPCToolkit: CXI: no counter %s on cxi%d", name, dev);
    }
    return;
  }
  // name_* : every name_<digits>
  DIR* d = opendir(dir);
  if (d == NULL)
    return;
  struct dirent* e;
  while ((e = readdir(d)) != NULL) {
    if (strncmp(e->d_name, name, len - 1) != 0)
      continue;
    const char* p = e->d_name + len - 1;
    if (*p == 0 || strspn(p, "0123456789") != strlen(p))
      continue;
    char path[192];
    snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0 && n_files < CXI_MAX_FILES) {
      files[n_files].fd = fd;
      files[n_files++].counter = c;
    }
  }
  closedir(d);
}

static int list_devices(int* devs) {
  int n = 0;
  const char* e = getenv("HPCRUN_CXI_DEVICES");
  if (e && *e && strcmp(e, "all") != 0) {
    char* s = strdup(e);
    for (char* t = strtok(s, ","); t && n < CXI_MAX_DEVICES; t = strtok(NULL, ","))
      devs[n++] = atoi(t);
    free(s);
    return n;
  }
  // Default: the NICs local to our CPUs (device/local_cpulist overlaps the affinity
  // mask) -- the one CXI picks for us, at a quarter of the read cost on a 4-NIC node.
  // HPCRUN_CXI_DEVICES=all reads every NIC.
  bool all = e && strcmp(e, "all") == 0;
  cpu_set_t mine;
  if (sched_getaffinity(0, sizeof(mine), &mine) != 0)
    all = true;
  int present[CXI_MAX_DEVICES], np = 0;
  for (int d = 0; d < CXI_MAX_DEVICES; d++) {
    char dir[96];
    snprintf(dir, sizeof(dir), CXI_TELEMETRY, d);
    if (access(dir, R_OK) != 0)
      continue;
    present[np++] = d;
    char path[128], buf[256];
    snprintf(path, sizeof(path), "/sys/class/cxi/cxi%d/device/local_cpulist", d);
    FILE* f = fopen(path, "r");
    bool local = all || f == NULL;
    if (f && fgets(buf, sizeof(buf), f)) {
      for (char* t = strtok(buf, ",\n"); t && !local; t = strtok(NULL, ",\n")) {
        int a, b;
        if (sscanf(t, "%d-%d", &a, &b) != 2)
          a = b = atoi(t);
        for (int c = a; c <= b && !local; c++)
          local = c < CPU_SETSIZE && CPU_ISSET(c, &mine);
      }
    }
    if (f)
      fclose(f);
    if (local)
      devs[n++] = d;
  }
  if (n == 0) {  // no locality information matched: all of them
    for (int i = 0; i < np; i++)
      devs[n++] = present[i];
  }
  return n;
}

static void cxi_blame(void* arg, int metric_id, cct_node_t* node, int metric_incr) {
  (void)arg;
  (void)metric_incr;
  if (!active || node == NULL)
    return;
  // Only time and cycle samples drive reads: one trigger stream, whatever else is on.
  metric_desc_t* md = hpcrun_id2metric(metric_id);
  if (md == NULL || !(md->properties.time | md->properties.cycles))
    return;
  uint64_t t = now_ns();
  uint64_t due = atomic_load_explicit(&next_due_ns, memory_order_relaxed);
  if (t < due)
    return;
  if (!atomic_compare_exchange_strong(&next_due_ns, &due, t + period_ns))
    return;  // another thread took this period
  if (atomic_flag_test_and_set_explicit(&reading, memory_order_acquire))
    return;  // a slow read is still in progress
  uint64_t sums[CXI_MAX_COUNTERS];
  read_all(sums);
  for (int c = 0; c < n_counters; c++) {
    uint64_t d = sums[c] >= counters[c].prev ? sums[c] - counters[c].prev : 0;
    counters[c].prev = sums[c];
    if (d) {
      counters[c].total += d;
      cct_metric_data_increment(counters[c].metric_id, node, (cct_metric_data_t){.i = d});
    }
  }
  uint64_t t1 = now_ns();
  n_reads++;
  read_ns_total += t1 - t;
  t_last_ns = t1;
  atomic_flag_clear_explicit(&reading, memory_order_release);
}

static void write_totals(void) {
  if (!active || n_counters == 0)
    return;
  active = false;
  while (atomic_flag_test_and_set_explicit(&reading, memory_order_acquire))
    ;  // a sample on another hart is mid-read; it is a few hundred us
  // Final read so the totals cover the run, not just up to the last sample.
  uint64_t sums[CXI_MAX_COUNTERS];
  read_all(sums);
  for (int c = 0; c < n_counters; c++)
    if (sums[c] >= counters[c].prev)
      counters[c].total += sums[c] - counters[c].prev;
  t_last_ns = now_ns();
  char path[512];
  snprintf(path, sizeof(path), "%s/cxi-%d.txt", hpcrun_files_output_directory(), (int)getpid());
  FILE* f = fopen(path, "w");
  if (f == NULL)
    return;
  fprintf(f, "# CXI (Slingshot) counter totals, summed over devices; %.3f s; %lu reads, %.1f us/read; %d files\n",
          (t_last_ns - t_first_ns) * 1e-9, (unsigned long)n_reads,
          n_reads ? read_ns_total / 1e3 / n_reads : 0.0, n_files);
  for (int c = 0; c < n_counters; c++)
    fprintf(f, "%-32s %20lu\n", counters[c].name, (unsigned long)counters[c].total);
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
    STDERR_MSG("HPCToolkit: CXI needs a time or cycles sample source to ride on "
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
  return hpcrun_ev_is(ev_str, "CXI");
}

static void METHOD_FN(process_event_list) {
  char* evlist = METHOD_CALL(self, get_event_str);
  char* ev = strstr(evlist, "CXI");
  if (ev && ev[3] == '@') {
    long ms = atol(ev + 4);
    if (ms > 0)
      period_ns = (uint64_t)ms * 1000000ull;
  }

  const char* list = getenv("HPCRUN_CXI_COUNTERS");
  char* s = strdup(list && *list ? list : cxi_default_counters);
  for (char* t = strtok(s, ","); t && n_counters < CXI_MAX_COUNTERS; t = strtok(NULL, ",")) {
    while (*t == ' ')
      t++;
    if (*t)
      snprintf(counters[n_counters++].name, sizeof(counters[0].name), "%s", t);
  }
  free(s);

  int devs[CXI_MAX_DEVICES];
  int nd = list_devices(devs);
  if (nd == 0) {
    EEMSG("HPCToolkit: CXI: no Slingshot devices (/sys/class/cxi); CXI metrics stay zero");
  }
  for (int c = 0; c < n_counters; c++)
    for (int d = 0; d < nd; d++)
      open_counter_files(c, devs[d]);

  kind_info_t* kind = hpcrun_metrics_new_kind();
  for (int c = 0; c < n_counters; c++) {
    char mname[80];
    snprintf(mname, sizeof(mname), "CXI:%s", counters[c].name);
    counters[c].metric_id = hpcrun_set_new_metric_info(kind, strdup(mname));
  }
  hpcrun_close_kind(kind);

  uint64_t sums[CXI_MAX_COUNTERS];
  read_all(sums);
  for (int c = 0; c < n_counters; c++)
    counters[c].prev = sums[c];
  t_first_ns = now_ns();
  atomic_store(&next_due_ns, t_first_ns + period_ns);

  bs_entry.fn = cxi_blame;
  bs_entry.next = 0;
  blame_shift_register(&bs_entry);
  active = n_files > 0;
  TMSG(CXI, "%d counters, %d files on %d devices, period %lu ms", n_counters, n_files, nd,
       (unsigned long)(period_ns / 1000000));
}

static void METHOD_FN(finalize_event_list) {}

static void METHOD_FN(gen_event_set) {}

static void METHOD_FN(display_events) {
  printf("===========================================================================\n");
  printf("Slingshot (Cassini) NIC counters\n");
  printf("===========================================================================\n");
  printf("CXI[@ms]  NIC counter deltas charged to the sampled context (needs CPUTIME,\n"
         "          REALTIME or cycles). HPCRUN_CXI_COUNTERS=name,name_*,...  default:\n"
         "          %s\n", cxi_default_counters);
  printf("\n");
}

/***************************************************************************
 * object
 ***************************************************************************/

#define ss_name cxi
#define ss_cls SS_SOFTWARE

#include "ss_obj.h"
