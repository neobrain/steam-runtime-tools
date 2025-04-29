# Copyright © 2025 Collabora Ltd.
# SPDX-License-Identifier: MIT

test_depends += shared_module(
  'loaded-from-origin',
  'loaded-from-origin.c',
  install: true,
  install_dir: arch_tests_dir,
  name_prefix: '',
)

test_depends += shared_module(
  'load-from-origin',
  'load-from-origin.c',
  meson.project_source_root() / 'steam-runtime-tools/libc-utils.c',
  c_args: ['-DTHIS_MODULE="load-from-origin"'],
  include_directories : project_include_dirs,
  install: true,
  install_dir: arch_tests_dir,
  name_prefix: '',
  override_options : ['b_sanitize=none'],
)

sizeof_pointer = c_compiler.sizeof('void*')

if sizeof_pointer == 8
  test_depends += shared_module(
    'only64',
    'load-from-origin.c',
    meson.project_source_root() / 'steam-runtime-tools/libc-utils.c',
    c_args: ['-DTHIS_MODULE="only64"'],
    include_directories : project_include_dirs,
    install: true,
    install_dir: arch_tests_dir,
    name_prefix: '',
    override_options : ['b_sanitize=none'],
  )
elif sizeof_pointer == 4
  test_depends += shared_module(
    'only32',
    'load-from-origin.c',
    meson.project_source_root() / 'steam-runtime-tools/libc-utils.c',
    c_args: ['-DTHIS_MODULE="only32"'],
    include_directories : project_include_dirs,
    install: true,
    install_dir: arch_tests_dir,
    name_prefix: '',
    override_options : ['b_sanitize=none'],
  )
endif
