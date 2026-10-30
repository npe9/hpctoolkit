// SPDX-FileCopyrightText: Contributors to the HPCToolkit Project
//
// SPDX-License-Identifier: Apache-2.0

// -*-Mode: C++;-*- // technically C99

//----------------------------------------------------------------------

// This file implements the __thread local variables for the ompt files.
//
// The reason for splitting this off is that ompt (and cct) pull in a
// lot of extra header files, including _GNU_SOURCE.  This way, we
// avoid polluting every file that uses __thread with all of the ompt
// and cct headers.
//
// The rules are the same as in tls_specific.
//
//   "All problems in computer science can be solved by adding a layer
//   of indirection."  -- M. Fagan

//----------------------------------------------------------------------

#define _GNU_SOURCE

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ompt-specific.h"
#include "../messages/messages.h"

//----------------------------------------------------------------------

size_t
hpcrun_ompt_specific_size(void)
{
  return sizeof(struct ompt_tls_data);
}

// Initialize a block of hpcrun_ompt_specific_size() bytes: one per OpenMP thread.
// Lithe contexts get theirs from lithe/lithe-tool.c (hpcrun_malloc'd: no malloc in
// vcore context); OS threads from hpcrun_ompt_alloc_specific.
//
void *
hpcrun_ompt_init_specific(void *mem)
{
  struct ompt_tls_data * data = mem;

  memset(data, 0, sizeof(struct ompt_tls_data));

  data->ompt_callstack_debug = 0;
  data->ompt_need_flush = false;
  data->ompt_runtime_api_flag = false;
  data->target_node = NULL;
  data->trace_node = NULL;
  data->ompt_idle_count = 0;
  data->private_region_freelist_head = NULL;

  data->private_threads_queue = NULL;
  data->notification_freelist_head = NULL;
  data->thread_region_freelist_head = NULL;
  data->top_index = -1;
  data->not_master_region = NULL;
  data->cct_not_master_region = NULL;
  data->ending_region = NULL;
  data->unresolved_cnt = 0;
  data->ompt_thread_type = ompt_thread_unknown;

  return data;
}

// Allocate and initialize the ompt TLS variables.
//
void *
hpcrun_ompt_alloc_specific(void)
{
  struct ompt_tls_data * data = malloc(sizeof(struct ompt_tls_data));

  if (data == NULL) {
    hpcrun_abort("hpcrun: malloc(ompt_tls_data) failed");
  }

  return hpcrun_ompt_init_specific(data);
}
