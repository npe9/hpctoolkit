// SPDX-FileCopyrightText: 2026 Contributors to the HPCToolkit Project
//
// SPDX-License-Identifier: BSD-3-Clause

// Lithe / parlib support (2026-10-27).
//
// Under parlib a hart (vcore) is an OS thread, but the TLS -- and so hpcrun's and
// libmonitor's pthread_getspecific lookups -- belongs to whatever user-level context
// is running on it. Unported, hpcrun charged one context's samples to another
// thread's data (garbage CPUTIME deltas) and lost its timer after the first sample in
// a context. This module, driven by parlib's tool interface (parlib/tool.h, pulled by
// parlib like ompt_start_tool):
//   - keys hpcrun's per-OS-thread state (timers, sample state) and libmonitor's
//     thread node by hart id, which parlib keeps right in every TLS block;
//   - gives every Lithe context its own profile and trace (core_profile_trace_data),
//     swapped into the hart's thread data at each switch, so samples are charged to
//     the context that was running; time in the scheduler stays on the hart's own;
//   - removes hpcrun's return-address trampoline at every switch (it patches the
//     stack of the running context and keeps its state per OS thread);
//   - drops parlib's service threads (TLS donors, helpers, the parked main thread)
//     from libmonitor's exit shootdown: they block every signal and the shootdown
//     waited out its timeout for them.
// HPCRUN_LITHE=0 declines the tool interface (old behaviour).

#ifndef HPCRUN_LITHE_TOOL_H
#define HPCRUN_LITHE_TOOL_H

#include <stdint.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// The parlib tool ABI, PARLIB_TOOL_VERSION 1 (parlib/tool.h). Tools carry the
// interface header, as with omp-tools.h.
struct uthread;

struct parlib_tool_uthread_info {
  int id;
  int rank;
  char label[24];
};

struct parlib_tool_api {
  unsigned version;
  int (*hart_id)(void);
  int (*max_harts)(void);
  int (*hart_cpu)(int hart);
  int (*hart_tid)(int hart);
  int (*uthread_info)(struct uthread* u, struct parlib_tool_uthread_info* out);
  int (*stack_base_pc)(void* pc);
  int (*uthread_is_initial)(struct uthread* u);  // version 2
};

struct parlib_tool_ops {
  unsigned version;
  void (*hart_start)(int hart);
  void (*switch_in)(struct uthread* u, int hart);
  void (*switch_out)(struct uthread* u, int hart);
  void (*aux_thread)(void);
  void (*uthread_exit)(struct uthread* u, int hart);
};

const struct parlib_tool_ops* hpcrun_lithe_tool_start(unsigned version,
                                                      const struct parlib_tool_api* api);

// Lookups for thread_data.c and libmonitor: the state of the hart the calling OS
// thread is, or NULL (not a hart, no tool, hart not registered).
struct thread_data_t;
extern int (*hpcrun_lithe_hart_id)(void);
extern struct thread_data_t** hpcrun_lithe_hart_td;
extern void** hpcrun_lithe_hart_tls;  // tls_specific.h's struct tls_data per hart
extern int hpcrun_lithe_max_harts;

static inline int hpcrun_lithe_current_hart(void) {
  int (*f)(void) = hpcrun_lithe_hart_id;
  if (__builtin_expect(f != 0, 0)) {
    int h = f();
    if (h >= 0 && h < hpcrun_lithe_max_harts)
      return h;
  }
  return -1;
}

// Non-zero if pc is in a function at the base of a Lithe/parlib context stack: the
// unwinder stops there as at a thread's start routine. Async-signal-safe.
int hpcrun_lithe_stack_base_pc(void* pc);

// GPU activities are drained by whichever thread holds the launching thread's channel
// (in its TLS): under Lithe that is a context on any hart, a backing thread running the
// context's blocking call, or the exit path for one launched before Lithe started. The
// node's metrics belong in the map of the profile whose tree holds it, not the current
// one (whose map is written by walking its own tree: a foreign node there is lost).
// enter swaps that map in if the owner is another profile nobody else is using; leave
// undoes it. enter returns false (and leave is a no-op) when nothing was swapped.
struct cct_node_t;
struct cct2metrics_t;
typedef struct hpcrun_lithe_metrics_t {
  struct thread_data_t* td;
  struct cct2metrics_t** slot;
  struct cct2metrics_t* saved;
  int prev_inside;
} hpcrun_lithe_metrics_t;
bool hpcrun_lithe_metrics_enter(struct cct_node_t* node, hpcrun_lithe_metrics_t* s);
void hpcrun_lithe_metrics_leave(hpcrun_lithe_metrics_t* s);

// On a hart's OS thread, before its thread data is finalized: put the hart's own
// profile back (a context's may be swapped in).
void hpcrun_lithe_thread_fini(void);

// Process exit, before thread data is written: stop swapping, restore every hart's
// own profile, and write every context's profile and trace.
void hpcrun_lithe_process_fini(void);

#ifdef __cplusplus
}
#endif

#endif // HPCRUN_LITHE_TOOL_H
