// SPDX-FileCopyrightText: Contributors to the HPCToolkit Project
//
// SPDX-License-Identifier: Apache-2.0

// -*-Mode: C++;-*- // technically C99

//----------------------------------------------------------------------

#ifndef _tls_specific_h_
#define _tls_specific_h_

#include <sys/types.h>
#include <stdint.h>
#include <pthread.h>

#include "memory/newmem.h"
#include "sample-sources/pthread-blame.h"
#include "sample-sources/perf/perf_constants.h"
#include "unwind/common/binarytree_uwi.h"
#include "lithe/lithe-tool.h"

struct cct_node_t;
struct csklnode_s;
struct bitree_uwi_s;
struct ilmstat_btuwi_pair_s;

struct tls_data;
typedef struct tls_data hpcrun_tls_data_t;

pthread_key_t hpcrun_get_tls_key(void);
void hpcrun_tls_specific_init_process(void);
void * hpcrun_tls_specific_init_thread(void);

static inline void *
lazy_getspecific(void)
{
  // Lithe harts (lithe/lithe-tool.h): per-OS-thread state, found by hart id; the
  // TLS belongs to the running context (and a lazy malloc here ran in the signal
  // handler, once per context).
  int h = hpcrun_lithe_current_hart();
  if (h >= 0 && hpcrun_lithe_hart_tls[h] != NULL) {
    return hpcrun_lithe_hart_tls[h];
  }
  void * spec = pthread_getspecific(hpcrun_get_tls_key());

  if (spec != NULL) {
    return spec;
  }

  return hpcrun_tls_specific_init_thread();
}

// new macros ...

#define TLS_GET(field)  (((struct tls_data *) lazy_getspecific())->field)

#define TLS_GET_BASE_PTR()  ((void *) lazy_getspecific())

#define TLS_BASE_GET(base, field)  (((struct tls_data *) (base))->field)

//----------------------------------------------------------------------

// TLS Data Struct -- this contains all the __thread local variables
// outside of thread_data_t

struct tls_data {
  // ompt tls variables
  void * ompt_specific;

  // main.c
  bool suppress_sample;

  // sample_sources_all.c
  int ignore_thread;

  // thread_data.c
  int  monitor_tid;
  bool mem_pool_initialized;

  // trace.c
  uint64_t prev_nanotime;

  // cct/cct.c
  struct cct_node_t * cct_node_freelist_head;

  // memory/mem.c
  hpcrun_meminfo_t memstore;
  int mem_low;

  // sample-sources/itimer.c
  bool wallclock_ok;

  // sample-sources/papi-c-intel.c
  bool event_set_created;
  bool event_set_finalized;
  int  my_event_set;

  // sample-sources/pthread-blame.c
  blame_t pthread_blame;

  // sample-sources/perf/kernel_blocking.c
  // time when leaving the application process
  u64 perf_time_cs_out;
  // cct of the last access to kernel
  struct cct_node_t * perf_cct_kernel;
  // cpu of the last sample
  u32 perf_cpu;
  // last pid/tid
  u32 perf_pid;
  u32 perf_tid;

  // skiplist/cskiplist.c
  // thread local free csklnode list
  struct csklnode_s * lf_cskl_nodes;

  // skiplist/urand.c
  int urand_initialized;
  unsigned int urand_data;

  // unwind/common/binarytree_uwi.c
  // thread local free unwind interval tree
  bitree_uwi_t * lf_uwi_tree[NUM_UNWINDERS];

  // unwind/common/uw_recipe_map.c
  // thread local free list of ilmstat_btuwi_pair_t *
  struct ilmstat_btuwi_pair_s * lf_ilmstat_btuwi;
};

#endif  // _tls_specific_h_
