# Copyright © 2019-2021 Collabora Ltd.
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

srt_bin = []
uninstalled_env_vars += {'SRT_BIN_PATH': meson.current_build_dir()}

# Programs that only need glibc
libc_programs = [
  'launcher-interface-0',
]

foreach bin_name : libc_programs
  srt_bin += executable(
    'steam-runtime-' + bin_name,
    bin_name + '.c',
    c_args : srt_c_args,
    dependencies : [libsteamrt_libc_utils_dep],
    install : true,
  )
endforeach

# Programs that need GLib
glib_programs = [
  'check-requirements',
  'identify-library-abi',
  'steam-remote',
  'supervisor',
  'urlopen',
]

foreach bin_name : glib_programs
  srt_bin += executable(
    'steam-runtime-' + bin_name,
    bin_name + '.c',
    c_args : srt_c_args,
    dependencies : [glib, gobject, gio_unix, libglnx_dep, libsteamrt_static_dep],
    install : true,
    # Use the adjacent GLib, ignoring LD_LIBRARY_PATH even if set
    build_rpath : bin_rpath,
    install_rpath : bin_rpath,
  )
endforeach

# Programs that need GLib and json-glib
json_programs = [
  'input-monitor',
  'system-info',
]

foreach bin_name : json_programs
  srt_bin += executable(
    'steam-runtime-' + bin_name,
    bin_name + '.c',
    c_args : srt_c_args,
    dependencies : [glib, gobject, gio_unix, json_glib, libglnx_dep, libsteamrt_static_dep],
    install : true,
    # Use the adjacent json-glib, ignoring LD_LIBRARY_PATH even if set
    build_rpath : bin_rpath,
    install_rpath : bin_rpath,
  )
endforeach

# Slightly different: different prefix (so that `pkill steam` will leave
# it running for long enough to finish writing its log!),
# and install in libexecdir
srt_bin += executable(
  'srt-logger',
  sources: [
    'logger.c',
    'logger-mkfifo.c',
  ],
  c_args : srt_c_args,
  dependencies : [
    gio_unix,
    libglnx_dep,
    libsteamrt_static_dep,
  ],
  install : true,
  install_dir : pkglibexecdir,
  build_rpath : pkglibexec_rpath,
  install_rpath : pkglibexec_rpath,
)

# Slightly different: needs threads and pv_include_dirs
srt_bin += executable(
  'steam-runtime-launch-client',
  sources: [
    'launch-client.c',
  ],
  c_args : srt_c_args,
  dependencies : [
    threads,
    gio_unix,
    libglnx_dep,
    libsteamrt_static_dep,
  ],
  # Needs pv_include_dirs for flatpak-*.h
  include_directories : pv_include_dirs,
  install : true,
  build_rpath : bin_rpath,
  install_rpath : bin_rpath,
)

# Slightly different: needs threads and codegen
srt_bin += executable(
  'steam-runtime-launcher-service',
  sources: [
    'launcher-service.c',
  ],
  c_args : srt_c_args,
  dependencies : [
    launcher_codegen_dep,
    threads,
    gio_unix,
    libglnx_dep,
    libsteamrt_static_dep,
  ],
  install : true,
  build_rpath : bin_rpath,
  install_rpath : bin_rpath,
)

# Slightly different: different prefix (so that it's distinguished as an
# internal tool), and install in libexecdir
srt_bin += executable(
  'srt-run-outside-ldlp',
  'run-outside-ldlp.c',
  c_args : srt_c_args,
  dependencies : [glib, gobject, gio_unix, libglnx_dep, libsteamrt_static_dep],
  install : true,
  install_dir : pkglibexecdir,
  build_rpath : pkglibexec_rpath,
  install_rpath : pkglibexec_rpath,
)

# Different: needs threads and UI
if sdl2_dep.found() and sdl2_ttf_dep.found()
  glib_programs += 'dialog-ui'
  srt_bin += executable(
    'steam-runtime-dialog-ui',
    sources: [
      'dialog-ui.c',
    ],
    c_args : srt_c_args,
    dependencies : [
      threads,
      gio_unix,
      libglnx_dep,
      libsteamrt_static_dep,
      libsteamrt_ui_dep,
      sdl2_dep,
      sdl2_ttf_dep,
      x11_dep,
    ],
    install : true,
  )
endif

sh_scripts = [
  'dialog',
  'launch-options',
]

foreach script : sh_scripts
  install_data(
    script + '.sh',
    rename : ['steam-runtime-' + script],
    install_dir : get_option('bindir'),
    install_mode : 'rwxr-xr-x',
  )
endforeach

libexec_data = [
  'logger-0.bash',
]

foreach file : libexec_data
  install_data(
    file,
    install_dir : pkglibexecdir,
    install_mode : 'rw-r--r--',
  )
endforeach

libexec_scripts = [
  'launch-options.py',
]

foreach script : libexec_scripts
  install_data(
    script,
    install_dir : pkglibexecdir,
    install_mode : 'rwxr-xr-x',
  )
endforeach

if build_man_pages
  foreach bin_name : (
    libc_programs
    + glib_programs
    + json_programs
    + sh_scripts
    + [
      'launch-client',
      'launcher-service',
    ]
  )
    custom_target(
      'steam-runtime-' + bin_name + '.1',
      build_by_default : true,
      command : [
        pandoc,
        '-s',
        '-o', '@OUTPUT@',
        '-f', pandoc_markdown_nosmart,
        '-t', 'man',
        '@INPUT@',
      ],
      input : bin_name + '.md',
      output : 'steam-runtime-' + bin_name + '.1',
      install : true,
      install_dir : join_paths(get_option('prefix'), get_option('mandir'), 'man1'),
    )
  endforeach

  foreach bin_name : ['logger', 'run-outside-ldlp']
    custom_target(
      'srt-' + bin_name + '.1',
      build_by_default : true,
      command : [
        pandoc,
        '-s',
        '-o', '@OUTPUT@',
        '-f', pandoc_markdown_nosmart,
        '-t', 'man',
        '@INPUT@',
      ],
      input : bin_name + '.md',
      output : 'srt-' + bin_name + '.1',
      install : true,
      install_dir : join_paths(get_option('prefix'), get_option('mandir'), 'man1'),
    )
  endforeach
endif

bubblewrap_subproject = subproject(
  'bubblewrap',
  default_options : [
    'build_rpath=' + pkglibexec_rpath,
    'bwrapdir=' + pkglibexecdir,
    'install_rpath=' + pkglibexec_rpath,
    'program_prefix=srt-',
    'selinux=disabled',
    'tests=false',
  ],
)
srt_bin += bubblewrap_subproject.get_variable('bwrap')

test_depends += srt_bin

# vim:set sw=2 sts=2 et:
