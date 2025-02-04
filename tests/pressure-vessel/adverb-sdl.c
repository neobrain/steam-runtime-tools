/*
 * Copyright © 2023-2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <glib.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "adverb-sdl.h"
#include "tests/test-utils.h"

#define SDL_DYNAMIC_API "SDL_DYNAMIC_API"
#define SDL2_SONAME "libSDL2-2.0.so.0"
#define SDL3_DYNAMIC_API "SDL3_DYNAMIC_API"
#define SDL3_SONAME "libSDL3.so.0"

typedef struct
{
  TestsOpenFdSet old_fds;
  FlatpakBwrap *bwrap;
  PvPerArchDirs *lib_temp_dirs;
  GError *lib_temp_dirs_error;
  GLnxTmpDir mock_prefix;
  GLnxTmpDir mock_overrides;
} Fixture;

typedef struct
{
  int unused;
} Config;

static void
setup (Fixture *f,
       gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
  g_autoptr(GError) local_error = NULL;
  gsize i;

  f->old_fds = tests_check_fd_leaks_enter ();
  f->bwrap = flatpak_bwrap_new (flatpak_bwrap_empty_env);
  f->lib_temp_dirs = pv_per_arch_dirs_new (&f->lib_temp_dirs_error);
#ifdef _SRT_TESTS_STRICT
  /* We allow this to fail because it might fail on particularly strange
   * OS configurations, but for platforms we actively support,
   * we expect it to work */
  g_assert_no_error (f->lib_temp_dirs_error);
  g_assert_nonnull (f->lib_temp_dirs);
#endif

  if (f->lib_temp_dirs != NULL)
    {
      g_test_message ("Cross-platform module prefix: %s",
                      f->lib_temp_dirs->libdl_token_path);

      for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
        g_test_message ("Concrete path for %s architecture: %s",
                        pv_multiarch_tuples[i],
                        f->lib_temp_dirs->abi_paths[i]);
    }

  glnx_mkdtemp ("usr-XXXXXX", 0700, &f->mock_prefix, &local_error);
  g_assert_no_error (local_error);

  glnx_mkdtemp ("overrides-XXXXXX", 0700, &f->mock_overrides, &local_error);
  g_assert_no_error (local_error);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_clear_pointer (&f->lib_temp_dirs, pv_per_arch_dirs_free);
  g_clear_error (&f->lib_temp_dirs_error);
  g_clear_pointer (&f->bwrap, flatpak_bwrap_free);
  glnx_tmpdir_cleanup (&f->mock_prefix);
  glnx_tmpdir_cleanup (&f->mock_overrides);
  tests_check_fd_leaks_leave (f->old_fds);
}

static void touch (gchar **, const char *, ...) G_GNUC_NULL_TERMINATED;
static void
touch (gchar **path_out,
       const char *path,
       ...)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *joined = NULL;
  g_autofree gchar *parent = NULL;
  va_list ap;

  va_start (ap, path);
  joined = g_build_filename_valist (path, &ap);
  va_end (ap);
  parent = g_path_get_dirname (joined);

  glnx_shutil_mkdir_p_at (AT_FDCWD, parent, 0755, NULL, &local_error);
  g_assert_no_error (local_error);
  glnx_file_replace_contents_at (AT_FDCWD, joined, (const guint8 *) "", 0,
                                 (GLNX_FILE_REPLACE_NODATASYNC
                                  | GLNX_FILE_REPLACE_INCREASING_MTIME),
                                 NULL, &local_error);
  g_assert_no_error (local_error);

  if (path_out != NULL)
    *path_out = g_steal_pointer (&joined);
}

static void
assert_symlink (const char *arch_dir,
                const char *soname,
                const char *expect_target,
                GIOErrorEnum expect_error)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *joined = NULL;
  g_autofree gchar *target = NULL;

  joined = g_build_filename (arch_dir, soname, NULL);
  target = glnx_readlinkat_malloc (AT_FDCWD, joined, NULL, &local_error);

  if (target != NULL)
    g_test_message ("%s -> %s", joined, target);
  else
    g_test_message ("%s doesn't exist", joined);

  if (expect_target != NULL)
    {
      g_assert_no_error (local_error);
      g_assert_cmpstr (target, ==, expect_target);
    }
  else
    {
      g_assert_error (local_error, G_IO_ERROR, expect_error);
      g_assert_null (target);
    }
}

static void
assert_env (gchar **envp,
            const char *var,
            const char *expected_dir,
            const char *expected_file)
{
  g_autofree gchar *expected = NULL;
  const char *actual;

  if (expected_dir != NULL)
    expected = g_build_filename (expected_dir, expected_file, NULL);

  actual = g_environ_getenv (envp, var);

  if (actual != NULL)
    g_test_message ("%s=%s", var, actual);
  else
    g_test_message ("$%s is unset", var);

  g_assert_cmpstr (actual, ==, expected);
}

static void
test_basic (Fixture *f,
            gconstpointer context)
{
  g_autofree gchar *sdl2_target = NULL;
  g_autofree gchar *sdl3_target = NULL;

  if (f->lib_temp_dirs == NULL)
    {
      g_test_skip (f->lib_temp_dirs_error->message);
      return;
    }

  g_test_summary ("Basic setup of " SDL_DYNAMIC_API);

  touch (&sdl2_target,
         f->mock_prefix.path, "lib", pv_multiarch_tuples[PV_PRIMARY_ARCHITECTURE],
         SDL2_SONAME, NULL);
  touch (&sdl3_target,
         f->mock_prefix.path, "lib", pv_multiarch_tuples[PV_PRIMARY_ARCHITECTURE],
         SDL3_SONAME, NULL);

  g_test_message ("With no flags, not setup is done...");
  pv_adverb_set_up_dynamic_sdls (f->bwrap,
                                 f->lib_temp_dirs,
                                 f->mock_prefix.path,
                                 f->mock_overrides.path,
                                 SRT_STEAM_COMPAT_FLAGS_NONE);
  assert_symlink (f->lib_temp_dirs->abi_paths[PV_PRIMARY_ARCHITECTURE],
                  SDL2_SONAME, NULL, G_IO_ERROR_NOT_FOUND);
  assert_symlink (f->lib_temp_dirs->abi_paths[PV_PRIMARY_ARCHITECTURE],
                  SDL3_SONAME, NULL, G_IO_ERROR_NOT_FOUND);
  assert_env (f->bwrap->envp, SDL_DYNAMIC_API, NULL, NULL);
  assert_env (f->bwrap->envp, SDL3_DYNAMIC_API, NULL, NULL);

  g_test_message ("SDL2 flag sets up SDL2...");
  pv_adverb_set_up_dynamic_sdls (f->bwrap,
                                 f->lib_temp_dirs,
                                 f->mock_prefix.path,
                                 f->mock_overrides.path,
                                 SRT_STEAM_COMPAT_FLAGS_RUNTIME_SDL2);
  assert_symlink (f->lib_temp_dirs->abi_paths[PV_PRIMARY_ARCHITECTURE],
                  SDL2_SONAME, sdl2_target, 0);
  assert_symlink (f->lib_temp_dirs->abi_paths[PV_PRIMARY_ARCHITECTURE],
                  SDL3_SONAME, NULL, G_IO_ERROR_NOT_FOUND);
  assert_env (f->bwrap->envp, SDL_DYNAMIC_API,
              f->lib_temp_dirs->libdl_token_path, SDL2_SONAME);
  assert_env (f->bwrap->envp, SDL3_DYNAMIC_API, NULL, NULL);

  g_test_message ("SDL3 flag additionally sets up SDL3...");
  pv_adverb_set_up_dynamic_sdls (f->bwrap,
                                 f->lib_temp_dirs,
                                 f->mock_prefix.path,
                                 f->mock_overrides.path,
                                 SRT_STEAM_COMPAT_FLAGS_RUNTIME_SDL3);
  assert_symlink (f->lib_temp_dirs->abi_paths[PV_PRIMARY_ARCHITECTURE],
                  SDL2_SONAME, sdl2_target, 0);
  assert_symlink (f->lib_temp_dirs->abi_paths[PV_PRIMARY_ARCHITECTURE],
                  SDL3_SONAME, sdl3_target, 0);
  assert_env (f->bwrap->envp, SDL_DYNAMIC_API,
              f->lib_temp_dirs->libdl_token_path, SDL2_SONAME);
  assert_env (f->bwrap->envp, SDL3_DYNAMIC_API,
              f->lib_temp_dirs->libdl_token_path, SDL3_SONAME);
}

static void
test_cannot_symlink (Fixture *f,
                     gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *sdl2_target = NULL;
  gboolean res;

  if (f->lib_temp_dirs == NULL)
    {
      g_test_skip (f->lib_temp_dirs_error->message);
      return;
    }

  touch (&sdl2_target,
         f->mock_prefix.path, "lib", pv_multiarch_tuples[PV_PRIMARY_ARCHITECTURE],
         SDL2_SONAME, NULL);
  touch (NULL,
         f->lib_temp_dirs->abi_paths[PV_PRIMARY_ARCHITECTURE],
         SDL2_SONAME, NULL);

  g_test_summary ("If we can't create the symlink, setup fails");
  res = pv_adverb_set_up_dynamic_sdl (f->bwrap,
                                      f->lib_temp_dirs,
                                      f->mock_prefix.path,
                                      f->mock_overrides.path,
                                      SDL_DYNAMIC_API,
                                      SDL2_SONAME,
                                      &local_error);
  g_assert_nonnull (local_error);
  /* pv-adverb would log the GError as a warning */
  g_test_message ("%s", local_error->message);
  g_assert_false (res);
  assert_symlink (f->lib_temp_dirs->abi_paths[PV_PRIMARY_ARCHITECTURE],
                  SDL2_SONAME, NULL, G_IO_ERROR_INVALID_ARGUMENT);
  assert_env (f->bwrap->envp, SDL_DYNAMIC_API, NULL, NULL);
}

static void
test_impossible (Fixture *f,
                 gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *sdl2_target = NULL;
  gboolean res;

  touch (&sdl2_target,
         f->mock_prefix.path, "lib", pv_multiarch_tuples[PV_PRIMARY_ARCHITECTURE],
         SDL2_SONAME, NULL);

  g_test_summary ("If we don't know the $LIB or $PLATFORM, nothing happens, "
                  "with a warning");
  res = pv_adverb_set_up_dynamic_sdl (f->bwrap,
                                      NULL,
                                      f->mock_prefix.path,
                                      f->mock_overrides.path,
                                      SDL_DYNAMIC_API,
                                      SDL2_SONAME,
                                      &local_error);
  g_assert_nonnull (local_error);
  /* pv-adverb would log the GError as a warning */
  g_test_message ("%s", local_error->message);
  g_assert_false (res);
  assert_symlink (f->lib_temp_dirs->abi_paths[PV_PRIMARY_ARCHITECTURE],
                  SDL2_SONAME, NULL, G_IO_ERROR_NOT_FOUND);
  assert_env (f->bwrap->envp, SDL_DYNAMIC_API, NULL, NULL);
}

static void
test_in_gfx_stack (Fixture *f,
                   gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *sdl2_target = NULL;
  g_autofree gchar *gfx_sdl2_target = NULL;
  gboolean res;

  if (f->lib_temp_dirs == NULL)
    {
      g_test_skip (f->lib_temp_dirs_error->message);
      return;
    }

  touch (&sdl2_target,
         f->mock_prefix.path, "lib", pv_multiarch_tuples[PV_PRIMARY_ARCHITECTURE],
         SDL2_SONAME, NULL);
  touch (&gfx_sdl2_target,
         f->mock_overrides.path, "lib", pv_multiarch_tuples[PV_PRIMARY_ARCHITECTURE],
         SDL2_SONAME, NULL);

  g_test_summary ("We prefer SDL from the graphics provider if present");
  res = pv_adverb_set_up_dynamic_sdl (f->bwrap,
                                      f->lib_temp_dirs,
                                      f->mock_prefix.path,
                                      f->mock_overrides.path,
                                      SDL_DYNAMIC_API,
                                      SDL2_SONAME,
                                      &local_error);
  g_assert_no_error (local_error);
  g_assert_true (res);
  assert_symlink (f->lib_temp_dirs->abi_paths[PV_PRIMARY_ARCHITECTURE],
                  SDL2_SONAME, gfx_sdl2_target, 0);
  assert_env (f->bwrap->envp, SDL_DYNAMIC_API,
              f->lib_temp_dirs->libdl_token_path, SDL2_SONAME);
}

static void
test_in_gfx_stack_only (Fixture *f,
                        gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *gfx_sdl2_target = NULL;
  gboolean res;

  if (f->lib_temp_dirs == NULL)
    {
      g_test_skip (f->lib_temp_dirs_error->message);
      return;
    }

  touch (&gfx_sdl2_target,
         f->mock_overrides.path, "lib", pv_multiarch_tuples[PV_PRIMARY_ARCHITECTURE],
         SDL2_SONAME, NULL);

  g_test_summary ("We use SDL from the graphics provider if necessary");
  res = pv_adverb_set_up_dynamic_sdl (f->bwrap,
                                      f->lib_temp_dirs,
                                      f->mock_prefix.path,
                                      f->mock_overrides.path,
                                      SDL_DYNAMIC_API,
                                      SDL2_SONAME,
                                      &local_error);
  g_assert_no_error (local_error);
  g_assert_true (res);
  assert_symlink (f->lib_temp_dirs->abi_paths[PV_PRIMARY_ARCHITECTURE],
                  SDL2_SONAME, gfx_sdl2_target, 0);
  assert_env (f->bwrap->envp, SDL_DYNAMIC_API,
              f->lib_temp_dirs->libdl_token_path, SDL2_SONAME);
}

static void
test_missing (Fixture *f,
              gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;
  gboolean res;

  if (f->lib_temp_dirs == NULL)
    {
      g_test_skip (f->lib_temp_dirs_error->message);
      return;
    }

  g_test_summary ("If SDL is missing, nothing happens, with a warning");
  res = pv_adverb_set_up_dynamic_sdl (f->bwrap,
                                      f->lib_temp_dirs,
                                      f->mock_prefix.path,
                                      f->mock_overrides.path,
                                      SDL_DYNAMIC_API,
                                      SDL2_SONAME,
                                      &local_error);
  g_assert_nonnull (local_error);
  /* pv-adverb would log the GError as a warning */
  g_test_message ("%s", local_error->message);
  g_assert_false (res);
  assert_symlink (f->lib_temp_dirs->abi_paths[PV_PRIMARY_ARCHITECTURE],
                  SDL2_SONAME, NULL, G_IO_ERROR_NOT_FOUND);
  assert_env (f->bwrap->envp, SDL_DYNAMIC_API, NULL, NULL);
}

static void
test_overridden (Fixture *f,
                 gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;
  gboolean res;

  if (f->lib_temp_dirs == NULL)
    {
      g_test_skip (f->lib_temp_dirs_error->message);
      return;
    }

  g_test_summary ("Setting " SDL_DYNAMIC_API " takes precedence");
  flatpak_bwrap_set_env (f->bwrap, SDL_DYNAMIC_API, "/whatever", TRUE);
  res = pv_adverb_set_up_dynamic_sdl (f->bwrap,
                                      f->lib_temp_dirs,
                                      f->mock_prefix.path,
                                      f->mock_overrides.path,
                                      SDL_DYNAMIC_API,
                                      SDL2_SONAME,
                                      &local_error);
  g_assert_no_error (local_error);
  g_assert_true (res);
  assert_symlink (f->lib_temp_dirs->abi_paths[PV_PRIMARY_ARCHITECTURE],
                  SDL2_SONAME, NULL, G_IO_ERROR_NOT_FOUND);
  assert_env (f->bwrap->envp, SDL_DYNAMIC_API, "/whatever", NULL);
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();

  /* In unit tests it isn't always straightforward to find the real
   * ${PLATFORM}, so use a predictable mock implementation that always
   * uses PvMultiarchDetails.platforms[0] */
  g_setenv ("PRESSURE_VESSEL_TEST_STANDARDIZE_PLATFORM", "1", TRUE);

  _srt_tests_init (&argc, &argv, NULL);
  g_test_add ("/pv-adverb/sdl/basic", Fixture, NULL,
              setup, test_basic, teardown);
  g_test_add ("/pv-adverb/sdl/cannot-symlink", Fixture, NULL,
              setup, test_cannot_symlink, teardown);
  g_test_add ("/pv-adverb/sdl/impossible", Fixture, NULL,
              setup, test_impossible, teardown);
  g_test_add ("/pv-adverb/sdl/in_gfx_stack", Fixture, NULL,
              setup, test_in_gfx_stack, teardown);
  g_test_add ("/pv-adverb/sdl/in_gfx_stack_only", Fixture, NULL,
              setup, test_in_gfx_stack_only, teardown);
  g_test_add ("/pv-adverb/sdl/missing", Fixture, NULL,
              setup, test_missing, teardown);
  g_test_add ("/pv-adverb/sdl/overridden", Fixture, NULL,
              setup, test_overridden, teardown);

  return g_test_run ();
}
