# Copyright © 2019-2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

pv_bin = []

conf_data = configuration_data()
conf_data.set_quoted('VERSION', version)

configure_file(
  input : 'config.h.in',
  output : '_pressure-vessel-config.h',
  configuration : conf_data,
)

pv_dir = 'lib/pressure-vessel/relocatable'
pv_bindir = pv_dir + '/bin'
pv_rpath = bin_rpath

pv_c_args = ['-include', '_pressure-vessel-config.h']

# This trips some warnings in the libglnx subproject but not in
# pressure-vessel itself
pv_warning_cflags = [
    '-Wsign-compare',
]

scripts = [
  'pressure-vessel-unruntime',
]

foreach script : scripts
  install_data(
    script,
    install_dir : pv_bindir,
  )
endforeach

libexec_scripts = [
  'pv-locale-gen',
]

foreach script : libexec_scripts
  install_data(
    script,
    install_dir : pkglibexecdir,
  )
endforeach

# Headers to scan for enum/flags types.
headers = [
  'mtree.h',
  'runtime.h',
  'utils.h',
]

enums = gnome.mkenums_simple(
  'enumtypes',
  sources : headers,
)

pressure_vessel_utils = static_library(
  'pressure-vessel-utils',
  sources : [
    'adverb-preload.c',
    'adverb-preload.h',
    'flatpak-bwrap.c',
    'flatpak-bwrap-private.h',
    'flatpak-utils-base.c',
    'flatpak-utils-base-private.h',
    'flatpak-utils.c',
    'flatpak-utils-private.h',
    'mtree.c',
    'mtree.h',
    'supported-architectures.c',
    'supported-architectures.h',
    'tree-copy.c',
    'tree-copy.h',
    'utils.c',
    'utils.h',
  ] + enums,
  c_args : pv_c_args,
  dependencies : [
    threads,
    gio_unix,
    libglnx_dep,
    libsteamrt_generated_headers_dep,
    libsteamrt_static_dep,
  ],
  include_directories : pv_include_dirs,
  install: false,
)
pressure_vessel_utils_dep = declare_dependency(
  link_with : pressure_vessel_utils,
  dependencies : libsteamrt_generated_headers_dep,
  sources : [enums[1]],
)

pv_adverb_lib = static_library(
  'pv-adverb-lib',
  sources : [
    'adverb-sdl.c',
    'adverb-sdl.h',
    'per-arch-dirs.c',
    'per-arch-dirs.h',
    'wrap-interactive.c',
    'wrap-interactive.h',
  ],
  c_args : pv_c_args,
  dependencies : [
    libsteamrt_generated_headers_dep,
    pressure_vessel_utils_dep,
    gio_unix,
    libglnx_dep,
  ],
  include_directories : pv_include_dirs,
)
pv_adverb_lib_dep = declare_dependency(
  dependencies : libsteamrt_generated_headers_dep,
  link_with : pv_adverb_lib,
)

pv_bin += executable(
  'pv-adverb',
  sources : [
    'adverb.c',
  ],
  c_args : pv_c_args,
  dependencies : [
    libsteamrt_generated_headers_dep,
    pv_adverb_lib_dep,
    threads,
    gio_unix,
    libglnx_dep,
    json_glib,
  ],
  include_directories : pv_include_dirs,
  install : true,
  install_dir : pkglibexecdir,
  build_rpath : pkglibexec_rpath,
  install_rpath : pkglibexec_rpath,
)

pressure_vessel_wrap_lib = static_library(
  'pressure-vessel-wrap-lib',
  sources : [
    'bwrap.c',
    'bwrap.h',
    'exports.c',
    'exports.h',
    'flatpak-common-types-private.h',
    'flatpak-context-private.h',
    'flatpak-exports.c',
    'flatpak-exports-private.h',
    'flatpak-glib-backports.c',
    'flatpak-run.c',
    'flatpak-run-dbus.c',
    'flatpak-run-private.h',
    'flatpak-run-pulseaudio.c',
    'flatpak-run-sockets.c',
    'flatpak-run-wayland.c',
    'flatpak-run-x11.c',
    'graphics-provider.c',
    'graphics-provider.h',
    'passwd.c',
    'passwd.h',
    'runtime.c',
    'runtime.h',
    'wrap-context.c',
    'wrap-context.h',
    'wrap-discord.c',
    'wrap-discord.h',
    'wrap-flatpak.c',
    'wrap-flatpak.h',
    'wrap-home.h',
    'wrap-home.c',
    'wrap-pipewire.c',
    'wrap-pipewire.h',
    'wrap-setup.c',
    'wrap-setup.h',
  ],
  c_args : pv_c_args,
  dependencies : [
    libsteamrt_generated_headers_dep,
    pressure_vessel_utils_dep,
    systemd_codegen_dep,
    gio_unix,
    xau,
    libglnx_dep,
    json_glib,
  ],
  include_directories : pv_include_dirs,
)
pressure_vessel_wrap_lib_dep = declare_dependency(
  dependencies : [libsteamrt_generated_headers_dep],
  link_with : pressure_vessel_wrap_lib,
  sources : [systemd_dbus[1]],
)

pv_bin += executable(
  'pressure-vessel-wrap',
  sources : [
    'wrap.c',
  ],
  c_args : pv_c_args,
  dependencies : [
    gio_unix,
    libglnx_dep,
    pressure_vessel_wrap_lib_dep,
  ],
  include_directories : pv_include_dirs,
  install : true,
  install_dir : pv_bindir,
  build_rpath : pv_rpath,
  install_rpath : pv_rpath,
)

pv_bin += executable(
  'pv-verify',
  sources : [
    'verify.c',
  ],
  c_args : pv_c_args,
  dependencies : [
    gio_unix,
    libglnx_dep,
    pressure_vessel_utils_dep,
  ],
  include_directories : pv_include_dirs,
  install : true,
  install_dir : pv_bindir,
  build_rpath : pv_rpath,
  install_rpath : pv_rpath,
)

pv_bin += executable(
  'pv-try-setlocale',
  sources : [
    'try-setlocale.c',
  ],
  c_args : pv_c_args,
  include_directories : pv_include_dirs,
  install : true,
  install_dir : pkglibexecdir,
)

if build_man_pages
  foreach tool : [
    'pv-adverb',
    'pv-locale-gen',
    'pv-try-setlocale',
    'pv-verify',
    'unruntime',
    'wrap',
  ]
    if tool.startswith('pv-')
      input = tool.substring(3) + '.1.md'
      output = tool + '.1'
    else
      input = tool + '.1.md'
      output = 'pressure-vessel-' + tool + '.1'
    endif

    custom_target(
      tool + '.1',
      build_by_default : true,
      command : [
        pandoc,
        '-s',
        '-o', '@OUTPUT@',
        '-f', pandoc_markdown_nosmart,
        '-t', 'man',
        '@INPUT@',
      ],
      input : input,
      output : output,
      install : true,
      install_dir : join_paths(
        get_option('prefix'),
        get_option('mandir'),
        'man1',
      ),
    )
  endforeach
endif

test_depends += pv_bin

# vim:set sw=2 sts=2 et:
