/*<private_header>*/
/*
 * Copyright © 2019 Collabora Ltd.
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

#include "steam-runtime-tools/runtime.h"

/* Include this at the beginning so that every backport of
 * G_DEFINE_AUTOPTR_CLEANUP_FUNC will be visible */
#include "steam-runtime-tools/glib-backports-internal.h"

#include "steam-runtime-tools/os-internal.h"

#include <glib.h>
#include <glib-object.h>

typedef struct
{
  gchar *path;
  gchar *expected_version;
  gchar *version;
  SrtRuntimeIssues issues;
} SrtRuntime;

static inline gboolean _srt_runtime_is_populated (const SrtRuntime *self)
{
  return self->issues != SRT_RUNTIME_ISSUES_NONE || self->path != NULL;
}

static inline void _srt_runtime_clear_outputs (SrtRuntime *self)
{
  g_clear_pointer (&self->path, g_free);
  g_clear_pointer (&self->version, g_free);
  self->issues = SRT_RUNTIME_ISSUES_NONE;
}

static inline void _srt_runtime_clear (SrtRuntime *self)
{
  _srt_runtime_clear_outputs (self);
  g_clear_pointer (&self->expected_version, g_free);
}

G_GNUC_INTERNAL
void _srt_runtime_check_execution_environment (SrtRuntime *self,
                                               const char * const *envp,
                                               SrtOsInfo *os_info,
                                               const char *bin32);

typedef enum {
  SRT_ESCAPE_RUNTIME_FLAGS_CLEAN_PATH = (1 << 0),
  SRT_ESCAPE_RUNTIME_FLAGS_NONE = 0,
} SrtEscapeRuntimeFlags;

G_GNUC_INTERNAL
GStrv _srt_environ_escape_steam_runtime (GStrv env,
                                         SrtEscapeRuntimeFlags flags);
