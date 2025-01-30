/*
 * Copyright © 2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <glib.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "adverb-preload.h"
#include "flatpak-utils-base-private.h"
#include "tests/test-utils.h"

typedef struct
{
  TestsOpenFdSet old_fds;
  FlatpakBwrap *bwrap;
  PvPerArchDirs *lib_temp_dirs;
} Fixture;

typedef struct
{
  gboolean can_discover_platform;
} Config;

static const Config default_config = {
  .can_discover_platform = TRUE,
};

static void
setup (Fixture *f,
       gconstpointer context)
{
  const Config *config = context;
  g_autoptr(GError) local_error = NULL;
  gsize i;

  if (config == NULL)
    config = &default_config;

  f->old_fds = tests_check_fd_leaks_enter ();
  f->bwrap = flatpak_bwrap_new (flatpak_bwrap_empty_env);

  if (config->can_discover_platform)
    {
      f->lib_temp_dirs = pv_per_arch_dirs_new (&local_error);
#ifdef _SRT_TESTS_STRICT
      /* We allow this to fail because it might fail on particularly strange
       * OS configurations, but for platforms we actively support,
       * we expect it to work */
      g_assert_no_error (local_error);
      g_assert_nonnull (f->lib_temp_dirs);
#endif

      if (f->lib_temp_dirs == NULL)
        {
          g_test_skip (local_error->message);
          return;
        }

      g_test_message ("Cross-platform module prefix: %s",
                      f->lib_temp_dirs->libdl_token_path);

      for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
        g_test_message ("Concrete path for %s architecture: %s",
                        pv_multiarch_tuples[i],
                        f->lib_temp_dirs->abi_paths[i]);

    }
  else
    {
      g_test_message ("Pretending we cannot use $LIB/$PLATFORM");
    }
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_clear_pointer (&f->lib_temp_dirs, pv_per_arch_dirs_free);
  g_clear_pointer (&f->bwrap, flatpak_bwrap_free);
  tests_check_fd_leaks_leave (f->old_fds);
}

static void
test_basic (Fixture *f,
            gconstpointer context)
{
  static const PvAdverbPreloadModule modules[] =
  {
    {
      .name = (char *) "",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_AUDIT,
      .abi_index = 0,
    },
    {
      .name = (char *) "/opt/libaudit.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_AUDIT,
      .abi_index = 0,
    },
    {
      .name = (char *) "",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_AUDIT,
      .abi_index = PV_UNSPECIFIED_ABI,
    },
    {
      .name = (char *) "/opt/libpreload.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = 0,
    },
    {
      .name = (char *) "/opt/unspecified.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = PV_UNSPECIFIED_ABI,
    },
    {
      .name = (char *) "/opt/libpreload2.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = 0,
    },
    {
      .name = (char *) "/opt/unspecified2.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = PV_UNSPECIFIED_ABI,
    },
  };
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GString) expected = g_string_new ("");
  g_autoptr(GString) path = g_string_new ("");
  gboolean ret;
  gsize i;

  ret = pv_adverb_set_up_preload_modules (f->bwrap,
                                          f->lib_temp_dirs,
                                          modules,
                                          G_N_ELEMENTS (modules),
                                          &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);

  flatpak_bwrap_sort_envp (f->bwrap);
  g_assert_nonnull (f->bwrap->envp);
  i = 0;

  g_string_assign (expected, "LD_AUDIT=");

  if (f->lib_temp_dirs != NULL)
    g_string_append_printf (expected, "%s/libaudit.so",
                            f->lib_temp_dirs->libdl_token_path);
  else
    g_string_append (expected, "/opt/libaudit.so");

  g_assert_cmpstr (f->bwrap->envp[i], ==, expected->str);
  i++;

  /* Order is preserved, independent of whether an ABI is specified */
  g_string_assign (expected, "LD_PRELOAD=");

  if (f->lib_temp_dirs != NULL)
    g_string_append_printf (expected, "%s/libpreload.so",
                            f->lib_temp_dirs->libdl_token_path);
  else
    g_string_append (expected, "/opt/libpreload.so");

  g_string_append_c (expected, ':');
  g_string_append (expected, "/opt/unspecified.so");
  g_string_append_c (expected, ':');

  if (f->lib_temp_dirs != NULL)
    g_string_append_printf (expected, "%s/libpreload2.so",
                            f->lib_temp_dirs->libdl_token_path);
  else
    g_string_append (expected, "/opt/libpreload2.so");

  g_string_append_c (expected, ':');
  g_string_append (expected, "/opt/unspecified2.so");
  g_assert_cmpstr (f->bwrap->envp[i], ==, expected->str);
  i++;

  g_assert_cmpstr (f->bwrap->envp[i], ==, NULL);

  for (i = 0; i < G_N_ELEMENTS (modules); i++)
    {
      g_autofree gchar *target = NULL;

      if (f->lib_temp_dirs == NULL)
        continue;

      /* Empty module entries are ignored */
      if (modules[i].name[0] == '\0')
        continue;

      g_string_assign (path, f->lib_temp_dirs->abi_paths[0]);
      g_string_append_c (path, G_DIR_SEPARATOR);
      g_string_append (path, glnx_basename (modules[i].name));
      target = flatpak_readlink (path->str, &local_error);

      /* Only the modules that have architecture-specific variations
       * (in practice those that originally had $LIB or $PLATFORM) need
       * symlinks created for them, because only those modules get their
       * LD_PRELOAD entries rewritten */
      if (modules[i].abi_index == 0)
        {
          g_assert_no_error (local_error);
          g_test_message ("%s -> %s", path->str, target);
          g_assert_cmpstr (target, ==, modules[i].name);
        }
      else
        {
          g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
          g_clear_error (&local_error);
        }
    }
}

static void
test_biarch (Fixture *f,
             gconstpointer context)
{
#if PV_N_SUPPORTED_ARCHITECTURES >= 2
  static const PvAdverbPreloadModule modules[] =
  {
    {
      .name = (char *) "/opt/libpreload.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = PV_UNSPECIFIED_ABI,
    },
    /* In practice x86_64-linux-gnu */
    {
      .name = (char *) "/opt/lib0/libpreload.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = 0,
    },
    /* In practice i386-linux-gnu */
    {
      .name = (char *) "/opt/lib1/libpreload.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = 1,
    },
  };
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GString) expected = g_string_new ("");
  g_autoptr(GString) path = g_string_new ("");
  gboolean ret;
  gsize i;

  ret = pv_adverb_set_up_preload_modules (f->bwrap,
                                          f->lib_temp_dirs,
                                          modules,
                                          G_N_ELEMENTS (modules),
                                          &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);

  flatpak_bwrap_sort_envp (f->bwrap);
  g_assert_nonnull (f->bwrap->envp);
  i = 0;

  /* We don't have any LD_AUDIT modules in this example, so we don't set
   * those up at all, and therefore we expect f->bwrap->envp not to
   * contain LD_AUDIT. */

  g_string_assign (expected, "LD_PRELOAD=");
  g_string_append (expected, "/opt/libpreload.so");
  g_string_append_c (expected, ':');

  if (f->lib_temp_dirs != NULL)
    {
      g_string_append_printf (expected, "%s/libpreload.so",
                              f->lib_temp_dirs->libdl_token_path);
    }
  else
    {
      g_string_append (expected, "/opt/lib0/libpreload.so");
      g_string_append_c (expected, ':');
      g_string_append (expected, "/opt/lib1/libpreload.so");
    }

  g_assert_cmpstr (f->bwrap->envp[i], ==, expected->str);
  i++;

  g_assert_cmpstr (f->bwrap->envp[i], ==, NULL);

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_autofree gchar *target = NULL;

      if (f->lib_temp_dirs == NULL)
        continue;

      g_string_assign (path, f->lib_temp_dirs->abi_paths[i]);
      g_string_append_c (path, G_DIR_SEPARATOR);
      g_string_append (path, "libpreload.so");

      target = flatpak_readlink (path->str, &local_error);
      g_assert_no_error (local_error);
      g_test_message ("%s -> %s", path->str, target);

      g_string_assign (expected, "");
      g_string_append_printf (expected, "/opt/lib%zu/libpreload.so", i);
      g_assert_cmpstr (target, ==, expected->str);
    }
#else
  /* In practice this is reached on non-x86 */
  g_test_skip ("Biarch libraries not supported on this architecture");
#endif
}

typedef struct
{
  const char *option;
  PvAdverbPreloadModule expected;
} CommandLineTest;

static void
test_cli (Fixture *f,
          gconstpointer context)
{
  static const CommandLineTest tests[] =
  {
    {
      .option = "",
      .expected = {
        .name = (char *) "",
        .abi_index = PV_UNSPECIFIED_ABI,
      },
    },
    {
      .option = "libpreload.so",
      .expected = {
        .name = (char *) "libpreload.so",
        .abi_index = PV_UNSPECIFIED_ABI,
      },
    },
#if defined(__x86_64__) || defined(__i386__)
    {
      .option = "/lib64/libpreload.so:abi=" SRT_ABI_X86_64,
      .expected = {
        .name = (char *) "/lib64/libpreload.so",
        .abi_index = 0,
      },
    },
    {
      .option = "/lib32/libpreload.so:abi=" SRT_ABI_I386,
      .expected = {
        .name = (char *) "/lib32/libpreload.so",
        .abi_index = 1,
      },
    },
#endif
#if defined(_SRT_MULTIARCH)
    {
      .option = "/tmp/libabi.so:abi=" _SRT_MULTIARCH,
      .expected = {
        .name = (char *) "/tmp/libabi.so",
#if defined(__i386__)
        /* On i386, we treat x86_64 as the primary architecture */
        .abi_index = 1,
#else
        .abi_index = 0,
#endif
      },
    },
#endif
    {
      .option = "/tmp/libabi.so:nonsense",
      .expected = {
        .name = NULL,
      },
    },
  };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      const CommandLineTest *test = &tests[i];
      /* There's no real difference between our handling of LD_AUDIT
       * and LD_PRELOAD, so we alternate between testing them both */
      const PvPreloadVariableIndex which = i % 2;
      const char *option = pv_preload_variables[which].adverb_option;
      g_autoptr(GError) local_error = NULL;
      g_auto(PvAdverbPreloadModule) actual = PV_ADVERB_PRELOAD_MODULE_INIT;
      gboolean ret;

      ret = pv_adverb_preload_module_parse_adverb_cli (&actual,
                                                       option,
                                                       which,
                                                       test->option,
                                                       &local_error);

      if (ret)
        {
          const char *abi = "(unspecified)";

          if (actual.abi_index != PV_UNSPECIFIED_ABI)
            {
              g_assert_cmpuint (actual.abi_index, >=, 0);
              g_assert_cmpuint (actual.abi_index, <, PV_N_SUPPORTED_ARCHITECTURES);
              abi = pv_multiarch_details[actual.abi_index].tuple;
            }

          g_test_message ("\"%s\" -> \"%s\", abi=%s",
                          test->option, actual.name, abi);
        }
      else
        {
          g_test_message ("\"%s\" -> error \"%s\"",
                          test->option, local_error->message);
        }

      if (test->expected.name == NULL)
        {
          g_assert_false (ret);
          g_assert_null (actual.name);
          g_assert_nonnull (local_error);
        }
      else
        {
          g_assert_no_error (local_error);
          g_assert_true (ret);
          g_assert_cmpstr (actual.name, ==, test->expected.name);
          g_assert_cmpuint (actual.abi_index, ==, test->expected.abi_index);
          g_assert_cmpuint (actual.index_in_preload_variables, ==, which);
          pv_adverb_preload_module_clear (&actual);
        }
    }
}

/*
 * There is a special case for gameoverlayrenderer.so:
 * pv-adverb --ld-preload=/.../ubuntu12_32/gameoverlayrenderer.so is
 * treated as if it had been .../gameoverlayrenderer.so:abi=i386-linux-gnu,
 * and so on.
 */
static void
test_gameoverlayrenderer (Fixture *f,
                          gconstpointer context)
{
#if defined(__x86_64__) || defined(__i386__)
  static const PvAdverbPreloadModule modules[] =
  {
    {
      .name = (char *) "/opt/steam/some-other-abi/gameoverlayrenderer.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = PV_UNSPECIFIED_ABI,
    },
    {
      .name = (char *) "/opt/steam/ubuntu12_32/gameoverlayrenderer.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = PV_UNSPECIFIED_ABI,
    },
    {
      .name = (char *) "/opt/steam/ubuntu12_64/gameoverlayrenderer.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = PV_UNSPECIFIED_ABI,
    },
    {
      .name = (char *) "/opt/steam/some-other-abi/gameoverlayrenderer.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = PV_UNSPECIFIED_ABI,
    },
  };
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GString) expected = g_string_new ("");
  g_autoptr(GString) path = g_string_new ("");
  gboolean ret;
  gsize i;

  G_STATIC_ASSERT (PV_N_SUPPORTED_ARCHITECTURES == 2);

  ret = pv_adverb_set_up_preload_modules (f->bwrap,
                                          f->lib_temp_dirs,
                                          modules,
                                          G_N_ELEMENTS (modules),
                                          &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);

  flatpak_bwrap_sort_envp (f->bwrap);
  g_assert_nonnull (f->bwrap->envp);
  i = 0;

  g_string_assign (expected, "LD_PRELOAD=");
  g_string_append (expected, "/opt/steam/some-other-abi/gameoverlayrenderer.so");
  g_string_append_c (expected, ':');

  if (f->lib_temp_dirs != NULL)
    {
      g_string_append_printf (expected, "%s/gameoverlayrenderer.so",
                              f->lib_temp_dirs->libdl_token_path);
    }
  else
    {
      g_string_append (expected, "/opt/steam/ubuntu12_32/gameoverlayrenderer.so");
      g_string_append_c (expected, ':');
      g_string_append (expected, "/opt/steam/ubuntu12_64/gameoverlayrenderer.so");
    }

  g_string_append_c (expected, ':');
  g_string_append (expected, "/opt/steam/some-other-abi/gameoverlayrenderer.so");
  g_assert_cmpstr (f->bwrap->envp[i], ==, expected->str);
  i++;

  g_assert_cmpstr (f->bwrap->envp[i], ==, NULL);

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_autofree gchar *target = NULL;

      if (f->lib_temp_dirs == NULL)
        continue;

      g_string_assign (path, f->lib_temp_dirs->abi_paths[i]);
      g_string_append_c (path, G_DIR_SEPARATOR);
      g_string_append (path, "gameoverlayrenderer.so");

      target = flatpak_readlink (path->str, &local_error);
      g_assert_no_error (local_error);
      g_test_message ("%s -> %s", path->str, target);

      g_string_assign (expected, "");
      g_string_append_printf (expected, "/opt/steam/%s/gameoverlayrenderer.so",
                              pv_multiarch_details[i].gameoverlayrenderer_dir);
      g_assert_cmpstr (target, ==, expected->str);
    }
#else
  g_test_skip ("gameoverlayrenderer special-case is only implemented on x86");
#endif
}

/*
 * steamrt/tasks#302: pv-adverb would fail if /usr/$LIB/libMangoHud.so
 * was (uselessly) added to the LD_PRELOAD path more than once.
 * This test exercises the same thing for gameoverlayrenderer.so, too.
 */
static void
test_repetition (Fixture *f,
                 gconstpointer context)
{
  static const PvAdverbPreloadModule modules[] =
  {
    {
      .name = (char *) "/opt/lib0/libfirst.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = 0,
    },
    {
      .name = (char *) "/opt/lib0/one/same-basename.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = 0,
    },
    {
      .name = (char *) "/opt/lib0/two/same-basename.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = 0,
    },
    {
      .name = (char *) "/opt/lib0/libpreload.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = 0,
    },
#if PV_N_SUPPORTED_ARCHITECTURES > 1
    {
      .name = (char *) "/opt/lib1/libpreload.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = 1,
    },
#endif
#if defined(__x86_64__) || defined(__i386__)
    {
      .name = (char *) "/opt/steam/ubuntu12_32/gameoverlayrenderer.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = PV_UNSPECIFIED_ABI,
    },
    {
      .name = (char *) "/opt/steam/ubuntu12_64/gameoverlayrenderer.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = PV_UNSPECIFIED_ABI,
    },
#endif
    {
      .name = (char *) "/opt/lib0/libmiddle.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = 0,
    },
    {
      .name = (char *) "/opt/lib0/libpreload.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = 0,
    },
#if PV_N_SUPPORTED_ARCHITECTURES > 1
    {
      .name = (char *) "/opt/lib1/libpreload.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = 1,
    },
#endif
#if defined(__x86_64__) || defined(__i386__)
    {
      .name = (char *) "/opt/steam/ubuntu12_32/gameoverlayrenderer.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = PV_UNSPECIFIED_ABI,
    },
    {
      .name = (char *) "/opt/steam/ubuntu12_64/gameoverlayrenderer.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = PV_UNSPECIFIED_ABI,
    },
#endif
    {
      .name = (char *) "/opt/lib0/liblast.so",
      .index_in_preload_variables = PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      .abi_index = 0,
    },
  };
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GString) expected = g_string_new ("");
  g_autoptr(GString) path = g_string_new ("");
  gboolean ret;
  gsize i;

  ret = pv_adverb_set_up_preload_modules (f->bwrap,
                                          f->lib_temp_dirs,
                                          modules,
                                          G_N_ELEMENTS (modules),
                                          &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);

  flatpak_bwrap_sort_envp (f->bwrap);
  g_assert_nonnull (f->bwrap->envp);
  i = 0;

  g_string_assign (expected, "LD_PRELOAD=");

  if (f->lib_temp_dirs != NULL)
    g_string_append_printf (expected, "%s/libfirst.so",
                            f->lib_temp_dirs->libdl_token_path);
  else
    g_string_append (expected, "/opt/lib0/libfirst.so");

  g_string_append_c (expected, ':');

  if (f->lib_temp_dirs != NULL)
    g_string_append_printf (expected, "%s/same-basename.so",
                            f->lib_temp_dirs->libdl_token_path);
  else
    g_string_append (expected, "/opt/lib0/one/same-basename.so");

  g_string_append_c (expected, ':');
  /* We don't do the per-architecture split if there's a basename
   * collision */
  g_string_append (expected, "/opt/lib0/two/same-basename.so");
  g_string_append_c (expected, ':');

  if (f->lib_temp_dirs != NULL)
    {
      g_string_append_printf (expected, "%s/libpreload.so",
                              f->lib_temp_dirs->libdl_token_path);
    }
  else
    {
      g_string_append (expected, "/opt/lib0/libpreload.so");
#if PV_N_SUPPORTED_ARCHITECTURES > 1
      g_string_append_c (expected, ':');
      g_string_append (expected, "/opt/lib1/libpreload.so");
#endif
    }

#if defined(__x86_64__) || defined(__i386__)
  g_string_append_c (expected, ':');

  if (f->lib_temp_dirs != NULL)
    {
      g_string_append_printf (expected, "%s/gameoverlayrenderer.so",
                              f->lib_temp_dirs->libdl_token_path);
    }
  else
    {
      g_string_append (expected, "/opt/steam/ubuntu12_32/gameoverlayrenderer.so");
      g_string_append_c (expected, ':');
      g_string_append (expected, "/opt/steam/ubuntu12_64/gameoverlayrenderer.so");
    }
#endif

  g_string_append_c (expected, ':');

  if (f->lib_temp_dirs != NULL)
    g_string_append_printf (expected, "%s/libmiddle.so",
                            f->lib_temp_dirs->libdl_token_path);
  else
    g_string_append (expected, "/opt/lib0/libmiddle.so");

  g_string_append_c (expected, ':');

  if (f->lib_temp_dirs != NULL)
    {
      /* If we are able to split up the modules by architecture,
       * then the duplicates don't appear in the search path a second time */
      g_string_append_printf (expected, "%s/liblast.so",
                              f->lib_temp_dirs->libdl_token_path);
    }
  else
    {
      /* If we were unable to split up the modules by architecture,
       * we change as little as possible, so in this case we do not
       * deduplicate */
      g_string_append (expected, "/opt/lib0/libpreload.so");
      g_string_append_c (expected, ':');
#if PV_N_SUPPORTED_ARCHITECTURES > 1
      g_string_append (expected, "/opt/lib1/libpreload.so");
      g_string_append_c (expected, ':');
#endif
#if defined(__x86_64__) || defined(__i386__)
      g_string_append (expected, "/opt/steam/ubuntu12_32/gameoverlayrenderer.so");
      g_string_append_c (expected, ':');
      g_string_append (expected, "/opt/steam/ubuntu12_64/gameoverlayrenderer.so");
      g_string_append_c (expected, ':');
#endif

      g_string_append (expected, "/opt/lib0/liblast.so");
    }

  g_assert_cmpstr (f->bwrap->envp[i], ==, expected->str);
  i++;

  g_assert_cmpstr (f->bwrap->envp[i], ==, NULL);

  /* The symlinks get created (but only once) */

  for (i = 0; i < MIN (PV_N_SUPPORTED_ARCHITECTURES, 2); i++)
    {
      gsize j;

      if (f->lib_temp_dirs == NULL)
        continue;

      for (j = 0; j < G_N_ELEMENTS (modules); j++)
        {
          g_autofree gchar *target = NULL;

          if (modules[j].abi_index != i)
            {
              g_test_message ("Not expecting a %s symlink for %s",
                              pv_multiarch_tuples[i], modules[j].name);
              continue;
            }

          if (g_str_equal (modules[j].name, "/opt/lib0/two/same-basename.so"))
            {
              g_test_message ("Not expecting a symlink for %s because it "
                              "collides with a basename seen earlier",
                              modules[j].name);
              continue;
            }

          g_string_assign (path, f->lib_temp_dirs->abi_paths[i]);
          g_string_append_c (path, G_DIR_SEPARATOR);
          g_string_append (path, glnx_basename (modules[j].name));

          target = flatpak_readlink (path->str, &local_error);
          g_assert_no_error (local_error);
          g_test_message ("%s -> %s", path->str, target);

          g_assert_cmpstr (target, ==, modules[j].name);
        }
    }

#if defined(__x86_64__) || defined(__i386__)
  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_autofree gchar *target = NULL;

      if (f->lib_temp_dirs == NULL)
        continue;

      g_string_assign (path, f->lib_temp_dirs->abi_paths[i]);
      g_string_append_c (path, G_DIR_SEPARATOR);
      g_string_append (path, "gameoverlayrenderer.so");

      target = flatpak_readlink (path->str, &local_error);
      g_assert_no_error (local_error);
      g_test_message ("%s -> %s", path->str, target);

      g_string_assign (expected, "");
      g_string_append_printf (expected, "/opt/steam/%s/gameoverlayrenderer.so",
                              pv_multiarch_details[i].gameoverlayrenderer_dir);
      g_assert_cmpstr (target, ==, expected->str);
    }
#endif
}

static const Config cannot_discover_platform = {
  .can_discover_platform = FALSE,
};

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

#define add_test(name, function) \
  do { \
    g_test_add (name, \
                Fixture, NULL, \
                setup, function, teardown); \
    g_test_add (name "/cannot-discover-platform", \
                Fixture, &cannot_discover_platform, \
                setup, function, teardown); \
  } while (0)

  add_test ("/basic", test_basic);
  add_test ("/biarch", test_biarch);
  add_test ("/gameoverlayrenderer", test_gameoverlayrenderer);
  add_test ("/repetition", test_repetition);

  /* This one isn't affected by whether we have the PvPerArchDirs or not */
  g_test_add ("/cli", Fixture, NULL,
              setup, test_cli, teardown);

  return g_test_run ();
}
