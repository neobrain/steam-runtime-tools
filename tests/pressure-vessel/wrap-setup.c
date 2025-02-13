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

#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "tests/test-utils.h"

#include "bwrap.h"
#include "passwd.h"
#include "supported-architectures.h"
#include "wrap-context.h"
#include "wrap-home.h"
#include "wrap-setup.h"
#include "utils.h"

/* These match the first entry in PvMultiArchdetails.platforms,
 * which is the easiest realistic thing for a mock implementation of
 * srt_system_info_check_library() to use. */
#define MOCK_PLATFORM_32 "i686"
#define MOCK_PLATFORM_64 "xeon_phi"
#define MOCK_PLATFORM_GENERIC "mock"

/* These match Debian multiarch, which is as good a thing as any for
 * a mock implementation of srt_system_info_check_library() to use. */
#define MOCK_LIB_32 "lib/" SRT_ABI_I386
#define MOCK_LIB_64 "lib/" SRT_ABI_X86_64
#define MOCK_LIB_GENERIC "lib/" _SRT_MULTIARCH

#if defined(__i386__) || defined(__x86_64__)
  /* On x86, we treat x86_64 as the primary architecture.
   * This means it's the first one whenever we have a list of
   * per-architecture things, and if we pretend to only support
   * one architecture for test coverage purposes, that architecture
   * will be x86_64. */
# define PRIMARY_ABI SRT_ABI_X86_64
# define PRIMARY_PLATFORM MOCK_PLATFORM_64
# define PRIMARY_LIB MOCK_LIB_64
#else
  /* On non-x86, the mock implementation of srt_system_info_check_library()
   * uses these expansions for ${PLATFORM} and ${LIB} instead of the
   * real ones. */
# define PRIMARY_ABI _SRT_MULTIARCH
# define PRIMARY_PLATFORM MOCK_PLATFORM_GENERIC
# define PRIMARY_LIB MOCK_LIB_GENERIC
#endif

typedef struct
{
  TestsOpenFdSet old_fds;
  PvWrapContext *context;
  SrtSysroot *mock_host;
  FlatpakBwrap *bwrap;
  gchar *home;
  gchar *tmpdir;
  gchar *mock_runtime;
  gchar *var;
  int tmpdir_fd;
  int mock_runtime_fd;
  int var_fd;
} Fixture;

typedef struct
{
  PvRuntimeFlags runtime_flags;
  PvAppendPreloadFlags preload_flags;
} Config;

static const Config default_config = {};
static const Config copy_config =
{
  .runtime_flags = PV_RUNTIME_FLAGS_COPY_RUNTIME,
};
static const Config interpreter_root_config =
{
  .runtime_flags = (PV_RUNTIME_FLAGS_COPY_RUNTIME
                    | PV_RUNTIME_FLAGS_INTERPRETER_ROOT),
};
static const Config one_arch_config =
{
  .preload_flags = PV_APPEND_PRELOAD_FLAGS_ONE_ARCHITECTURE,
};

static int
open_or_die (const char *path,
             int flags,
             int mode)
{
  glnx_autofd int fd = open (path, flags | O_CLOEXEC, mode);

  if (fd >= 0)
    return g_steal_fd (&fd);
  else
    g_error ("open(%s, 0x%x): %s", path, flags, g_strerror (errno));
}

/*
 * Populate root_fd with the given directories and symlinks.
 * The paths use a simple domain-specific language:
 * - symlinks are given as "link>target"
 * - directories are given as "dir/"
 * - any other string is created as a regular 0-byte file
 */
static void
fixture_populate_dir (Fixture *f,
                      int root_fd,
                      const char * const *paths,
                      gsize n_paths)
{
  g_autoptr(GError) local_error = NULL;
  gsize i;

  for (i = 0; i < n_paths; i++)
    {
      const char *path = paths[i];

      /* All paths we create should be created relative to the mock root */
      while (path[0] == '/')
        path++;

      if (strchr (path, '>'))
        {
          g_auto(GStrv) pieces = g_strsplit (path, ">", 2);

          g_test_message ("Creating symlink %s -> %s", pieces[0], pieces[1]);
          g_assert_no_errno (TEMP_FAILURE_RETRY (symlinkat (pieces[1], root_fd, pieces[0])));
        }
      else if (g_str_has_suffix (path, "/"))
        {
          g_test_message ("Creating directory %s", path);

          glnx_shutil_mkdir_p_at (root_fd, path, 0755, NULL, &local_error);
          g_assert_no_error (local_error);
        }
      else
        {
          g_autofree char *dir = g_path_get_dirname (path);

          g_test_message ("Creating directory %s", dir);
          glnx_shutil_mkdir_p_at (root_fd, dir, 0755, NULL, &local_error);
          g_assert_no_error (local_error);

          g_test_message ("Creating file %s", path);
          glnx_file_replace_contents_at (root_fd, path,
                                         (const guint8 *) "", 0, 0, NULL,
                                         &local_error);
          g_assert_no_error (local_error);
        }
    }
}

static void
fixture_create_exports (Fixture *f)
{
  glnx_autofd int fd = open_or_die (f->mock_host->path, O_RDONLY | O_DIRECTORY, 0755);

  g_return_if_fail (f->context->exports == NULL);
  f->context->exports = flatpak_exports_new ();
  flatpak_exports_take_host_fd (f->context->exports, g_steal_fd (&fd));
}

static void
fixture_create_runtime (Fixture *f,
                        PvRuntimeFlags flags)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(PvGraphicsProvider) graphics_provider = NULL;
  const char *gfx_in_container;

  g_assert_null (f->context->runtime);

  if (flags & PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX)
    gfx_in_container = "/run/parent";
  else
    gfx_in_container = "/run/host";

  graphics_provider = pv_graphics_provider_new ("/", gfx_in_container,
                                                TRUE, &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (graphics_provider);

  f->context->runtime = pv_runtime_new (f->mock_runtime,
                                        f->var,
                                        NULL,
                                        graphics_provider,
                                        NULL,
                                        _srt_peek_environ_nonnull (),
                                        (flags
                                         | PV_RUNTIME_FLAGS_VERBOSE
                                         | PV_RUNTIME_FLAGS_SINGLE_THREAD),
                                        PV_WORKAROUND_FLAGS_NONE,
                                        &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (f->context->runtime);
}

static void
setup (Fixture *f,
       gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *mock_host = NULL;

  f->old_fds = tests_check_fd_leaks_enter ();
  f->tmpdir = g_dir_make_tmp ("pressure-vessel-tests.XXXXXX", &local_error);
  g_assert_no_error (local_error);
  glnx_opendirat (AT_FDCWD, f->tmpdir, TRUE, &f->tmpdir_fd, &local_error);
  g_assert_no_error (local_error);

  mock_host = g_build_filename (f->tmpdir, "host", NULL);
  f->mock_runtime = g_build_filename (f->tmpdir, "runtime", NULL);
  f->var = g_build_filename (f->tmpdir, "var", NULL);
  g_assert_no_errno (g_mkdir (mock_host, 0755));
  g_assert_no_errno (g_mkdir (f->mock_runtime, 0755));
  g_assert_no_errno (g_mkdir (f->var, 0755));
  f->mock_host = _srt_sysroot_new (mock_host, &local_error);
  g_assert_no_error (local_error);
  glnx_opendirat (AT_FDCWD, f->mock_runtime, TRUE, &f->mock_runtime_fd, &local_error);
  g_assert_no_error (local_error);
  glnx_opendirat (AT_FDCWD, f->var, TRUE, &f->var_fd, &local_error);
  g_assert_no_error (local_error);

  f->home = g_build_filename (f->mock_host->path, "home", "me", NULL);
  glnx_shutil_mkdir_p_at (AT_FDCWD, f->home, 0755, NULL, &local_error);
  g_assert_no_error (local_error);
  f->context = pv_wrap_context_new (f->mock_host, "/home/me", &local_error);
  g_assert_no_error (local_error);
  f->bwrap = flatpak_bwrap_new (flatpak_bwrap_empty_env);

  /* Some tests need to know where Steam is installed;
   * pretend that we have it installed in /steam */
  f->context->original_environ = g_environ_setenv (f->context->original_environ,
                                                   "STEAM_COMPAT_CLIENT_INSTALL_PATH",
                                                   "/steam",
                                                   TRUE);
}

/*
 * PreloadTest:
 * @input: One of the colon-separated items in LD_PRELOAD
 * @warning: (nullable): If not null, expect the item to be ignored
 *  with this warning
 * @touch: (nullable): If not null, create this regular file associated
 *  with the item in the mock sysroot.
 *  "=" may be used as a shorthand for repeating @input.
 * @touch_i386: (nullable): If not null, create this regular file in the
 *  mock sysroot, but only if the i386 architecture is supported.
 * @expected: The arguments we expect to see passed to
 *  `pv-adverb --ld-preload` as a result, with no /run/host/ prefix.
 *  "=" may be used as a shorthand for repeating @input.
 *  If prefixed with "i386:", then we expect the rest of the value as an
 *  argument if and only if the i386 architecture is supported.
 *  %NULL items are ignored.
 */
typedef struct
{
  const char *input;
  const char *warning;
  const char *touch;
  const char *touch_i386;
  /* Array lengths are arbitrary, expand as required */
  const char *expected[2];
} PreloadTest;

static const PreloadTest ld_preload_tests[] =
{
  {
    .input = "",
    .warning = "Ignoring invalid loadable module \"\"",
  },
  {
    .input = "",
    .warning = "Ignoring invalid loadable module \"\"",
  },
  {
    .input = "/app/lib/libpreloadA.so",
    .touch = "=",
    .expected = { "=" },
  },
  {
    .input = "/platform/plat-$PLATFORM/libpreloadP.so",
    .touch = "/platform/plat-" PRIMARY_PLATFORM "/libpreloadP.so",
    .touch_i386 = "/platform/plat-" MOCK_PLATFORM_32 "/libpreloadP.so",
    .expected = {
      "/platform/plat-" PRIMARY_PLATFORM "/libpreloadP.so:abi=" PRIMARY_ABI,
      "i386:/platform/plat-" MOCK_PLATFORM_32 "/libpreloadP.so:abi=" SRT_ABI_I386,
    },
  },
  {
    .input = "/opt/${LIB}/libpreloadL.so",
    .touch = "/opt/" PRIMARY_LIB "/libpreloadL.so",
    .touch_i386 = "/opt/" MOCK_LIB_32 "/libpreloadL.so",
    .expected = {
      "/opt/" PRIMARY_LIB "/libpreloadL.so:abi=" PRIMARY_ABI,
      "i386:/opt/" MOCK_LIB_32 "/libpreloadL.so:abi=" SRT_ABI_I386,
    },
  },
  {
    .input = "/lib/libpreload-rootfs.so",
    .touch = "=",
    .expected = { "=" },
  },
  {
    .input = "/usr/lib/libpreloadU.so",
    .touch = "=",
    .expected = { "=" },
  },
  {
    .input = "/home/me/libpreloadH.so",
    .touch = "=",
    .expected = { "=" },
  },
  {
    .input = "/steam/lib/gameoverlayrenderer.so",
    .touch = "=",
    .expected = { "=" },
  },
  {
    .input = "/overlay/libs/${ORIGIN}/../lib/libpreloadO.so",
    .touch = "=",
    .expected = { "=" },
  },
  {
    .input = "/future/libs-$FUTURE/libpreloadF.so",
    .touch = "/future/libs-post2038/.exists",
    .expected = { "=" },
  },
  {
    .input = "/in-root-plat-${PLATFORM}-only-32-bit.so",
    .touch_i386 = "/in-root-plat-" MOCK_PLATFORM_32 "-only-32-bit.so",
    .expected = {
      "i386:/in-root-plat-i686-only-32-bit.so:abi=" SRT_ABI_I386,
    },
  },
  {
    .input = "/in-root-${FUTURE}.so",
    .expected = { "=" },
  },
  {
    .input = "./${RELATIVE}.so",
    .expected = { "=" },
  },
  {
    .input = "./relative.so",
    .expected = { "=" },
  },
  {
    /* Our mock implementation of pv_runtime_has_library() behaves as though
     * libfakeroot is not in the runtime or graphics stack provider, only
     * the current namespace */
    .input = "libfakeroot.so",
    .expected = {
      "/path/to/" PRIMARY_LIB "/libfakeroot.so:abi=" PRIMARY_ABI,
      "i386:/path/to/" MOCK_LIB_32 "/libfakeroot.so:abi=" SRT_ABI_I386,
    },
  },
  {
    /* Our mock implementation of pv_runtime_has_library() behaves as though
     * libpthread.so.0 *is* in the runtime, as we would expect */
    .input = "libpthread.so.0",
    .expected = { "=" },
  },
  {
    .input = "/usr/local/lib/libgtk3-nocsd.so.0",
    .touch = "=",
    .warning = "Disabling gtk3-nocsd LD_PRELOAD: it is known to cause crashes.",
  },
  {
    .input = "",
    .warning = "Ignoring invalid loadable module \"\"",
  },
};

static void
setup_ld_preload (Fixture *f,
                  gconstpointer context)
{
  const Config *config = context;
  g_autoptr(GPtrArray) touch = g_ptr_array_new ();
#if defined(__i386__) || defined(__x86_64__)
  g_autoptr(GPtrArray) touch_i386 = g_ptr_array_new ();
#endif
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (ld_preload_tests); i++)
    {
      const PreloadTest *test = &ld_preload_tests[i];

      if (g_strcmp0 (test->touch, "=") == 0)
        g_ptr_array_add (touch, (char *) test->input);
      else if (test->touch != NULL)
        g_ptr_array_add (touch, (char *) test->touch);

#if defined(__i386__) || defined(__x86_64__)
      if (test->touch_i386 != NULL)
        g_ptr_array_add (touch_i386, (char *) test->touch_i386);
#endif
    }

  setup (f, context);
  fixture_populate_dir (f, f->mock_host->fd,
                        (const char * const *) touch->pdata, touch->len);

  if (!(config->preload_flags & PV_APPEND_PRELOAD_FLAGS_ONE_ARCHITECTURE))
    {
#if defined(__i386__) || defined(__x86_64__)
      fixture_populate_dir (f, f->mock_host->fd,
                            (const char * const *) touch_i386->pdata,
                            touch_i386->len);
#endif
    }
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;

  glnx_close_fd (&f->tmpdir_fd);
  glnx_close_fd (&f->mock_runtime_fd);
  glnx_close_fd (&f->var_fd);

  if (f->tmpdir != NULL)
    {
      glnx_shutil_rm_rf_at (-1, f->tmpdir, NULL, &local_error);
      g_assert_no_error (local_error);
    }

  g_clear_object (&f->context);
  g_clear_object (&f->mock_host);
  g_clear_pointer (&f->mock_runtime, g_free);
  g_clear_pointer (&f->home, g_free);
  g_clear_pointer (&f->tmpdir, g_free);
  g_clear_pointer (&f->var, g_free);
  g_clear_pointer (&f->bwrap, flatpak_bwrap_free);

  tests_check_fd_leaks_leave (f->old_fds);
}

static void
dump_bwrap (FlatpakBwrap *bwrap)
{
  guint i;

  g_test_message ("FlatpakBwrap object:");

  for (i = 0; i < bwrap->argv->len; i++)
    {
      const char *arg = g_ptr_array_index (bwrap->argv, i);

      g_test_message ("\t%s", arg);
    }
}

/* For simplicity we look for argument sequences of length exactly 3:
 * everything we're interested in for this test-case meets that description */
static void
assert_bwrap_contains (FlatpakBwrap *bwrap,
                       const char *one,
                       const char *two,
                       const char *three)
{
  guint i;

  g_assert_cmpuint (bwrap->argv->len, >=, 3);

  for (i = 0; i < bwrap->argv->len - 2; i++)
    {
      if (g_str_equal (g_ptr_array_index (bwrap->argv, i), one)
          && g_str_equal (g_ptr_array_index (bwrap->argv, i + 1), two)
          && g_str_equal (g_ptr_array_index (bwrap->argv, i + 2), three))
        return;
    }

  dump_bwrap (bwrap);
  g_error ("Expected to find: %s %s %s", one, two, three);
}

static void
assert_bwrap_does_not_contain (FlatpakBwrap *bwrap,
                               const char *path)
{
  guint i;

  for (i = 0; i < bwrap->argv->len; i++)
    {
      const char *arg = g_ptr_array_index (bwrap->argv, i);

      g_assert_cmpstr (arg, !=, NULL);
      g_assert_cmpstr (arg, !=, path);
    }
}

static void
test_bind_into_container (Fixture *f,
                          gconstpointer context)
{
  const Config *config = context;
  g_autoptr(GError) error = NULL;
  gboolean ok;

  fixture_create_runtime (f, config->runtime_flags);
  g_assert_nonnull (f->context->runtime);

  /* Successful cases */

  ok = pv_runtime_bind_into_container (f->context->runtime, f->bwrap,
                                       "/etc/machine-id", NULL, 0,
                                       "/etc/machine-id",
                                       PV_RUNTIME_EMULATION_ROOTS_BOTH,
                                       &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = pv_runtime_bind_into_container (f->context->runtime, f->bwrap,
                                       "/etc/arm-file", NULL, 0,
                                       "/etc/arm-file",
                                       PV_RUNTIME_EMULATION_ROOTS_REAL_ONLY,
                                       &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = pv_runtime_bind_into_container (f->context->runtime, f->bwrap,
                                       "/fex/etc/x86-file", NULL, 0,
                                       "/etc/x86-file",
                                       PV_RUNTIME_EMULATION_ROOTS_INTERPRETER_ONLY,
                                       &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  /* Error cases */

  ok = pv_runtime_bind_into_container (f->context->runtime, f->bwrap,
                                       "/nope", NULL, 0,
                                       "/nope",
                                       PV_RUNTIME_EMULATION_ROOTS_REAL_ONLY,
                                       &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
  g_assert_false (ok);
  g_test_message ("Editing /nope not allowed, as expected: %s", error->message);
  g_clear_error (&error);

  ok = pv_runtime_bind_into_container (f->context->runtime, f->bwrap,
                                       "/usr/foo", NULL, 0,
                                       "/usr/foo",
                                       PV_RUNTIME_EMULATION_ROOTS_BOTH,
                                       &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
  g_assert_false (ok);
  g_test_message ("Editing /usr/foo not allowed, as expected: %s", error->message);
  g_clear_error (&error);

  /* Check that the right things happened */

  dump_bwrap (f->bwrap);
  assert_bwrap_does_not_contain (f->bwrap, "/nope");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/foo");
  assert_bwrap_contains (f->bwrap,
                         "--ro-bind", "/etc/machine-id", "/etc/machine-id");
  assert_bwrap_contains (f->bwrap,
                         "--ro-bind", "/etc/arm-file", "/etc/arm-file");
  assert_bwrap_does_not_contain (f->bwrap,
                                 PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/arm-file");

  if (config->runtime_flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
    {
      assert_bwrap_contains (f->bwrap,
                             "--ro-bind", "/etc/machine-id",
                             PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/machine-id");
      assert_bwrap_contains (f->bwrap,
                             "--ro-bind", "/fex/etc/x86-file",
                             PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/x86-file");
      assert_bwrap_does_not_contain (f->bwrap, "/etc/x86-file");
    }
  else
    {
      assert_bwrap_contains (f->bwrap,
                             "--ro-bind", "/fex/etc/x86-file", "/etc/x86-file");
      assert_bwrap_does_not_contain (f->bwrap,
                                     PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/os-machine-id");
      assert_bwrap_does_not_contain (f->bwrap,
                                     PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/x86-file");
    }
}

static void
test_bind_merged_usr (Fixture *f,
                      gconstpointer context)
{
  static const char * const paths[] =
  {
    "bin>usr/bin",
    "home/",
    "lib>usr/lib",
    "lib32>usr/lib32",
    "lib64>usr/lib",
    "libexec>usr/libexec",
    "opt/",
    "sbin>usr/bin",
    "usr/",
  };
  g_autoptr(GError) local_error = NULL;
  gboolean ret;

  fixture_populate_dir (f, f->mock_host->fd, paths, G_N_ELEMENTS (paths));
  ret = pv_bwrap_bind_usr (f->bwrap, "/provider", f->mock_host->fd, "/run/gfx",
                           &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);
  dump_bwrap (f->bwrap);

  assert_bwrap_contains (f->bwrap, "--symlink", "usr/bin", "/run/gfx/bin");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib", "/run/gfx/lib");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib", "/run/gfx/lib64");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib32", "/run/gfx/lib32");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/bin", "/run/gfx/sbin");
  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider/usr", "/run/gfx/usr");
  assert_bwrap_does_not_contain (f->bwrap, "home");
  assert_bwrap_does_not_contain (f->bwrap, "/home");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/home");
  assert_bwrap_does_not_contain (f->bwrap, "libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "opt");
  assert_bwrap_does_not_contain (f->bwrap, "/opt");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/opt");
}

static void
test_bind_unmerged_usr (Fixture *f,
                        gconstpointer context)
{
  static const char * const paths[] =
  {
    "bin/",
    "home/",
    "lib/",
    "lib64/",
    "libexec/",
    "opt/",
    "sbin/",
    "usr/",
  };
  g_autoptr(GError) local_error = NULL;
  gboolean ret;

  fixture_populate_dir (f, f->mock_host->fd, paths, G_N_ELEMENTS (paths));
  ret = pv_bwrap_bind_usr (f->bwrap, "/provider", f->mock_host->fd, "/run/gfx",
                           &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);
  dump_bwrap (f->bwrap);

  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider/bin", "/run/gfx/bin");
  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider/lib", "/run/gfx/lib");
  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider/lib64", "/run/gfx/lib64");
  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider/sbin", "/run/gfx/sbin");
  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider/usr", "/run/gfx/usr");
  assert_bwrap_does_not_contain (f->bwrap, "home");
  assert_bwrap_does_not_contain (f->bwrap, "/home");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/home");
  assert_bwrap_does_not_contain (f->bwrap, "libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "opt");
  assert_bwrap_does_not_contain (f->bwrap, "/opt");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/opt");
}

static void
test_bind_usr (Fixture *f,
               gconstpointer context)
{
  static const char * const paths[] =
  {
    "bin/",
    "lib/",
    "lib64/",
    "libexec/",
    "local/",
    "share/",
  };
  g_autoptr(GError) local_error = NULL;
  gboolean ret;

  fixture_populate_dir (f, f->mock_host->fd, paths, G_N_ELEMENTS (paths));
  ret = pv_bwrap_bind_usr (f->bwrap, "/provider", f->mock_host->fd, "/run/gfx",
                           &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);
  dump_bwrap (f->bwrap);

  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider", "/run/gfx/usr");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/bin", "/run/gfx/bin");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib", "/run/gfx/lib");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib64", "/run/gfx/lib64");
  assert_bwrap_does_not_contain (f->bwrap, "local");
  assert_bwrap_does_not_contain (f->bwrap, "/local");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/local");
  assert_bwrap_does_not_contain (f->bwrap, "share");
  assert_bwrap_does_not_contain (f->bwrap, "/share");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/share");
}

/*
 * Test that pv_export_root_dirs_like_filesystem_host() behaves the same
 * as Flatpak --filesystem=host.
 */
static void
test_export_root_dirs (Fixture *f,
                       gconstpointer context)
{
  static const char * const paths[] =
  {
    "boot/",
    "bin>usr/bin",
    "dev/pts/",
    "etc/hosts",
    "games/SteamLibrary/",
    "home/user/.steam",
    "lib>usr/lib",
    "lib32>usr/lib32",
    "lib64>usr/lib",
    "libexec>usr/libexec",
    "opt/extras/kde/",
    "proc/1/fd/",
    "root/",
    "run/dbus/",
    "run/gfx/",
    "run/host/",
    "run/media/",
    "run/pressure-vessel/",
    "run/systemd/",
    "tmp/",
    "sbin>usr/bin",
    "sys/",
    "usr/local/",
    "var/tmp/",
  };
  g_autoptr(GError) local_error = NULL;
  gboolean ret;

  fixture_create_exports (f);
  g_assert_nonnull (f->context->exports);
  fixture_populate_dir (f, f->mock_host->fd, paths, G_N_ELEMENTS (paths));
  ret = pv_export_root_dirs_like_filesystem_host (f->mock_host->fd,
                                                  f->context->exports,
                                                  FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                                  _srt_dirent_strcmp,
                                                  &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);
  flatpak_exports_append_bwrap_args (f->context->exports, f->bwrap);

  dump_bwrap (f->bwrap);

  /* We don't export mutable OS state in this particular function,
   * for parity with Flatpak --filesystem=host (which does not imply
   * --filesystem=/tmp or --filesystem=/var) */
  assert_bwrap_does_not_contain (f->bwrap, "/etc");
  assert_bwrap_does_not_contain (f->bwrap, "/tmp");
  assert_bwrap_does_not_contain (f->bwrap, "/var");

  /* We do export miscellaneous top-level directories */
  assert_bwrap_contains (f->bwrap, "--bind", "/games", "/games");
  assert_bwrap_contains (f->bwrap, "--bind", "/home", "/home");
  assert_bwrap_contains (f->bwrap, "--bind", "/opt", "/opt");

  /* /run/media gets a special case here for parity with Flatpak's
   * --filesystem=host, even though it's not top-level */
  assert_bwrap_contains (f->bwrap, "--bind", "/run/media", "/run/media");

  /* We don't export /usr and friends in this particular function
   * (flatpak --filesystem=host would mount them in /run/host instead) */
  assert_bwrap_does_not_contain (f->bwrap, "/bin");
  assert_bwrap_does_not_contain (f->bwrap, "/lib");
  assert_bwrap_does_not_contain (f->bwrap, "/lib32");
  assert_bwrap_does_not_contain (f->bwrap, "/lib64");
  assert_bwrap_does_not_contain (f->bwrap, "/usr");
  assert_bwrap_does_not_contain (f->bwrap, "/sbin");

  /* We don't export these for various reasons */
  assert_bwrap_does_not_contain (f->bwrap, "/app");
  assert_bwrap_does_not_contain (f->bwrap, "/boot");
  assert_bwrap_does_not_contain (f->bwrap, "/dev");
  assert_bwrap_does_not_contain (f->bwrap, "/dev/pts");
  assert_bwrap_does_not_contain (f->bwrap, "/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/proc");
  assert_bwrap_does_not_contain (f->bwrap, "/root");
  assert_bwrap_does_not_contain (f->bwrap, "/run");
  assert_bwrap_does_not_contain (f->bwrap, "/run/dbus");
  assert_bwrap_does_not_contain (f->bwrap, "/run/gfx");
  assert_bwrap_does_not_contain (f->bwrap, "/run/host");
  assert_bwrap_does_not_contain (f->bwrap, "/run/pressure-vessel");
  assert_bwrap_does_not_contain (f->bwrap, "/run/systemd");
  assert_bwrap_does_not_contain (f->bwrap, "/sys");

  /* We would export these if they existed, but they don't */
  assert_bwrap_does_not_contain (f->bwrap, "/mnt");
  assert_bwrap_does_not_contain (f->bwrap, "/srv");
}

/*
 * Check that pv-wrap defaults are as expected
 */
static void
test_options_defaults (Fixture *f,
                       gconstpointer context)
{
  const PvWrapOptions *options = &f->context->options;
  size_t i;

  /*
   * First iteration: Check the defaults.
   * Second iteration: Check the defaults after parsing empty argv.
   */
  for (i = 0; i < 2; i++)
    {
      static const char * const original_argv[] =
      {
        "pressure-vessel-wrap-test",
        "--",
        "COMMAND",
        "ARGS",
        NULL
      };
      static const char * const expected_argv[] =
      {
        "pressure-vessel-wrap-test",
        "COMMAND",
        "ARGS",
        NULL
      };
      /* This slightly strange copying ensures that we can still free
       * strings that were removed from argv during parsing */
      g_auto(GStrv) argv_copy = _srt_strdupv (original_argv);
      g_autofree gchar **argv = g_memdup2 (argv_copy, sizeof (original_argv));
      g_autoptr(GError) local_error = NULL;
      int argc = G_N_ELEMENTS (original_argv) - 1;

      g_assert_null (options->env_if_host);
      g_assert_null (options->filesystems);
      g_assert_cmpstr (options->freedesktop_app_id, ==, NULL);
      g_assert_cmpstr (options->graphics_provider, ==, NULL);
      g_assert_cmpstr (options->home, ==, NULL);
      g_assert_cmpuint (options->pass_fds->len, ==, 0);
      g_assert_cmpuint (options->preload_modules->len, ==, 0);
      g_assert_cmpstr (options->runtime, ==, NULL);
      g_assert_cmpstr (options->runtime_base, ==, NULL);
      g_assert_cmpstr (options->steam_app_id, ==, NULL);
      g_assert_cmpstr (options->variable_dir, ==, NULL);
      g_assert_cmpstr (options->write_final_argv, ==, NULL);
      g_assert_cmpfloat (options->terminate_idle_timeout, <=, 0);
      g_assert_cmpfloat (options->terminate_idle_timeout, >=, 0);
      g_assert_cmpfloat (options->terminate_timeout, <, 0);
      g_assert_cmpint (options->shell, ==, PV_SHELL_NONE);
      g_assert_cmpint (options->terminal, ==, PV_TERMINAL_AUTO);
      g_assert_cmpint (options->share_home, ==, TRISTATE_MAYBE);
      g_assert_cmpint (options->batch, ==, FALSE);
      g_assert_cmpint (options->copy_runtime, ==, FALSE);
      g_assert_cmpint (options->deterministic, ==, FALSE);
      g_assert_cmpint (options->devel, ==, FALSE);
      g_assert_cmpint (options->gc_runtimes, ==, TRUE);
      g_assert_cmpint (options->generate_locales, ==, TRUE);
      g_assert_cmpint (options->import_vulkan_layers, ==, TRUE);
      g_assert_cmpint (options->launcher, ==, FALSE);
      g_assert_cmpint (options->only_prepare, ==, FALSE);
      g_assert_cmpint (options->remove_game_overlay, ==, FALSE);
      g_assert_cmpint (options->share_pid, ==, TRUE);
      g_assert_cmpint (options->single_thread, ==, FALSE);
      g_assert_cmpint (options->systemd_scope, ==, FALSE);
      g_assert_cmpint (options->test, ==, FALSE);
      g_assert_cmpint (options->verbose, ==, FALSE);
      g_assert_cmpint (options->version, ==, FALSE);
      g_assert_cmpint (options->version_only, ==, FALSE);

      pv_wrap_context_parse_argv (f->context, &argc, &argv, &local_error);
      g_assert_no_error (local_error);

      g_assert_cmpint (f->context->original_argc,
                       ==, G_N_ELEMENTS (original_argv) - 1);
      g_assert_cmpstrv (f->context->original_argv, (gchar **) original_argv);
      g_assert_cmpstrv (argv, (gchar **) expected_argv);
    }
}

/*
 * Check the effect of explicitly setting various CLI options to false
 * or empty.
 */
static void
test_options_false (Fixture *f,
                    gconstpointer context)
{
  static const char * const original_argv[] =
  {
    "pressure-vessel-wrap-test",
    "--graphics-provider=",
    "--no-copy-runtime",
    "--no-gc-runtimes",
    "--no-generate-locales",
    "--no-import-vulkan-layers",
    "--no-systemd-scope",
    "--runtime=",
    "--terminal=none",
    "--terminate-idle-timeout=0",
    "--terminate-timeout=0",
    "--unshare-home",
    "--unshare-pid",
    "--",
    "COMMAND",
    "ARGS",
    NULL
  };
  static const char * const expected_argv[] =
  {
    "pressure-vessel-wrap-test",
    "COMMAND",
    "ARGS",
    NULL
  };
  const PvWrapOptions *options = &f->context->options;
  /* This slightly strange copying ensures that we can still free
   * strings that were removed from argv during parsing */
  g_auto(GStrv) argv_copy = _srt_strdupv (original_argv);
  g_autofree gchar **argv = g_memdup2 (argv_copy, sizeof (original_argv));
  g_autoptr(GError) local_error = NULL;
  int argc = G_N_ELEMENTS (original_argv) - 1;

  pv_wrap_context_parse_argv (f->context, &argc, &argv, &local_error);
  g_assert_no_error (local_error);

  g_assert_cmpint (f->context->original_argc,
                   ==, G_N_ELEMENTS (original_argv) - 1);
  g_assert_cmpstrv (f->context->original_argv, (gchar **) original_argv);
  g_assert_cmpstrv (argv, (gchar **) expected_argv);

  g_assert_null (options->env_if_host);
  g_assert_null (options->filesystems);
  g_assert_cmpstr (options->freedesktop_app_id, ==, NULL);
  g_assert_cmpstr (options->graphics_provider, ==, "");
  g_assert_cmpstr (options->home, ==, NULL);
  g_assert_cmpuint (options->pass_fds->len, ==, 0);
  g_assert_cmpuint (options->preload_modules->len, ==, 0);
  g_assert_cmpstr (options->runtime, ==, "");
  g_assert_cmpstr (options->runtime_base, ==, NULL);
  g_assert_cmpstr (options->steam_app_id, ==, NULL);
  g_assert_cmpstr (options->variable_dir, ==, NULL);
  g_assert_cmpstr (options->write_final_argv, ==, NULL);
  g_assert_cmpfloat (options->terminate_idle_timeout, <=, 0);
  g_assert_cmpfloat (options->terminate_idle_timeout, >=, 0);
  g_assert_cmpfloat (options->terminate_timeout, >=, 0);
  g_assert_cmpfloat (options->terminate_timeout, <=, 0);
  g_assert_cmpint (options->shell, ==, PV_SHELL_NONE);
  g_assert_cmpint (options->terminal, ==, PV_TERMINAL_NONE);
  g_assert_cmpint (options->share_home, ==, TRISTATE_NO);
  g_assert_cmpint (options->batch, ==, FALSE);
  g_assert_cmpint (options->copy_runtime, ==, FALSE);
  g_assert_cmpint (options->deterministic, ==, FALSE);
  g_assert_cmpint (options->devel, ==, FALSE);
  g_assert_cmpint (options->gc_runtimes, ==, FALSE);
  g_assert_cmpint (options->generate_locales, ==, FALSE);
  g_assert_cmpint (options->import_vulkan_layers, ==, FALSE);
  g_assert_cmpint (options->launcher, ==, FALSE);
  g_assert_cmpint (options->only_prepare, ==, FALSE);
  g_assert_cmpint (options->remove_game_overlay, ==, FALSE);
  g_assert_cmpint (options->share_pid, ==, FALSE);
  g_assert_cmpint (options->single_thread, ==, FALSE);
  g_assert_cmpint (options->systemd_scope, ==, FALSE);
  g_assert_cmpint (options->test, ==, FALSE);
  g_assert_cmpint (options->verbose, ==, FALSE);
  g_assert_cmpint (options->version, ==, FALSE);
  g_assert_cmpint (options->version_only, ==, FALSE);
}

/*
 * Check the effect of explicitly setting various CLI options to true
 * or non-empty.
 */
static void
test_options_true (Fixture *f,
                   gconstpointer context)
{
  static const char * const original_argv[] =
  {
    "pressure-vessel-wrap-test",
    "--batch",
    "--copy-runtime",
    "--deterministic",
    "--devel",
    "--env-if-host=ONE=1",
    "--env-if-host=TWO=two",
    "--filesystem=/foo",
    "--filesystem=/bar",
    "--freedesktop-app-id=com.example.Foo",
    "--gc-runtimes",
    "--generate-locales",
    "--graphics-provider=/gfx",
    "--home=/home/steam",
    "--import-vulkan-layers",
    "--launcher",
    "--ld-audit=libaudit.so",
    "--ld-audits=libaudit1.so:libaudit2.so",
    "--ld-preload=libpreload.so",
    "--ld-preloads=libpreload1.so libpreload2.so:libpreload3.so",
    "--only-prepare",
    "--pass-fd=2",
    "--remove-game-overlay",
    "--runtime=sniper",
    "--runtime-base=/runtimes",
    "--share-home",
    "--share-pid",
    "--shell=instead",
    "--single-thread",
    "--steam-app-id=12345",
    "--systemd-scope",
    "--terminal=xterm",
    "--terminate-idle-timeout=10",
    "--terminate-timeout=5",
    "--test",
    "--variable-dir=/runtimes/var",
    "--verbose",
    "--version",
    "--version-only",
    "--write-final-argv=/dev/null",
    NULL
  };
  static const char * const expected_argv[] =
  {
    "pressure-vessel-wrap-test",
    NULL
  };
  static const char * const expected_env_if_host[] =
  {
    "ONE=1",
    "TWO=two",
    NULL
  };
  static const char * const expected_filesystems[] =
  {
    "/foo",
    "/bar",
    NULL
  };
  const PvWrapOptions *options = &f->context->options;
  /* This slightly strange copying ensures that we can still free
   * strings that were removed from argv during parsing */
  g_auto(GStrv) argv_copy = _srt_strdupv (original_argv);
  g_autofree gchar **argv = g_memdup2 (argv_copy, sizeof (original_argv));
  g_autoptr(GError) local_error = NULL;
  int argc = G_N_ELEMENTS (original_argv) - 1;
  const WrapPreloadModule *module;
  gsize i = 0;

  pv_wrap_context_parse_argv (f->context, &argc, &argv, &local_error);
  g_assert_no_error (local_error);

  g_assert_cmpint (f->context->original_argc,
                   ==, G_N_ELEMENTS (original_argv) - 1);
  g_assert_cmpstrv (f->context->original_argv, (gchar **) original_argv);
  g_assert_cmpstrv (argv, (gchar **) expected_argv);

  g_assert_cmpstrv (options->env_if_host, (gchar **) expected_env_if_host);
  g_assert_cmpstrv (options->filesystems, (gchar **) expected_filesystems);
  g_assert_cmpstr (options->freedesktop_app_id, ==, "com.example.Foo");
  g_assert_cmpstr (options->graphics_provider, ==, "/gfx");
  g_assert_cmpstr (options->home, ==, "/home/steam");
  g_assert_cmpuint (options->pass_fds->len, ==, 1);
  g_assert_cmpint (g_array_index (options->pass_fds, int, 0), ==, 2);
  g_assert_cmpstr (options->runtime, ==, "sniper");
  g_assert_cmpstr (options->runtime_base, ==, "/runtimes");
  g_assert_cmpstr (options->steam_app_id, ==, "12345");
  g_assert_cmpstr (options->variable_dir, ==, "/runtimes/var");
  g_assert_cmpstr (options->write_final_argv, ==, "/dev/null");
  g_assert_cmpfloat (options->terminate_idle_timeout, <=, 10);
  g_assert_cmpfloat (options->terminate_idle_timeout, >=, 10);
  g_assert_cmpfloat (options->terminate_timeout, >=, 5);
  g_assert_cmpfloat (options->terminate_timeout, <=, 5);
  g_assert_cmpint (options->shell, ==, PV_SHELL_INSTEAD);
  g_assert_cmpint (options->terminal, ==, PV_TERMINAL_XTERM);
  g_assert_cmpint (options->share_home, ==, TRISTATE_YES);
  g_assert_cmpint (options->batch, ==, TRUE);
  g_assert_cmpint (options->copy_runtime, ==, TRUE);
  g_assert_cmpint (options->deterministic, ==, TRUE);
  g_assert_cmpint (options->devel, ==, TRUE);
  g_assert_cmpint (options->gc_runtimes, ==, TRUE);
  g_assert_cmpint (options->generate_locales, ==, TRUE);
  g_assert_cmpint (options->import_vulkan_layers, ==, TRUE);
  g_assert_cmpint (options->launcher, ==, TRUE);
  g_assert_cmpint (options->only_prepare, ==, TRUE);
  g_assert_cmpint (options->remove_game_overlay, ==, TRUE);
  g_assert_cmpint (options->share_pid, ==, TRUE);
  g_assert_cmpint (options->single_thread, ==, TRUE);
  g_assert_cmpint (options->systemd_scope, ==, TRUE);
  g_assert_cmpint (options->test, ==, TRUE);
  g_assert_cmpint (options->verbose, ==, TRUE);
  g_assert_cmpint (options->version, ==, TRUE);
  g_assert_cmpint (options->version_only, ==, TRUE);

  i = 0;

  g_assert_cmpuint (options->preload_modules->len, >, i);
  module = &g_array_index (options->preload_modules, WrapPreloadModule, i++);
  g_assert_cmpint (module->which, ==, PV_PRELOAD_VARIABLE_INDEX_LD_AUDIT);
  g_assert_cmpstr (module->preload, ==, "libaudit.so");

  g_assert_cmpuint (options->preload_modules->len, >, i);
  module = &g_array_index (options->preload_modules, WrapPreloadModule, i++);
  g_assert_cmpint (module->which, ==, PV_PRELOAD_VARIABLE_INDEX_LD_AUDIT);
  g_assert_cmpstr (module->preload, ==, "libaudit1.so");

  g_assert_cmpuint (options->preload_modules->len, >, i);
  module = &g_array_index (options->preload_modules, WrapPreloadModule, i++);
  g_assert_cmpint (module->which, ==, PV_PRELOAD_VARIABLE_INDEX_LD_AUDIT);
  g_assert_cmpstr (module->preload, ==, "libaudit2.so");

  g_assert_cmpuint (options->preload_modules->len, >, i);
  module = &g_array_index (options->preload_modules, WrapPreloadModule, i++);
  g_assert_cmpint (module->which, ==, PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD);
  g_assert_cmpstr (module->preload, ==, "libpreload.so");

  g_assert_cmpuint (options->preload_modules->len, >, i);
  module = &g_array_index (options->preload_modules, WrapPreloadModule, i++);
  g_assert_cmpint (module->which, ==, PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD);
  g_assert_cmpstr (module->preload, ==, "libpreload1.so");

  g_assert_cmpuint (options->preload_modules->len, >, i);
  module = &g_array_index (options->preload_modules, WrapPreloadModule, i++);
  g_assert_cmpint (module->which, ==, PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD);
  g_assert_cmpstr (module->preload, ==, "libpreload2.so");

  g_assert_cmpuint (options->preload_modules->len, >, i);
  module = &g_array_index (options->preload_modules, WrapPreloadModule, i++);
  g_assert_cmpint (module->which, ==, PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD);
  g_assert_cmpstr (module->preload, ==, "libpreload3.so");

  g_assert_cmpuint (i, ==, options->preload_modules->len);
}

static void
test_make_symlink_in_container (Fixture *f,
                                gconstpointer context)
{
  const Config *config = context;
  g_autoptr(GError) error = NULL;
  gboolean ok;
  SrtSysroot *mutable_sysroot;

  fixture_create_runtime (f, config->runtime_flags);
  g_assert_nonnull (f->context->runtime);
  mutable_sysroot = pv_runtime_get_mutable_sysroot (f->context->runtime);

  if (config->runtime_flags & PV_RUNTIME_FLAGS_COPY_RUNTIME)
    g_assert_nonnull (mutable_sysroot);
  else
    g_assert_null (mutable_sysroot);

  /* Successful cases */

  ok = pv_runtime_make_symlink_in_container (f->context->runtime, f->bwrap,
                                             "../usr/lib/os-release",
                                             "/etc/os-release",
                                             PV_RUNTIME_EMULATION_ROOTS_BOTH,
                                             &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = pv_runtime_make_symlink_in_container (f->context->runtime, f->bwrap,
                                             "/run/host/foo",
                                             "/var/foo",
                                             PV_RUNTIME_EMULATION_ROOTS_REAL_ONLY,
                                             &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = pv_runtime_make_symlink_in_container (f->context->runtime, f->bwrap,
                                             "/run/x86/bar",
                                             "/var/bar",
                                             PV_RUNTIME_EMULATION_ROOTS_INTERPRETER_ONLY,
                                             &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  /* Conditionally OK, if there is an on-disk directory we can edit */

  ok = pv_runtime_make_symlink_in_container (f->context->runtime, f->bwrap,
                                             "/run/host/foo",
                                             "/usr/foo",
                                             PV_RUNTIME_EMULATION_ROOTS_REAL_ONLY,
                                             &error);

  if (mutable_sysroot == NULL)
    {
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
      g_assert_false (ok);
      g_test_message ("Editing /usr not allowed, as expected: %s",
                      error->message);
      g_clear_error (&error);
    }
  else if (config->runtime_flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
    {
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
      g_assert_false (ok);
      g_test_message ("Editing real /usr not allowed, as expected: %s",
                      error->message);
      g_clear_error (&error);
    }
  else
    {
      g_assert_no_error (error);
      g_assert_true (ok);

    }

  ok = pv_runtime_make_symlink_in_container (f->context->runtime, f->bwrap,
                                             "/run/x86/bar",
                                             "/usr/bar",
                                             PV_RUNTIME_EMULATION_ROOTS_INTERPRETER_ONLY,
                                             &error);

  if (mutable_sysroot == NULL)
    {
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
      g_assert_false (ok);
      g_test_message ("Editing /usr not allowed, as expected: %s",
                      error->message);
      g_clear_error (&error);
    }
  else
    {
      g_assert_no_error (error);
      g_assert_true (ok);
    }

  ok = pv_runtime_make_symlink_in_container (f->context->runtime, f->bwrap,
                                             "/run/baz",
                                             "/usr/baz",
                                             PV_RUNTIME_EMULATION_ROOTS_BOTH,
                                             &error);

  if (mutable_sysroot == NULL)
    {
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
      g_assert_false (ok);
      g_test_message ("Editing /usr not allowed, as expected: %s",
                      error->message);
      g_clear_error (&error);
    }
  else if (config->runtime_flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
    {
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
      g_assert_false (ok);
      g_test_message ("Editing real /usr not allowed, as expected: %s",
                      error->message);
      g_clear_error (&error);
    }
  else
    {
      g_assert_no_error (error);
      g_assert_true (ok);
    }

  /* Error cases */

  ok = pv_runtime_make_symlink_in_container (f->context->runtime, f->bwrap,
                                             "/nope",
                                             "/nope",
                                             PV_RUNTIME_EMULATION_ROOTS_REAL_ONLY,
                                             &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
  g_assert_false (ok);
  g_test_message ("Editing /nope not allowed, as expected: %s", error->message);
  g_clear_error (&error);

  /* Check that the right things happened */

  dump_bwrap (f->bwrap);
  assert_bwrap_does_not_contain (f->bwrap, "/nope");
  /* /etc/os-release is in the real root (and, if used, the interpreter
   * root, but that's checked later) */
  assert_bwrap_contains (f->bwrap,
                         "--symlink", "../usr/lib/os-release", "/etc/os-release");
  /* /var/foo is in the real root only */
  assert_bwrap_contains (f->bwrap,
                         "--symlink", "/run/host/foo", "/var/foo");
  assert_bwrap_does_not_contain (f->bwrap,
                                 PV_RUNTIME_PATH_INTERPRETER_ROOT "/var/foo");

  if (config->runtime_flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
    {
      /* /etc/os-release is in the interpreter root (and the real root) */
      assert_bwrap_contains (f->bwrap,
                             "--symlink", "../usr/lib/os-release",
                             PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/os-release");
      /* /var/bar is in the interpreter root only */
      assert_bwrap_contains (f->bwrap,
                             "--symlink", "/run/x86/bar",
                             PV_RUNTIME_PATH_INTERPRETER_ROOT "/var/bar");
    }
  else
    {
      /* We're not using an interpreter root */
      assert_bwrap_does_not_contain (f->bwrap,
                                     PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/os-release");
      assert_bwrap_does_not_contain (f->bwrap,
                                     PV_RUNTIME_PATH_INTERPRETER_ROOT "/var/bar");

      /* /var/bar would have been in the interpreter root only, but because
       * we don't have an interpreter root, it ends up in the real root */
      assert_bwrap_contains (f->bwrap, "--symlink", "/run/x86/bar", "/var/bar");
    }

  /* We must not try to edit /usr with --symlink: that can't work,
   * because /usr is read-only */
  assert_bwrap_does_not_contain (f->bwrap, "/usr/foo");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/bar");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/baz");
  assert_bwrap_does_not_contain (f->bwrap,
                                 PV_RUNTIME_PATH_INTERPRETER_ROOT "/usr/foo");
  assert_bwrap_does_not_contain (f->bwrap,
                                 PV_RUNTIME_PATH_INTERPRETER_ROOT "/usr/bar");
  assert_bwrap_does_not_contain (f->bwrap,
                                 PV_RUNTIME_PATH_INTERPRETER_ROOT "/usr/baz");

  if (mutable_sysroot != NULL)
    {
      g_autofree gchar *target = NULL;
      struct stat stat_buf;

      /* /usr/foo is only created if the mutable sysroot is the real root */
      target = glnx_readlinkat_malloc (mutable_sysroot->fd,
                                       "usr/foo", NULL, NULL);

      if (config->runtime_flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
        g_assert_cmpstr (target, ==, NULL);
      else
        g_assert_cmpstr (target, ==, "/run/host/foo");

      g_clear_pointer (&target, g_free);

      /* /usr/bar is created if the mutable sysroot is the interpreter root,
       * or if we are not using a separate interpreter root */
      target = glnx_readlinkat_malloc (mutable_sysroot->fd,
                                       "usr/bar", NULL, NULL);
      g_assert_cmpstr (target, ==, "/run/x86/bar");
      g_clear_pointer (&target, g_free);

      /* /usr/baz was only created if we are not using a separate
       * interpreter root, because if we were, we would have been unable
       * to create it in both roots */
      target = glnx_readlinkat_malloc (mutable_sysroot->fd,
                                       "usr/baz", NULL, NULL);

      if (config->runtime_flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
        g_assert_cmpstr (target, ==, NULL);
      else
        g_assert_cmpstr (target, ==, "/run/baz");

      g_clear_pointer (&target, g_free);

      /* We never create/edit the interpreter root as a subdir of the
       * mutable sysroot */
      g_assert_cmpint (fstatat (mutable_sysroot->fd,
                                "run/pressure-vessel/interpreter-root",
                                &stat_buf, 0) == 0 ? 0 : errno,
                       ==, ENOENT);
    }
}

static void
test_passwd (Fixture *f,
             gconstpointer context)
{
  /* A realistic passwd(5) entry for root */
#define MOCK_PASSWD_ROOT "root:x:0:0:System administrator:/root:/bin/sh\n"
  /* A realistic passwd(5) entry for our mock user */
#define MOCK_PASSWD_GFREEMAN "gfreeman:!:1998:1119:Dr Gordon Freeman,,,:/home/gfreeman:/bin/csh\n"
  /* This exercises handling of lines without the usual structure */
#define MOCK_PASSWD_COMMENT "#?\n"
  /* A realistic passwd(5) entry for 'nobody', intentionally with no
   * trailing newline */
#define MOCK_PASSWD_NOBODY_NOEOL "nobody:x:65534:65534:&:/nonexistent:/bin/false"
  static const char mock_passwd_text[] =
    MOCK_PASSWD_ROOT
    MOCK_PASSWD_GFREEMAN
    MOCK_PASSWD_COMMENT
    MOCK_PASSWD_NOBODY_NOEOL;
  static const char strange_passwd_text[] =
    MOCK_PASSWD_ROOT
    "\n"
    "\n"
    MOCK_PASSWD_NOBODY_NOEOL "\n";
  /* A realistic group(5) entry for 'nogroup' */
#define MOCK_GROUP_NOGROUP "nogroup:x:65534:\n"
  static const char mock_group_text[] =
    MOCK_GROUP_NOGROUP;
  static const char strange_group_text[] = "\n\n\n";
  /* A realistic mock user, which does not fully match the one we place
   * in /etc/passwd */
  static const struct passwd mock_user =
    {
      .pw_name = (char *) "gfreeman",
      .pw_passwd = (char *) "!",
      .pw_uid = 1998,
      .pw_gid = 1119,
      .pw_gecos = (char *) "Gordon Freeman",
      .pw_dir = (char *) "/blackmesa/gfreeman",
      .pw_shell = (char *) "/bin/zsh",
    };
  /* A realistic mock group (the Anomalous Materials Laboratory) */
  static const char * const members[] = { "evance", "gfreeman", "ikleiner", NULL };
  static const struct group mock_group =
    {
      .gr_name = (char *) "materials",
      .gr_passwd = (char *) "*",
      .gr_gid = 1119,
      .gr_mem = (char **) members,
    };
  /* A user with some non-representable fields */
  static const struct passwd strange_user =
    {
      .pw_name = (char *) "g:man",
      .pw_passwd = (char *) "!",
      .pw_uid = 2004,
      .pw_gid = 1116,
      .pw_gecos = (char *) "\n",
      .pw_dir = (char *) "/xen",
      .pw_shell = (char *) "/bin/zsh",
    };
  /* A group with some non-representable fields */
  static const struct group strange_group =
    {
      .gr_name = (char *) "not\nrepresentable",
      .gr_passwd = (char *) "*",
      .gr_gid = 1116,
      .gr_mem = NULL,
    };
  PvMockPasswdLookup mock_lookup_successfully =
    {
      .uid = getuid (),
      .gid = getgid (),
      .pwd = &mock_user,
      .grp = &mock_group,
      .lookup_errno = 0,
    };
  PvMockPasswdLookup mock_lookup_strange =
    {
      .uid = getuid (),
      .gid = getgid (),
      .pwd = &strange_user,
      .grp = &strange_group,
      .lookup_errno = 0,
    };
  g_auto(GLnxTmpDir) temp = { FALSE };
  g_autoptr(GError) local_error = NULL;
  g_autoptr(SrtSysroot) sysroot = NULL;
  g_autoptr(SrtSysroot) direct = NULL;
  const char *gecos;
  const char *home;
  const char *username;

  glnx_mkdtemp ("pv-test.XXXXXX", 0755, &temp, &local_error);
  g_assert_no_error (local_error);

  sysroot = _srt_sysroot_new (temp.path, &local_error);
  g_assert_no_error (local_error);

  direct = _srt_sysroot_new_direct (&local_error);
  g_assert_no_error (local_error);

  /* First test with an empty sysroot: we will be unable to open /etc/passwd
   * or /etc/group */
    {
      g_autofree gchar *pw = NULL;
      g_autofree gchar *gr = NULL;

      g_test_message ("Sub-test: lookup successful, files inaccessible");

      g_test_message ("/etc/passwd for container:\n%s\n.", pw);
      pw = pv_generate_etc_passwd (sysroot, &mock_lookup_successfully);
      /* Note that this ends with /bin/bash, not /bin/zsh: we override
       * the shell because non-bash shells will generally not exist in
       * the container. */
      g_assert_cmpstr (pw, ==,
                       "gfreeman:x:1998:1119:Gordon Freeman:/blackmesa/gfreeman:/bin/bash\n");
      gr = pv_generate_etc_group (sysroot, &mock_lookup_successfully);
      g_test_message ("/etc/group for container:\n%s\n.", gr);
      g_assert_cmpstr (gr, ==, "materials:x:1119:\n");
    }

  /* Mock up an /etc/passwd and /etc/group in the sysroot */
  glnx_ensure_dir (temp.fd, "etc", 0755, &local_error);
  g_assert_no_error (local_error);
  glnx_file_replace_contents_at (temp.fd, "etc/passwd",
                                 (const guint8 *) mock_passwd_text,
                                 strlen (mock_passwd_text),
                                 GLNX_FILE_REPLACE_NODATASYNC,
                                 NULL, &local_error);
  g_assert_no_error (local_error);
  glnx_file_replace_contents_at (temp.fd, "etc/group",
                                 (const guint8 *) mock_group_text,
                                 strlen (mock_group_text),
                                 GLNX_FILE_REPLACE_NODATASYNC,
                                 NULL, &local_error);
  g_assert_no_error (local_error);

  /* Test again now that we can open /etc/passwd and /etc/group */
    {
      g_autofree gchar *pw = NULL;
      g_autofree gchar *gr = NULL;

      g_test_message ("Sub-test: lookup successful, files merged");

      /* This exercises the case where the first line that we synthesize
       * matches a line taken from the file, which we exclude.
       * For the fields that are different (name, home, shell),
       * we use the ones from the mock getpwuid(), not the ones from the
       * mock /etc/passwd.
       *
       * This emulates a situation where a module like libnss_systemd
       * (or LDAP or something) can provide better information than
       * /etc/passwd.
       *
       * It also exercises the case where /etc/passwd (or /etc/group) does
       * not end with a newline: we normalize by adding one. */
      pw = pv_generate_etc_passwd (sysroot, &mock_lookup_successfully);
      g_test_message ("/etc/passwd for container:\n%s\n.", pw);
      g_assert_cmpstr (pw, ==,
                       "gfreeman:x:1998:1119:Gordon Freeman:/blackmesa/gfreeman:/bin/bash\n"
                       MOCK_PASSWD_ROOT
                       MOCK_PASSWD_COMMENT
                       MOCK_PASSWD_NOBODY_NOEOL "\n");

      /* This exercises the case where the first line that we synthesize
       * does not match any line from the file. */
      gr = pv_generate_etc_group (sysroot, &mock_lookup_successfully);
      g_test_message ("/etc/group for container:\n%s\n.", gr);
      g_assert_cmpstr (gr, ==,
                       "materials:x:1119:\n"
                       MOCK_GROUP_NOGROUP);
    }

  username = g_get_user_name ();

  if (username == NULL)
    username = "user";

  gecos = g_get_real_name ();

  if (gecos == NULL)
    gecos = username;

  home = g_get_home_dir ();

  /* Exercise the fallback that occurs if getpwuid(), getgrgid() fail */
    {
      g_autofree gchar *expected_pw = NULL;
      g_autofree gchar *pw = NULL;
      g_autofree gchar *gr = NULL;
      PvMockPasswdLookup mock_lookup_not_found =
        {
          .uid = getuid (),
          .gid = getgid (),
          .pwd = NULL,
          .grp = NULL,
          .lookup_errno = 0,
        };
      PvMockPasswdLookup mock_lookup_error =
        {
          .uid = getuid (),
          .gid = getgid (),
          .pwd = NULL,
          .grp = NULL,
          .lookup_errno = ENOSYS,
        };
      const char *maybe_root = MOCK_PASSWD_ROOT;
      const char *maybe_gfreeman = MOCK_PASSWD_GFREEMAN;
      const char *maybe_nobody = MOCK_PASSWD_NOBODY_NOEOL "\n";

      g_test_message ("Sub-test: lookup fails, we fall back");

      g_assert_nonnull (username);
      g_assert_nonnull (gecos);
      g_assert_nonnull (home);

      /* If we happen to be running as one of the users mentioned in the
       * mock /etc/passwd, then we'll drop the corresponding line from
       * the output. */
      if (g_str_equal (username, "root"))
        maybe_root = "";
      else if (g_str_equal (username, "gfreeman"))
        maybe_gfreeman = "";
      else if (g_str_equal (username, "nobody"))
        maybe_nobody = "";

      expected_pw = g_strdup_printf ("%s:x:%d:%d:%s:%s:/bin/bash\n%s%s%s%s",
                                     username, getuid (), getgid (), gecos, home,
                                     maybe_root,
                                     maybe_gfreeman,
                                     MOCK_PASSWD_COMMENT,
                                     maybe_nobody);
      pw = pv_generate_etc_passwd (sysroot, &mock_lookup_error);
      g_test_message ("/etc/passwd for container:\n%s\n.", pw);
      g_assert_cmpstr (pw, ==, expected_pw);

      /* If we can't look up our own group, we use /etc/group as-is. */
      gr = pv_generate_etc_group (sysroot, &mock_lookup_error);
      g_test_message ("/etc/group for container:\n%s\n.", gr);
      g_assert_cmpstr (gr, ==, MOCK_GROUP_NOGROUP);

      g_clear_pointer (&pw, g_free);
      g_clear_pointer (&gr, g_free);

      /* getpwuid(), getgrgid() can also return null without setting errno */
      pw = pv_generate_etc_passwd (sysroot, &mock_lookup_not_found);
      g_test_message ("/etc/passwd for container:\n%s\n.", pw);
      g_assert_cmpstr (pw, ==, expected_pw);

      gr = pv_generate_etc_group (sysroot, &mock_lookup_not_found);
      g_test_message ("/etc/group for container:\n%s\n.", gr);
      g_assert_cmpstr (gr, ==, MOCK_GROUP_NOGROUP);
    }

  /* Re-test with fields that cannot be represented losslessly, which
   * could theoretically be produced by nsswitch plugins */

  glnx_file_replace_contents_at (temp.fd, "etc/passwd",
                                 (const guint8 *) strange_passwd_text,
                                 strlen (strange_passwd_text),
                                 GLNX_FILE_REPLACE_NODATASYNC,
                                 NULL, &local_error);
  g_assert_no_error (local_error);
  glnx_file_replace_contents_at (temp.fd, "etc/group",
                                 (const guint8 *) strange_group_text,
                                 strlen (strange_group_text),
                                 GLNX_FILE_REPLACE_NODATASYNC,
                                 NULL, &local_error);
  g_assert_no_error (local_error);

    {
      g_autofree gchar *pw = NULL;
      g_autofree gchar *gr = NULL;

      g_test_message ("Sub-test: files merged, invalid fields exist");

      pw = pv_generate_etc_passwd (sysroot, &mock_lookup_strange);
      g_test_message ("/etc/passwd for container:\n%s\n.", pw);
      g_assert_cmpstr (pw, ==,
                       "g_man:x:2004:1116:_:/xen:/bin/bash\n"
                       MOCK_PASSWD_ROOT
                       /* We skip completely blank lines */
                       MOCK_PASSWD_NOBODY_NOEOL "\n");

      gr = pv_generate_etc_group (sysroot, &mock_lookup_strange);
      g_test_message ("/etc/group for container:\n%s\n.", gr);
      g_assert_cmpstr (gr, ==,
                       "not_representable:x:1116:\n");
    }

  /* A smoke-test of the real situation: we can't usefully make any
   * particular assertions about this, but we can at least confirm it
   * doesn't crash, and output the text of the files for manual checking */
    {
      g_autofree gchar *pw = NULL;
      g_autofree gchar *gr = NULL;

      g_test_message ("Sub-test: real data");

      pw = pv_generate_etc_passwd (direct, NULL);
      g_test_message ("/etc/passwd for container:\n%s\n.", pw);
      gr = pv_generate_etc_group (direct, NULL);
      g_test_message ("/etc/group for container:\n%s\n.", gr);
    }
}

static void
populate_ld_preload (Fixture *f,
                     GPtrArray *argv,
                     PvAppendPreloadFlags flags)
{
  gsize i;

  if (flags & PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX)
    g_assert_null (f->context->exports);
  else
    g_assert_nonnull (f->context->exports);

  for (i = 0; i < G_N_ELEMENTS (ld_preload_tests); i++)
    {
      const PreloadTest *test = &ld_preload_tests[i];
      GLogLevelFlags old_fatal_mask = G_LOG_FATAL_MASK;

      /* We expect a warning for libgtk3-nocsd.so.0, but the test framework
       * makes warnings and critical warnings fatal, in addition to the
       * usual fatal errors. Temporarily relax that to just critical
       * warnings and fatal errors. */
      if (test->warning != NULL)
        {
          old_fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
          /* Note that this assumes pressure-vessel doesn't define
           * G_LOG_USE_STRUCTURED, because g_test_expect_message() only
           * works for the old unstructured logging API.
           * If we want to start using G_LOG_USE_STRUCTURED, then we'll
           * have to introduce some design-for-test to allow this warning
           * to be trapped. */
          g_test_expect_message ("pressure-vessel",
                                 G_LOG_LEVEL_WARNING,
                                 test->warning);
        }

      pv_wrap_append_preload (f->context,
                              argv,
                              PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
                              test->input,
                              flags | PV_APPEND_PRELOAD_FLAGS_IN_UNIT_TESTS);

      /* If we modified the fatal mask, put back the old value. */
      if (test->warning != NULL)
        {
          g_test_assert_expected_messages ();
          g_log_set_always_fatal (old_fatal_mask);
        }
    }

  for (i = 0; i < argv->len; i++)
    g_test_message ("argv[%" G_GSIZE_FORMAT "]: %s",
                    i, (const char *) g_ptr_array_index (argv, i));

  g_test_message ("argv->len: %" G_GSIZE_FORMAT, i);
}

static GPtrArray *
filter_expected_paths (const Config *config)
{
  g_autoptr(GPtrArray) filtered = g_ptr_array_new_with_free_func (NULL);
  gsize i, j;

  /* Some of the expected paths are only expected to appear on i386.
   * Filter the list accordingly. */
  for (i = 0; i < G_N_ELEMENTS (ld_preload_tests); i++)
    {
      for (j = 0; j < G_N_ELEMENTS (ld_preload_tests[i].expected); j++)
        {
          const char *path = ld_preload_tests[i].expected[j];

          if (path == NULL)
            continue;

          if (g_str_equal (path, "="))
            path = ld_preload_tests[i].input;

          if (g_str_has_prefix (path, "i386:"))
            {
#if defined(__i386__) || defined(__x86_64__)
              if (!(config->preload_flags & PV_APPEND_PRELOAD_FLAGS_ONE_ARCHITECTURE))
                g_ptr_array_add (filtered, (char *) (path + strlen ("i386:")));
#endif
            }
          else
            {
              g_ptr_array_add (filtered, (char *) path);
            }
        }
    }

  return g_steal_pointer (&filtered);
}

static void
test_remap_ld_preload (Fixture *f,
                       gconstpointer context)
{
  const Config *config = context;
  FlatpakExports *exports;
  g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GPtrArray) filtered = filter_expected_paths (config);
  gsize i;
  gboolean expect_i386 = FALSE;

#if defined(__i386__) || defined(__x86_64__)
  if (!(config->preload_flags & PV_APPEND_PRELOAD_FLAGS_ONE_ARCHITECTURE))
    expect_i386 = TRUE;
#endif

  fixture_create_exports (f);
  exports = f->context->exports;
  g_assert_nonnull (exports);

  fixture_create_runtime (f, PV_RUNTIME_FLAGS_NONE);
  g_assert_nonnull (f->context->runtime);
  populate_ld_preload (f, argv, config->preload_flags);

  g_assert_cmpuint (argv->len, ==, filtered->len);

  for (i = 0; i < argv->len; i++)
    {
      char *expected = g_ptr_array_index (filtered, i);
      char *argument = g_ptr_array_index (argv, i);

      g_assert_true (g_str_has_prefix (argument, "--ld-preload="));
      argument += strlen("--ld-preload=");

      if (g_str_has_prefix (expected, "/lib/")
          || g_str_has_prefix (expected, "/usr/lib/"))
        {
          g_assert_true (g_str_has_prefix (argument, "/run/host/"));
          argument += strlen("/run/host");
        }

      g_assert_cmpstr (argument, ==, expected);
    }

  /* FlatpakExports never exports /app */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app/lib/libpreloadA.so"));

  /* We don't always export /home etc. so we have to explicitly export
   * this one */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/home"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/home/me"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/home/me/libpreloadH.so"));

  /* We don't always export /opt and /platform, so we have to explicitly export
   * these. */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/opt"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/opt/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/platform"));

  g_assert_true (flatpak_exports_path_is_visible (exports, "/opt/" PRIMARY_LIB "/libpreloadL.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/platform/plat-" PRIMARY_PLATFORM "/libpreloadP.so"));

  g_assert_cmpint (flatpak_exports_path_is_visible (exports, "/opt/" MOCK_LIB_32 "/libpreloadL.so"), ==, expect_i386);
  g_assert_cmpint (flatpak_exports_path_is_visible (exports, "/platform/plat-" MOCK_PLATFORM_32 "/libpreloadP.so"), ==, expect_i386);

  /* FlatpakExports never exports /lib as /lib */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/lib/libpreload-rootfs.so"));

  /* FlatpakExports never exports /usr as /usr */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr/lib/libpreloadU.so"));

  /* We assume STEAM_COMPAT_CLIENT_INSTALL_PATH is dealt with separately */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/steam"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/steam/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/steam/lib/gameoverlayrenderer.so"));

  /* We don't know what ${ORIGIN} will expand to, so we have to cut off at
   * /overlay/libs */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/overlay"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/overlay/libs"));

  /* We don't know what ${FUTURE} will expand to, so we have to cut off at
   * /future */
  g_assert_true (flatpak_exports_path_is_visible (exports, "/future"));
}

static void
test_remap_ld_preload_flatpak (Fixture *f,
                               gconstpointer context)
{
  const Config *config = context;
  g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GPtrArray) filtered = filter_expected_paths (config);
  gsize i;

  fixture_create_runtime (f, PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX);
  g_assert_nonnull (f->context->runtime);
  populate_ld_preload (f, argv,
                       (config->preload_flags
                        | PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX));

  g_assert_cmpuint (argv->len, ==, filtered->len);

  for (i = 0; i < argv->len; i++)
    {
      char *expected = g_ptr_array_index (filtered, i);
      char *argument = g_ptr_array_index (argv, i);

      g_assert_true (g_str_has_prefix (argument, "--ld-preload="));
      argument += strlen("--ld-preload=");

      if (g_str_has_prefix (expected, "/app/")
          || g_str_has_prefix (expected, "/lib/")
          || g_str_has_prefix (expected, "/usr/lib/"))
        {
          g_assert_true (g_str_has_prefix (argument, "/run/parent/"));
          argument += strlen("/run/parent");
        }

      g_assert_cmpstr (argument, ==, expected);
    }
}

/*
 * In addition to testing the rare case where there's no runtime,
 * this one also exercises --remove-game-overlay.
 */
static void
test_remap_ld_preload_no_runtime (Fixture *f,
                                  gconstpointer context)
{
  const Config *config = context;
  g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GPtrArray) filtered = filter_expected_paths (config);
  FlatpakExports *exports;
  gsize i, j;
  gboolean expect_i386 = FALSE;

  f->context->options.remove_game_overlay = TRUE;

#if defined(__i386__) || defined(__x86_64__)
  if (!(config->preload_flags & PV_APPEND_PRELOAD_FLAGS_ONE_ARCHITECTURE))
    expect_i386 = TRUE;
#endif

  fixture_create_exports (f);
  exports = f->context->exports;
  g_assert_nonnull (exports);

  g_assert_null (f->context->runtime);
  populate_ld_preload (f, argv, config->preload_flags);

  g_assert_cmpuint (argv->len, ==, filtered->len - 1);

  for (i = 0, j = 0; i < argv->len; i++, j++)
    {
      char *expected = g_ptr_array_index (filtered, j);
      char *argument = g_ptr_array_index (argv, i);

      g_assert_true (g_str_has_prefix (argument, "--ld-preload="));
      argument += strlen("--ld-preload=");

      /* /steam/lib/gameoverlayrenderer.so is missing because we used the
       * equivalent of --remove-game-overlay */
      if (g_str_has_suffix (expected, "/gameoverlayrenderer.so"))
        {
          /* We expect to skip only one element */
          g_assert_cmpint (i, ==, j);
          j++;
          expected = g_ptr_array_index (filtered, j);
        }

      g_assert_cmpstr (argument, ==, expected);
    }

  /* FlatpakExports never exports /app */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app/lib/libpreloadA.so"));

  /* We don't always export /home etc. so we have to explicitly export
   * this one */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/home"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/home/me"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/home/me/libpreloadH.so"));

  /* We don't always export /opt and /platform, so we have to explicitly export
   * these. */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/opt"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/opt/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/platform"));

  g_assert_true (flatpak_exports_path_is_visible (exports, "/opt/" PRIMARY_LIB "/libpreloadL.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/platform/plat-" PRIMARY_PLATFORM "/libpreloadP.so"));

  g_assert_cmpint (flatpak_exports_path_is_visible (exports, "/opt/" MOCK_LIB_32 "/libpreloadL.so"), ==, expect_i386);
  g_assert_cmpint (flatpak_exports_path_is_visible (exports, "/platform/plat-" MOCK_PLATFORM_32 "/libpreloadP.so"), ==, expect_i386);

  /* FlatpakExports never exports /lib as /lib */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/lib/libpreload-rootfs.so"));

  /* FlatpakExports never exports /usr as /usr */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr/lib/libpreloadU.so"));

  /* We don't know what ${ORIGIN} will expand to, so we have to cut off at
   * /overlay/libs */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/overlay"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/overlay/libs"));

  /* We don't know what ${FUTURE} will expand to, so we have to cut off at
   * /future */
  g_assert_true (flatpak_exports_path_is_visible (exports, "/future"));
}

static void
test_remap_ld_preload_flatpak_no_runtime (Fixture *f,
                                          gconstpointer context)
{
  const Config *config = context;
  g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GPtrArray) filtered = filter_expected_paths (config);
  gsize i;

  g_assert_null (f->context->runtime);
  populate_ld_preload (f, argv,
                       (config->preload_flags
                        | PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX));

  g_assert_cmpuint (argv->len, ==, filtered->len);

  for (i = 0; i < argv->len; i++)
    {
      char *expected = g_ptr_array_index (filtered, i);
      char *argument = g_ptr_array_index (argv, i);

      g_assert_true (g_str_has_prefix (argument, "--ld-preload="));
      argument += strlen("--ld-preload=");

      g_assert_cmpstr (argument, ==, expected);
    }
}

/*
 * Test that the table of supported architectures is internally consistent
 */
static void
test_supported_archs (Fixture *f,
                      gconstpointer context)
{
  gsize i;

#if defined(__i386__) || defined(__x86_64__)

  /* The primary architecture is x86_64, followed by i386
   * (implicitly secondary) */
  g_assert_cmpint (PV_N_SUPPORTED_ARCHITECTURES, ==, 2);
  g_assert_cmpstr (pv_multiarch_tuples[PV_PRIMARY_ARCHITECTURE], ==,
                   SRT_ABI_X86_64);
  g_assert_cmpstr (pv_multiarch_tuples[1], ==, SRT_ABI_I386);

  /* We also support running x86 on an aarch64 emulator host */
  g_assert_cmpint (PV_N_SUPPORTED_ARCHITECTURES_AS_EMULATOR_HOST, ==, 1);
  g_assert_cmpstr (pv_multiarch_as_emulator_tuples[0], ==, SRT_ABI_AARCH64);

#else /* !x86 */

  /* The only supported architecture is the one we were compiled for */
  g_assert_cmpint (PV_N_SUPPORTED_ARCHITECTURES, ==, 1);
#if defined(__aarch64__)
  g_assert_cmpstr (pv_multiarch_tuples[PV_PRIMARY_ARCHITECTURE], ==,
                   SRT_ABI_AARCH64);
#else /* !aarch64 */
  g_assert_cmpstr (pv_multiarch_tuples[PV_PRIMARY_ARCHITECTURE], ==,
                   _SRT_MULTIARCH);
#endif /* !aarch64 */

#endif /* !x86 */

  /* multiarch_details and multiarch_tuples are in the same order */
  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      const PvMultiarchDetails *details = &pv_multiarch_details[i];

      g_assert_cmpstr (pv_multiarch_tuples[i], ==, details->tuple);
    }

  /* The array of tuples has one extra element, to make it a GStrv */
  g_assert_cmpstr (pv_multiarch_tuples[i], ==, NULL);

#if PV_N_SUPPORTED_ARCHITECTURES_AS_EMULATOR_HOST > 0
  /* Emulator host details and tuples are also in the same order */
  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES_AS_EMULATOR_HOST; i++)
    {
      const PvMultiarchDetails *details = &pv_multiarch_as_emulator_details[i];

      g_assert_cmpstr (pv_multiarch_as_emulator_tuples[i], ==, details->tuple);
    }
#else
  i = 0;
#endif

  /* Array of tuples has one extra element, again */
  g_assert_cmpstr (pv_multiarch_as_emulator_tuples[i], ==, NULL);
}

/*
 * Test that pv_wrap_use_home(PV_HOME_MODE_SHARED) makes nearly everything
 * available.
 */
static void
test_use_home_shared (Fixture *f,
                      gconstpointer context)
{
  static const char * const paths[] =
  {
    "app/",
    "bin>usr/bin",
    "config/",
    "dangling>nonexistent",
    "data/",
    "dev/pts/",
    "etc/hosts",
    "games/SteamLibrary/",
    "home/user/.config/",
    "home/user/.config/cef_user_data>../../config/cef_user_data",
    "home/user/.local/",
    "home/user/.local/share>../../../data",
    "home/user/.steam",
    "lib>usr/lib",
    "lib32>usr/lib32",
    "lib64>usr/lib",
    "libexec>usr/libexec",
    "media/",
    "mnt/",
    "offload/user/data/",
    "offload/user/state/",
    "offload/rw2/",
    "overrides/forbidden/",
    "proc/1/fd/",
    "ro/",
    "root/",
    "run/dbus/",
    "run/gfx/",
    "run/host/",
    "run/pressure-vessel/",
    "run/systemd/",
    "rw/",
    "rw2>offload/rw2",
    "sbin>usr/bin",
    "single:/dir:/and:/deprecated/",
    "srv/data/",
    "sys/",
    "tmp/",
    "usr/local/share/",
    "usr/share/",
    "var/tmp/",
  };
  static const char * const mock_environ[] =
  {
    "STEAM_COMPAT_TOOL_PATH=/single:/dir:/and:/deprecated",
    "STEAM_COMPAT_MOUNTS=/overrides/forbidden",
    "PRESSURE_VESSEL_FILESYSTEMS_RO=/ro",
    "PRESSURE_VESSEL_FILESYSTEMS_RW=:/rw:/rw2:/nonexistent:::::",
    "XDG_DATA_HOME=/offload/user/data",
    "XDG_STATE_HOME=/offload/user/state",
    NULL
  };
  g_autoptr(FlatpakBwrap) env_bwrap = NULL;
  FlatpakExports *exports;
  FlatpakExports *env_exports;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(SrtEnvOverlay) container_env = _srt_env_overlay_new ();
  GLogLevelFlags was_fatal;
  gboolean ret;

  fixture_create_exports (f);
  exports = f->context->exports;
  g_assert_nonnull (exports);

  fixture_populate_dir (f, f->mock_host->fd, paths, G_N_ELEMENTS (paths));
  ret = pv_wrap_use_home (PV_HOME_MODE_SHARED, "/home/user", NULL, exports,
                          f->bwrap, container_env, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);
  flatpak_exports_append_bwrap_args (exports, f->bwrap);

  dump_bwrap (f->bwrap);

  /* /usr and friends are out of scope here */
  assert_bwrap_does_not_contain (f->bwrap, "/bin");
  assert_bwrap_does_not_contain (f->bwrap, "/lib");
  assert_bwrap_does_not_contain (f->bwrap, "/lib32");
  assert_bwrap_does_not_contain (f->bwrap, "/lib64");
  assert_bwrap_does_not_contain (f->bwrap, "/usr");
  assert_bwrap_does_not_contain (f->bwrap, "/sbin");

  /* Various FHS and FHS-adjacent directories go along with the home
   * directory */
  assert_bwrap_contains (f->bwrap, "--bind", "/home", "/home");
  assert_bwrap_contains (f->bwrap, "--bind", "/media", "/media");
  assert_bwrap_contains (f->bwrap, "--bind", "/mnt", "/mnt");
  assert_bwrap_contains (f->bwrap, "--bind", "/srv", "/srv");
  assert_bwrap_contains (f->bwrap, "--bind", "/var/tmp", "/var/tmp");

  /* Some directories that are commonly symlinks get handled, by
   * mounting the target of a symlink if any */
  assert_bwrap_contains (f->bwrap, "--bind", "/data", "/data");

  /* Mutable OS state is not tied to the home directory */
  assert_bwrap_does_not_contain (f->bwrap, "/etc");
  assert_bwrap_does_not_contain (f->bwrap, "/var");

  /* We do share /tmp, but this particular function is not responsible
   * for it */
  assert_bwrap_does_not_contain (f->bwrap, "/tmp");

  /* We don't currently export miscellaneous top-level directories */
  assert_bwrap_does_not_contain (f->bwrap, "/games");

  /* /run is out of scope */
  assert_bwrap_does_not_contain (f->bwrap, "/run/dbus");

  /* We don't export these here for various reasons */
  assert_bwrap_does_not_contain (f->bwrap, "/app");
  assert_bwrap_does_not_contain (f->bwrap, "/boot");
  assert_bwrap_does_not_contain (f->bwrap, "/dev");
  assert_bwrap_does_not_contain (f->bwrap, "/dev/pts");
  assert_bwrap_does_not_contain (f->bwrap, "/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/proc");
  assert_bwrap_does_not_contain (f->bwrap, "/root");
  assert_bwrap_does_not_contain (f->bwrap, "/run");
  assert_bwrap_does_not_contain (f->bwrap, "/run/gfx");
  assert_bwrap_does_not_contain (f->bwrap, "/run/host");
  assert_bwrap_does_not_contain (f->bwrap, "/run/pressure-vessel");
  assert_bwrap_does_not_contain (f->bwrap, "/sys");

  /* We would export these if they existed, but they don't */
  assert_bwrap_does_not_contain (f->bwrap, "/opt");
  assert_bwrap_does_not_contain (f->bwrap, "/run/media");

  env_bwrap = flatpak_bwrap_new (flatpak_bwrap_empty_env);

  g_clear_pointer (&f->context->original_environ, g_strfreev);
  f->context->original_environ = _srt_strdupv (mock_environ);

  g_clear_pointer (&f->context->exports, flatpak_exports_free);
  exports = NULL;

  fixture_create_exports (f);
  env_exports = f->context->exports;
  g_assert_nonnull (env_exports);

  /* Don't crash on warnings here */
  was_fatal = g_log_set_always_fatal (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
  pv_bind_and_propagate_from_environ (f->context,
                                      PV_HOME_MODE_SHARED,
                                      container_env);
  g_log_set_always_fatal (was_fatal);

  flatpak_exports_append_bwrap_args (env_exports, env_bwrap);
  dump_bwrap (env_bwrap);
  assert_bwrap_contains (env_bwrap, "--ro-bind", "/ro", "/ro");
  assert_bwrap_contains (env_bwrap, "--bind", "/rw", "/rw");
  assert_bwrap_contains (env_bwrap, "--symlink", "offload/rw2", "/rw2");
  assert_bwrap_contains (env_bwrap, "--bind", "/offload/rw2", "/offload/rw2");
  assert_bwrap_contains (env_bwrap, "--bind", "/offload/user/data",
                         "/offload/user/data");
  assert_bwrap_contains (env_bwrap, "--bind", "/offload/user/state",
                         "/offload/user/state");
  assert_bwrap_does_not_contain (env_bwrap, "/usr/local/share");
  assert_bwrap_does_not_contain (env_bwrap, "/usr/share");
  /* These are in PRESSURE_VESSEL_FILESYSTEMS_RW but don't actually exist. */
  assert_bwrap_does_not_contain (env_bwrap, "/nonexistent");
  assert_bwrap_does_not_contain (env_bwrap, "/dangling");
  /* STEAM_COMPAT_TOOL_PATH is deprecated (not explicitly tested, but
   * you'll see a warning in the test log), and because it doesn't have
   * the COLON_DELIMITED flag, it's parsed as a single oddly-named
   * directory. */
  assert_bwrap_contains (env_bwrap, "--bind",
                         "/single:/dir:/and:/deprecated",
                         "/single:/dir:/and:/deprecated");
  /* Paths below /overrides are not used, with a warning. */
  assert_bwrap_does_not_contain (env_bwrap, "/overrides/forbidden");
}

/*
 * Test that pv_wrap_use_host_os() makes nearly everything from the host OS
 * available. (This is what we do if run with no runtime, although
 * SteamLinuxRuntime_* never actually does this.)
 */
static void
test_use_host_os (Fixture *f,
                  gconstpointer context)
{
  static const char * const paths[] =
  {
    "boot/",
    "bin>usr/bin",
    "dev/pts/",
    "etc/hosts",
    "games/SteamLibrary/",
    "home/user/.steam",
    "lib>usr/lib",
    "lib32>usr/lib32",
    "lib64>usr/lib",
    "libexec>usr/libexec",
    "opt/extras/kde/",
    "overrides/",
    "proc/1/fd/",
    "root/",
    "run/dbus/",
    "run/gfx/",
    "run/host/",
    "run/media/",
    "run/pressure-vessel/",
    "run/systemd/",
    "tmp/",
    "sbin>usr/bin",
    "sys/",
    "usr/local/",
    "var/tmp/",
  };
  g_autoptr(GError) local_error = NULL;
  gboolean ret;

  fixture_create_exports (f);
  g_assert_nonnull (f->context->exports);
  fixture_populate_dir (f, f->mock_host->fd, paths, G_N_ELEMENTS (paths));
  ret = pv_wrap_use_host_os (f->mock_host->fd, f->context->exports, f->bwrap,
                             _srt_dirent_strcmp, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);
  flatpak_exports_append_bwrap_args (f->context->exports, f->bwrap);

  dump_bwrap (f->bwrap);

  /* We do export /usr and friends */
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/bin", "/bin");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib", "/lib");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib", "/lib64");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib32", "/lib32");
  assert_bwrap_contains (f->bwrap, "--ro-bind", "/usr", "/usr");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/bin", "/sbin");

  /* We do export mutable OS state */
  assert_bwrap_contains (f->bwrap, "--bind", "/etc", "/etc");
  assert_bwrap_contains (f->bwrap, "--bind", "/tmp", "/tmp");
  assert_bwrap_contains (f->bwrap, "--bind", "/var", "/var");

  /* We do export miscellaneous top-level directories */
  assert_bwrap_contains (f->bwrap, "--bind", "/games", "/games");
  assert_bwrap_contains (f->bwrap, "--bind", "/home", "/home");
  assert_bwrap_contains (f->bwrap, "--bind", "/opt", "/opt");

  /* We do export most of the contents of /run, but not /run itself */
  assert_bwrap_contains (f->bwrap, "--bind", "/run/dbus", "/run/dbus");
  assert_bwrap_contains (f->bwrap, "--bind", "/run/media", "/run/media");
  assert_bwrap_contains (f->bwrap, "--bind", "/run/systemd", "/run/systemd");

  /* We don't export these in pv_wrap_use_host_os() for various reasons */
  assert_bwrap_does_not_contain (f->bwrap, "/app");
  assert_bwrap_does_not_contain (f->bwrap, "/boot");
  assert_bwrap_does_not_contain (f->bwrap, "/dev");
  assert_bwrap_does_not_contain (f->bwrap, "/dev/pts");
  assert_bwrap_does_not_contain (f->bwrap, "/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/overrides");
  assert_bwrap_does_not_contain (f->bwrap, "/proc");
  assert_bwrap_does_not_contain (f->bwrap, "/root");
  assert_bwrap_does_not_contain (f->bwrap, "/run");
  assert_bwrap_does_not_contain (f->bwrap, "/run/gfx");
  assert_bwrap_does_not_contain (f->bwrap, "/run/host");
  assert_bwrap_does_not_contain (f->bwrap, "/run/pressure-vessel");
  assert_bwrap_does_not_contain (f->bwrap, "/sys");

  /* We would export these if they existed, but they don't */
  assert_bwrap_does_not_contain (f->bwrap, "/mnt");
  assert_bwrap_does_not_contain (f->bwrap, "/srv");
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();

  _srt_tests_init (&argc, &argv, NULL);

  g_test_add ("/bind-into-container/normal", Fixture,
              &default_config,
              setup, test_bind_into_container, teardown);
  g_test_add ("/bind-into-container/copy", Fixture,
              &copy_config,
              setup, test_bind_into_container, teardown);
  g_test_add ("/bind-into-container/interpreter-root", Fixture,
              &interpreter_root_config,
              setup, test_bind_into_container, teardown);
  g_test_add ("/bind-merged-usr", Fixture, NULL,
              setup, test_bind_merged_usr, teardown);
  g_test_add ("/bind-unmerged-usr", Fixture, NULL,
              setup, test_bind_unmerged_usr, teardown);
  g_test_add ("/bind-usr", Fixture, NULL,
              setup, test_bind_usr, teardown);
  g_test_add ("/export-root-dirs", Fixture, NULL,
              setup, test_export_root_dirs, teardown);
  g_test_add ("/make-symlink-in-container/normal", Fixture,
              &default_config,
              setup, test_make_symlink_in_container, teardown);
  g_test_add ("/make-symlink-in-container/copy", Fixture,
              &copy_config,
              setup, test_make_symlink_in_container, teardown);
  g_test_add ("/make-symlink-in-container/interpreter-root", Fixture,
              &interpreter_root_config,
              setup, test_make_symlink_in_container, teardown);
  g_test_add ("/options/defaults", Fixture, NULL,
              setup, test_options_defaults, teardown);
  g_test_add ("/options/false", Fixture, NULL,
              setup, test_options_false, teardown);
  g_test_add ("/options/true", Fixture, NULL,
              setup, test_options_true, teardown);
  g_test_add ("/passwd", Fixture, NULL, setup, test_passwd, teardown);
  g_test_add ("/remap-ld-preload", Fixture, &default_config,
              setup_ld_preload, test_remap_ld_preload, teardown);
  g_test_add ("/remap-ld-preload-flatpak", Fixture, &default_config,
              setup_ld_preload, test_remap_ld_preload_flatpak, teardown);
  g_test_add ("/remap-ld-preload-no-runtime", Fixture, &default_config,
              setup_ld_preload, test_remap_ld_preload_no_runtime, teardown);
  g_test_add ("/remap-ld-preload-flatpak-no-runtime", Fixture, &default_config,
              setup_ld_preload, test_remap_ld_preload_flatpak_no_runtime, teardown);
  g_test_add ("/supported-archs", Fixture, NULL,
              setup, test_supported_archs, teardown);
  g_test_add ("/use-home/shared", Fixture, NULL,
              setup, test_use_home_shared, teardown);
  g_test_add ("/use-host-os", Fixture, NULL,
              setup, test_use_host_os, teardown);

  g_test_add ("/one-arch/remap-ld-preload", Fixture, &one_arch_config,
              setup_ld_preload, test_remap_ld_preload, teardown);
  g_test_add ("/one-arch/remap-ld-preload-flatpak", Fixture, &one_arch_config,
              setup_ld_preload, test_remap_ld_preload_flatpak, teardown);
  g_test_add ("/one-arch/remap-ld-preload-no-runtime", Fixture, &one_arch_config,
              setup_ld_preload, test_remap_ld_preload_no_runtime, teardown);
  g_test_add ("/one-arch/remap-ld-preload-flatpak-no-runtime", Fixture, &one_arch_config,
              setup_ld_preload, test_remap_ld_preload_flatpak_no_runtime, teardown);

  return g_test_run ();
}
