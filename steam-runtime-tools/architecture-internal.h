/*<private_header>*/
/*
 * Copyright © 2019-2021 Collabora Ltd.
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

#include "steam-runtime-tools/subprocess-internal.h"

typedef struct
{
  const char *multiarch_tuple;
  const char *interoperable_runtime_linker;
  /* One of the values from:
   * https://registry.khronos.org/OpenXR/specs/1.1/loader.html#architecture-identifiers
   */
  const char *openxr_1_architecture;
  guint16 machine_type;
  guint8 elf_class;
  guint8 elf_encoding;
  guint8 sizeof_pointer;
} SrtKnownArchitecture;

G_GNUC_INTERNAL const SrtKnownArchitecture *_srt_architecture_get_known (void);
G_GNUC_INTERNAL const SrtKnownArchitecture *_srt_architecture_get_by_tuple (const char *multiarch_tuple);

G_GNUC_INTERNAL gboolean _srt_architecture_can_run (SrtSubprocessRunner *runner,
                                                    const char *multiarch);

const gchar *_srt_architecture_guess_from_elf (int dfd,
                                               const char *file_path,
                                               GError **error);
