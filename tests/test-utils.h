/*
 * Copyright © 2019-2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <glib.h>

/* for its backports of g_test_skip(), etc. */
#include <libglnx.h>

/*
 * Other assorted test helpers.
 */

void _srt_tests_init (int *argc,
                      char ***argv,
                      const char *reserved);
gboolean _srt_tests_init_was_called (void);
void _srt_tests_global_debug_log_to_stderr (void);

gchar *_srt_global_setup_private_xdg_dirs (void);
gboolean _srt_global_teardown_private_xdg_dirs (void);

typedef GHashTable *TestsOpenFdSet;
TestsOpenFdSet tests_check_fd_leaks_enter (void);
void tests_check_fd_leaks_leave (TestsOpenFdSet fds);

gchar *_srt_global_setup_sysroots (const char *argv0);
gboolean _srt_global_teardown_sysroots (void);

gboolean _srt_tests_skip_if_really_in_steam_runtime (void);

void _srt_show_diff (const char *expected,
                     const char *actual);

/**
 * Asserts two strings as equal, showing a line-based diff of their contents via
 * _srt_show_diff() if they don't match.
 */
#define _srt_assert_streq_diff(a, b) \
  G_STMT_START { \
    const char *__a = (a), *__b = (b); \
    if (!g_str_equal (__a, __b)) {\
      _srt_show_diff (__a, __b); \
      g_assertion_message (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,\
                           "assertion failed (" #a " == " #b ")"); \
    } \
  } G_STMT_END
