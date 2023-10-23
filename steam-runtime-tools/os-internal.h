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

#include <glib.h>
#include <glib-object.h>

#include "steam-runtime-tools/resolve-in-sysroot-internal.h"

typedef struct
{
  gchar *build_id;
  gchar *id;
  gchar *id_like;
  gchar *name;
  gchar *pretty_name;
  gchar *variant;
  gchar *variant_id;
  gchar *version_codename;
  gchar *version_id;
  gboolean populated;
} SrtOsRelease;

G_GNUC_INTERNAL void _srt_os_release_init (SrtOsRelease *self);
G_GNUC_INTERNAL void _srt_os_release_populate (SrtOsRelease *self,
                                               SrtSysroot *sysroot,
                                               GString *messages);
void _srt_os_release_populate_from_data (SrtOsRelease *self,
                                         const char *path,
                                         const char *contents,
                                         gsize len,
                                         GString *messages);
G_GNUC_INTERNAL void _srt_os_release_clear (SrtOsRelease *self);
