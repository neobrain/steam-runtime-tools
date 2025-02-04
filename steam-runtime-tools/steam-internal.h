/*<private_header>*/
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

#include "steam-runtime-tools/steam.h"

#include <glib.h>
#include <glib-object.h>

/*
 * _srt_library_new:
 *
 * Returns: (transfer full): A new #SrtSteam
 */
static inline SrtSteam *_srt_steam_new (SrtSteamIssues issues,
                                        const char *install_path,
                                        const char *data_path,
                                        const char *bin32_path,
                                        const char *steamscript_path,
                                        const char *steamscript_version);

#ifndef __GTK_DOC_IGNORE__
static inline SrtSteam *
_srt_steam_new (SrtSteamIssues issues,
                const char *install_path,
                const char *data_path,
                const char *bin32_path,
                const char *steamscript_path,
                const char *steamscript_version)
{
  return g_object_new (SRT_TYPE_STEAM,
                       "issues", issues,
                       "install-path", install_path,
                       "data-path", data_path,
                       "bin32-path", bin32_path,
                       "steamscript-path", steamscript_path,
                       "steamscript-version", steamscript_version,
                       NULL);
}
#endif

#define SRT_STEAM_ISSUES_DESKTOP_FILE_RELATED \
  (SRT_STEAM_ISSUES_MISSING_STEAM_URI_HANDLER \
   | SRT_STEAM_ISSUES_UNEXPECTED_STEAM_URI_HANDLER \
   | SRT_STEAM_ISSUES_UNEXPECTED_STEAM_DESKTOP_ID)

G_GNUC_INTERNAL
SrtSteamIssues _srt_steam_check (const char * const *my_environ,
                                 SrtSteamIssues only_check,
                                 SrtSteam **more_details_out);

typedef enum
{
  SRT_STEAM_COMPAT_FLAGS_SEARCH_CWD = (1 << 0),
  SRT_STEAM_COMPAT_FLAGS_SEARCH_CWD_FIRST = (1 << 1),
  SRT_STEAM_COMPAT_FLAGS_SYSTEM_TRACING = (1 << 2),
  SRT_STEAM_COMPAT_FLAGS_RUNTIME_SDL2 = (1 << 3),
  SRT_STEAM_COMPAT_FLAGS_RUNTIME_SDL3 = (1 << 4),
  SRT_STEAM_COMPAT_FLAGS_RUNTIME_SDL2_COMPAT = (1 << 5),
  SRT_STEAM_COMPAT_FLAGS_NONE = 0
} SrtSteamCompatFlags;

SrtSteamCompatFlags _srt_steam_get_compat_flags (const char * const *envp);
