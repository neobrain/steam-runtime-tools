/*
 * Copyright 2024 Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright 2025 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "wrap-openxr.h"

#include <glib.h>

void
pv_wrap_add_openxr_args (FlatpakBwrap *sharing_bwrap)
{
  /* OpenXR runtimes tend to require communication with an external socket,
   * something that isn't really standardized, so we need to check for the
   * known socket locations for the popular runtimes. */
  static const char *runtime_subpaths[] = {
    /* https://gitlab.freedesktop.org/monado/monado/-/blob/88588213b455be7cf1c8ad002eeffbe3672251be/CMakeLists.txt#L349 */
    "monado_comp_ipc",
    /* https://github.com/WiVRn/WiVRn/blob/798ecd1693eadf82a82f9bbdf2ea45baa200a720/server/CMakeLists.txt#L66 */
    "wivrn/comp_ipc",
  };

  /* Monado always falls back to /tmp:
   * https://gitlab.freedesktop.org/monado/monado/-/blob/88588213b455be7cf1c8ad002eeffbe3672251be/src/xrt/auxiliary/util/u_file.c#L196-218
   * WiVRn uses Monado's own helpers for this, so the behavior matches:
   * https://github.com/WiVRn/WiVRn/blob/798ecd1693eadf82a82f9bbdf2ea45baa200a720/server/main.cpp#L73-L79
   */
  const char *runtime_dir = g_getenv ("XDG_RUNTIME_DIR") ?: "/tmp";
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (runtime_subpaths); i++)
    {
      g_autofree gchar *host_socket =
        g_build_filename (runtime_dir, runtime_subpaths[i], NULL);

      g_debug ("Checking for OpenXR runtime socket %s", host_socket);

      if (g_file_test (host_socket, G_FILE_TEST_EXISTS))
        {
          g_autofree gchar *container_socket =
            g_strdup_printf ("/run/user/%d/%s", getuid (), runtime_subpaths[i]);

          g_debug ("OpenXR runtime socket %s found", host_socket);
          flatpak_bwrap_add_args (sharing_bwrap,
                                  "--ro-bind",
                                  host_socket,
                                  container_socket,
                                  NULL);
        }
    }
}
