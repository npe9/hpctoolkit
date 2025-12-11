# SPDX-FileCopyrightText: Contributors to the HPCToolkit Project
#
# SPDX-License-Identifier: Apache-2.0

test_srcs = files(
  '../common/lean/usec_time.c',
  'sample-sources/exclude-test.cpp',
  'sample-sources/exclude.c',
  'sample-sources/perf/event_name_parser.c',
  'sample-sources/perf/event_name_parser_test.cpp',
  'skiplist/randomizer-support.c',
  'skiplist/randomizer-test.cpp',
  'skiplist/randomizer.c',
  'skiplist/urand.c',
  'tls_specific.c',
)

test(
  'hpcrun-unit',
  executable(
    'hpcrun-unit-test',
    test_srcs,
    dependencies: [gtest_main_dep, gmock_dep, papi_dep],
  ),
  protocol: 'gtest',
)


libs = []

hpcrun_sonames = configuration_data()

# Shim to create a private link namespace, to hold libunwind and any other libraries in
# need of protection from the application (e.g. C++).
private_ns_lib = shared_library(
  'hpcrun_private_ns',
  'audit/private-ns.c',
  'foil/workarounds/dl-iterate-phdr.c',
  'foil/workarounds/pthread-key.c',
  'audit/exports.c',
  gnu_symbol_visibility: 'hidden',
  implicit_include_directories: false,
  dependencies: [dl_dep, threads_dep],
  link_language: 'c',
  install: true,
)
hpcrun_sonames.set_quoted('HPCRUN_PRIVATE_NS_SO', fs.name(private_ns_lib.full_path()))
libs += private_ns_lib

# Shim to load and access libunwind in a private link namespace.
unwind_lib = shared_library(
  'hpcrun_unwind',
  'unwind/common/libunwind-pvt.c',
  gnu_symbol_visibility: 'hidden',
  implicit_include_directories: false,
  dependencies: [libunwind_dep],
  link_language: 'c',
  install: true,
)
hpcrun_sonames.set_quoted('HPCRUN_UNWIND_SO', fs.name(unwind_lib.full_path()))
libs += unwind_lib

# Main library for hpcrun, this is where most everything happens.

srcs = files(
  'audit/binding.c',
  'cct/cct-node-vector.c',
  'cct/cct.c',
  'cct/cct_bundle.c',
  'cct/cct_ctxt.c',
  'cct2metrics.c',
  'cct_backtrace_finalize.c',
  'cct_insert_backtrace.c',
  'closure-registry.c',
  'control-knob.c',
  'device-finalizers.c',
  'device-initializers.c',
  'disabled.c',
  'env.c',
  'epoch.c',
  'files.c',
  'fnbounds/fnbounds.c',
  'fnbounds/fnbounds_common.c',
  'fnbounds/fnbounds_dynamic.c',
  'fnbounds/scan.c',
  'foil/client.c',
  'foil/common.c',
  'foil/ga.c',
  'foil/libc.c',
  'foil/mpi.c',
  'foil/ompt.c',
  'gpu/activity/correlation/gpu-correlation-channel.c',
  'gpu/activity/correlation/gpu-host-correlation-map.c',
  'gpu/activity/gpu-activity-channel.c',
  'gpu/activity/gpu-activity-process.c',
  'gpu/activity/gpu-activity-send.c',
  'gpu/activity/gpu-activity.c',
  'gpu/activity/gpu-event-id-map.c',
  'gpu/activity/gpu-op-placeholders.c',
  'gpu/api/common/gpu-binary.c',
  'gpu/api/common/gpu-cct.c',
  'gpu/api/common/gpu-channel-item-allocator.c',
  'gpu/api/common/gpu-cid-map.c',
  'gpu/api/common/gpu-flush-alarm.c',
  'gpu/api/common/gpu-instrumentation.c',
  'gpu/api/common/gpu-kernel-table.c',
  'gpu/api/ompt/ompt-activity-translate.c',
  'gpu/api/ompt/ompt-gpu-api.c',
  'gpu/blame-shifting/active-kernels-map.c',
  'gpu/blame-shifting/blame-helper.c',
  'gpu/blame-shifting/blame-kernel-map.c',
  'gpu/blame-shifting/blame-queue-map.c',
  'gpu/common/base-channel-set.c',
  'gpu/common/gpu-counter-set.c',
  'gpu/common/gpu-monitoring.c',
  'gpu/gpu-application-thread-api.c',
  'gpu/gpu-metrics.c',
  'gpu/gpu-monitoring-thread-api.c',
  'gpu/gpu-splay-allocator.c',
  'gpu/gpu-timestamp.c',
  'gpu/trace/gpu-context-id-map.c',
  'gpu/trace/gpu-stream-id-map.c',
  'gpu/trace/gpu-trace-api.c',
  'gpu/trace/gpu-trace-channel-set.c',
  'gpu/trace/gpu-trace-channel.c',
  'gpu/trace/gpu-trace-demultiplexer.c',
  'gpu/trace/gpu-trace-item.c',
  'handling_sample.c',
  'hpcrun-initializers.c',
  'hpcrun_options.c',
  'hpcrun_signals.c',
  'hpcrun_stats.c',
  'libmonitor/callback.c',
  'libmonitor/fork.c',
  'libmonitor/main.c',
  'libmonitor/mpi.c',
  'libmonitor/mpi_comm_c.c',
  'libmonitor/mpi_comm_f0.c',
  'libmonitor/mpi_comm_f1.c',
  'libmonitor/mpi_comm_f2.c',
  'libmonitor/mpi_final_c.c',
  'libmonitor/mpi_final_f0.c',
  'libmonitor/mpi_final_f1.c',
  'libmonitor/mpi_final_f2.c',
  'libmonitor/mpi_init_c.c',
  'libmonitor/mpi_init_f0.c',
  'libmonitor/mpi_init_f1.c',
  'libmonitor/mpi_init_f2.c',
  'libmonitor/mpi_init_thread_c.c',
  'libmonitor/mpi_init_thread_f0.c',
  'libmonitor/mpi_init_thread_f1.c',
  'libmonitor/mpi_init_thread_f2.c',
  'libmonitor/pmpi.c',
  'libmonitor/pthread.c',
  'libmonitor/signal.c',
  'libmonitor/utils.c',
  'loadmap.c',
  'logical/common.c',
  'main.c',
  'memory/mem.c',
  'memory/mmap.c',
  'messages/debug-flag.c',
  'messages/errors.c',
  'messages/fmt.c',
  'messages/messages-async.c',
  'messages/messages-sync.c',
  'metrics.c',
  'module-ignore-map.c',
  'monitor-exts/openmp.c',
  'name.c',
  'ompt/ompt-callstack.c',
  'ompt/ompt-defer-write.c',
  'ompt/ompt-defer.c',
  'ompt/ompt-device-map.c',
  'ompt/ompt-device.c',
  'ompt/ompt-interface.c',
  'ompt/ompt-queues.c',
  'ompt/ompt-region-debug.c',
  'ompt/ompt-region.c',
  'ompt/ompt-specific.c',
  'ompt/ompt-task.c',
  'ompt/ompt-thread.c',
  'rank.c',
  'safe-sampling.c',
  'sample-sources/blame-shift/blame-map.c',
  'sample-sources/blame-shift/blame-shift.c',
  'sample-sources/blame-shift/directed.c',
  'sample-sources/blame-shift/undirected.c',
  'sample-sources/common.c',
  'sample-sources/display.c',
  'sample-sources/exclude.c',
  'sample-sources/ga-overrides.c',
  'sample-sources/ga.c',
  'sample-sources/idle.c',
  'sample-sources/io-over.c',
  'sample-sources/io.c',
  'sample-sources/itimer.c',
  'sample-sources/memleak-overrides.c',
  'sample-sources/memleak.c',
  'sample-sources/none.c',
  'sample-sources/omp-idle.c',
  'sample-sources/omp-mutex.c',
  'sample-sources/pthread-blame-overrides.c',
  'sample-sources/pthread-blame.c',
  'sample-sources/retcnt.c',
  'sample-sources/sample-filters.c',
  'sample-sources/sync.c',
  'sample_event.c',
  'sample_prob.c',
  'sample_sources_all.c',
  'sample_sources_registered.c',
  'segv_handler.c',
  'skiplist/cskiplist.c',
  'skiplist/randomizer.c',
  'skiplist/urand.c',
  'start-stop.c',
  'term_handler.c',
  'thread_data.c',
  'thread_finalize.c',
  'thread_use.c',
  'threadmgr.c',
  'tls_specific.c',
  'trace.c',
  'unwind/common/backtrace.c',
  'unwind/common/binarytree_uwi.c',
  'unwind/common/interval_t.c',
  'unwind/common/libunw_intervals.c',
  'unwind/common/libunwind-interface.c',
  'unwind/common/stack_troll.c',
  'unwind/common/unw-throw.c',
  'unwind/common/uw_hash.c',
  'unwind/common/uw_recipe_map.c',
  'utilities/executable-path.c',
  'utilities/first_func.c',
  'utilities/hpcrun-nanotime.c',
  'utilities/ip-normalized.c',
  'utilities/last_func.c',
  'utilities/line_wrapping.c',
  'utilities/linuxtimer.c',
  'utilities/timer.c',
  'utilities/tokenize.c',
  'utilities/unlink.c',
  'weak.c',
  'write_data.c',
)

if host_machine.cpu_family() in ['x86', 'x86_64']
  srcs += files('trampoline/x86-family/x86-tramp.S')

  if xed_dep.found()
    srcs += files(
      'unwind/x86-family/amd-xop.c',
      'unwind/x86-family/manual-intervals/x86-32bit-icc-variant.c',
      'unwind/x86-family/manual-intervals/x86-32bit-main.c',
      'unwind/x86-family/manual-intervals/x86-fail-intervals.c',
      'unwind/x86-family/manual-intervals/x86-gcc-adjust.c',
      'unwind/x86-family/manual-intervals/x86-gcc-main64.c',
      'unwind/x86-family/manual-intervals/x86-intel-align32.c',
      'unwind/x86-family/manual-intervals/x86-intel-align64.c',
      'unwind/x86-family/manual-intervals/x86-intel-composer2013-mic.c',
      'unwind/x86-family/manual-intervals/x86-intel11-f90main.c',
      'unwind/x86-family/manual-intervals/x86-linux-dlresolver.c',
      'unwind/x86-family/manual-intervals/x86-pgi-mp_pexit.c',
      'unwind/x86-family/x86-addsub.c',
      'unwind/x86-family/x86-and.c',
      'unwind/x86-family/x86-build-intervals.c',
      'unwind/x86-family/x86-call.c',
      'unwind/x86-family/x86-canonical.c',
      'unwind/x86-family/x86-cold-path.c',
      'unwind/x86-family/x86-debug.c',
      'unwind/x86-family/x86-decoder.c',
      'unwind/x86-family/x86-enter.c',
      'unwind/x86-family/x86-jump.c',
      'unwind/x86-family/x86-lea.c',
      'unwind/x86-family/x86-leave.c',
      'unwind/x86-family/x86-move.c',
      'unwind/x86-family/x86-process-inst.c',
      'unwind/x86-family/x86-push.c',
      'unwind/x86-family/x86-return.c',
      'unwind/x86-family/x86-unwind-interval-fixup.c',
      'unwind/x86-family/x86-unwind-interval.c',
      'unwind/x86-family/x86-unwind-support.c',
      'unwind/x86-family/x86-unwind.c',
      'unwind/x86-family/x86-validate-retn-addr.c',
      'utilities/arch/x86-family/x86-context-pc.c',
    )
  else
    # Fallback to the generic libunwind-based unwinder
    srcs += files(
      'unwind/common/default_validation_summary.c',
      'unwind/generic-libunwind/libunw-unwind.c',
      'utilities/arch/libunwind/libunwind-context-pc.c',
    )
  endif
elif host_machine.cpu_family() == 'ppc64'
  srcs += files(
    'trampoline/ppc64/ppc64-tramp.s',
    'unwind/common/default_validation_summary.c',
    'unwind/ppc64/ppc64-unwind-interval.c',
    'unwind/ppc64/ppc64-unwind.c',
    'utilities/arch/ppc64/ppc64-context-pc.c',
  )
elif host_machine.cpu_family() == 'aarch64'
  srcs += files(
    'trampoline/aarch64/aarch64-tramp.c',
    'unwind/common/default_validation_summary.c',
    'unwind/generic-libunwind/libunw-unwind.c',
    'utilities/arch/libunwind/libunwind-context-pc.c',
  )
elif host_machine.cpu_family() == 'mips'
  srcs += files('unwind/common/default_validation_summary.c')
elif host_machine.cpu_family() == 'ia64'
  srcs += files(
    'trampoline/ia64/ia64-tramp.s',
    'unwind/common/default_validation_summary.c',
    'unwind/generic-libunwind/libunw-unwind.c',
    'utilities/arch/ia64/ia64-context-pc.c',
  )
endif

if host_machine.cpu_family() == 'ppc64'
  srcs += files('trampoline/common/trampoline_eager.c')
else
  srcs += files('trampoline/common/trampoline_lazy.c')
endif

if has_perf_events
  srcs += files(
    'sample-sources/perf/event_custom.c',
    'sample-sources/perf/event_name_parser.c',
    'sample-sources/perf/linux_perf.c',
    'sample-sources/perf/perf-util.c',
    'sample-sources/perf/perf_event_open.c',
    'sample-sources/perf/perf_mmap.c',
    'sample-sources/perf/perf_skid.c',
  )

  if perfmon_dep.found()
    srcs += files('sample-sources/perf/perfmon-util.c')
  else
    srcs += files('sample-sources/perf/perfmon-util-dummy.c')
  endif

  _kernel_43_code = '''
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
#else
#error Kernel version too low, less than 4.3
#endif
'''
  if cc.compiles(_kernel_43_code, name: 'Linux >= 4.3')
    srcs += files('sample-sources/perf/kernel_blocking.c')
  else
    srcs += files('sample-sources/perf/kernel_blocking_stub.c')
  endif
endif

if papi_dep.found()
  if papi_has_components
    srcs += files(
      'sample-sources/papi-c-extended-info.c',
      'sample-sources/papi-c-intel.c',
      'sample-sources/papi-c.c',
    )
    if false  # get_option('papi_cupti')  # --enable-papi-c-cupti
      srcs += files('sample-sources/papi-c-cupti.c')
    endif
  else
    srcs += files('sample-sources/papi.c')
  endif

  if false  # get_option('papi_cupti')  # --enable-papi-c-cupti
    srcs += files('sample-sources/papi-c-cupti.c')
  endif
endif

if python.found()
  srcs += files('foil/python.c', 'logical/python.c')
endif

if cupti_dep.found()
  srcs += files(
    'foil/nvidia.c',
    'gpu/api/nvidia/cubin-id-map.c',
    'gpu/api/nvidia/cubin-symbols.c',
    'gpu/api/nvidia/cuda-api.c',
    'gpu/api/nvidia/cuda-correlation-id-map.c',
    'gpu/api/nvidia/cuda-device-map.c',
    'gpu/api/nvidia/cuda-function-id-map.c',
    'gpu/api/nvidia/cuda-function-name-map.c',
    'gpu/api/nvidia/cuda-kernel-data-map.c',
    'gpu/api/nvidia/cupti-activity-translate.c',
    'gpu/api/nvidia/cupti-analysis.c',
    'gpu/api/nvidia/cupti-api.c',
    'gpu/api/nvidia/cupti-gpu-api.c',
    'sample-sources/nvidia.c',
  )

  # Access to CUDA/CUPTI is relegated through this dlopen'd foil
  dlopen_nvidia_lib = shared_library(
    'hpcrun_dlopen_nvidia',
    'foil/nvidia-dlopen.c',
    gnu_symbol_visibility: 'hidden',
    implicit_include_directories: false,
    dependencies: [cupti_dep],
    install: true,
  )
  hpcrun_sonames.set_quoted(
    'HPCRUN_DLOPEN_NVIDIA_SO',
    fs.name(dlopen_nvidia_lib.full_path()),
  )
  libs += dlopen_nvidia_lib
endif

if rocm_dep.found()
  srcs += files(
    'foil/rocm.c',
    'gpu/api/rocm/rocm-activity.c',
    'gpu/api/rocm/rocm-agent-profile-map.c',
    'gpu/api/rocm/rocm-agent.c',
    'gpu/api/rocm/rocm-api.c',
    'gpu/api/rocm/rocm-binaries.c',
    'gpu/api/rocm/rocm-buffer.c',
    'gpu/api/rocm/rocm-bufferset.c',
    'gpu/api/rocm/rocm-callback.c',
    'gpu/api/rocm/rocm-cid.c',
    'gpu/api/rocm/rocm-codeobject-map.c',
    'gpu/api/rocm/rocm-codeobject.c',
    'gpu/api/rocm/rocm-configure.c',
    'gpu/api/rocm/rocm-context.c',
    'gpu/api/rocm/rocm-counter-set.c',
    'gpu/api/rocm/rocm-counter-vector.c',
    'gpu/api/rocm/rocm-counters.c',
    'gpu/api/rocm/rocm-extid.c',
    'gpu/api/rocm/rocm-interface.c',
    'gpu/api/rocm/rocm-ompt.c',
    'gpu/api/rocm/rocm-pauseresume.c',
    'gpu/api/rocm/rocm-regex.c',
    'gpu/api/rocm/rocm-sampling.c',
    'gpu/api/rocm/rocm-symbol-map.c',
    'gpu/api/rocm/rocm-threads.c',
    'gpu/api/rocm/rocm-utils.c',
    'sample-sources/openmp-target.c',
    'sample-sources/rocm-ss.c',
  )
endif

if opencl_dep.found()
  srcs += files(
    'foil/opencl.c',
    'gpu/api/opencl/opencl-activity-translate.c',
    'gpu/api/opencl/opencl-api.c',
    'gpu/api/opencl/opencl-context-map.c',
    'gpu/api/opencl/opencl-h2d-map.c',
    'gpu/api/opencl/opencl-kernel-loadmap-map.c',
    'gpu/api/opencl/opencl-memory-manager.c',
    'gpu/api/opencl/opencl-queue-map.c',
    'gpu/blame-shifting/blame-kernel-cleanup-map.c',
    'gpu/blame-shifting/blame.c',
    'sample-sources/opencl.c',
  )
endif

if level0_dep.found()
  srcs += files(
    'foil/level0.c',
    'gpu/api/intel/binaries/symbolVector.c',
    'gpu/api/intel/binaries/zebinSymbols.c',
    'gpu/api/intel/level0/level0-api.c',
    'gpu/api/intel/level0/level0-binary.c',
    'gpu/api/intel/level0/level0-command-list-context-map.c',
    'gpu/api/intel/level0/level0-command-list-map.c',
    'gpu/api/intel/level0/level0-command-process.c',
    'gpu/api/intel/level0/level0-command-queue-map.c',
    'gpu/api/intel/level0/level0-correlation-device-map.c',
    'gpu/api/intel/level0/level0-data-node.c',
    'gpu/api/intel/level0/level0-debug.c',
    'gpu/api/intel/level0/level0-device-properties.c',
    'gpu/api/intel/level0/level0-event-map.c',
    'gpu/api/intel/level0/level0-fence-map.c',
    'gpu/api/intel/level0/level0-handle-map.c',
    'gpu/api/intel/level0/level0-id-map.c',
    'gpu/api/intel/level0/level0-kernel-module-map.c',
    'gpu/api/intel/level0/level0-timestamps.c',
    'sample-sources/level0.c',
  )

  level0_pc_cpp_srcs = files(
    'gpu/api/intel/level0/level0-correlation-device-map.c',
    'gpu/api/intel/level0/pcsampling/level0-activity-generate.cpp',
    'gpu/api/intel/level0/pcsampling/level0-activity-send.cpp',
    'gpu/api/intel/level0/pcsampling/level0-activity-translate.cpp',
    'gpu/api/intel/level0/pcsampling/level0-assert.cpp',
    'gpu/api/intel/level0/pcsampling/level0-cmdlist-device-map.cpp',
    'gpu/api/intel/level0/pcsampling/level0-collector.cpp',
    'gpu/api/intel/level0/pcsampling/level0-context.cpp',
    'gpu/api/intel/level0/pcsampling/level0-correlation-id.cpp',
    'gpu/api/intel/level0/pcsampling/level0-device.cpp',
    'gpu/api/intel/level0/pcsampling/level0-driver.cpp',
    'gpu/api/intel/level0/pcsampling/level0-event.cpp',
    'gpu/api/intel/level0/pcsampling/level0-eventpool.cpp',
    'gpu/api/intel/level0/pcsampling/level0-kernel-properties-cache.cpp',
    'gpu/api/intel/level0/pcsampling/level0-kernel-properties.cpp',
    'gpu/api/intel/level0/pcsampling/level0-kernel-size-map.cpp',
    'gpu/api/intel/level0/pcsampling/level0-logging.cpp',
    'gpu/api/intel/level0/pcsampling/level0-metric-list.cpp',
    'gpu/api/intel/level0/pcsampling/level0-metric-profiler.cpp',
    'gpu/api/intel/level0/pcsampling/level0-metric-streamer.cpp',
    'gpu/api/intel/level0/pcsampling/level0-metric.cpp',
    'gpu/api/intel/level0/pcsampling/level0-module.cpp',
    'gpu/api/intel/level0/pcsampling/level0-pc-api-receiver.cpp',
    'gpu/api/intel/level0/pcsampling/level0-pc-interface.cpp',
    'gpu/api/intel/level0/pcsampling/level0-pc-manager.cpp',
    'gpu/api/intel/level0/pcsampling/level0-pc-stubs.cpp',
    'gpu/api/intel/level0/pcsampling/level0-tracing-callback-methods.cpp',
    'gpu/api/intel/level0/pcsampling/level0-tracing-callbacks.cpp',
    'gpu/api/intel/level0/pcsampling/level0-tracing.cpp',
    'gpu/api/intel/level0/pcsampling/level0-unique-id.cpp',
  )

  level0_pc_cxx_lib = shared_library(
    'hpcrun_level0_pc_cxx',
    level0_pc_cpp_srcs,
    gnu_symbol_visibility: 'hidden',
    implicit_include_directories: false,
    override_options: ['b_lundef=false'],
    dependencies: [
      common_lean_deps,
      dl_dep,
      level0_dep.partial_dependency(compile_args: true, includes: true),
      libunwind_dep.partial_dependency(compile_args: true, includes: true),
      threads_dep,
    ],
    link_language: 'cpp',
    install: true,
  )
  hpcrun_sonames.set_quoted(
    'HPCRUN_LEVEL0_PC_CXX_SO',
    fs.name(level0_pc_cxx_lib.full_path()),
  )
  libs += level0_pc_cxx_lib

  srcs += files('gpu/api/intel/level0/level0-pc-shim.c')
endif

if gtpin_dep.found()
  srcs += files('gpu/api/intel/gtpin/gtpin-shim.c')

  gtpin_cxx_lib = shared_library(
    'hpcrun_gtpin_cxx',
    'foil/gtpin.cpp',
    'gpu/api/intel/gtpin/gtpin-instrumentation.cpp',
    'gpu/api/intel/binaries/symbolVector.c',
    'gpu/api/intel/binaries/zebinSymbols.c',
    common_lean_srcs,
    gnu_symbol_visibility: 'hidden',
    implicit_include_directories: false,
    dependencies: [
      common_lean_deps,
      dl_dep,
      gtpin_dep.partial_dependency(compile_args: true, includes: true),
      libelf_dep,
      libunwind_dep,
      tbb_malloc_dep,
      threads_dep,
    ],
    install: true,
  )
  hpcrun_sonames.set_quoted('HPCRUN_GTPIN_CXX_SO', fs.name(gtpin_cxx_lib.full_path()))
  libs += gtpin_cxx_lib

  # Access to GTPin is relegated through this dlopen'd foil
  dlopen_gtpin_lib = shared_library(
    'hpcrun_dlopen_gtpin',
    'foil/gtpin-dlopen.cpp',
    gnu_symbol_visibility: 'hidden',
    implicit_include_directories: false,
    dependencies: [gtpin_dep],
    install: true,
  )
  hpcrun_sonames.set_quoted(
    'HPCRUN_DLOPEN_GTPIN_SO',
    fs.name(dlopen_gtpin_lib.full_path()),
  )
  libs += dlopen_gtpin_lib
endif

main_hpcrun_lib = shared_library(
  'hpcrun',
  srcs,
  common_lean_srcs,
  gnu_symbol_visibility: 'hidden',
  link_language: 'c',
  implicit_include_directories: true,
  dependencies: [
    common_lean_deps,
    cupti_dep.partial_dependency(compile_args: true, includes: true),
    dl_dep,
    gtpin_dep.partial_dependency(compile_args: true, includes: true),
    level0_dep.partial_dependency(compile_args: true, includes: true),
    libdw_dep,
    libelf_dep,
    libunwind_dep,
    lzma_dep,
    math_dep,
    opencl_dep,
    papi_dep,
    perfmon_dep,
    rocprofiler_dep.partial_dependency(compile_args: true, includes: true),
    #rocm_dep.partial_dependency(compile_args: true, includes: true),
    rt_dep,
    xed_dep,
    (python.found() ? python.dependency() : dependency('', required: false)).partial_dependency(
      compile_args: true,
      includes: true,
    ),
  ],
  install: true,
)
hpcrun_sonames.set_quoted('HPCRUN_SO', fs.name(main_hpcrun_lib.full_path()))
libs += main_hpcrun_lib

# Some of the libraries defined below are designed to be loaded into the application
# using LD_PRELOAD. These libraries must not have RPATH or RUNPATH attributes, which may
# alter and conflict with the library search path of the application. We use patchelf to
# remove these attributes during the build.
patchelf_versions = '>=0.11'
patchelf_default_options = ['warning_level=2']
if 'patchelf' in get_option('force_fallback_for') or get_option('wrap_mode') == 'forcefallback'
  subproject(
    'patchelf',
    version: patchelf_versions,
    default_options: patchelf_default_options,
  )
endif
patchelf = find_program(
  'patchelf',
  version: patchelf_versions,
  native: true,
  default_options: patchelf_default_options,
)

# Libraries needed to support the "audit" interface for collecting loaded libraries
audit_lib = shared_library(
  'hpcrun_audit',
  'audit/auditor.c',
  'audit/dtv.c',
  'audit/private-ns.c',
  'audit/symbol-bindings.c',
  'foil/workarounds/dl-iterate-phdr.c',
  'foil/workarounds/pthread-key.c',
  'audit/symbol-bindings.c',
  gnu_symbol_visibility: 'hidden',
  implicit_include_directories: false,
  dependencies: [dl_dep, threads_dep],
  link_language: 'c',
  install: true,
)
hpcrun_sonames.set_quoted('HPCRUN_AUDIT_SO', fs.name(audit_lib.full_path()))
libs += audit_lib

# Symbol interceptions are handled via this LD_PRELOAD'd foil
preload_rawlib = shared_library(
  'hpcrun_preload_UNSANITIZED',
  'foil/common-preload.c',
  'foil/client-preload.c',
  'foil/libc-preload.c',
  'foil/mpi-preload.c',
  'foil/ompt-preload.c',
  gnu_symbol_visibility: 'hidden',
  implicit_include_directories: false,
  dependencies: [dl_dep, threads_dep],
  link_language: 'c',
)
preload_lib_soname = fs.name(preload_rawlib.full_path()).replace('_UNSANITIZED', '')
preload_lib = custom_target(
  input: preload_rawlib,
  output: preload_lib_soname,
  command: [patchelf, '--remove-rpath', '--output', '@OUTPUT@', '@INPUT@'],
  install: true,
  install_dir: get_option('libdir'),
)
hpcrun_sonames.set_quoted('HPCRUN_PRELOAD_SO', preload_lib_soname)
libs += preload_lib

# Optional symbol interception LD_PRELOAD'd foils for Glibc
preload_libc_io_rawlib = shared_library(
  'hpcrun_preload_libc_io_UNSANITIZED',
  'foil/common-preload.c',
  'foil/libc-io-preload.c',
  gnu_symbol_visibility: 'hidden',
  implicit_include_directories: false,
  dependencies: [dl_dep, threads_dep],
  link_language: 'c',
)
preload_libc_io_lib_soname = fs.name(preload_libc_io_rawlib.full_path()).replace(
  '_UNSANITIZED',
  '',
)
preload_libc_io_lib = custom_target(
  input: preload_libc_io_rawlib,
  output: preload_libc_io_lib_soname,
  command: [patchelf, '--remove-rpath', '--output', '@OUTPUT@', '@INPUT@'],
  install: true,
  install_dir: get_option('libdir'),
)
hpcrun_sonames.set_quoted('HPCRUN_PRELOAD_LIBC_IO_SO', preload_libc_io_lib_soname)
libs += preload_libc_io_lib

preload_libc_alloc_rawlib = shared_library(
  'hpcrun_preload_libc_alloc_UNSANITIZED',
  'foil/common-preload.c',
  'foil/libc-alloc-preload.c',
  gnu_symbol_visibility: 'hidden',
  implicit_include_directories: false,
  dependencies: [dl_dep, threads_dep],
  link_language: 'c',
)
preload_libc_alloc_lib_soname = fs.name(preload_libc_alloc_rawlib.full_path()).replace(
  '_UNSANITIZED',
  '',
)
preload_libc_alloc_lib = custom_target(
  input: preload_libc_alloc_rawlib,
  output: preload_libc_alloc_lib_soname,
  command: [patchelf, '--remove-rpath', '--output', '@OUTPUT@', '@INPUT@'],
  install: true,
  install_dir: get_option('libdir'),
)
hpcrun_sonames.set_quoted('HPCRUN_PRELOAD_LIBC_ALLOC_SO', preload_libc_alloc_lib_soname)
libs += preload_libc_alloc_lib

preload_libc_sync_rawlib = shared_library(
  'hpcrun_preload_libc_sync_UNSANITIZED',
  'foil/common-preload.c',
  'foil/libc-sync-preload.c',
  gnu_symbol_visibility: 'hidden',
  implicit_include_directories: false,
  dependencies: [dl_dep, threads_dep],
  link_language: 'c',
)
preload_libc_sync_lib_soname = fs.name(preload_libc_sync_rawlib.full_path()).replace(
  '_UNSANITIZED',
  '',
)
preload_libc_sync_lib = custom_target(
  input: preload_libc_sync_rawlib,
  output: preload_libc_sync_lib_soname,
  command: [patchelf, '--remove-rpath', '--output', '@OUTPUT@', '@INPUT@'],
  install: true,
  install_dir: get_option('libdir'),
)
hpcrun_sonames.set_quoted('HPCRUN_PRELOAD_LIBC_SYNC_SO', preload_libc_sync_lib_soname)
libs += preload_libc_sync_lib

# Optional symbol interception LD_PRELOAD'd foils for libdl
preload_libdl_rawlib = shared_library(
  'hpcrun_preload_libdl_UNSANITIZED',
  'foil/libdl-preload.c',
  gnu_symbol_visibility: 'hidden',
  implicit_include_directories: true,
  dependencies: [dl_dep, threads_dep],
  link_language: 'c',
)
preload_libdl_lib_soname = fs.name(preload_libdl_rawlib.full_path()).replace(
  '_UNSANITIZED',
  '',
)
preload_libdl_lib = custom_target(
  input: preload_libdl_rawlib,
  output: preload_libdl_lib_soname,
  command: [patchelf, '--remove-rpath', '--output', '@OUTPUT@', '@INPUT@'],
  install: true,
  install_dir: get_option('libdir'),
)
hpcrun_sonames.set_quoted('HPCRUN_PRELOAD_LIBDL_SO', preload_libdl_lib_soname)
libs += preload_libdl_lib

preload_libdl_dlmopen_rawlib = shared_library(
  'hpcrun_preload_libdl_dlmopen_UNSANITIZED',
  'foil/libdl-dlmopen-preload.c',
  gnu_symbol_visibility: 'hidden',
  implicit_include_directories: false,
  dependencies: [dl_dep],
  link_language: 'c',
)
preload_libdl_dlmopen_lib_soname = fs.name(preload_libdl_dlmopen_rawlib.full_path()).replace(
  '_UNSANITIZED',
  '',
)
preload_libdl_dlmopen_lib = custom_target(
  input: preload_libdl_dlmopen_rawlib,
  output: preload_libdl_dlmopen_lib_soname,
  command: [patchelf, '--remove-rpath', '--output', '@OUTPUT@', '@INPUT@'],
  install: true,
  install_dir: get_option('libdir'),
)
hpcrun_sonames.set_quoted(
  'HPCRUN_PRELOAD_LIBDL_DLMOPEN_SO',
  preload_libdl_dlmopen_lib_soname,
)
libs += preload_libdl_dlmopen_lib

# Optional symbol interception LD_PRELOAD'd foils for smaller features
preload_gprof_rawlib = shared_library(
  'hpcrun_preload_gprof_UNSANITIZED',
  'foil/gprof-preload.c',
  gnu_symbol_visibility: 'hidden',
  implicit_include_directories: false,
  link_language: 'c',
)
preload_gprof_lib_soname = fs.name(preload_gprof_rawlib.full_path()).replace(
  '_UNSANITIZED',
  '',
)
preload_gprof_lib = custom_target(
  input: preload_gprof_rawlib,
  output: preload_gprof_lib_soname,
  command: [patchelf, '--remove-rpath', '--output', '@OUTPUT@', '@INPUT@'],
  install: true,
  install_dir: get_option('libdir'),
)
hpcrun_sonames.set_quoted('HPCRUN_PRELOAD_GPROF_SO', preload_gprof_lib_soname)
libs += preload_gprof_lib

preload_ga_rawlib = shared_library(
  'hpcrun_preload_ga_UNSANITIZED',
  'foil/common-preload.c',
  'foil/ga-preload.c',
  gnu_symbol_visibility: 'hidden',
  implicit_include_directories: false,
  dependencies: [dl_dep, threads_dep],
  link_language: 'c',
)
preload_ga_lib_soname = fs.name(preload_ga_rawlib.full_path()).replace(
  '_UNSANITIZED',
  '',
)
preload_ga_lib = custom_target(
  input: preload_ga_rawlib,
  output: preload_ga_lib_soname,
  command: [patchelf, '--remove-rpath', '--output', '@OUTPUT@', '@INPUT@'],
  install: true,
  install_dir: get_option('libdir'),
)
hpcrun_sonames.set_quoted('HPCRUN_PRELOAD_GA_SO', preload_ga_lib_soname)
libs += preload_ga_lib

# Symbols needed for integration with rocprofiler-sdk are defined by this foil
if rocm_dep.found()
  preload_rocm_rawlib = shared_library(
    'hpcrun_preload_rocm_UNSANITIZED',
    'foil/common-preload.c',
    'foil/rocm-preload.c',
    gnu_symbol_visibility: 'hidden',
    implicit_include_directories: false,
    dependencies: [
      dl_dep,
      rocm_dep.partial_dependency(compile_args: true, includes: true),
      threads_dep,
    ],
    link_language: 'c',
  )
  preload_rocm_lib_soname = fs.name(preload_rocm_rawlib.full_path()).replace(
    '_UNSANITIZED',
    '',
  )
  preload_rocm_lib = custom_target(
    input: preload_rocm_rawlib,
    output: preload_rocm_lib_soname,
    command: [patchelf, '--remove-rpath', '--output', '@OUTPUT@', '@INPUT@'],
    install: true,
    install_dir: get_option('libdir'),
  )
  hpcrun_sonames.set_quoted('HPCRUN_PRELOAD_ROCM_SO', preload_rocm_lib_soname)
  libs += preload_rocm_lib
  dlopen_rocm_lib = shared_library(
    'hpcrun_dlopen_rocm',
    'foil/rocm-dlopen.c',
    gnu_symbol_visibility: 'hidden',
    implicit_include_directories: false,
    link_with: main_hpcrun_lib,
    dependencies: [rocm_dep],
    install: true,
  )
  hpcrun_sonames.set_quoted(
    'HPCRUN_DLOPEN_ROCM_SO',
    fs.name(dlopen_rocm_lib.full_path()),
  )
  libs += dlopen_rocm_lib
endif

# Optional symbol interception LD_PRELOAD'd foil for OpenCL
if opencl_dep.found()
  preload_opencl_rawlib = shared_library(
    'hpcrun_preload_opencl_UNSANITIZED',
    'foil/common-preload.c',
    'foil/opencl-preload.c',
    gnu_symbol_visibility: 'hidden',
    implicit_include_directories: false,
    dependencies: [dl_dep, threads_dep, opencl_dep],
    link_language: 'c',
  )
  preload_opencl_lib_soname = fs.name(preload_opencl_rawlib.full_path()).replace(
    '_UNSANITIZED',
    '',
  )
  preload_opencl_lib = custom_target(
    input: preload_opencl_rawlib,
    output: preload_opencl_lib_soname,
    command: [patchelf, '--remove-rpath', '--output', '@OUTPUT@', '@INPUT@'],
    install: true,
    install_dir: get_option('libdir'),
  )
  hpcrun_sonames.set_quoted('HPCRUN_PRELOAD_OPENCL_SO', preload_opencl_lib_soname)
  libs += preload_opencl_lib
endif

# Optional symbol interception LD_PRELOAD'd foil for Level Zero
if level0_dep.found()
  preload_level0_rawlib = shared_library(
    'hpcrun_preload_level0_UNSANITIZED',
    'foil/common-preload.c',
    'foil/level0-preload.c',
    gnu_symbol_visibility: 'hidden',
    implicit_include_directories: false,
    dependencies: [dl_dep, threads_dep, level0_dep],
    link_language: 'c',
  )
  preload_level0_lib_soname = fs.name(preload_level0_rawlib.full_path()).replace(
    '_UNSANITIZED',
    '',
  )
  preload_level0_lib = custom_target(
    input: preload_level0_rawlib,
    output: preload_level0_lib_soname,
    command: [patchelf, '--remove-rpath', '--output', '@OUTPUT@', '@INPUT@'],
    install: true,
    install_dir: get_option('libdir'),
  )
  hpcrun_sonames.set_quoted('HPCRUN_PRELOAD_LEVEL0_SO', preload_level0_lib_soname)
  libs += preload_level0_lib
endif

# Finalize the sonames header before any builds happen
hpcrun_sonames.set_quoted(
  'HPCRUN_INSTALL_LIBDIR',
  get_option('prefix') / get_option('libdir'),
)
configure_file(output: 'hpcrun-sonames.h', configuration: hpcrun_sonames)

# Configure and install the hpcrun launcher
hpcrun = executable(
  'hpcrun',
  'launch.cpp',
  version_cpp,
  cpp_args: [
    '-DHPCRUN_USE_AUDITOR_BY_DEFAULT=@0@'.format(
      get_option('hpcrun_use_auditor_by_default') ? '1' : '0',
    ),
  ],
  install: true,
)

# Build and install the client API
install_headers('hpctoolkit.h')
hpctoolkit_client_lib = library('hpctoolkit', 'hpctoolkit.c', install: true)

# Settings needed for dependencies
hpcrun_test_depends = libs
test_depends += hpcrun_test_depends
