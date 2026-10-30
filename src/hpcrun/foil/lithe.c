// SPDX-FileCopyrightText: 2026 Contributors to the HPCToolkit Project
//
// SPDX-License-Identifier: BSD-3-Clause

#define _GNU_SOURCE

#include "common.h"
#include "lithe-private.h"

const struct hpcrun_foil_hookdispatch_lithe* hpcrun_foil_fetch_hooks_lithe() {
  static const struct hpcrun_foil_hookdispatch_lithe hooks = {
      .parlib_tool_start = hpcrun_lithe_tool_start,
  };
  return &hooks;
}
