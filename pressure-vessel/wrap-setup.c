/*
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2021 Collabora Ltd.
 * Copyright © 2017 Jonas Ådahl
 * Copyright © 2018 Erick555
 * Copyright © 2022 Julian Orth
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "wrap-setup.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/libdl-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/subprocess-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "steam-runtime-tools/virtualization-internal.h"
#include "libglnx.h"

#include <string.h>

#include "adverb-preload.h"
#include "bwrap.h"
#include "exports.h"
#include "flatpak-run-dbus-private.h"
#include "flatpak-run-private.h"
#include "flatpak-run-pulseaudio-private.h"
#include "flatpak-run-sockets-private.h"
#include "flatpak-run-wayland-private.h"
#include "flatpak-run-x11-private.h"
#include "flatpak-utils-private.h"
#include "supported-architectures.h"
#include "utils.h"

/* Taken from Flatpak flatpak-context.c, except where noted.
 * Last updated: Flatpak 1.14.6 */
static const char *dont_mount_in_root[] = {
  ".",
  "..",
  "app",
  "bin",
  "boot",
  "dev",
  "efi",
  "etc",
  "lib",
  "lib32",
  "lib64",
  "overrides",    /* pressure-vessel-specific */
  "proc",
  "root",
  "run",
  "sbin",
  "sys",
  "tmp",
  "usr",
  "var",
  NULL
};

gchar *
pv_wrap_check_bwrap (gboolean only_prepare,
                     SrtBwrapFlags *flags_out,
                     GError **error)
{
  g_autoptr(SrtSubprocessRunner) runner = NULL;
  g_autofree gchar *bwrap = NULL;
  const char *argv[] = { NULL, "--version", NULL };

  runner = _srt_subprocess_runner_new ();
  bwrap = _srt_check_bwrap (runner, only_prepare, flags_out, error);

  if (bwrap == NULL)
    return NULL;

  /* We're just running this so that the output ends up in the
   * debug log, so it's OK that the exit status and stdout are ignored. */
  argv[0] = bwrap;
  pv_run_sync (argv, NULL, NULL, NULL, NULL);

  return g_steal_pointer (&bwrap);
}

/* Based on Flatpak's flatpak_run_add_wayland_args() */
static void
pv_wrap_add_gamescope_args (FlatpakBwrap *sharing_bwrap,
                            SrtEnvOverlay *container_env)
{
  const char *wayland_display;
  g_autofree char *user_runtime_dir = flatpak_get_real_xdg_runtime_dir ();
  g_autofree char *wayland_socket = NULL;
  const char *sandbox_wayland_socket = NULL;
  struct stat statbuf;

  wayland_display = g_getenv ("GAMESCOPE_WAYLAND_DISPLAY");

  if (wayland_display == NULL)
    return;

  if (wayland_display[0] == '/')
    wayland_socket = g_strdup (wayland_display);
  else
    wayland_socket = g_build_filename (user_runtime_dir, wayland_display, NULL);

  if (stat (wayland_socket, &statbuf) == 0 &&
      (statbuf.st_mode & S_IFMT) == S_IFSOCK)
    {
      sandbox_wayland_socket = "/run/pressure-vessel/gamescope-socket";
      _srt_env_overlay_set (container_env, "GAMESCOPE_WAYLAND_DISPLAY",
                         sandbox_wayland_socket);
      flatpak_bwrap_add_args (sharing_bwrap,
                              "--ro-bind", wayland_socket, sandbox_wayland_socket,
                              NULL);
    }
}

/*
 * Use code borrowed from Flatpak to share various bits of the
 * execution environment with the host system, in particular Wayland,
 * X11 and PulseAudio sockets.
 */
FlatpakBwrap *
pv_wrap_share_sockets (SrtEnvOverlay *container_env,
                       const char * const *original_environ,
                       gboolean using_a_runtime,
                       gboolean is_flatpak_env)
{
  FlatpakContextShares shares;
  FlatpakContextSockets sockets;
  g_autoptr(FlatpakBwrap) sharing_bwrap =
    flatpak_bwrap_new (flatpak_bwrap_empty_env);
  g_auto(GStrv) envp = NULL;
  gsize i;

  g_return_val_if_fail (container_env != NULL, NULL);

  /* All potentially relevant sharing flags */
  shares = (FLATPAK_CONTEXT_SHARED_IPC
            | FLATPAK_CONTEXT_SHARED_NETWORK);
  /* We don't currently do anything with SSH_AUTH, PCSC, CUPS or GPG_AGENT.
   * We also don't use $WAYLAND_SOCKET, which is unsuitable for
   * pressure-vessel games because it only accepts one connection. */
  sockets = (FLATPAK_CONTEXT_SOCKET_PULSEAUDIO
             | FLATPAK_CONTEXT_SOCKET_SESSION_BUS
             | FLATPAK_CONTEXT_SOCKET_SYSTEM_BUS
             | FLATPAK_CONTEXT_SOCKET_WAYLAND
             | FLATPAK_CONTEXT_SOCKET_X11);

  /* If these are set by flatpak_run_add_x11_args(), etc., we'll
   * change them from unset to set later.
   * Every variable that is unset with flatpak_bwrap_unset_env() in
   * the functions we borrow from Flatpak (below) should be listed
   * here. */
  _srt_env_overlay_set (container_env, "DISPLAY", NULL);
  _srt_env_overlay_set (container_env, "PULSE_SERVER", NULL);
  _srt_env_overlay_set (container_env, "XAUTHORITY", NULL);

  flatpak_run_add_font_path_args (sharing_bwrap);

  flatpak_run_add_icon_path_args (sharing_bwrap);

  /* We need to set up IPC rendezvous points relatively late, so that
   * even if we are sharing /tmp via --filesystem=/tmp, we'll still
   * mount our own /tmp/.X11-unix over the top of the OS's. */
  if (using_a_runtime)
    {
      /* TODO: Try to call flatpak_run_add_socket_args_environment() here
       * instead of reinventing it */
      (void) sockets;

      flatpak_run_add_wayland_args (sharing_bwrap, FALSE);
      pv_wrap_add_gamescope_args (sharing_bwrap, container_env);

      /* When in a Flatpak container the "DISPLAY" env is equal to ":99.0",
       * but it might be different on the host system. As a workaround we simply
       * bind the whole "/tmp/.X11-unix" directory and later unset the container
       * "DISPLAY" env.
       */
      if (is_flatpak_env)
        {
          flatpak_bwrap_add_args (sharing_bwrap,
                                  "--ro-bind", "/tmp/.X11-unix", "/tmp/.X11-unix",
                                  NULL);
        }
      else
        {
          flatpak_run_add_x11_args (sharing_bwrap, TRUE, shares);
        }

      flatpak_run_add_pulseaudio_args (sharing_bwrap, shares);
      flatpak_run_add_session_dbus_args (sharing_bwrap);
      flatpak_run_add_system_dbus_args (sharing_bwrap);
      flatpak_run_add_socket_args_late (sharing_bwrap, shares);
      flatpak_run_add_a11y_dbus_args (sharing_bwrap);
      pv_wrap_add_pipewire_args (sharing_bwrap, container_env);
      pv_wrap_add_discord_args (sharing_bwrap);
    }

  flatpak_bwrap_populate_runtime_dir (sharing_bwrap, NULL);

  envp = pv_bwrap_steal_envp (sharing_bwrap);

  for (i = 0; envp[i] != NULL; i++)
    {
      static const char * const known_vars[] =
      {
        "AT_SPI_BUS_ADDRESS",
        "DBUS_SESSION_BUS_ADDRESS",
        "DBUS_SYSTEM_BUS_ADDRESS",
        "DISPLAY",
        "PULSE_CLIENTCONFIG",
        "PULSE_SERVER",
        "XAUTHORITY",
      };
      char *equals = strchr (envp[i], '=');
      const char *var = envp[i];
      const char *val = NULL;
      gsize j;

      if (equals != NULL)
        {
          *equals = '\0';
          val = equals + 1;
        }

      for (j = 0; j < G_N_ELEMENTS (known_vars); j++)
        {
          if (strcmp (var, known_vars[j]) == 0)
            break;
        }

      /* If this warning is reached, we might need to add this
       * variable to the block of
       * _srt_env_overlay_set (container_env, ., NULL) calls above */
      if (j >= G_N_ELEMENTS (known_vars))
        g_warning ("Extra environment variable %s set during container "
                   "setup but not in known_vars; check logic",
                   var);

      _srt_env_overlay_set (container_env, var, val);
    }

  /* flatpak_run_add_x11_args assumes that the default is to inherit
   * the caller's DISPLAY */
  if (_srt_env_overlay_get (container_env, "DISPLAY") == NULL)
    _srt_env_overlay_inherit (container_env, "DISPLAY");

  pv_wrap_set_icons_env_vars (container_env, original_environ);

  g_warn_if_fail (g_strv_length (sharing_bwrap->envp) == 0);
  return g_steal_pointer (&sharing_bwrap);
}

/*
 * Set the environment variables XCURSOR_PATH and XDG_DATA_DIRS to
 * support the icons from the host system.
 */
void
pv_wrap_set_icons_env_vars (SrtEnvOverlay *container_env,
                            const char * const *original_environ)
{
  g_autoptr(GString) new_data_dirs = g_string_new ("");
  g_autoptr(GString) new_xcursor_path = g_string_new ("");
  const gchar *initial_xdg_data_dirs = NULL;
  const gchar *original_xcursor_path = NULL;
  const gchar *container_xdg_data_home = NULL;
  g_autofree gchar *data_home_icons = NULL;

  original_xcursor_path = _srt_environ_getenv (original_environ, "XCURSOR_PATH");
  /* Cursors themes are searched in a few hardcoded paths. However if "XCURSOR_PATH"
   * is set, the user specified paths will override the hardcoded ones.
   * In order to keep the hardcoded paths in place, if "XCURSOR_PATH" is unset, we
   * append the default values first. Reference:
   * https://gitlab.freedesktop.org/xorg/lib/libxcursor/-/blob/80192583/src/library.c#L32 */
  if (original_xcursor_path == NULL)
    {
      /* We assume that this function is called after use_tmpfs_home() or
       * use_fake_home(), if we are going to. */
      container_xdg_data_home = _srt_env_overlay_get (container_env, "XDG_DATA_HOME");
      if (container_xdg_data_home == NULL)
        container_xdg_data_home = "~/.local/share";
      data_home_icons = g_build_filename (container_xdg_data_home, "icons", NULL);

      /* Note that unlike most path-searching implementations, libXcursor and
       * the derived code in Wayland expand '~' to the home directory. */
      pv_search_path_append (new_xcursor_path, data_home_icons);
      pv_search_path_append (new_xcursor_path, "~/.icons");
      pv_search_path_append (new_xcursor_path, "/usr/share/icons");
      pv_search_path_append (new_xcursor_path, "/usr/share/pixmaps");
      pv_search_path_append (new_xcursor_path, "/usr/X11R6/lib/X11/icons");
    }
  else
    {
      /* Append the XCURSOR_PATH values from the host. This is expected to work
       * only for the paths that have been bind-mounted to the same exact
       * location inside the container. One example would be the home directory,
       * unless pv was executed with the `--unshare-home` option. */
      pv_search_path_append (new_xcursor_path, original_xcursor_path);
    }
  /* Finally append the binded paths from the host */
  pv_search_path_append (new_xcursor_path, "/run/host/user-share/icons");
  pv_search_path_append (new_xcursor_path, "/run/host/share/icons");
  _srt_env_overlay_set (container_env, "XCURSOR_PATH", new_xcursor_path->str);

  initial_xdg_data_dirs = _srt_env_overlay_get (container_env, "XDG_DATA_DIRS");
  if (initial_xdg_data_dirs == NULL)
    initial_xdg_data_dirs = _srt_environ_getenv (original_environ, "XDG_DATA_DIRS");

  /* Reference:
   * https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html */
  if (initial_xdg_data_dirs == NULL)
    initial_xdg_data_dirs = "/usr/local/share:/usr/share";

  /* Append the host "share" directories to "XDG_DATA_DIRS".
   * Currently this is only useful to load the provider's icons */
  pv_search_path_append (new_data_dirs, initial_xdg_data_dirs);
  pv_search_path_append (new_data_dirs, "/run/host/user-share");
  pv_search_path_append (new_data_dirs, "/run/host/share");
  _srt_env_overlay_set (container_env, "XDG_DATA_DIRS", new_data_dirs->str);
}

/*
 * Export most root directories, but not the ones that
 * "flatpak run --filesystem=host" would skip.
 * (See flatpak_context_export(), which might replace this function
 * later on.)
 *
 * If we are running inside Flatpak, we assume that any directory
 * that is made available in the root, and is not in dont_mount_in_root,
 * came in via --filesystem=host or similar and matches its equivalent
 * on the real root filesystem.
 */
gboolean
pv_export_root_dirs_like_filesystem_host (int root_fd,
                                          FlatpakExports *exports,
                                          FlatpakFilesystemMode mode,
                                          SrtDirentCompareFunc arbitrary_dirent_order,
                                          GError **error)
{
  g_auto(SrtDirIter) iter = SRT_DIR_ITER_CLEARED;
  const char *member = NULL;

  g_return_val_if_fail (root_fd >= 0, FALSE);
  g_return_val_if_fail (exports != NULL, FALSE);
  g_return_val_if_fail ((unsigned) mode <= FLATPAK_FILESYSTEM_MODE_LAST, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!_srt_dir_iter_init_at (&iter, root_fd, ".",
                              SRT_DIR_ITER_FLAGS_FOLLOW,
                              arbitrary_dirent_order,
                              error))
    return FALSE;

  while (TRUE)
    {
      g_autofree gchar *path = NULL;
      struct dirent *dent;

      if (!_srt_dir_iter_next_dent (&iter, &dent, NULL, error))
        return FALSE;

      if (dent == NULL)
        break;

      member = dent->d_name;

      if (g_strv_contains (dont_mount_in_root, member))
        continue;

      path = g_build_filename ("/", member, NULL);

      /* See flatpak_context_export() for why we downgrade warnings
       * to debug messages here */
      pv_exports_expose_quietly (exports, mode, path);
    }

  /* For parity with Flatpak's handling of --filesystem=host */
  pv_exports_expose_or_log (exports, mode, "/run/media");

  return TRUE;
}

/*
 * This function assumes that /run on the host is the same as in the
 * current namespace, so it won't work in Flatpak.
 */
static gboolean
export_contents_of_run (int root_fd,
                        FlatpakBwrap *bwrap,
                        SrtDirentCompareFunc arbitrary_dirent_order,
                        GError **error)
{
  static const char *ignore[] =
  {
    "gfx",              /* can be created by pressure-vessel */
    "host",             /* created by pressure-vessel */
    "media",            /* see export_root_dirs_like_filesystem_host() */
    "pressure-vessel",  /* created by pressure-vessel */
    NULL
  };
  g_auto(SrtDirIter) iter = SRT_DIR_ITER_CLEARED;
  const char *member = NULL;

  g_return_val_if_fail (root_fd >= 0, FALSE);
  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (!g_file_test ("/.flatpak-info", G_FILE_TEST_IS_REGULAR),
                        FALSE);

  if (!_srt_dir_iter_init_at (&iter, root_fd, "run",
                              SRT_DIR_ITER_FLAGS_FOLLOW,
                              arbitrary_dirent_order,
                              error))
    return FALSE;

  while (TRUE)
    {
      g_autofree gchar *path = NULL;
      struct dirent *dent;

      if (!_srt_dir_iter_next_dent (&iter, &dent, NULL, error))
        return FALSE;

      if (dent == NULL)
        break;

      member = dent->d_name;

      if (g_strv_contains (ignore, member))
        continue;

      path = g_build_filename ("/run", member, NULL);
      flatpak_bwrap_add_args (bwrap,
                              "--bind", path, path,
                              NULL);
    }

  return TRUE;
}

/*
 * Configure @exports and @bwrap to use the host operating system to
 * provide basically all directories.
 *
 * /app and /boot are excluded, but are assumed to be unnecessary.
 *
 * /dev, /proc and /sys are assumed to have been handled by
 * pv_bwrap_add_api_filesystems() already.
 */
gboolean
pv_wrap_use_host_os (int root_fd,
                     FlatpakExports *exports,
                     FlatpakBwrap *bwrap,
                     SrtDirentCompareFunc arbitrary_dirent_order,
                     GError **error)
{
  static const char * const export_os_mutable[] = { "/etc", "/tmp", "/var" };
  gsize i;

  g_return_val_if_fail (root_fd >= 0, FALSE);
  g_return_val_if_fail (exports != NULL, FALSE);
  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!pv_bwrap_bind_usr (bwrap, "/", root_fd, "/", error))
    return FALSE;

  for (i = 0; i < G_N_ELEMENTS (export_os_mutable); i++)
    {
      const char *dir = export_os_mutable[i];
      struct stat stat_buf;

      g_assert (dir[0] == '/');

      if (TEMP_FAILURE_RETRY (fstatat (root_fd, dir + 1, &stat_buf, 0)) == 0)
        flatpak_bwrap_add_args (bwrap, "--bind", dir, dir, NULL);
    }

  /* We do each subdirectory of /run separately, so that we can
   * always create /run/host and /run/pressure-vessel. */
  if (!export_contents_of_run (root_fd, bwrap, arbitrary_dirent_order, error))
    return FALSE;

  /* This handles everything except:
   *
   * /app (should be unnecessary)
   * /boot (should be unnecessary)
   * /dev (handled by pv_bwrap_add_api_filesystems())
   * /etc (handled by export_os_mutable above)
   * /overrides (used internally by PvRuntime)
   * /proc (handled by pv_bwrap_add_api_filesystems())
   * /root (should be unnecessary)
   * /run (handled by export_contents_of_run() above)
   * /sys (handled by pv_bwrap_add_api_filesystems())
   * /tmp (handled by export_os_mutable above)
   * /usr, /lib, /lib32, /lib64, /bin, /sbin
   *  (all handled by pv_bwrap_bind_usr() above)
   * /var (handled by export_os_mutable above)
   */
  if (!pv_export_root_dirs_like_filesystem_host (root_fd,
                                                 exports,
                                                 FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                                 arbitrary_dirent_order,
                                                 error))
    return FALSE;

  return TRUE;
}

/*
 * Try to move the current process into a scope defined by the given
 * Steam app ID. If that's not possible, ignore.
 */
void
pv_wrap_move_into_scope (const char *steam_app_id)
{
  g_autoptr(GError) local_error = NULL;

  if (steam_app_id != NULL)
    {
      if (steam_app_id[0] == '\0')
        steam_app_id = NULL;
      else if (strcmp (steam_app_id, "0") == 0)
        steam_app_id = NULL;
    }

  if (steam_app_id != NULL)
    flatpak_run_in_transient_unit ("steam", "app", steam_app_id, &local_error);
  else
    flatpak_run_in_transient_unit ("steam", "", "unknown", &local_error);

  if (local_error != NULL)
    g_debug ("Cannot move into a systemd scope: %s", local_error->message);
}

/*
 * export_path_for_preload:
 * @context: Context we are running in
 * @path: Absolute path to a LD_PRELOAD or LD_AUDIT module
 *
 * Return the path that we would need to add to `context->exports`
 * to make this module available to the container, or %NULL if no path
 * should be added.
 *
 * Returns: (transfer full) (nullable): Path to export, or %NULL
 */
static gchar *
export_path_for_preload (PvWrapContext *context,
                         const char *path)
{
  g_autofree gchar *parent = NULL;
  glnx_autofd int parent_fd = -1;
  FlatpakExports *exports = context->exports;

  if (exports == NULL)
    return NULL;

  /* Normally we export the directory containing the LD_PRELOAD module,
   * and not the module itself. For example, if we're going to load
   * /home/me/.local/lib64/mangohud/libMangoHud_shim.so,
   * we want to share /home/me/.local/lib64/mangohud with the
   * container, so that libMangoHud_shim.so can dlopen
   * libMangoHud_opengl.so in the same directory, as expected.
   * This is a compromise between exporting too much (which we
   * could easily do if we walked further up the directory tree,
   * for example undoing the effect of --unshare-home)
   * and exporting too little (just libMangoHud_shim.so, which
   * doesn't do anything useful on its own). */
  parent = g_path_get_dirname (path);
  parent_fd = _srt_sysroot_open (context->current_root,
                                 parent,
                                 SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY,
                                 NULL,
                                 NULL);

  /* Special case: we don't want to export all of $HOME if we are doing
   * --unshare-home, so override that to only exporting the minimum
   *  that we can: the module itself. */
  if (context->current_home_fd >= 0
      && parent_fd >= 0
      && _srt_fstatat_is_same_file (context->current_home_fd, "",
                                    parent_fd, ""))
    {
      g_debug ("Not exporting parent of \"%s\" because it is $HOME",
               path);
      return g_strdup (path);
    }

  g_debug ("Exporting parent of \"%s\" -> \"%s\"", path, parent);
  return g_steal_pointer (&parent);
}

static void
append_preload_internal (PvWrapContext *context,
                         GPtrArray *argv,
                         PvPreloadVariableIndex which,
                         gsize abi_index,
                         const char *export_path,
                         const char *original_path,
                         PvAppendPreloadFlags flags)
{
  g_auto(PvAdverbPreloadModule) module = PV_ADVERB_PRELOAD_MODULE_INIT;
  g_autofree gchar *arg = NULL;
  gboolean flatpak_subsandbox = ((flags & PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX) != 0);

  module.index_in_preload_variables = which;
  module.abi_index = abi_index;

  if (context->runtime != NULL
      && (g_str_has_prefix (original_path, "/usr/")
          || g_str_has_prefix (original_path, "/lib")
          || (flatpak_subsandbox && g_str_has_prefix (original_path, "/app/"))))
    {
      const char *target = flatpak_subsandbox ? "/run/parent" : "/run/host";

      module.name = g_build_filename (target, original_path, NULL);
      g_debug ("%s -> %s", original_path, module.name);
    }
  else
    {
      module.name = g_strdup (original_path);
      g_debug ("%s -> unmodified", original_path);

      if (context->exports != NULL && export_path != NULL && export_path[0] == '/')
        {
          const gchar *steam_path = g_environ_getenv (context->original_environ, "STEAM_COMPAT_CLIENT_INSTALL_PATH");

          if (steam_path != NULL
              && flatpak_has_path_prefix (export_path, steam_path))
            {
              g_debug ("Skipping exposing \"%s\" because it is located "
                       "under the Steam client install path that we "
                       "bind by default", export_path);
            }
          else
            {
              g_debug ("%s needs adding to exports", export_path);
              pv_exports_expose_or_log (context->exports,
                                        FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                                        export_path);
            }
        }
    }

  arg = pv_adverb_preload_module_to_adverb_cli (&module);
  g_return_if_fail (arg != NULL);
  g_ptr_array_add (argv, g_steal_pointer (&arg));
}

/*
 * Deal with a LD_PRELOAD or LD_AUDIT module that contains tokens whose
 * expansion we can't control or predict, such as ${ORIGIN} or future
 * additions. We can't do much with these, because we can't assume that
 * the dynamic string tokens will expand in the same way for us as they
 * will for other programs.
 *
 * We mostly have to pass them into the container and hope for the best.
 * We can rewrite a /usr/, /lib or /app/ prefix, and we can export the
 * directory containing the first path component that has a dynamic
 * string token: for example, /opt/plat-${PLATFORM}/preload.so or
 * /opt/$PLATFORM/preload.so both have to be exported as /opt.
 *
 * Arguments are the same as for pv_wrap_append_preload().
 */
static void
append_preload_unsupported_token (PvWrapContext *context,
                                  GPtrArray *argv,
                                  PvPreloadVariableIndex which,
                                  const char *preload,
                                  PvAppendPreloadFlags flags)
{
  g_autofree gchar *export_path = NULL;
  char *dollar;
  char *slash;

  g_debug ("Found $ORIGIN or unsupported token in \"%s\"",
           preload);

  if (preload[0] == '/')
    {
      export_path = g_strdup (preload);
      dollar = strchr (export_path, '$');
      g_assert (dollar != NULL);
      /* Truncate before '$' */
      dollar[0] = '\0';
      slash = strrchr (export_path, '/');
      /* It's an absolute path, so there is definitely a '/' before '$' */
      g_assert (slash != NULL);
      /* Truncate before last '/' before '$' */
      slash[0] = '\0';

      /* If that truncation leaves it empty, don't try to expose
       * the whole root filesystem */
      if (export_path[0] != '/')
        {
          g_debug ("Not exporting root filesystem for \"%s\"",
                   preload);
          g_clear_pointer (&export_path, g_free);
        }
      else
        {
          g_debug ("Exporting \"%s\" for \"%s\"",
                   export_path, preload);
        }
    }
  else
    {
      /* Original path was relative and contained an unsupported
       * token like $ORIGIN. Pass it through as-is, without any extra
       * exports (because we don't know what the token means!), and
       * hope for the best. export_path stays NULL. */
      g_debug ("Not exporting \"%s\": not an absolute path, or starts "
               "with $ORIGIN",
               preload);
    }

  append_preload_internal (context,
                           argv,
                           which,
                           PV_UNSPECIFIED_ABI,
                           export_path,
                           preload,
                           flags);
}

/*
 * Deal with a LD_PRELOAD or LD_AUDIT module that contains tokens whose
 * expansion is ABI-dependent but otherwise fixed. We do these by
 * breaking it up into several ABI-dependent LD_PRELOAD modules, which
 * are recombined by pv-adverb. We have to do this because the expansion
 * of the ABI-dependent tokens could be different in the container, due
 * to using a different glibc.
 *
 * Arguments are the same as for pv_wrap_append_preload().
 */
static void
append_preload_per_architecture (PvWrapContext *context,
                                 GPtrArray *argv,
                                 PvPreloadVariableIndex which,
                                 const char *preload,
                                 PvAppendPreloadFlags flags)
{
  g_autoptr(SrtSystemInfo) system_info = srt_system_info_new (NULL);
  gsize n_supported_architectures = PV_N_SUPPORTED_ARCHITECTURES;
  gsize i;

  if (flags & PV_APPEND_PRELOAD_FLAGS_ONE_ARCHITECTURE)
    {
      G_STATIC_ASSERT (PV_N_SUPPORTED_ARCHITECTURES >= 1);
      n_supported_architectures = 1;
    }

  for (i = 0; i < n_supported_architectures; i++)
    {
      g_autoptr(GString) mock_path = NULL;
      g_autoptr(SrtLibrary) details = NULL;
      const gchar *multiarch_tuple = pv_multiarch_details[i].tuple;
      const char *path = NULL;

      if (!(flags & PV_APPEND_PRELOAD_FLAGS_IN_UNIT_TESTS))
        {
          srt_system_info_check_library (system_info,
                                         pv_multiarch_details[i].tuple,
                                         preload,
                                         &details);
          path = srt_library_get_absolute_path (details);
        }
      else
        {
          /* Use mock results to get predictable behaviour in the unit
           * tests. This avoids needing to have real libraries in place
           * when we do unit testing.
           *
           * tests/pressure-vessel/wrap-setup.c is the other side of this. */
          g_autofree gchar *lib = NULL;
          const char *platform = NULL;

#if defined(__i386__) || defined(__x86_64__)
          /* As a mock ${PLATFORM}, use the first one listed. */
          platform = pv_multiarch_details[i].platforms[0];
#else
          platform = "mock";
#endif
          /* As a mock ${LIB}, behave like Debian or the fdo SDK. */
          lib = g_strdup_printf ("lib/%s", multiarch_tuple);

          mock_path = g_string_new (preload);

          if (strchr (preload, '/') == NULL)
            {
              g_string_printf (mock_path, "/path/to/%s/%s", lib, preload);
            }
          else
            {
              g_string_replace (mock_path, "$LIB", lib, 0);
              g_string_replace (mock_path, "${LIB}", lib, 0);
              g_string_replace (mock_path, "$PLATFORM", platform, 0);
              g_string_replace (mock_path, "${PLATFORM}", platform, 0);
            }

          path = mock_path->str;

          /* As a special case, pretend one 64-bit library failed to load,
           * so we can exercise what happens when there's only a 32-bit
           * library available. */
          if (strstr (path, "only-32-bit") != NULL
              && strcmp (multiarch_tuple, SRT_ABI_I386) != 0)
            path = NULL;
        }

      if (path != NULL)
        {
          g_autofree gchar *export_path = export_path_for_preload (context, path);

          g_debug ("Found %s version of %s at %s",
                   multiarch_tuple, preload, path);
          append_preload_internal (context,
                                   argv,
                                   which,
                                   i,
                                   export_path,
                                   path,
                                   flags);
        }
      else
        {
          g_info ("Unable to load %s version of %s",
                  multiarch_tuple, preload);
        }
    }
}

static void
append_preload_basename (PvWrapContext *context,
                         GPtrArray *argv,
                         PvPreloadVariableIndex which,
                         const char *preload,
                         PvAppendPreloadFlags flags)
{
  gboolean runtime_has_library = FALSE;

  if (context->runtime != NULL)
    runtime_has_library = pv_runtime_has_library (context->runtime, preload);

  if (flags & PV_APPEND_PRELOAD_FLAGS_IN_UNIT_TESTS)
    {
      /* Mock implementation for unit tests: behave as though the
       * container has everything except libfakeroot/libfakechroot. */
      if (g_str_has_prefix (preload, "libfake"))
        runtime_has_library = FALSE;
      else
        runtime_has_library = TRUE;
    }

  if (runtime_has_library)
    {
      /* If the library exists in the container runtime or in the
       * stack we imported from the graphics provider, e.g.
       * LD_PRELOAD=libpthread.so.0, then we certainly don't want
       * to be loading it from the current namespace: that would
       * bypass our logic for comparing library versions and picking
       * the newest. Just pass through the LD_PRELOAD item into the
       * container, and let the dynamic linker in the container choose
       * what it means (container runtime or graphics provider as
       * appropriate). */
      g_debug ("Found \"%s\" in runtime or graphics stack provider, "
               "passing %s through as-is",
               preload,
               pv_preload_variables[which].variable);
      append_preload_internal (context,
                               argv,
                               which,
                               PV_UNSPECIFIED_ABI,
                               NULL,  /* don't export anything */
                               preload,
                               flags);
    }
  else
    {
      /* There's no such library in the container runtime or in the
       * graphics provider, so it's OK to inject the version from the
       * current namespace. Use the same trick as for ${PLATFORM} to
       * turn it into (up to) one absolute path per ABI. */
      g_debug ("Did not find \"%s\" in runtime or graphics stack provider, "
               "splitting architectures",
               preload);
      append_preload_per_architecture (context,
                                       argv,
                                       which,
                                       preload,
                                       flags);
    }
}

/**
 * pv_wrap_append_preload:
 * @context: Context we are running in
 * @argv: (element-type filename): Array of command-line options to populate
 * @which: Either `LD_AUDIT` or `LD_PRELOAD`
 * @preload: (type filename): Path of preloadable module in current
 *  namespace, possibly including special ld.so tokens such as `$LIB`,
 *  or basename of a preloadable module to be found in the standard
 *  library search path
 * @flags: Flags to adjust behaviour
 *
 * Adjust @preload to be valid for the container and append it
 * to @argv.
 */
void
pv_wrap_append_preload (PvWrapContext *context,
                        GPtrArray *argv,
                        PvPreloadVariableIndex which,
                        const char *preload,
                        PvAppendPreloadFlags flags)
{
  SrtLoadableKind kind;
  SrtLoadableFlags loadable_flags;
  const char *variable;

  g_return_if_fail (PV_IS_WRAP_CONTEXT (context));
  g_return_if_fail (argv != NULL);
  g_return_if_fail (preload != NULL);
  g_return_if_fail (which >= 0);
  g_return_if_fail (which < G_N_ELEMENTS (pv_preload_variables));

  variable = pv_preload_variables[which].variable;

  if (strstr (preload, "gtk3-nocsd") != NULL)
    {
      g_warning ("Disabling gtk3-nocsd %s: it is known to cause crashes.",
                 variable);
      return;
    }

  if (context->options.remove_game_overlay
      && g_str_has_suffix (preload, "/gameoverlayrenderer.so"))
    {
      g_info ("Disabling Steam Overlay: %s", preload);
      return;
    }

  kind = _srt_loadable_classify (preload, &loadable_flags);

  switch (kind)
    {
      case SRT_LOADABLE_KIND_BASENAME:
        /* Basenames can't have dynamic string tokens. */
        g_warn_if_fail ((loadable_flags & SRT_LOADABLE_FLAGS_DYNAMIC_TOKENS) == 0);
        append_preload_basename (context,
                                 argv,
                                 which,
                                 preload,
                                 flags);
        break;

      case SRT_LOADABLE_KIND_PATH:
        /* Paths can have dynamic string tokens. */
        if (loadable_flags & (SRT_LOADABLE_FLAGS_ORIGIN
                              | SRT_LOADABLE_FLAGS_UNKNOWN_TOKENS))
          {
            append_preload_unsupported_token (context,
                                              argv,
                                              which,
                                              preload,
                                              flags);
          }
        else if (loadable_flags & SRT_LOADABLE_FLAGS_ABI_DEPENDENT)
          {
            g_debug ("Found $LIB or $PLATFORM in \"%s\", splitting architectures",
                     preload);
            append_preload_per_architecture (context,
                                             argv,
                                             which,
                                             preload,
                                             flags);
          }
        else
          {
            g_autofree gchar *export_path = export_path_for_preload (context, preload);

            /* All dynamic tokens should be handled above, so we can
             * assume that preload is a concrete filename */
            g_warn_if_fail ((loadable_flags & SRT_LOADABLE_FLAGS_DYNAMIC_TOKENS) == 0);
            append_preload_internal (context,
                                     argv,
                                     which,
                                     PV_UNSPECIFIED_ABI,
                                     export_path,
                                     preload,
                                     flags);
          }
        break;

      case SRT_LOADABLE_KIND_ERROR:
      default:
        /* Empty string or similar syntactically invalid token:
         * ignore with a warning. Since steam-runtime-tools!352 and
         * steamlinuxruntime!64, the wrapper scripts don't give us
         * an empty argument any more. */
        g_warning ("Ignoring invalid loadable module \"%s\"", preload);

        break;
    }
}

/*
 * Nvidia Vulkan ray-tracing requires to load the `nvidia_uvm.ko` kernel
 * module, and this is usually done in `libcuda.so.1` by running the setuid
 * binary `nvidia-modprobe`. But when we are inside a container we don't bind
 * `nvidia-modprobe` and, even if we did, its setuid would not be effective
 * because we have `PR_SET_NO_NEW_PRIVS` and we don't have `CAP_SYS_MODULE` in
 * our capability bounding set.
 * For this reason if the current system is using the proprietary Nvidia
 * drivers, and `nvidia_uvm.ko` has not been already loaded, we should execute
 * `nvidia-modprobe` before entering in the container environment.
 */
gboolean
pv_wrap_maybe_load_nvidia_modules (GError **error)
{
  const char *nvidia_modprobe_argv[] =
  {
    "nvidia-modprobe",
    "-u",
    "-c=0",
    NULL
  };

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (g_file_test ("/sys/module/nvidia/version", G_FILE_TEST_IS_REGULAR)
      && !g_file_test ("/sys/module/nvidia_uvm", G_FILE_TEST_IS_DIR))
    return pv_run_sync (nvidia_modprobe_argv, NULL, NULL, NULL, error);

  return TRUE;
}

/**
 * pv_wrap_detect_virtualization:
 * @interpreter_root_out: (out) (optional): Used to return the absolute path
 *  to the overlay, if we are running under an interpreter/emulator like FEX.
 *  Otherwise, return %NULL.
 * @host_machine_out: (out) (optional): Used to return the host machine type, if
 *  an emulator is in use, or %SRT_MACHINE_TYPE_UNKNOWN if unknown or not
 *  applicable.
 */
void
pv_wrap_detect_virtualization (SrtSysroot **interpreter_root_out,
                               SrtMachineType *host_machine_out)
{
  g_autoptr(SrtVirtualizationInfo) virt_info = NULL;
  const char *val;

  g_return_if_fail (interpreter_root_out == NULL || *interpreter_root_out == NULL);

  /* At the moment we only care about FEX-Emu here, which we happen to
   * know implements CPUID, so it's faster to skip the filesystem-based
   * checks */
  virt_info = _srt_check_virtualization (NULL, NULL);

  if (host_machine_out != NULL)
    *host_machine_out = srt_virtualization_info_get_host_machine (virt_info);

  if (interpreter_root_out != NULL)
    {
      val = srt_virtualization_info_get_interpreter_root (virt_info);

      /* We happen to know that the way _srt_check_virtualization() gets
       * this information guarantees an object with a canonicalized path,
       * so we don't need to canonicalize it again. */
      if (val != NULL)
        *interpreter_root_out = _srt_sysroot_new (val, NULL);
      else
        *interpreter_root_out = NULL;
    }
}

/**
 * pv_share_temp_dir:
 * @exports: exported directories
 * @container_env: environment variables for the container
 *
 * Ensure that temporary directories are available.
 */
void
pv_share_temp_dir (FlatpakExports *exports,
                   SrtEnvOverlay *container_env)
{
  static const char * const temp_dir_vars[] =
  {
    "TEMP",
    "TEMPDIR",
    "TMP",
    "TMPDIR",
  };
  gsize i;

  /* Always export /tmp for now. SteamVR uses this as a rendezvous
   * directory for IPC.
   * Should always succeed, but if it somehow doesn't, make more noise
   * about this than usual: not sharing /tmp will break expectations. */
  pv_exports_expose_or_warn (exports,
                             FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                             "/tmp");

  for (i = 0; i < G_N_ELEMENTS (temp_dir_vars); i++)
    {
      const char *var = temp_dir_vars[i];
      const char *value = g_getenv (var);

      if (value == NULL)
        continue;

      if (value[0] != '/')
        {
          /* There's not much we can do with this... */
          g_warning ("%s is a relative path '%s', is this really intentional?",
                     var, value);
          continue;
        }

      /* Snap sets TMPDIR=$XDG_RUNTIME_DIR/snap.steam, but won't allow us
       * to bind-mount that path into our container. Unset TMPDIR in that
       * case, so that applications (and pv-adverb) will fall back to /tmp. */
      if (_srt_get_path_after (value, "run/user") != NULL)
        {
          g_debug ("%s '%s' is in /run/user, unsetting it",
                   var, value);
          _srt_env_overlay_set (container_env, var, NULL);
          continue;
        }

      /* Otherwise, try to share the directory with the container. */
      pv_exports_expose_or_log (exports, FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                value);
    }
}

typedef enum
{
  ENV_MOUNT_FLAGS_COLON_DELIMITED = (1 << 0),
  ENV_MOUNT_FLAGS_DEPRECATED = (1 << 1),
  ENV_MOUNT_FLAGS_READ_ONLY = (1 << 2),
  ENV_MOUNT_FLAGS_IF_HOME_SHARED = (1 << 3),
  ENV_MOUNT_FLAGS_NONE = 0
} EnvMountFlags;

typedef struct
{
  const char *name;
  EnvMountFlags flags;
  PvWrapExportFlags export_flags;
} EnvMount;

static const EnvMount known_required_env[] =
{
    { "PRESSURE_VESSEL_FILESYSTEMS_RO",
      ENV_MOUNT_FLAGS_READ_ONLY | ENV_MOUNT_FLAGS_COLON_DELIMITED },
    { "PRESSURE_VESSEL_FILESYSTEMS_RW", ENV_MOUNT_FLAGS_COLON_DELIMITED },
    { "PROTON_LOG_DIR", ENV_MOUNT_FLAGS_NONE },
    { "STEAM_COMPAT_APP_LIBRARY_PATH", ENV_MOUNT_FLAGS_DEPRECATED },
    { "STEAM_COMPAT_APP_LIBRARY_PATHS",
      ENV_MOUNT_FLAGS_COLON_DELIMITED | ENV_MOUNT_FLAGS_DEPRECATED },
    { "STEAM_COMPAT_CLIENT_INSTALL_PATH", ENV_MOUNT_FLAGS_NONE },
    { "STEAM_COMPAT_DATA_PATH", ENV_MOUNT_FLAGS_NONE },
    { "STEAM_COMPAT_INSTALL_PATH", ENV_MOUNT_FLAGS_NONE },
    { "STEAM_COMPAT_LIBRARY_PATHS", ENV_MOUNT_FLAGS_COLON_DELIMITED },
    { "STEAM_COMPAT_MOUNT_PATHS",
      ENV_MOUNT_FLAGS_COLON_DELIMITED | ENV_MOUNT_FLAGS_DEPRECATED },
    { "STEAM_COMPAT_MOUNTS", ENV_MOUNT_FLAGS_COLON_DELIMITED },
    { "STEAM_COMPAT_SHADER_PATH", ENV_MOUNT_FLAGS_NONE },
    { "STEAM_COMPAT_TOOL_PATH", ENV_MOUNT_FLAGS_DEPRECATED },
    { "STEAM_COMPAT_TOOL_PATHS", ENV_MOUNT_FLAGS_COLON_DELIMITED },
    { "STEAM_EXTRA_COMPAT_TOOLS_PATHS", ENV_MOUNT_FLAGS_COLON_DELIMITED },
    { "STEAM_RUNTIME_SCOUT", ENV_MOUNT_FLAGS_NONE },
    { "XDG_CACHE_HOME", ENV_MOUNT_FLAGS_IF_HOME_SHARED },
    { "XDG_CONFIG_HOME", ENV_MOUNT_FLAGS_IF_HOME_SHARED },
    { "XDG_DATA_HOME", ENV_MOUNT_FLAGS_IF_HOME_SHARED },
    { "XDG_STATE_HOME", ENV_MOUNT_FLAGS_IF_HOME_SHARED },
};

static void
bind_and_propagate_from_environ (PvWrapContext *self,
                                 PvHomeMode home_mode,
                                 SrtEnvOverlay *container_env,
                                 const char *variable,
                                 EnvMountFlags flags,
                                 PvWrapExportFlags export_flags)
{
  g_auto(GStrv) values = NULL;
  FlatpakFilesystemMode mode = FLATPAK_FILESYSTEM_MODE_READ_WRITE;
  SrtSysroot *sysroot;
  const char *value;
  const char *before;
  const char *after;
  gboolean changed = FALSE;
  gsize i;

  g_return_if_fail (PV_IS_WRAP_CONTEXT (self));
  g_return_if_fail (self->exports != NULL);
  g_return_if_fail (variable != NULL);
  sysroot = self->current_root;
  g_return_if_fail (sysroot != NULL);

  if (home_mode != PV_HOME_MODE_SHARED
      && (flags & ENV_MOUNT_FLAGS_IF_HOME_SHARED))
    return;

  if (_srt_env_overlay_contains (container_env, variable))
    value = _srt_env_overlay_get (container_env, variable);
  else
    value = g_environ_getenv (self->original_environ, variable);

  if (value == NULL)
    return;

  if (flags & ENV_MOUNT_FLAGS_DEPRECATED)
    g_message ("Setting $%s is deprecated", variable);

  if (flags & ENV_MOUNT_FLAGS_READ_ONLY)
    mode = FLATPAK_FILESYSTEM_MODE_READ_ONLY;

  if (flags & ENV_MOUNT_FLAGS_COLON_DELIMITED)
    {
      values = g_strsplit (value, ":", -1);
      before = "...:";
      after = ":...";
    }
  else
    {
      values = g_new0 (gchar *, 2);
      values[0] = g_strdup (value);
      values[1] = NULL;
      before = "";
      after = "";
    }

  for (i = 0; values[i] != NULL; i++)
    {
      g_autofree gchar *value_host = NULL;
      g_autofree gchar *canon = NULL;

      if (values[i][0] == '\0')
        continue;

      if (!_srt_sysroot_test (sysroot, values[i], SRT_RESOLVE_FLAGS_NONE, NULL))
        {
          g_info ("Not bind-mounting %s=\"%s%s%s\" because it does not exist",
                  variable, before, values[i], after);
          continue;
        }

      canon = g_canonicalize_filename (values[i], NULL);
      value_host = pv_current_namespace_path_to_host_path (canon);

      if (!pv_wrap_context_export_if_allowed (self,
                                              mode,
                                              canon,
                                              value_host,
                                              variable,
                                              before,
                                              after,
                                              export_flags))
        continue;

      if (strcmp (values[i], value_host) != 0)
        {
          g_clear_pointer (&values[i], g_free);
          values[i] = g_steal_pointer (&value_host);
          changed = TRUE;
        }
    }

  if (changed
      || _srt_sysroot_test (sysroot, "/.flatpak-info",
                            SRT_RESOLVE_FLAGS_NONE, NULL))
    {
      g_autofree gchar *joined = g_strjoinv (":", values);

      _srt_env_overlay_set (container_env, variable, joined);
    }
}

void
pv_bind_and_propagate_from_environ (PvWrapContext *self,
                                    PvHomeMode home_mode,
                                    SrtEnvOverlay *container_env)
{
  gsize i;

  g_return_if_fail (container_env != NULL);

  g_debug ("Making Steam environment variables available if required...");

  for (i = 0; i < G_N_ELEMENTS (known_required_env); i++)
    {
      const char *name = known_required_env[i].name;

      if (self->exports != NULL)
        {
          /* If we're using bubblewrap directly, we can and must make
           * sure that all required directories are bind-mounted */
          bind_and_propagate_from_environ (self, home_mode,
                                           container_env, name,
                                           known_required_env[i].flags,
                                           known_required_env[i].export_flags);
        }
      else
        {
          /* If we're using a Flatpak subsandbox, we have no choice but to
           * rely on the fact that any directory available to the parent app
           * is also going to be available to the subsandbox */
          g_return_if_fail (home_mode == PV_HOME_MODE_SHARED);

          if (!_srt_env_overlay_contains (container_env, name))
            _srt_env_overlay_set (container_env, name,
                                  g_environ_getenv (self->original_environ, name));
        }
    }
}
