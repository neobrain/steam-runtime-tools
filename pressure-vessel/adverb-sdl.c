/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"
#include "adverb-sdl.h"

/*
 * pv_adverb_set_up_dynamic_sdl:
 * @wrapped_command: The command to run and its environment
 * @lib_temp_dirs (nullable): A set of per-architecture directories
 *  using the dynamic string tokens `${PLATFORM}` or `${LIB}`
 * @prefix: Where to find SDL, normally `/usr` unless unit-testing
 * @overrides: Path to libraries overridden by the #PvRuntime,
 *  for example `/overrides`
 * @dynamic_var: Environment variable name such as `SDL_DYNAMIC_API`
 * @library: Path to SDL relative to `${libdir}`,
 *  for example `sdl2-compat/libSDL2-2.0.so.0`
 * @error: Used to report error on failure
 */
gboolean
pv_adverb_set_up_dynamic_sdl (FlatpakBwrap *wrapped_command,
                              PvPerArchDirs *lib_temp_dirs,
                              const char *prefix,
                              const char *overrides,
                              const char *dynamic_var,
                              const char *library,
                              GError **error)
{
  gboolean have_any = FALSE;
  /* Only the filename part of @library */
  const char *soname = glnx_basename (library);
  size_t abi_index;
  const char *existing_value;

  existing_value = g_environ_getenv (wrapped_command->envp, dynamic_var);

  if (existing_value != NULL)
    {
      /* Treat SDL{,3}_DYNAMIC_API from e.g. launch options as being
       * more important than STEAM_COMPAT_FLAGS=runtime-sdl{2,3} */
      g_message ("Not using %s from runtime because %s is already set to \"%s\"",
                 library, dynamic_var, existing_value);
      return TRUE;
    }

  if (lib_temp_dirs == NULL)
    return glnx_throw (error,
                       "Cannot set up dynamic %s without per-architecture directories",
                       soname);

  for (abi_index = 0; abi_index < PV_N_SUPPORTED_ARCHITECTURES; abi_index++)
    {
      const char *multiarch_tuple = pv_multiarch_tuples[abi_index];
      g_autofree gchar *dest = NULL;
      g_autofree gchar *from_runtime = NULL;
      g_autofree gchar *override = NULL;
      const char *target;

      /* We assume a Debian multiarch layout here: in practice this
       * is true for all Steam Runtime branches. */
      from_runtime = g_build_filename (prefix, "lib", multiarch_tuple, library, NULL);
      override = g_build_filename (overrides, "lib", multiarch_tuple, library, NULL);

      if (g_file_test (override, G_FILE_TEST_EXISTS))
        {
          /* This is quite unexpected - we hope that none of the
           * graphics drivers and Vulkan layers will have pulled in
           * something as big as SDL, because if they do, that really
           * undermines what we're trying to achieve. But if something
           * in the graphics stack does depend on SDL, we really have
           * no choice but to use that version. */
          g_message ("Using %s %s from graphics stack provider instead of runtime",
                     multiarch_tuple, library);
          target = override;
        }
      else if (g_file_test (from_runtime, G_FILE_TEST_EXISTS))
        {
          target = from_runtime;
        }
      else
        {
          g_info ("%s doesn't exist in container", from_runtime);
          continue;
        }

      dest = g_build_filename (lib_temp_dirs->abi_paths[abi_index],
                               soname, NULL);
      g_info ("Creating symlink \"%s\" -> \"%s\" in container",
              dest, target);

      if (symlink (target, dest) != 0)
        return glnx_throw_errno_prefix (error,
                                        "While creating symlink \"%s\"",
                                        dest);

      have_any = TRUE;
    }

  if (have_any)
    {
      g_autofree gchar *value = NULL;

      value = g_build_filename (lib_temp_dirs->libdl_token_path, soname, NULL);
      g_info ("Setting %s=\"%s\" to use runtime's %s", dynamic_var, soname, library);
      flatpak_bwrap_set_env (wrapped_command, dynamic_var, value, TRUE);
    }
  else
    {
      return glnx_throw (error,
                         "Unable to set %s: %s wasn't available for any architecture",
                         dynamic_var, library);
    }

  return TRUE;
}

void
pv_adverb_set_up_dynamic_sdls (FlatpakBwrap *wrapped_command,
                               PvPerArchDirs *lib_temp_dirs,
                               const char *prefix,
                               const char *overrides,
                               SrtSteamCompatFlags compat_flags)
{
  static const struct
    {
      const char *dynamic_var;
      const char *library;
      SrtSteamCompatFlags if_flag;
    }
  sdls[] =
    {
        {
            "SDL_DYNAMIC_API",
            "sdl2-compat/libSDL2-2.0.so.0",
            SRT_STEAM_COMPAT_FLAGS_RUNTIME_SDL2_COMPAT
        },
        {
            "SDL_DYNAMIC_API",
            "libSDL2-2.0.so.0",
            SRT_STEAM_COMPAT_FLAGS_RUNTIME_SDL2
        },
        {
            "SDL3_DYNAMIC_API",
            "libSDL3.so.0",
            SRT_STEAM_COMPAT_FLAGS_RUNTIME_SDL3
        },
    };
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (sdls); i++)
    {
      if (compat_flags & sdls[i].if_flag)
        {
          g_autoptr(GError) local_error = NULL;

          if (!pv_adverb_set_up_dynamic_sdl (wrapped_command,
                                             lib_temp_dirs,
                                             prefix,
                                             overrides,
                                             sdls[i].dynamic_var,
                                             sdls[i].library,
                                             &local_error))
            g_warning ("%s", local_error->message);
        }
    }
}
