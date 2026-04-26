# SPDX-License-Identifier: MIT OR Apache-2.0
# MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
# Meson build system (alternative to CMake)
#
# Usage:
#   meson setup builddir
#   ninja -C builddir
#   ninja -C builddir test

project('mvgal', 'c',
  version : '0.2.0',
  license : 'GPL-2.0-only',
  default_options : [
    'c_std=c17',
    'warning_level=2',
    'b_pie=true',
  ],
  meson_version : '>= 1.0',
)

# ============================================================================
# Options
# ============================================================================

opt_vulkan     = get_option('with_vulkan')
opt_opencl     = get_option('with_opencl')
opt_cuda       = get_option('with_cuda')
opt_daemon     = get_option('with_daemon')
opt_tests      = get_option('with_tests')
opt_benchmarks = get_option('with_benchmarks')
opt_kernel     = get_option('with_kernel_module')

# ============================================================================
# Compiler flags
# ============================================================================

cc = meson.get_compiler('c')

# Only add flags not already covered by warning_level=2
add_project_arguments(['-Wshadow', '-Wformat=2', '-D_GNU_SOURCE', '-D_FILE_OFFSET_BITS=64'], language : 'c')

# ============================================================================
# Dependencies
# ============================================================================

threads_dep = dependency('threads')
drm_dep     = dependency('libdrm', required : true)

# Try libpci first, then pciaccess (Fedora/Arch), then make optional
pci_dep = dependency('libpci', required : false)
if not pci_dep.found()
  pci_dep = dependency('pciaccess', required : false)
endif
if not pci_dep.found()
  warning('libpci/pciaccess not found — PCI functionality will be limited')
  pci_dep = dependency('', required : false)  # empty dep
endif

udev_dep = dependency('libudev', required : true)
m_dep    = cc.find_library('m', required : false)
dl_dep   = cc.find_library('dl', required : false)

vulkan_dep = dependency('vulkan', required : opt_vulkan)
opencl_dep = dependency('OpenCL', required : opt_opencl)

# ============================================================================
# Include directories
# ============================================================================

# inc_mvgal_headers exposes bare-name includes like "mvgal_types.h"
# inc_mvgal exposes path-qualified includes like "mvgal/mvgal_types.h"
inc_root          = include_directories('.')
inc_mvgal         = include_directories('include')
inc_mvgal_headers = include_directories('include/mvgal')
inc_userspace     = include_directories('src/userspace')

# Convenience list used by all core targets
core_incs = [inc_root, inc_mvgal, inc_mvgal_headers, inc_userspace]

# ============================================================================
# Core library sources
# ============================================================================

core_sources = files(
  'src/userspace/api/mvgal_api.c',
  'src/userspace/api/mvgal_log.c',
  'src/userspace/daemon/gpu_manager.c',
  'src/userspace/daemon/config.c',
  'src/userspace/daemon/ipc.c',
  'src/userspace/execution/execution.c',
  'src/userspace/memory/memory.c',
  'src/userspace/memory/dmabuf.c',
  'src/userspace/memory/allocator.c',
  'src/userspace/memory/sync.c',
  'src/userspace/scheduler/scheduler.c',
  'src/userspace/scheduler/load_balancer.c',
  'src/userspace/scheduler/workload_splitter.c',
  'src/userspace/scheduler/strategy/afr.c',
  'src/userspace/scheduler/strategy/sfr.c',
  'src/userspace/scheduler/strategy/task.c',
  'src/userspace/scheduler/strategy/compute_offload.c',
  'src/userspace/scheduler/strategy/hybrid.c',
)

core_deps = [threads_dep, drm_dep, pci_dep, udev_dep, m_dep]

# Static core library
libmvgal_core = static_library('mvgal_core',
  core_sources,
  include_directories : core_incs,
  dependencies : core_deps,
  install : true,
)

# Shared library
libmvgal = shared_library('mvgal',
  core_sources,
  include_directories : core_incs,
  dependencies : core_deps,
  version : meson.project_version(),
  soversion : '0',
  install : true,
)

mvgal_dep = declare_dependency(
  link_with : libmvgal,
  include_directories : [inc_root, inc_mvgal, inc_mvgal_headers],
)

# ============================================================================
# Vulkan layer (optional)
# ============================================================================

if opt_vulkan and vulkan_dep.found()
  libvk_layer = shared_library('VK_LAYER_MVGAL',
    'src/userspace/intercept/vulkan/vk_layer.c',
    include_directories : core_incs,
    dependencies : [threads_dep, vulkan_dep],
    link_args : ['-Wl,-Bsymbolic-functions'],
    install : true,
    install_dir : get_option('libdir'),
  )

  # Generate the layer manifest JSON next to the .so so tests can find it
  vk_manifest_conf = configuration_data()
  vk_manifest_conf.set('VK_LAYER_LIB', libvk_layer.full_path())
  configure_file(
    input  : 'src/userspace/intercept/vulkan/manifest.json.in',
    output : 'VK_LAYER_MVGAL.json',
    copy   : true,
  )

  install_data(
    'src/userspace/intercept/vulkan/manifest.json.in',
    install_dir : get_option('datadir') / 'vulkan' / 'implicit_layer.d',
    rename : 'VK_LAYER_MVGAL.json',
  )
endif

# ============================================================================
# OpenCL layer (optional)
# ============================================================================

if opt_opencl and opencl_dep.found()
  shared_library('mvgal_opencl',
    'src/userspace/intercept/opencl/cl_intercept.c',
    include_directories : core_incs,
    dependencies : [threads_dep, opencl_dep, dl_dep],
    link_with : libmvgal_core,
    install : true,
  )
endif

# ============================================================================
# Daemon (optional)
# ============================================================================

if opt_daemon
  executable('mvgal-daemon',
    files(
      'src/userspace/daemon/main.c',
      'src/userspace/daemon/gpu_manager.c',
      'src/userspace/daemon/config.c',
      'src/userspace/daemon/ipc.c',
    ),
    include_directories : core_incs,
    dependencies : core_deps,
    link_with : libmvgal_core,
    install : true,
    install_dir : get_option('sbindir'),
  )
endif

# ============================================================================
# Tests (optional)
# ============================================================================

if opt_tests
  test_deps = [threads_dep, drm_dep, pci_dep, udev_dep]

  foreach t : ['test_gpu_detection', 'test_memory', 'test_scheduler',
               'test_core_api', 'test_execution', 'test_config']
    exe = executable(t,
      'src/tests/tests/unit/' + t + '.c',
      include_directories : core_incs,
      dependencies : test_deps,
      link_with : libmvgal_core,
    )
    test(t, exe)
  endforeach

  exe_multi = executable('test_multi_gpu',
    'src/tests/tests/integration/test_multi_gpu_validation.c',
    include_directories : core_incs,
    dependencies : test_deps,
    link_with : libmvgal_core,
  )
  test('test_multi_gpu', exe_multi)

  if opt_vulkan and vulkan_dep.found()
    exe_vk = executable('test_vulkan_layer_submit',
      'src/tests/tests/integration/test_vulkan_layer_submit.c',
      include_directories : [inc_root, inc_mvgal, inc_mvgal_headers],
      dependencies : [vulkan_dep],
      # Point the test at the build root where VK_LAYER_MVGAL.json is generated
      c_args : ['-DMVGAL_TEST_LAYER_DIR="' + meson.current_build_dir() + '"'],
    )
    test('test_vulkan_layer_submit', exe_vk)
  endif
endif

# ============================================================================
# Install headers
# ============================================================================

install_headers(
  'include/mvgal/mvgal.h',
  'include/mvgal/mvgal_types.h',
  'include/mvgal/mvgal_gpu.h',
  'include/mvgal/mvgal_memory.h',
  'include/mvgal/mvgal_scheduler.h',
  'include/mvgal/mvgal_log.h',
  'include/mvgal/mvgal_config.h',
  'include/mvgal/mvgal_ipc.h',
  'include/mvgal/mvgal_version.h',
  'include/mvgal/mvgal_execution.h',
  subdir : 'mvgal',
)

# ============================================================================
# pkg-config
# ============================================================================

pkg = import('pkgconfig')
pkg.generate(libmvgal,
  name : 'mvgal',
  description : 'Multi-Vendor GPU Aggregation Layer for Linux',
  version : meson.project_version(),
  filebase : 'mvgal',
)

# ============================================================================
# Summary
# ============================================================================

summary({
  'Vulkan layer'  : opt_vulkan and vulkan_dep.found(),
  'OpenCL layer'  : opt_opencl and opencl_dep.found(),
  'Daemon'        : opt_daemon,
  'Tests'         : opt_tests,
  'Benchmarks'    : opt_benchmarks,
  'Kernel module' : opt_kernel,
}, section : 'Components')
