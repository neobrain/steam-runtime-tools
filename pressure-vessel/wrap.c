/* pressure-vessel-wrap — run a program in a container that protects $HOME,
 * optionally using a Flatpak-style runtime.
 *
 * Contains code taken from Flatpak.
 *
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2022 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include "steam-runtime-tools/env-overlay-internal.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/profiling-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "bwrap.h"
#include "exports.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-run-private.h"
#include "flatpak-utils-base-private.h"
#include "flatpak-utils-private.h"
#include "graphics-provider.h"
#include "runtime.h"
#include "supported-architectures.h"
#include "utils.h"
#include "wrap-context.h"
#include "wrap-flatpak.h"
#include "wrap-home.h"
#include "wrap-interactive.h"
#include "wrap-setup.h"

typedef enum
{
  PV_WRAP_LOG_FLAGS_OVERRIDES = (1 << 0),
  PV_WRAP_LOG_FLAGS_CONTAINER = (1 << 1),
  PV_WRAP_LOG_FLAGS_NONE = 0
} PvWrapLogFlags;

static const GDebugKey pv_debug_keys[] =
{
  { "overrides", PV_WRAP_LOG_FLAGS_OVERRIDES },
  { "container", PV_WRAP_LOG_FLAGS_CONTAINER },
};

#define usage_error(...) _srt_log_failure (__VA_ARGS__)

int
main (int argc,
      char *argv[])
{
  g_autoptr(PvWrapContext) self = NULL;
  g_autoptr(GArray) inherit_fds = g_array_new (FALSE, FALSE, sizeof (int));
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  int ret = 2;
  gsize i;
  PvHomeMode home_mode;
  g_autoptr(FlatpakBwrap) flatpak_subsandbox = NULL;
  g_autoptr(SrtEnvOverlay) container_env = NULL;
  g_autoptr(FlatpakBwrap) bwrap = NULL;
  g_autoptr(FlatpakBwrap) bwrap_filesystem_arguments = NULL;
  g_autoptr(FlatpakBwrap) bwrap_home_arguments = NULL;
  g_autoptr(FlatpakBwrap) argv_in_container = NULL;
  g_autoptr(FlatpakBwrap) final_argv = NULL;
  g_autoptr(FlatpakExports) exports = NULL;
  g_autoptr(SrtSysroot) real_root = NULL;
  g_autoptr(SrtSysroot) interpreter_root = NULL;
  g_autofree gchar *bwrap_executable = NULL;
  SrtBwrapFlags bwrap_flags = SRT_BWRAP_FLAGS_NONE;
  g_autofree gchar *cwd_p = NULL;
  g_autofree gchar *cwd_l = NULL;
  g_autofree gchar *cwd_p_host = NULL;
  g_autofree gchar *private_home = NULL;
  const gchar *home;
  g_autofree gchar *tools_dir = NULL;
  glnx_autofd int original_stdout = -1;
  glnx_autofd int original_stderr = -1;
  const char *graphics_provider_mount_point = NULL;
  const char *steam_app_id;
  g_autoptr(GPtrArray) adverb_preload_argv = NULL;
  int result;
  PvAppendPreloadFlags append_preload_flags = PV_APPEND_PRELOAD_FLAGS_NONE;
  SrtMachineType host_machine = SRT_MACHINE_TYPE_UNKNOWN;
  SrtLogFlags log_flags;
  PvWrapLogFlags pv_log_flags = PV_WRAP_LOG_FLAGS_NONE;
  SrtSteamCompatFlags compat_flags;
  PvWorkaroundFlags workarounds;
  const char *prefix = NULL;

  setlocale (LC_ALL, "");

  /* Set up the initial base logging */
  if (!_srt_util_set_glib_log_handler ("pressure-vessel-wrap",
                                       G_LOG_DOMAIN, SRT_LOG_FLAGS_NONE,
                                       &original_stdout, &original_stderr, error))
    goto out;

  g_info ("pressure-vessel version %s", VERSION);

  if (g_getenv ("STEAM_RUNTIME") != NULL)
    {
      usage_error ("This program should not be run in the Steam Runtime. "
                   "Use pressure-vessel-unruntime instead.");
      ret = 2;
      goto out;
    }

  /* FEX-Emu transparently rewrites most file I/O to check its "rootfs"
   * first, and only use the real root if the corresponding file
   * doesn't exist in the "rootfs". In many places we actively don't want
   * this, because we're inspecting paths in order to pass them to bwrap,
   * which will use them to set up bind-mounts, which are not subject to
   * FEX-Emu's rewriting; so bypass it here. */
  real_root = _srt_sysroot_new_real_root (error);

  if (real_root == NULL)
    return FALSE;

  self = pv_wrap_context_new (real_root, error);

  if (self == NULL)
    goto out;

  if (!pv_wrap_options_parse_environment (&self->options, error))
    goto out;

  if (!pv_wrap_context_parse_argv (self, &argc, &argv, error))
    goto out;

  log_flags = SRT_LOG_FLAGS_DIVERT_STDOUT | SRT_LOG_FLAGS_OPTIONALLY_JOURNAL;

  if (self->options.deterministic)
    log_flags |= SRT_LOG_FLAGS_DIFFABLE;

  if (self->options.verbose)
    {
      log_flags |= SRT_LOG_FLAGS_DEBUG;

      /* We share the same environment variable as the rest of s-r-t, but look
       * for additional flags in it */
      pv_log_flags = g_parse_debug_string (g_getenv ("SRT_LOG"),
                                           pv_debug_keys,
                                           G_N_ELEMENTS (pv_debug_keys));
    }

  if (!_srt_util_set_glib_log_handler (NULL, G_LOG_DOMAIN, log_flags,
                                       NULL, NULL, error))
    goto out;

  pv_wrap_detect_virtualization (&interpreter_root, &host_machine);

  if (!pv_wrap_options_parse_environment_after_argv (&self->options,
                                                     interpreter_root,
                                                     error))
    goto out;

  if (self->options.version_only || self->options.version)
    {
      if (original_stdout >= 0
          && !_srt_util_restore_saved_fd (original_stdout, STDOUT_FILENO, error))
        goto out;

      if (self->options.version_only)
        g_print ("%s\n", VERSION);
      else
        g_print ("%s:\n"
                 " Package: pressure-vessel\n"
                 " Version: %s\n",
                 argv[0], VERSION);

      ret = 0;
      goto out;
    }

  _srt_unblock_signals ();
  _srt_setenv_disable_gio_modules ();

  if (argc < 2 && !self->options.test && !self->options.only_prepare)
    {
      usage_error ("An executable to run is required");
      goto out;
    }

  if (self->options.terminal == PV_TERMINAL_AUTO)
    {
      if (self->options.shell != PV_SHELL_NONE)
        self->options.terminal = PV_TERMINAL_XTERM;
      else
        self->options.terminal = PV_TERMINAL_NONE;
    }

  if (self->options.terminal == PV_TERMINAL_NONE
      && self->options.shell != PV_SHELL_NONE)
    {
      usage_error ("--terminal=none is incompatible with --shell");
      goto out;
    }

  /* --launcher implies --batch */
  if (self->options.launcher)
    self->options.batch = TRUE;

  if (self->options.batch)
    {
      /* --batch or PRESSURE_VESSEL_BATCH=1 overrides these */
      self->options.shell = PV_SHELL_NONE;
      self->options.terminal = PV_TERMINAL_NONE;
    }

  if (argc > 1 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (self->options.steam_app_id != NULL)
    steam_app_id = self->options.steam_app_id;
  else
    steam_app_id = _srt_get_steam_app_id ();

  home = g_get_home_dir ();

  if (self->options.share_home == TRISTATE_YES)
    {
      home_mode = PV_HOME_MODE_SHARED;
    }
  else if (self->options.home)
    {
      home_mode = PV_HOME_MODE_PRIVATE;
      private_home = g_strdup (self->options.home);
    }
  else if (self->options.share_home == TRISTATE_MAYBE)
    {
      home_mode = PV_HOME_MODE_SHARED;
    }
  else if (self->options.freedesktop_app_id)
    {
      home_mode = PV_HOME_MODE_PRIVATE;
      private_home = g_build_filename (home, ".var", "app",
                                       self->options.freedesktop_app_id,
                                       NULL);
    }
  else if (steam_app_id != NULL)
    {
      home_mode = PV_HOME_MODE_PRIVATE;
      self->options.freedesktop_app_id = g_strdup_printf ("com.steampowered.App%s",
                                                          steam_app_id);
      private_home = g_build_filename (home, ".var", "app",
                                       self->options.freedesktop_app_id,
                                       NULL);
    }
  else if (self->options.batch)
    {
      home_mode = PV_HOME_MODE_TRANSIENT;
      private_home = NULL;
      g_info ("Unsharing the home directory without choosing a valid "
              "candidate, using tmpfs as a fallback");
    }
  else
    {
      usage_error ("Either --home, --freedesktop-app-id, --steam-app-id "
                   "or $SteamAppId is required");
      goto out;
    }

  if (home_mode == PV_HOME_MODE_PRIVATE)
    g_assert (private_home != NULL);
  else
    g_assert (private_home == NULL);

  if (self->options.env_if_host != NULL)
    {
      for (i = 0; self->options.env_if_host[i] != NULL; i++)
        {
          const char *equals = strchr (self->options.env_if_host[i], '=');

          if (equals == NULL)
            {
              usage_error ("--env-if-host argument must be of the form "
                           "NAME=VALUE, not \"%s\"",
                           self->options.env_if_host[i]);
              goto out;
            }
        }
    }

  if (self->options.only_prepare && self->options.test)
    {
      usage_error ("--only-prepare and --test are mutually exclusive");
      goto out;
    }

  if (self->options.filesystems != NULL)
    {
      for (i = 0; self->options.filesystems[i] != NULL; i++)
        {
          if (strchr (self->options.filesystems[i], ':') != NULL ||
              strchr (self->options.filesystems[i], '\\') != NULL)
            {
              usage_error ("':' and '\\' in --filesystem argument "
                           "not handled yet");
              goto out;
            }
          else if (!g_path_is_absolute (self->options.filesystems[i]))
            {
              usage_error ("--filesystem argument must be an absolute "
                           "path, not \"%s\"", self->options.filesystems[i]);
              goto out;
            }
        }
    }

  if (self->options.copy_runtime && self->options.variable_dir == NULL)
    {
      usage_error ("--copy-runtime requires --variable-dir");
      goto out;
    }

  /* Finished parsing arguments, so any subsequent failures will make
   * us exit 1. */
  ret = 1;

  if ((result = _srt_set_compatible_resource_limits (0)) < 0)
    g_warning ("Unable to set normal resource limits: %s",
               g_strerror (-result));

  if (self->options.terminal != PV_TERMINAL_TTY && !self->options.devel)
    {
      int fd;

      if (!glnx_openat_rdonly (-1, "/dev/null", TRUE, &fd, error))
          goto out;

      if (dup2 (fd, STDIN_FILENO) < 0)
        {
          glnx_throw_errno_prefix (error,
                                   "Cannot replace stdin with /dev/null");
          goto out;
        }
    }

  compat_flags = _srt_steam_get_compat_flags (_srt_const_strv (self->original_environ));
  _srt_get_current_dirs (&cwd_p, &cwd_l);

  if (_srt_util_is_debugging ())
    {
      g_auto(GStrv) env = g_strdupv (self->original_environ);

      g_debug ("Original argv:");

      for (i = 0; i < self->original_argc; i++)
        {
          g_autofree gchar *quoted = g_shell_quote (self->original_argv[i]);

          g_debug ("\t%" G_GSIZE_FORMAT ": %s", i, quoted);
        }

      g_debug ("Current working directory:");
      g_debug ("\tPhysical: %s", cwd_p);
      g_debug ("\tLogical: %s", cwd_l);

      g_debug ("Environment variables:");

      qsort (env, g_strv_length (env), sizeof (char *), flatpak_envp_cmp);

      for (i = 0; env[i] != NULL; i++)
        {
          g_autofree gchar *quoted = g_shell_quote (env[i]);

          g_debug ("\t%s", quoted);
        }

      if (self->options.launcher)
        g_debug ("Arguments for s-r-launcher-service:");
      else
        g_debug ("Wrapped command:");

      for (i = 1; i < argc; i++)
        {
          g_autofree gchar *quoted = g_shell_quote (argv[i]);

          g_debug ("\t%" G_GSIZE_FORMAT ": %s", i, quoted);
        }
    }

  tools_dir = _srt_find_executable_dir (error);

  if (tools_dir == NULL)
    goto out;

  g_debug ("Found executable directory: %s", tools_dir);

  prefix = _srt_find_myself (NULL, NULL, error);

  if (prefix == NULL)
    goto out;

  /* If we are in a Flatpak environment we can't use bwrap directly */
  if (self->is_flatpak_env)
    {
      if (!pv_wrap_check_flatpak (tools_dir, &flatpak_subsandbox, error))
        goto out;
    }
  else
    {
      g_debug ("Checking for bwrap...");

      bwrap_executable = pv_wrap_check_bwrap (self->options.only_prepare,
                                              &bwrap_flags,
                                              error);

      if (bwrap_executable == NULL)
        goto out;

      g_debug ("OK (%s)", bwrap_executable);
    }

  workarounds = pv_get_workarounds (bwrap_flags,
                                    _srt_const_strv (self->original_environ));

  if (self->options.test)
    {
      ret = 0;
      goto out;
    }

  /* Invariant: we are in exactly one of these two modes */
  g_assert (((flatpak_subsandbox != NULL)
             + (!self->is_flatpak_env))
            == 1);

  if (flatpak_subsandbox == NULL)
    {
      /* Start with an empty environment and populate it later */
      bwrap = flatpak_bwrap_new (flatpak_bwrap_empty_env);
      g_assert (bwrap_executable != NULL);
      flatpak_bwrap_add_arg (bwrap, bwrap_executable);
      bwrap_filesystem_arguments = flatpak_bwrap_new (flatpak_bwrap_empty_env);
      exports = flatpak_exports_new ();
    }
  else
    {
      append_preload_flags |= PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX;
    }

  /* Invariant: we have bwrap or exports iff we also have the other */
  g_assert ((bwrap != NULL) == (exports != NULL));
  g_assert ((bwrap != NULL) == (bwrap_filesystem_arguments != NULL));
  g_assert ((bwrap != NULL) == (bwrap_executable != NULL));

  container_env = _srt_env_overlay_new ();

  if (bwrap != NULL)
    {
      FlatpakFilesystemMode sysfs_mode = FLATPAK_FILESYSTEM_MODE_READ_ONLY;

      g_assert (exports != NULL);
      g_assert (bwrap_filesystem_arguments != NULL);

      if ((_srt_util_get_log_flags () & SRT_LOG_FLAGS_LEVEL)
          && (bwrap_flags & SRT_BWRAP_FLAGS_HAS_LEVEL_PREFIX))
        flatpak_bwrap_add_arg (bwrap, "--level-prefix");

      /* When using an interpreter root, avoid /run/gfx and instead use a
       * directory in /var. As much as possible we want each top-level
       * directory to be either in the rootfs or in the real host system,
       * not some mixture of the two, and the majority of /run needs to
       * come from the real host system, for sockets and so on; but when
       * the rootfs contains a symlink, FEX-Emu interprets it as though
       * chrooted into the rootfs, so we have to mount the graphics
       * provider inside the rootfs instead of in the real root. */
      if (interpreter_root != NULL)
        graphics_provider_mount_point = "/var/pressure-vessel/gfx";
      else if (g_strcmp0 (self->options.graphics_provider, "/") == 0)
        graphics_provider_mount_point = "/run/host";
      else
        graphics_provider_mount_point = "/run/gfx";

      /* Protect the controlling terminal from the app/game, unless we are
       * running an interactive shell in which case that would break its
       * job control. */
      if (self->options.terminal != PV_TERMINAL_TTY && !self->options.devel)
        flatpak_bwrap_add_arg (bwrap, "--new-session");

      /* Start with just the root tmpfs (which appears automatically)
       * and the standard API filesystems */
      if (self->options.devel)
        sysfs_mode = FLATPAK_FILESYSTEM_MODE_READ_WRITE;

      pv_bwrap_add_api_filesystems (bwrap_filesystem_arguments,
                                    sysfs_mode,
                                    compat_flags);

      if (interpreter_root != NULL)
        {
          g_autofree gchar *etc_src = g_build_filename (interpreter_root->path,
                                                        "etc", NULL);

          /* Mount the interpreter root on /run/host. We'll use this
           * to look at paths like /run/host/etc/os-release. */
          flatpak_bwrap_add_args (bwrap_filesystem_arguments,
                                  "--ro-bind", etc_src, "/run/host/etc",
                                  NULL);

          if (!pv_bwrap_bind_usr (bwrap,
                                  interpreter_root->path,
                                  interpreter_root->fd,
                                  "/run/host", error))
            goto out;

          /* Mount the real root on /run/interpreter-host. We'll use this
           * to run the interpreter. */
          flatpak_bwrap_add_args (bwrap_filesystem_arguments,
                                  "--ro-bind", "/etc", "/run/interpreter-host/etc",
                                  NULL);

          if (!pv_bwrap_bind_usr (bwrap, "/",
                                  _srt_sysroot_get_fd (real_root),
                                  "/run/interpreter-host",
                                  error))
            goto out;

          /* PvRuntime will mount the graphics stack provider on /gfx
           * and PV_RUNTIME_PATH_INTERPRETER_ROOT/gfx if necessary. */
        }
      else
        {
          flatpak_bwrap_add_args (bwrap_filesystem_arguments,
                                  "--ro-bind", "/etc", "/run/host/etc", NULL);

          if (!pv_bwrap_bind_usr (bwrap, "/",
                                  _srt_sysroot_get_fd (real_root),
                                  "/run/host", error))
            goto out;
        }

      /* steam-runtime-system-info uses this to detect pressure-vessel, so we
       * need to create it even if it will be empty */
      flatpak_bwrap_add_args (bwrap_filesystem_arguments,
                              "--dir",
                              "/run/pressure-vessel",
                              NULL);
    }
  else
    {
      g_assert (flatpak_subsandbox != NULL);

      if (g_strcmp0 (self->options.graphics_provider, "/") == 0)
        {
          graphics_provider_mount_point = "/run/parent";
        }
      else if (g_strcmp0 (self->options.graphics_provider, "/run/host") == 0)
        {
          g_warning ("Using host graphics drivers in a Flatpak subsandbox "
                     "probably won't work");
          graphics_provider_mount_point = "/run/host";
        }
      else
        {
          glnx_throw (error,
                      "Flatpak subsandboxing can only use / or /run/host "
                      "to provide graphics drivers");
          goto out;
        }
    }

  if (self->options.runtime != NULL)
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer =
        _srt_profiling_start ("Setting up runtime");
      g_autoptr(PvGraphicsProvider) graphics_provider = NULL;
      g_autoptr(PvGraphicsProvider) interpreter_host_provider = NULL;
      PvRuntimeFlags flags = PV_RUNTIME_FLAGS_NONE;
      g_autofree gchar *runtime_resolved = NULL;
      const char *runtime_path = NULL;

      if (self->options.deterministic)
        flags |= PV_RUNTIME_FLAGS_DETERMINISTIC;

      if (self->options.gc_runtimes)
        flags |= PV_RUNTIME_FLAGS_GC_RUNTIMES;

      if (self->options.generate_locales)
        flags |= PV_RUNTIME_FLAGS_GENERATE_LOCALES;

      if (self->options.graphics_provider != NULL
          && self->options.graphics_provider[0] != '\0')
        {
          g_assert (graphics_provider_mount_point != NULL);
          graphics_provider = pv_graphics_provider_new (self->options.graphics_provider,
                                                        graphics_provider_mount_point,
                                                        TRUE, error);

          if (graphics_provider == NULL)
            goto out;
        }

      if (_srt_util_is_debugging ())
        flags |= PV_RUNTIME_FLAGS_VERBOSE;

      if (self->options.import_ca_certs)
        flags |= PV_RUNTIME_FLAGS_IMPORT_CA_CERTS;

      if (self->options.import_vulkan_layers)
        flags |= PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS;

      if (self->options.copy_runtime)
        flags |= PV_RUNTIME_FLAGS_COPY_RUNTIME;

      if (self->options.deterministic || self->options.single_thread)
        flags |= PV_RUNTIME_FLAGS_SINGLE_THREAD;

      if (flatpak_subsandbox != NULL)
        flags |= PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX;

      if (interpreter_root != NULL)
        {
          flags |= PV_RUNTIME_FLAGS_INTERPRETER_ROOT;

          /* Also include the real host graphics stack to allow thunking.
           * To avoid enumerating the same DRIs/layers twice, we only do
           * this if the host is not a supported architecture. */
          if (!pv_supported_architectures_include_machine_type (host_machine))
            {
              /* The trailing slash is needed to allow open(2) to work even if
               * it's using the O_NOFOLLOW flag. */
              interpreter_host_provider = pv_graphics_provider_new ("/proc/self/root/",
                                                                    "/proc/self/root/",
                                                                    FALSE, error);
              if (interpreter_host_provider == NULL)
                goto out;
            }
        }

      runtime_path = self->options.runtime;

      if (!g_path_is_absolute (runtime_path)
          && self->options.runtime_base != NULL
          && self->options.runtime_base[0] != '\0')
        {
          runtime_resolved = g_build_filename (self->options.runtime_base,
                                               runtime_path, NULL);
          runtime_path = runtime_resolved;
        }

      g_debug ("Configuring runtime %s...", runtime_path);

      if (self->is_flatpak_env && !self->options.copy_runtime)
        {
          glnx_throw (error,
                      "Cannot set up a runtime inside Flatpak without "
                      "making a mutable copy");
          goto out;
        }

      self->runtime = pv_runtime_new (runtime_path,
                                      self->options.variable_dir,
                                      bwrap_executable,
                                      graphics_provider,
                                      interpreter_host_provider,
                                      _srt_const_strv (self->original_environ),
                                      flags,
                                      workarounds,
                                      error);

      if (self->runtime == NULL)
        goto out;

      if (!pv_runtime_bind (self->runtime,
                            exports,
                            bwrap_filesystem_arguments,
                            container_env,
                            error))
        goto out;

      if (flatpak_subsandbox != NULL)
        {
          const char *app = pv_runtime_get_modified_app (self->runtime);
          const char *usr = pv_runtime_get_modified_usr (self->runtime);

          flatpak_bwrap_add_args (flatpak_subsandbox,
                                  "--app-path", app == NULL ? "" : app,
                                  "--share-pids",
                                  "--usr-path", usr,
                                  NULL);
        }
    }
  else if (flatpak_subsandbox != NULL)
    {
      /* Nothing special to do here: we'll just create the subsandbox
       * without changing the runtime, which means we inherit the
       * Flatpak's normal runtime. */
    }
  else
    {
      SrtDirentCompareFunc cmp = NULL;

      if (self->options.deterministic)
        cmp = _srt_dirent_strcmp;

      g_assert (!self->is_flatpak_env);
      g_assert (bwrap != NULL);
      g_assert (bwrap_filesystem_arguments != NULL);
      g_assert (exports != NULL);

      if (!pv_wrap_use_host_os (_srt_sysroot_get_fd (real_root),
                                exports, bwrap_filesystem_arguments,
                                cmp, error))
        goto out;
    }

  /* Protect other users' homes (but guard against the unlikely
   * situation that they don't exist). We use the FlatpakExports for this
   * so that it can be overridden by --filesystem=/home or
   * pv_wrap_use_home(), and so that it is sorted correctly with
   * respect to all the other home-directory-related exports. */
  if (exports != NULL
      && g_file_test ("/home", G_FILE_TEST_EXISTS))
    pv_exports_mask_or_log (exports, "/home");

  g_debug ("Making home directory available...");

  if (flatpak_subsandbox != NULL)
    {
      if (home_mode == PV_HOME_MODE_SHARED)
        {
          /* Nothing special to do here: we'll use the same home directory
           * and exports that the parent Flatpak sandbox used. */
        }
      else
        {
          /* Not yet supported */
          glnx_throw (error,
                      "Cannot use a game-specific home directory in a "
                      "Flatpak subsandbox");
          goto out;
        }
    }
  else
    {
      g_assert (!self->is_flatpak_env);
      g_assert (bwrap != NULL);
      g_assert (bwrap_filesystem_arguments != NULL);
      g_assert (exports != NULL);

      bwrap_home_arguments = flatpak_bwrap_new (flatpak_bwrap_empty_env);

      if (!pv_wrap_use_home (home_mode, home, private_home,
                             exports, bwrap_home_arguments, container_env,
                             error))
        goto out;
    }

  if (!self->options.share_pid)
    {
      if (bwrap != NULL)
        {
          g_warning ("Unsharing process ID namespace. This is not expected "
                     "to work...");
          flatpak_bwrap_add_arg (bwrap, "--unshare-pid");
        }
      else
        {
          g_assert (flatpak_subsandbox != NULL);
          /* steam-runtime-launch-client currently hard-codes this */
          g_warning ("Process ID namespace is always shared when using a "
                     "Flatpak subsandbox");
        }
    }

  if (exports != NULL)
    pv_share_temp_dir (exports, container_env);

  if (flatpak_subsandbox != NULL)
    {
      /* We special case libshared-library-guard because usually
       * its blockedlist file is located in `/app` and we need
       * to change that to the `/run/parent` counterpart */
      const gchar *blockedlist = g_getenv ("SHARED_LIBRARY_GUARD_CONFIG");

      if (blockedlist == NULL)
        blockedlist = "/app/etc/freedesktop-sdk.ld.so.blockedlist";

      if (g_file_test (blockedlist, G_FILE_TEST_EXISTS)
          && (g_str_has_prefix (blockedlist, "/app/")
              || g_str_has_prefix (blockedlist, "/usr/")
              || g_str_has_prefix (blockedlist, "/lib")))
        {
          g_autofree gchar *adjusted_blockedlist = NULL;
          adjusted_blockedlist = g_build_filename ("/run/parent",
                                                   blockedlist, NULL);
          _srt_env_overlay_set (container_env, "SHARED_LIBRARY_GUARD_CONFIG",
                             adjusted_blockedlist);
        }
    }

  adverb_preload_argv = g_ptr_array_new_with_free_func (g_free);

  if (self->options.remove_game_overlay)
    append_preload_flags |= PV_APPEND_PRELOAD_FLAGS_REMOVE_GAME_OVERLAY;

  /* We need the LD_PRELOADs from Steam visible at the paths that were
   * used for them, which might be their physical rather than logical
   * locations. Steam doesn't generally use LD_AUDIT, but the Steam app
   * on Flathub does, and it needs similar handling. */
  do
    {
      gsize j;

      g_debug ("Adjusting LD_AUDIT/LD_PRELOAD modules if any...");

      for (j = 0; j < self->options.preload_modules->len; j++)
        {
          const WrapPreloadModule *module = &g_array_index (self->options.preload_modules,
                                                            WrapPreloadModule,
                                                            j);

          g_assert (module->which >= 0);
          g_assert (module->which < G_N_ELEMENTS (pv_preload_variables));
          pv_wrap_append_preload (self,
                                  adverb_preload_argv,
                                  module->which,
                                  module->preload,
                                  append_preload_flags,
                                  exports);
        }
    }
  while (0);

  pv_bind_and_propagate_from_environ (self, home_mode, exports, container_env);

  if (flatpak_subsandbox == NULL)
    {
      const PvAppFrameworkPath *framework_paths;

      g_assert (bwrap != NULL);
      g_assert (bwrap_filesystem_arguments != NULL);
      g_assert (exports != NULL);

      /* Bind-mount /run/udev to support games that detect joysticks by using
       * udev directly. We only do that when the host's version of libudev.so.1
       * is in use, because there is no guarantees that the container's libudev
       * is compatible with the host's udevd. */
      if (self->runtime != NULL)
        {
          for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
            {
              GStatBuf ignored;
              g_autofree gchar *override = NULL;

              override = g_build_filename (pv_runtime_get_overrides (self->runtime),
                                           "lib", pv_multiarch_tuples[i],
                                           "libudev.so.1", NULL);

              if (g_lstat (override, &ignored) == 0)
                {
                  g_debug ("We are using the host's version of \"libudev.so.1\", trying to bind-mount /run/udev too...");
                  pv_exports_expose_or_log (exports,
                                            FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                                            "/run/udev");
                  break;
                }
            }
        }

      /* Expose hard-coded library paths from other app runtime frameworks'
       * dependency management: /nix, /snap, etc. */
      for (framework_paths = pv_runtime_get_other_app_framework_paths ();
           framework_paths->path != NULL;
           framework_paths++)
        {
          if (workarounds & framework_paths->ignore_if)
            g_warning ("Not sharing %s with container to work around %s",
                       framework_paths->path, framework_paths->bug);
          else
            pv_exports_expose_or_log (exports,
                                      FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                                      framework_paths->path);
        }

      /* Make arbitrary filesystems available. This is not as complete as
       * Flatpak yet. */
      if (self->options.filesystems != NULL)
        {
          g_debug ("Processing --filesystem arguments...");

          for (i = 0; self->options.filesystems[i] != NULL; i++)
            pv_wrap_context_export_if_allowed (self, exports,
                                               FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                               self->options.filesystems[i],
                                               self->options.filesystems[i],
                                               "--filesystem", "", "",
                                               PV_WRAP_EXPORT_FLAGS_NONE);
        }

      /* Make sure the current working directory (the game we are going to
       * run) is available. Some games write here. */
      g_debug ("Making current working directory available...");

      cwd_p_host = pv_current_namespace_path_to_host_path (cwd_p);

      if (_srt_is_same_file (home, cwd_p))
        {
          g_info ("Not making physical working directory \"%s\" available to "
                  "container because it is the home directory",
                  cwd_p);
        }
      else
        {
          /* Inability to share the current working directory is unexpected
           * and will break assumptions, so warn if that happens.
           *
           * If in Flatpak, we assume that cwd_p_host is visible in the
           * current namespace as well as in the host, because it's
           * either in our ~/.var/app/$FLATPAK_ID, or a --filesystem that
           * was exposed from the host. */
          pv_exports_expose_or_warn (exports,
                                     FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                     cwd_p_host);
        }

      flatpak_bwrap_add_args (bwrap,
                              "--chdir", cwd_p_host,
                              NULL);
    }
  else
    {
      flatpak_bwrap_add_args (flatpak_subsandbox,
                              "--directory", cwd_p,
                              NULL);
    }

  _srt_env_overlay_set (container_env, "PWD", NULL);

  /* Put Steam Runtime environment variables back, if /usr is mounted
   * from the host. */
  if (self->runtime == NULL)
    {
      g_debug ("Making Steam Runtime available...");

      /* We need libraries from the Steam Runtime, so make sure that's
       * visible (it should never need to be read/write though) */
      if (self->options.env_if_host != NULL)
        {
          for (i = 0; self->options.env_if_host[i] != NULL; i++)
            {
              char *equals = strchr (self->options.env_if_host[i], '=');

              g_assert (equals != NULL);

              /* $STEAM_RUNTIME is functionally necessary if used, so
               * always warn if it cannot be exposed */
              if (exports != NULL
                  && g_str_has_prefix (self->options.env_if_host[i],
                                       "STEAM_RUNTIME=/"))
                pv_exports_expose_or_warn (exports,
                                           FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                                           equals + 1);

              *equals = '\0';

              _srt_env_overlay_set (container_env, self->options.env_if_host[i],
                                   equals + 1);

              *equals = '=';
            }
        }
    }

  /* Convert the exported directories into extra bubblewrap arguments */
  if (exports != NULL)
    {
      g_autoptr(FlatpakBwrap) exports_bwrap =
        flatpak_bwrap_new (flatpak_bwrap_empty_env);

      g_assert (bwrap != NULL);
      g_assert (bwrap_filesystem_arguments != NULL);

      if (bwrap_home_arguments != NULL)
        {
          /* The filesystem arguments to set up a fake $HOME (if any) have
           * to come before the exports, as they do in Flatpak, so that
           * mounting the fake $HOME will not mask the exports used for
           * ~/.steam, etc. */
          g_warn_if_fail (g_strv_length (bwrap_home_arguments->envp) == 0);
          flatpak_bwrap_append_bwrap (bwrap, bwrap_home_arguments);
          g_clear_pointer (&bwrap_home_arguments, flatpak_bwrap_free);
        }

      flatpak_exports_append_bwrap_args (exports, exports_bwrap);
      g_warn_if_fail (g_strv_length (exports_bwrap->envp) == 0);
      if (!pv_bwrap_append_adjusted_exports (bwrap, exports_bwrap, home,
                                             interpreter_root, workarounds,
                                             error))
        goto out;

      /* The other filesystem arguments have to come after the exports
       * so that if the exports set up symlinks, the other filesystem
       * arguments like --dir work with the symlinks' targets. */
      g_warn_if_fail (g_strv_length (bwrap_filesystem_arguments->envp) == 0);
      flatpak_bwrap_append_bwrap (bwrap, bwrap_filesystem_arguments);
      g_clear_pointer (&bwrap_filesystem_arguments, flatpak_bwrap_free);
    }

  if (bwrap != NULL)
    {
      g_autoptr(FlatpakBwrap) sharing_bwrap = NULL;

      sharing_bwrap = pv_wrap_share_sockets (container_env,
                                             _srt_const_strv (self->original_environ),
                                             (self->runtime != NULL),
                                             self->is_flatpak_env);
      g_warn_if_fail (g_strv_length (sharing_bwrap->envp) == 0);

      if (!pv_bwrap_append_adjusted_exports (bwrap, sharing_bwrap, home,
                                             interpreter_root, workarounds,
                                             error))
        goto out;
    }
  else if (flatpak_subsandbox != NULL)
    {
      pv_wrap_set_icons_env_vars (container_env, _srt_const_strv (self->original_environ));
    }

  if (self->runtime != NULL)
    {
      if (!pv_runtime_use_shared_sockets (self->runtime, bwrap, container_env,
                                          error))
        goto out;
    }

  if (self->is_flatpak_env)
    {
      /* Let these inherit from the sub-sandbox environment */
      _srt_env_overlay_inherit (container_env, "FLATPAK_ID");
      _srt_env_overlay_inherit (container_env, "FLATPAK_SANDBOX_DIR");
      _srt_env_overlay_inherit (container_env, "DBUS_SESSION_BUS_ADDRESS");
      _srt_env_overlay_inherit (container_env, "DBUS_SYSTEM_BUS_ADDRESS");
      _srt_env_overlay_inherit (container_env, "DISPLAY");
      _srt_env_overlay_inherit (container_env, "XDG_RUNTIME_DIR");

      /* The bwrap envp will be completely ignored when calling
       * s-r-launch-client, and in fact putting them in its environment
       * variables would be wrong, because s-r-launch-client needs to see the
       * current execution environment's DBUS_SESSION_BUS_ADDRESS
       * (if different). For this reason we convert them to `--setenv`. */
      g_assert (flatpak_subsandbox != NULL);

      if (!pv_bwrap_container_env_to_subsandbox_argv (flatpak_subsandbox,
                                                      container_env,
                                                      error))
        goto out;
    }

  final_argv = flatpak_bwrap_new (self->original_environ);

  /* Populate final_argv->envp, overwriting its copy of original_environ.
   * We skip this if we are in a Flatpak environment, because in that case
   * we already used `--env-fd` for all the variables that we care about and
   * the final_argv->envp will be ignored anyway, other than as a way to
   * invoke s-r-launch-client (for which original_environ is appropriate). */
  if (!self->is_flatpak_env)
    pv_bwrap_container_env_to_envp (final_argv, container_env);

  /* Now that we've populated final_argv->envp, it's too late to change
   * any environment variables. Make sure that under normal circumstances
   * we get an assertion failure if we try.
   * However, if we're working around a setuid bwrap, we need to keep this
   * around a little bit longer. */
  if (!(workarounds & PV_WORKAROUND_FLAGS_BWRAP_SETUID))
    g_clear_pointer (&container_env, _srt_env_overlay_unref);

  if (bwrap != NULL)
    {
      /* Tell the application that it's running under a container manager
       * in a generic way (based on https://systemd.io/CONTAINER_INTERFACE/,
       * although a lot of that document is intended for "system"
       * containers and is less suitable for "app" containers like
       * Flatpak and pressure-vessel). */
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "container", "pressure-vessel",
                              NULL);
      if (!flatpak_bwrap_add_args_data (bwrap,
                                        "container-manager",
                                        "pressure-vessel\n", -1,
                                        "/run/host/container-manager",
                                        error))
        return FALSE;


      if (_srt_util_is_debugging ())
        {
          g_debug ("%s options before bundling:", bwrap_executable);

          for (i = 0; i < bwrap->argv->len; i++)
            {
              g_autofree gchar *quoted = NULL;

              quoted = g_shell_quote (g_ptr_array_index (bwrap->argv, i));
              g_debug ("\t%s", quoted);
            }
        }

      if (!self->options.only_prepare)
        {
          if (!flatpak_bwrap_bundle_args (bwrap, 1, -1, FALSE, error))
            goto out;
        }
    }

  argv_in_container = flatpak_bwrap_new (flatpak_bwrap_empty_env);

  /* Set up adverb inside container */
    {
      g_autoptr(FlatpakBwrap) adverb_argv = NULL;

      adverb_argv = flatpak_bwrap_new (flatpak_bwrap_empty_env);

      if (self->runtime != NULL)
        {
          /* This includes the arguments necessary to regenerate the
           * ld.so cache */
          if (!pv_runtime_get_adverb (self->runtime, adverb_argv, error))
            goto out;
        }
      else
        {
          /* If not using a runtime, the adverb in the container has the
           * same path as outside and we assume no special LD_LIBRARY_PATH
           * is needed */
          g_autofree gchar *adverb_in_container =
            g_build_filename (tools_dir, "pressure-vessel-adverb", NULL);

          flatpak_bwrap_add_arg (adverb_argv, adverb_in_container);
        }

      if (workarounds & PV_WORKAROUND_FLAGS_BWRAP_SETUID)
        {
          /* If bwrap is setuid, then it might have filtered some
           * environment variables out of the environment.
           * Use pv-adverb --env-fd to put them back.
           * We used to do this for only the environment variables that
           * a setuid executable would filter out, but now that we're using
           * --env-fd, it's just as easy to serialize all of them. */
          if (!pv_bwrap_container_env_to_env_fd (adverb_argv,
                                                 container_env,
                                                 error))
            goto out;
        }

      if (self->options.terminate_timeout >= 0.0)
        {
          char idle_buf[G_ASCII_DTOSTR_BUF_SIZE] = {};
          char timeout_buf[G_ASCII_DTOSTR_BUF_SIZE] = {};

          g_ascii_dtostr (idle_buf, sizeof (idle_buf),
                          self->options.terminate_idle_timeout);
          g_ascii_dtostr (timeout_buf, sizeof (timeout_buf),
                          self->options.terminate_timeout);

          if (self->options.terminate_idle_timeout > 0.0)
            flatpak_bwrap_add_arg_printf (adverb_argv,
                                          "--terminate-idle-timeout=%s",
                                          idle_buf);

          flatpak_bwrap_add_arg_printf (adverb_argv,
                                        "--terminate-timeout=%s",
                                        timeout_buf);
        }

      flatpak_bwrap_add_args (adverb_argv,
                              "--exit-with-parent",
                              "--subreaper",
                              NULL);

      g_array_append_val (inherit_fds, original_stdout);
      flatpak_bwrap_add_arg_printf (adverb_argv, "--assign-fd=%d=%d",
                                    STDOUT_FILENO, original_stdout);
      g_array_append_val (inherit_fds, original_stderr);
      flatpak_bwrap_add_arg_printf (adverb_argv, "--assign-fd=%d=%d",
                                    STDERR_FILENO, original_stderr);

      for (i = 0; i < self->options.pass_fds->len; i++)
        {
          int fd = g_array_index (self->options.pass_fds, int, i);

          g_array_append_val (inherit_fds, fd);
          flatpak_bwrap_add_arg_printf (adverb_argv, "--pass-fd=%d", fd);
        }

      switch (self->options.shell)
        {
          case PV_SHELL_AFTER:
            flatpak_bwrap_add_arg (adverb_argv, "--shell=after");
            break;

          case PV_SHELL_FAIL:
            flatpak_bwrap_add_arg (adverb_argv, "--shell=fail");
            break;

          case PV_SHELL_INSTEAD:
            flatpak_bwrap_add_arg (adverb_argv, "--shell=instead");
            break;

          case PV_SHELL_NONE:
            flatpak_bwrap_add_arg (adverb_argv, "--shell=none");
            break;

          default:
            g_warn_if_reached ();
        }

      switch (self->options.terminal)
        {
          case PV_TERMINAL_AUTO:
            flatpak_bwrap_add_arg (adverb_argv, "--terminal=auto");
            break;

          case PV_TERMINAL_NONE:
            flatpak_bwrap_add_arg (adverb_argv, "--terminal=none");
            break;

          case PV_TERMINAL_TTY:
            flatpak_bwrap_add_arg (adverb_argv, "--terminal=tty");
            break;

          case PV_TERMINAL_XTERM:
            flatpak_bwrap_add_arg (adverb_argv, "--terminal=xterm");
            break;

          default:
            g_warn_if_reached ();
            break;
        }

      flatpak_bwrap_append_args (adverb_argv, adverb_preload_argv);

      if (_srt_util_is_debugging ())
        flatpak_bwrap_add_arg (adverb_argv, "--verbose");

      flatpak_bwrap_add_arg (adverb_argv, "--");

      g_warn_if_fail (g_strv_length (adverb_argv->envp) == 0);
      flatpak_bwrap_append_bwrap (argv_in_container, adverb_argv);
    }

  if (self->options.launcher)
    {
      g_autoptr(FlatpakBwrap) launcher_argv =
        flatpak_bwrap_new (flatpak_bwrap_empty_env);
      g_autofree gchar *launcher_service = g_build_filename (tools_dir,
                                                             "steam-runtime-launcher-service",
                                                             NULL);
      g_debug ("Adding steam-runtime-launcher-service '%s'...", launcher_service);
      flatpak_bwrap_add_arg (launcher_argv, launcher_service);

      if (_srt_util_is_debugging ())
        flatpak_bwrap_add_arg (launcher_argv, "--verbose");

      /* In --launcher mode, arguments after the "--" separator are
       * passed to the launcher */
      flatpak_bwrap_append_argsv (launcher_argv, &argv[1], argc - 1);

      g_warn_if_fail (g_strv_length (launcher_argv->envp) == 0);
      flatpak_bwrap_append_bwrap (argv_in_container, launcher_argv);
    }
  else
    {
      /* In non-"--launcher" mode, arguments after the "--" separator
       * are the command to execute, passed to the adverb after "--".
       * Because we always use the adverb, we don't need to worry about
       * whether argv[1] starts with "-". */
      g_debug ("Setting arguments for wrapped command");
      flatpak_bwrap_append_argsv (argv_in_container, &argv[1], argc - 1);
    }

  if (flatpak_subsandbox != NULL)
    {
      for (i = 0; i < argv_in_container->fds->len; i++)
        {
          g_autofree char *fd_str = g_strdup_printf ("--forward-fd=%d",
                                                     g_array_index (argv_in_container->fds, int, i));
          flatpak_bwrap_add_arg (flatpak_subsandbox, fd_str);
        }

      for (i = 0; i < inherit_fds->len; i++)
        {
          g_autofree char *fd_str = g_strdup_printf ("--forward-fd=%d",
                                                     g_array_index (inherit_fds, int, i));
          flatpak_bwrap_add_arg (flatpak_subsandbox, fd_str);
        }

      flatpak_bwrap_add_arg (flatpak_subsandbox, "--");

      g_warn_if_fail (g_strv_length (flatpak_subsandbox->envp) == 0);
      flatpak_bwrap_append_bwrap (final_argv, flatpak_subsandbox);
    }
  else
    {
      g_assert (bwrap != NULL);
      g_warn_if_fail (g_strv_length (bwrap->envp) == 0);
      flatpak_bwrap_append_bwrap (final_argv, bwrap);
    }

  g_warn_if_fail (g_strv_length (argv_in_container->envp) == 0);
  flatpak_bwrap_append_bwrap (final_argv, argv_in_container);

  /* We'll have permuted the order anyway, so we might as well sort it,
   * to make debugging a bit easier. */
  flatpak_bwrap_sort_envp (final_argv);

  if (_srt_util_is_debugging ())
    {
      if (self->runtime != NULL && (pv_log_flags & PV_WRAP_LOG_FLAGS_OVERRIDES))
        pv_runtime_log_overrides (self->runtime);

      if (self->runtime != NULL && (pv_log_flags & PV_WRAP_LOG_FLAGS_CONTAINER))
        pv_runtime_log_container (self->runtime);

      g_debug ("Final command to execute:");

      for (i = 0; i < final_argv->argv->len; i++)
        {
          g_autofree gchar *quoted = NULL;

          quoted = g_shell_quote (g_ptr_array_index (final_argv->argv, i));
          g_debug ("\t%s", quoted);
        }

      g_debug ("Final environment:");

      for (i = 0; final_argv->envp != NULL && final_argv->envp[i] != NULL; i++)
        {
          g_autofree gchar *quoted = NULL;

          quoted = g_shell_quote (final_argv->envp[i]);
          g_debug ("\t%s", quoted);
        }
    }

  /* Clean up temporary directory before running our long-running process */
  if (self->runtime != NULL)
    pv_runtime_cleanup (self->runtime);

  flatpak_bwrap_finish (final_argv);

  if (self->options.write_final_argv != NULL)
    {
      FILE *file = fopen (self->options.write_final_argv, "w");
      if (file == NULL)
        {
          g_warning ("An error occurred trying to write out the arguments: %s",
                    g_strerror (errno));
          /* This is not a fatal error, try to continue */
        }
      else
        {
          for (i = 0; i < final_argv->argv->len; i++)
            fprintf (file, "%s%c",
                     (gchar *) g_ptr_array_index (final_argv->argv, i), '\0');

          fclose (file);
        }
    }

  if (!self->is_flatpak_env)
    {
      if (!pv_wrap_maybe_load_nvidia_modules (error))
        {
          g_debug ("Cannot load nvidia modules: %s", local_error->message);
          g_clear_error (&local_error);
        }
    }

  if (self->options.only_prepare)
    {
      ret = 0;
      goto out;
    }

  if (self->options.systemd_scope)
    pv_wrap_move_into_scope (steam_app_id);

  pv_bwrap_execve (final_argv,
                   (int *) inherit_fds->data, inherit_fds->len,
                   error);

out:
  if (local_error != NULL)
    _srt_log_failure ("%s", local_error->message);

  g_clear_pointer (&adverb_preload_argv, g_ptr_array_unref);

  g_debug ("Exiting with status %d", ret);
  return ret;
}
