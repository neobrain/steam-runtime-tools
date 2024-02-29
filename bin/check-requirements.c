/*
 * Copyright © 2020 Collabora Ltd.
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

/*
 * Perform some checks to ensure that the Steam client requirements are met.
 * Output a human-readable message on stdout if the current system does not
 * meet every requirement.
 */

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/bwrap-internal.h>
#include <steam-runtime-tools/cpu-feature-internal.h>
#include <steam-runtime-tools/steam-internal.h>
#include <steam-runtime-tools/utils-internal.h>

#define X86_FEATURES_REQUIRED (SRT_X86_FEATURE_X86_64 \
                               | SRT_X86_FEATURE_CMPXCHG16B \
                               | SRT_X86_FEATURE_SSE3)

enum
{
  OPTION_HELP = 1,
  OPTION_VERSION,
};

struct option long_options[] =
{
    { "version", no_argument, NULL, OPTION_VERSION },
    { "help", no_argument, NULL, OPTION_HELP },
    { NULL, 0, NULL, 0 }
};

static void usage (int code) __attribute__((__noreturn__));

/*
 * Print usage information and exit with status @code.
 */
static void
usage (int code)
{
  FILE *fp;

  if (code == 0)
    fp = stdout;
  else
    fp = stderr;

  fprintf (fp, "Usage: %s [OPTIONS]\n",
           program_invocation_short_name);
  exit (code);
}

static gboolean
check_x86_features (SrtX86FeatureFlags features)
{
  return ((features & X86_FEATURES_REQUIRED) == X86_FEATURES_REQUIRED);
}

static const char cannot_run_bwrap[] =
"Steam on Linux now requires the ability to create new user namespaces.\n"
"\n"
"If the file /proc/sys/kernel/unprivileged_userns_clone exists, check that\n"
"it contains value 1.\n"
"\n"
"If the file /proc/sys/user/max_user_namespaces exists, check that its\n"
"value is high enough.\n"
"\n"
"This requirement is the same as for Flatpak, which has more detailed\n"
"information available:\n"
"https://github.com/flatpak/flatpak/wiki/User-namespace-requirements\n"
;

static const char installed_in_usr[] =
"Steam on Linux is intended to install into the home directory of a user,\n"
"typically ~/.local/share/Steam.\n"
"\n"
"It cannot be installed below /usr.\n"
;

int
main (int argc,
      char **argv)
{
  FILE *original_stdout = NULL;
  GError *error = NULL;
  SrtX86FeatureFlags x86_features = SRT_X86_FEATURE_NONE;
  SrtX86FeatureFlags known = SRT_X86_FEATURE_NONE;
  SrtSteamIssues steam_issues;
  const gchar *output = NULL;
  const char *prefix = NULL;
  const char *pkglibexecdir = NULL;
  int opt;
  int exit_code = EXIT_SUCCESS;

  _srt_setenv_disable_gio_modules ();

  while ((opt = getopt_long (argc, argv, "", long_options, NULL)) != -1)
    {
      switch (opt)
        {
          case OPTION_VERSION:
            /* Output version number as YAML for machine-readability,
             * inspired by `ostree --version` and `docker version` */
            printf (
                "%s:\n"
                " Package: steam-runtime-tools\n"
                " Version: %s\n",
                argv[0], VERSION);
            return EXIT_SUCCESS;

          case OPTION_HELP:
            usage (0);
            break;

          case '?':
          default:
            usage (EX_USAGE);
            break;  /* not reached */
        }
    }

  if (optind != argc)
    usage (EX_USAGE);

  /* stdout is reserved for machine-readable output, so avoid having
   * things like g_debug() pollute it. */
  original_stdout = _srt_divert_stdout_to_stderr (&error);

  if (original_stdout == NULL)
    {
      g_warning ("%s", error->message);
      g_clear_error (&error);
      return EXIT_FAILURE;
    }

  _srt_unblock_signals ();

  x86_features = _srt_feature_get_x86_flags (NULL, &known);

  if (!check_x86_features (x86_features))
    {
      output = "Sorry, this computer's CPU is too old to run Steam.\n"
               "\nSteam requires at least an Intel Pentium 4 or AMD Opteron, with the following features:\n"
               "\t- x86-64 (AMD64) instruction set (lm in /proc/cpuinfo flags)\n"
               "\t- CMPXCHG16B instruction support (cx16 in /proc/cpuinfo flags)\n"
               "\t- SSE3 instruction support (pni in /proc/cpuinfo flags)\n";
      exit_code = EX_OSERR;
      goto out;
    }

  prefix = _srt_find_myself (NULL, &pkglibexecdir, &error);

  if (prefix == NULL)
    {
      g_warning ("Internal error: %s", error->message);
      g_clear_error (&error);
    }
  else if (g_file_test ("/run/pressure-vessel", G_FILE_TEST_IS_DIR))
    {
      g_info ("Already under pressure-vessel, not checking bwrap functionality.");
    }
  else if (g_file_test ("/.flatpak-info", G_FILE_TEST_IS_REGULAR))
    {
      g_info ("Running under Flatpak, not checking bwrap functionality.");
    }
  else
    {
      g_autofree gchar *bwrap = _srt_check_bwrap (pkglibexecdir, FALSE, NULL);

      if (bwrap == NULL)
        {
          output = cannot_run_bwrap;
          exit_code = EX_OSERR;
          goto out;
        }
    }

  steam_issues = _srt_steam_check (_srt_const_strv (environ), NULL);

  if (steam_issues & SRT_STEAM_ISSUES_INSTALLED_IN_USR)
    {
      output = installed_in_usr;
      exit_code = EX_OSERR;
      goto out;
    }

out:
  if (output != NULL)
    {
      if (fputs (output, original_stdout) < 0)
        g_warning ("Unable to write output: %s", g_strerror (errno));

      if (fputs ("\n", original_stdout) < 0)
        g_warning ("Unable to write final newline: %s", g_strerror (errno));
    }

  if (fclose (original_stdout) != 0)
    g_warning ("Unable to close stdout: %s", g_strerror (errno));

  return exit_code;
}
