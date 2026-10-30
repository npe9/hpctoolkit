// SPDX-FileCopyrightText: 2026 Contributors to the HPCToolkit Project
//
// SPDX-License-Identifier: BSD-3-Clause

// Lithe / parlib support: see lithe-tool.h.

#define _GNU_SOURCE

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "lithe-tool.h"

#include "../cct2metrics.h"
#include "../epoch.h"
#include "../libc-functions.h"
#include "../libmonitor/monitor.h"
#include "../loadmap.h"
#include "../main.h"
#include "../memory/hpcrun-malloc.h"
#include "../messages/messages.h"
#include "../rank.h"
#include "../sample_sources_all.h"
#include "../gpu/gpu-application-thread-api.h"
#include "../thread_data.h"
#include "../tls_specific.h"
#include "../trace.h"
#include "../trampoline/common/trampoline.h"
#include "../write_data.h"
#include "../../common/lean/OSUtil.h"
#include "../../common/lean/id-tuple.h"

// Context profiles are named (file id and THREAD logical id) HPCRUN_LITHE_ID_BASE +
// Lithe context id, so they never collide with the OS-thread profiles (0, 1, ...).
#define HPCRUN_LITHE_ID_BASE 10000
#define CTX_TABLE (1 << 16)

int (*hpcrun_lithe_hart_id)(void);
thread_data_t** hpcrun_lithe_hart_td;
void** hpcrun_lithe_hart_tls;
int hpcrun_lithe_max_harts;

// libmonitor (pthread.c): its thread node per hart, and service threads.
extern void monitor_lithe_record_hart(int hart, int max_harts);
extern void monitor_lithe_aux_self(void);

static const struct parlib_tool_api* api;
static atomic_bool main_parked;
extern thread_data_t* hpcrun_thread0_data(void);

// Per logical thread, not per OS thread: hpcrun keeps OpenMP (OMPT) and blame-shifting
// state in thread data and in the OMPT block of tls_data. On a hart both belong to
// the hart's OS thread, while the OpenMP thread is a context that runs on any hart and
// shares harts with others: without its own copy, a context read another's region ids,
// master flag and region stack, and ~40 % of OpenMP worker samples in Lithe LULESH
// were never placed in their parallel region (stock libomp: all of them).
typedef struct logical_td_t {
  int idle;
  uint64_t blame_target;
  int last_synch_sample;
  int last_sample;
  int overhead;
  int lockwait;
  void* lockid;
  uint64_t region_id;
  uint64_t outer_region_id;
  cct_node_t* outer_region_context;
  int defer_flag;
  cct_node_t* omp_task_context;
  int master;
  int team_master;
  int defer_write;
  int omp_thread;
  uint64_t last_bar_time_us;
  void* ompt_specific;  // tls_data's block
} logical_td_t;

typedef struct ctx_prof_t {
  int id;    // Lithe context id
  int rank;  // last rank the describer reported, -1 none
  char label[24];
  core_profile_trace_data_t cptd;
  logical_td_t lt;
} ctx_prof_t;

// Context id -> profile, open addressing; a slot is claimed once and never freed
// (contexts are few; ids are unique for the life of the process).
static _Atomic(ctx_prof_t*) ctx_table[CTX_TABLE];
#define CTX_CLAIMING ((ctx_prof_t*)1)  // slot taken, profile being made
static atomic_int ctx_overflow;
// The same profiles in creation order, for scans (hpcrun_lithe_metrics_enter).
static _Atomic(ctx_prof_t*) ctx_list[CTX_TABLE];
static atomic_uint ctx_count;

// Swap protocol per hart, written only by the hart's own OS thread. `busy` records
// how far a swap got, so process fini can finish one a signal interrupted:
//   1 saving the hart's own profile (td still holds it)   -> nothing to undo
//   2 loading the context's (own saved, td partial)        -> td := own
//   3 saving the context's (td holds it, storage partial)  -> storage := td, td := own
//   4 restoring the hart's own (storage done, td partial)  -> td := own
typedef struct hart_state_t {
  core_profile_trace_data_t own;
  logical_td_t own_lt;
  ctx_prof_t* cur;  // whose profile td->core_profile_trace_data holds; NULL = the hart's
  volatile int busy;
  volatile bool closed;  // this hart's OS thread finalized its thread data
} hart_state_t;
static hart_state_t* harts;
static atomic_bool closed;  // process fini: no more swaps anywhere
// switch_in outcomes, reported at fini under -dd LITHE: [uninit, no td, no info, ok]
static atomic_long sw_stat[4];

// ---------------------------------------------------------------------------

size_t hpcrun_ompt_specific_size(void);
void* hpcrun_ompt_init_specific(void* mem);

#define LT_FIELDS(X) \
  X(idle) X(blame_target) X(last_synch_sample) X(last_sample) X(overhead) X(lockwait) \
  X(lockid) X(region_id) X(outer_region_id) X(outer_region_context) X(defer_flag) \
  X(omp_task_context) X(master) X(team_master) X(defer_write) X(omp_thread) X(last_bar_time_us)

static void lt_save(logical_td_t* l, thread_data_t* td, int hart) {
#define LT_SAVE(f) l->f = td->f;
  LT_FIELDS(LT_SAVE)
#undef LT_SAVE
  void* t = hpcrun_lithe_hart_tls[hart];
  l->ompt_specific = t ? TLS_BASE_GET(t, ompt_specific) : NULL;
}

static void lt_load(thread_data_t* td, int hart, const logical_td_t* l) {
#define LT_LOAD(f) td->f = l->f;
  LT_FIELDS(LT_LOAD)
#undef LT_LOAD
  void* t = hpcrun_lithe_hart_tls[hart];
  if (t != NULL && l->ompt_specific != NULL)
    TLS_BASE_GET(t, ompt_specific) = l->ompt_specific;
}

// The main OS thread's tls_data, captured at tool start (on that thread, before any
// hart): its OMPT block belongs to the program's initial thread, which becomes a
// context (OMPT's thread_begin for it ran before Lithe started).
static void* main_tls;

static void ctx_prof_init(ctx_prof_t* p, const struct parlib_tool_uthread_info* info,
                          bool initial) {
  core_profile_trace_data_t* c = &p->cptd;
  memset(p, 0, sizeof(*p));
  p->id = info->id;
  p->rank = info->rank;
  memcpy(p->label, info->label, sizeof(p->label));
  c->id = HPCRUN_LITHE_ID_BASE + info->id;
  c->epoch = hpcrun_malloc(sizeof(epoch_t));
  memset(c->epoch, 0, sizeof(epoch_t));
  c->epoch->csdata_ctxt = NULL;
  hpcrun_cct_bundle_init(&c->epoch->csdata, NULL);
  c->epoch->loadmap = hpcrun_getLoadmap();
  c->epoch->next = NULL;
  hpcrun_cct2metrics_init(&c->cct2metrics_map);
  c->trace_is_ordered = true;
  c->trace_expected_disorder = 5;
  hpcrun_trace_open(c, HPCRUN_SAMPLE_TRACE);
  p->lt.last_synch_sample = -1;  // the rest starts at 0, as thread_data.c does
  thread_data_t* m = initial ? hpcrun_thread0_data() : NULL;
  if (m != NULL) {
    // The main thread's state so far (it parked when this context left it).
#define LT_INIT(f) p->lt.f = m->f;
    LT_FIELDS(LT_INIT)
#undef LT_INIT
  }
  if (initial && main_tls != NULL) {
    p->lt.ompt_specific = TLS_BASE_GET(main_tls, ompt_specific);
  } else {
    void* o = hpcrun_malloc(hpcrun_ompt_specific_size());
    p->lt.ompt_specific = o ? hpcrun_ompt_init_specific(o) : NULL;
  }
}

// The profile for context `info->id`, created on first sight. Vcore context, the
// hart's td current (hpcrun_malloc draws on its memstore).
static ctx_prof_t* ctx_prof_get(const struct parlib_tool_uthread_info* info, bool initial) {
  unsigned h = ((unsigned)info->id * 2654435761u) & (CTX_TABLE - 1);
  for (unsigned i = 0; i < CTX_TABLE; i++, h = (h + 1) & (CTX_TABLE - 1)) {
    ctx_prof_t* p = atomic_load_explicit(&ctx_table[h], memory_order_acquire);
    if (p == NULL) {
      // Claim the slot before making the profile: making one opens its trace file,
      // and a loser that had made one first (two ids probing from the same slot)
      // made a second for its id further on -- "hpctrace file exists", exit 255.
      ctx_prof_t* z = NULL;
      if (atomic_compare_exchange_strong(&ctx_table[h], &z, CTX_CLAIMING)) {
        ctx_prof_t* n = hpcrun_malloc(sizeof(ctx_prof_t));
        if (n != NULL)
          ctx_prof_init(n, info, initial);
        atomic_store_explicit(&ctx_table[h], n, memory_order_release);
        if (n != NULL)
          atomic_store_explicit(&ctx_list[atomic_fetch_add(&ctx_count, 1)], n,
                                memory_order_release);
        return n;
      }
      p = z;
    }
    // Another hart is making the profile in this slot (vcore context, a malloc and a
    // file open; it cannot wait on us). Its id may be ours.
    while (p == CTX_CLAIMING) {
      __builtin_ia32_pause();
      p = atomic_load_explicit(&ctx_table[h], memory_order_acquire);
    }
    if (p == NULL) {  // its malloc failed and the slot is free again: retry it
      h = (h - 1) & (CTX_TABLE - 1);
      i--;
      continue;
    }
    if (p->id == info->id) {
      if (info->rank >= 0)
        p->rank = info->rank;
      if (info->label[0] && !p->label[0])
        memcpy(p->label, info->label, sizeof(p->label));
      return p;
    }
  }
  atomic_fetch_add(&ctx_overflow, 1);
  return NULL;
}

// GPU attribution outcomes, reported at fini under -dd LITHE: [swapped, unowned]
static atomic_long attr_stat[2];

static void swap_out(hart_state_t* hs, thread_data_t* td, int hart) {
  if (hs->cur == NULL)
    return;
  hs->busy = 3;
  hs->cur->cptd = td->core_profile_trace_data;
  lt_save(&hs->cur->lt, td, hart);
  hs->busy = 4;
  td->core_profile_trace_data = hs->own;
  lt_load(td, hart, &hs->own_lt);
  hs->cur = NULL;
  hs->busy = 0;
}

static void swap_in(hart_state_t* hs, thread_data_t* td, int hart, ctx_prof_t* p) {
  hs->busy = 1;
  hs->own = td->core_profile_trace_data;
  lt_save(&hs->own_lt, td, hart);
  hs->busy = 2;
  td->core_profile_trace_data = p->cptd;
  lt_load(td, hart, &p->lt);
  hs->cur = p;
  hs->busy = 0;
}

// Finish whatever a signal interrupted, then restore the hart's own profile.
static void hart_restore(hart_state_t* hs, thread_data_t* td, int hart) {
  switch (hs->busy) {
  case 2:
  case 4:
    td->core_profile_trace_data = hs->own;
    lt_load(td, hart, &hs->own_lt);
    hs->cur = NULL;
    break;
  case 3:
    hs->cur->cptd = td->core_profile_trace_data;
    lt_save(&hs->cur->lt, td, hart);
    td->core_profile_trace_data = hs->own;
    lt_load(td, hart, &hs->own_lt);
    hs->cur = NULL;
    break;
  default:
    break;
  }
  hs->busy = 0;
  swap_out(hs, td, hart);
}

// ---------------------------------------------------------------------------
// parlib callbacks

static void tool_hart_start(int hart) {
  if (hart < 0 || hart >= hpcrun_lithe_max_harts)
    return;
  // This pthread's own TLS: the td and thread node libmonitor made for it.
  thread_data_t* td = hpcrun_safe_get_td();
  hpcrun_lithe_hart_tls[hart] = TLS_GET_BASE_PTR();
  hpcrun_lithe_hart_td[hart] = td;
  monitor_lithe_record_hart(hart, hpcrun_lithe_max_harts);
  TMSG(LITHE, "hart %d: td %p", hart, td);
}

static void tool_switch_in(struct uthread* u, int hart) {
  if (atomic_load_explicit(&closed, memory_order_relaxed) || harts[hart].closed)
    return;
  if (!hpcrun_is_initialized()) {
    atomic_fetch_add_explicit(&sw_stat[0], 1, memory_order_relaxed);
    return;
  }
  thread_data_t* td = hpcrun_lithe_hart_td[hart];
  if (td == NULL) {
    atomic_fetch_add_explicit(&sw_stat[1], 1, memory_order_relaxed);
    return;
  }
  int prev = td->inside_hpcrun;
  td->inside_hpcrun = 1;  // a sample landing now is dropped, not taken half-swapped
  hart_state_t* hs = &harts[hart];

  // The return-address trampoline belongs to the stack that ran last on this hart.
  hpcrun_trampoline_remove();

  struct parlib_tool_uthread_info info;
  ctx_prof_t* p = NULL;
  int rc = api->uthread_info(u, &info);
  if (rc == 0 && info.id >= 0)
    p = ctx_prof_get(&info, api->version >= 2 && api->uthread_is_initial != NULL &&
                                api->uthread_is_initial(u));
  else if (atomic_load_explicit(&sw_stat[2], memory_order_relaxed) < 3)
    TMSG(LITHE, "switch_in u %p: describe rc %d id %d", (void*)u, rc, rc == 0 ? info.id : -2);
  atomic_fetch_add_explicit(&sw_stat[p ? 3 : 2], 1, memory_order_relaxed);
  if (p != hs->cur) {
    swap_out(hs, td, hart);
    if (p != NULL)
      swap_in(hs, td, hart, p);
  }
  td->inside_hpcrun = prev;
}

static void tool_switch_out(struct uthread* u, int hart) {
  (void)u;
  if (atomic_load_explicit(&closed, memory_order_relaxed) || harts[hart].closed)
    return;
  thread_data_t* td = hpcrun_lithe_hart_td[hart];
  if (td == NULL)
    return;
  int prev = td->inside_hpcrun;
  td->inside_hpcrun = 1;
  hpcrun_trampoline_remove();  // still on u's stack: restore its return address
  swap_out(&harts[hart], td, hart);
  td->inside_hpcrun = prev;
}

// ---------------------------------------------------------------------------
// Charging a node of another profile's tree (GPU activities)

static bool cptd_owns(const core_profile_trace_data_t* c, cct_node_t* root) {
  if (c->epoch == NULL)
    return false;
  const cct_bundle_t* b = &c->epoch->csdata;
  return root == b->top || root == b->partial_unw_root || root == b->unresolved_root;
}

// Where the live metric map of the profile owning `root` is, if the caller may use it:
// nobody else is adding to it. NULL if the owner is unknown or live on another hart.
static cct2metrics_t** owner_map(cct_node_t* root, int hart) {
  unsigned n = atomic_load_explicit(&ctx_count, memory_order_acquire);
  for (unsigned i = 0; i < n; i++) {
    ctx_prof_t* p = atomic_load_explicit(&ctx_list[i], memory_order_acquire);
    if (p == NULL || !cptd_owns(&p->cptd, root))
      continue;
    // Swapped in on a hart: live in that hart's td. The caller holds the context's
    // channel, so normally it is the context and this is its own hart (handled by
    // the caller); another hart's is not ours to touch.
    for (int k = 0; k < hpcrun_lithe_max_harts; k++)
      if (harts[k].cur == p)
        return NULL;
    return &p->cptd.cct2metrics_map;
  }
  for (int k = 0; k < hpcrun_lithe_max_harts; k++) {
    thread_data_t* tdk = hpcrun_lithe_hart_td[k];
    if (tdk == NULL)
      continue;
    hart_state_t* hs = &harts[k];
    core_profile_trace_data_t* own = hs->cur ? &hs->own : &tdk->core_profile_trace_data;
    if (!cptd_owns(own, root))
      continue;
    if (k == hart || hs->closed || atomic_load(&closed))
      return &own->cct2metrics_map;
    return NULL;
  }
  thread_data_t* m = hpcrun_thread0_data();
  if (m != NULL && cptd_owns(&m->core_profile_trace_data, root) &&
      (atomic_load(&main_parked) || atomic_load(&closed)))
    return &m->core_profile_trace_data.cct2metrics_map;
  return NULL;
}

bool hpcrun_lithe_metrics_enter(cct_node_t* node, hpcrun_lithe_metrics_t* s) {
  s->slot = NULL;
  if (hpcrun_lithe_hart_id == NULL || node == NULL || !hpcrun_td_avail())
    return false;
  thread_data_t* td = hpcrun_get_thread_data();
  cct_node_t* root = node;
  for (cct_node_t* q = hpcrun_cct_parent(root); q != NULL; q = hpcrun_cct_parent(q))
    root = q;
  if (cptd_owns(&td->core_profile_trace_data, root))
    return false;
  int prev = td->inside_hpcrun;
  td->inside_hpcrun = 1;  // a sample landing now is dropped, not charged to the owner
  cct2metrics_t** slot = owner_map(root, hpcrun_lithe_current_hart());
  if (slot == NULL) {
    td->inside_hpcrun = prev;
    atomic_fetch_add_explicit(&attr_stat[1], 1, memory_order_relaxed);
    return false;
  }
  s->td = td;
  s->prev_inside = prev;
  s->slot = slot;
  s->saved = td->core_profile_trace_data.cct2metrics_map;
  td->core_profile_trace_data.cct2metrics_map = *slot;
  atomic_fetch_add_explicit(&attr_stat[0], 1, memory_order_relaxed);
  return true;
}

void hpcrun_lithe_metrics_leave(hpcrun_lithe_metrics_t* s) {
  if (s->slot == NULL)
    return;
  thread_data_t* td = s->td;
  *s->slot = td->core_profile_trace_data.cct2metrics_map;  // the splay root moves
  td->core_profile_trace_data.cct2metrics_map = s->saved;
  td->inside_hpcrun = s->prev_inside;
}

int hpcrun_lithe_stack_base_pc(void* pc) {
  const struct parlib_tool_api* a = api;
  return a != NULL && a->stack_base_pc != NULL && a->stack_base_pc(pc);
}

static void tool_aux_thread(void) {
  if (syscall(SYS_gettid) == getpid()) {
    // The main OS thread parks with every signal blocked; its TLS here may be
    // borrowed, so its sources are left alone (their signals stay pending).
    atomic_store(&main_parked, true);
  } else if (hpcrun_is_initialized() && hpcrun_lithe_current_hart() < 0 &&
             hpcrun_td_avail()) {
    // A service thread, still on its own TLS (parlib calls this first thing). Stop
    // its sources: it runs no application code, and a TLS donor goes on to adopt
    // another thread's TLS, where its next perf overflow found that thread's data,
    // did not know the fd, and libmonitor's default action killed the process.
    SAMPLE_SOURCES(stop);
  }
  monitor_lithe_aux_self();
}

// A context finishes: still on its stack and TLS, its profile still swapped in. Drain
// its GPU activity channel (kept in its TLS) now, as hpcrun does at a pthread's exit;
// without this the last kernels of every context were never attributed (31 of 40).
static void tool_uthread_exit(struct uthread* u, int hart) {
  (void)u;
  if (atomic_load_explicit(&closed, memory_order_relaxed) || harts[hart].closed ||
      hpcrun_lithe_hart_td[hart] == NULL || !hpcrun_is_initialized())
    return;
  thread_data_t* td = hpcrun_lithe_hart_td[hart];
  int prev = td->inside_hpcrun;
  td->inside_hpcrun = 1;
  gpu_application_thread_process_activities();
  td->inside_hpcrun = prev;
}

static const struct parlib_tool_ops tool_ops = {
    .version = 1,
    .hart_start = tool_hart_start,
    .switch_in = tool_switch_in,
    .switch_out = tool_switch_out,
    .aux_thread = tool_aux_thread,
    .uthread_exit = tool_uthread_exit,
};

const struct parlib_tool_ops* hpcrun_lithe_tool_start(unsigned version,
                                                      const struct parlib_tool_api* a) {
  const char* e = getenv("HPCRUN_LITHE");
  if ((e && *e == '0') || version < 1 || a == NULL || a->version < 1)
    return NULL;
  int n = a->max_harts();
  if (n <= 0)
    return NULL;
  // Called from parlib's init, possibly before hpcrun's process init: plain calloc.
  hpcrun_lithe_hart_td = calloc(n, sizeof(thread_data_t*));
  hpcrun_lithe_hart_tls = calloc(n, sizeof(void*));
  harts = calloc(n, sizeof(hart_state_t));
  if (hpcrun_lithe_hart_td == NULL || hpcrun_lithe_hart_tls == NULL || harts == NULL)
    return NULL;
  api = a;
  hpcrun_lithe_max_harts = n;
  if (syscall(SYS_gettid) == getpid()) {
    hpcrun_tls_specific_init_process();  // pthread_once: creates the key + main's data
    main_tls = pthread_getspecific(hpcrun_get_tls_key());
  }
  hpcrun_lithe_hart_id = a->hart_id;  // last: enables the lookups
  return &tool_ops;
}

// ---------------------------------------------------------------------------
// fini

void hpcrun_lithe_thread_fini(void) {
  if (hpcrun_lithe_hart_id == NULL)
    return;
  // Only this hart stops swapping. (This used to set the process-wide flag, and the
  // first service thread to exit -- an MPI or libfabric helper -- switched context
  // attribution off for every hart: no context profiles under perf events.)
  int h = hpcrun_lithe_current_hart();
  if (h < 0 || hpcrun_lithe_hart_td[h] == NULL)
    return;
  harts[h].closed = true;
  TMSG(LITHE, "hart %d: thread fini", h);
  // On this hart's thread. busy != 0 means this signal interrupted a swap on this
  // very thread; process fini finishes it.
  if (harts[h].busy == 0)
    swap_out(&harts[h], hpcrun_lithe_hart_td[h], h);
}

static void ctx_prof_id_tuple(ctx_prof_t* p) {
  pms_id_t ids[IDTUPLE_MAXTYPES];
  id_tuple_t t;
  id_tuple_constructor(&t, ids, IDTUPLE_MAXTYPES);
  id_tuple_push_back(&t, IDTUPLE_COMPOSE(IDTUPLE_NODE, IDTUPLE_IDS_LOGIC_LOCAL),
                     OSUtil_hostid(libc_getenv), 0);
  int rank = p->rank >= 0 ? p->rank : hpcrun_get_rank();
  if (rank >= 0)
    id_tuple_push_back(&t, IDTUPLE_COMPOSE(IDTUPLE_RANK, IDTUPLE_IDS_LOGIC_ONLY), rank, rank);
  id_tuple_push_back(&t, IDTUPLE_COMPOSE(IDTUPLE_THREAD, IDTUPLE_IDS_LOGIC_ONLY), p->cptd.id,
                     p->cptd.id);
  id_tuple_copy(&p->cptd.id_tuple, &t, hpcrun_malloc);
}

void hpcrun_lithe_process_fini(void) {
  if (hpcrun_lithe_hart_id == NULL)
    return;
  TMSG(LITHE, "process fini");
  atomic_store(&closed, true);
  // Harts still running contexts finish a swap they are in (closed stops new ones).
  for (int h = 0; h < hpcrun_lithe_max_harts; h++) {
    thread_data_t* td = hpcrun_lithe_hart_td[h];
    if (td == NULL)
      continue;
    for (int spin = 0; harts[h].busy != 0 && spin < 1000000; spin++)
      __builtin_ia32_pause();
    hart_restore(&harts[h], td, h);
  }
  // GPU activities that arrived after their context exited (the runtime's buffers are
  // flushed just before this) sit in channels no thread will read again.
  gpu_application_thread_process_all_activities();
  int n = 0;
  for (unsigned i = 0; i < CTX_TABLE; i++) {
    ctx_prof_t* p = atomic_load(&ctx_table[i]);
    if (p == NULL || p == CTX_CLAIMING)
      continue;
    ctx_prof_id_tuple(p);
    hpcrun_write_profile_data(&p->cptd);
    hpcrun_trace_close(&p->cptd);
    n++;
  }
  // The main OS thread parked when its context moved to a hart: libmonitor no longer
  // shoots it down (all its signals are blocked), so its data -- the samples before
  // the first hart -- is written here.
  if (atomic_load(&main_parked)) {
    thread_data_t* m = hpcrun_thread0_data();
    core_profile_trace_data_t* c = &m->core_profile_trace_data;
    pms_id_t ids[IDTUPLE_MAXTYPES];
    id_tuple_t t;
    id_tuple_constructor(&t, ids, IDTUPLE_MAXTYPES);
    id_tuple_push_back(&t, IDTUPLE_COMPOSE(IDTUPLE_NODE, IDTUPLE_IDS_LOGIC_LOCAL),
                       OSUtil_hostid(libc_getenv), 0);
    int rank = hpcrun_get_rank();
    if (rank >= 0)
      id_tuple_push_back(&t, IDTUPLE_COMPOSE(IDTUPLE_RANK, IDTUPLE_IDS_LOGIC_ONLY), rank, rank);
    id_tuple_push_back(&t, IDTUPLE_COMPOSE(IDTUPLE_THREAD, IDTUPLE_IDS_LOGIC_ONLY), c->id, c->id);
    id_tuple_copy(&c->id_tuple, &t, hpcrun_malloc);
    hpcrun_write_profile_data(c);
    hpcrun_trace_close(c);
  }
  if (atomic_load(&ctx_overflow))
    EMSG("lithe: %d context switches had no profile slot (table full)",
         atomic_load(&ctx_overflow));
  TMSG(LITHE, "wrote %d context profiles; switch_in uninit %ld no-td %ld no-info %ld ok %ld", n,
       atomic_load(&sw_stat[0]), atomic_load(&sw_stat[1]), atomic_load(&sw_stat[2]),
       atomic_load(&sw_stat[3]));
  TMSG(LITHE, "GPU activities charged to another profile's tree: %ld; owner unusable: %ld",
       atomic_load(&attr_stat[0]), atomic_load(&attr_stat[1]));
}
