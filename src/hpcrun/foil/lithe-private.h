// SPDX-FileCopyrightText: 2026 Contributors to the HPCToolkit Project
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once
#ifndef HPCRUN_FOIL_LITHE_PRIVATE_H
#define HPCRUN_FOIL_LITHE_PRIVATE_H

#ifdef __cplusplus
#error This is a C-only header
#endif

#include "../lithe/lithe-tool.h"

struct hpcrun_foil_hookdispatch_lithe {
  const struct parlib_tool_ops* (*parlib_tool_start)(unsigned version,
                                                     const struct parlib_tool_api* api);
};

#endif // HPCRUN_FOIL_LITHE_PRIVATE_H
