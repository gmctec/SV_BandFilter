# SPDX-License-Identifier: GPL-2.0-or-later
# SV_BandFilter - Copyright (C) 2026 gmctec

project(
  'sv_bandfilter',
  'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: [
    'warning_level=2',
    'buildtype=release',
  ],
  meson_version: '>= 0.57.0'
)

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)
inc = include_directories('include', 'src')
system = host_machine.system()

# Filter core as a static library: shared by the plugin and the tests.
core_lib = static_library(
  'bandfilter_core',
  'src/bandfilter_core.c',
  include_directories: inc,
  dependencies: m_dep,
  pic: true,
)

# Sonic Visualiser looks for *.dll on Windows, *.dylib or *.so on macOS,
# and *.so on Linux. Name the file without a "lib" prefix everywhere.
plugin_kwargs = {}
if system == 'windows'
  plugin_kwargs += { 'vs_module_defs': 'windows/sv_bandfilter.def' }
  plugin_suffix = 'dll'
elif system == 'darwin'
  plugin_suffix = 'dylib'
else
  plugin_suffix = 'so'
endif

plugin = shared_module(
  'sv_bandfilter',
  'src/bandfilter_ladspa.c',
  include_directories: inc,
  link_with: core_lib,
  dependencies: m_dep,
  name_prefix: '',
  name_suffix: plugin_suffix,
  gnu_symbol_visibility: 'default',
  install: true,
  install_dir: get_option('libdir') / 'ladspa',
  kwargs: plugin_kwargs,
)

# Response test: drives the plugin through its LADSPA descriptor, exactly
# as a host would, and checks gain inside and outside the band.
test_exe = executable(
  'test_response',
  'tests/test_response.c', 'src/bandfilter_ladspa.c',
  include_directories: inc,
  link_with: core_lib,
  dependencies: m_dep,
)
test('response', test_exe)
