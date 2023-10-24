/*<private_header>*/
/*
 * Copyright © 2019-2023 Collabora Ltd.
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

#include "steam-runtime-tools/os.h"

#include "steam-runtime-tools/resolve-in-sysroot-internal.h"

extern const char * const _srt_interesting_os_release_fields[];

SrtOsInfo *_srt_os_info_new_from_data (const char *path,
                                       const char *path_resolved,
                                       const char *data,
                                       gsize len,
                                       const char *previous_messages);
SrtOsInfo *_srt_os_info_new_from_sysroot (SrtSysroot *sysroot);

static inline SrtOsInfo *
_srt_os_info_new (GHashTable *fields,
                  const char *messages,
                  const char *source_path,
                  const char *source_path_resolved)
{
  return g_object_new (SRT_TYPE_OS_INFO,
                       "fields", fields,
                       "messages", messages,
                       "source-path", source_path,
                       "source-path-resolved", source_path_resolved,
                       NULL);
}
