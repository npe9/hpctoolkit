// SPDX-FileCopyrightText: 2026 Contributors to the HPCToolkit Project
//
// SPDX-License-Identifier: BSD-3-Clause

// parlib's tool interface (parlib/tool.h): parlib looks this symbol up with
// dlsym(RTLD_DEFAULT) before its first hart, like ompt_start_tool. Not an override:
// parlib defines no such function.

#define _GNU_SOURCE

#include "common-preload.h"
#include "common.h"
#include "lithe-private.h"

HPCRUN_EXPOSED_API const struct parlib_tool_ops*
parlib_tool_start(unsigned version, const struct parlib_tool_api* api) {
  return hpcrun_foil_fetch_hooks_lithe_dl()->parlib_tool_start(version, api);
}
