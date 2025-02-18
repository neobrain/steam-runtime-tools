/*
 * Copyright © 2019-2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <steam-runtime-tools/steam-runtime-tools.h>
#include "steam-runtime-tools/architecture-internal.h"

#include <gelf.h>
#include <libelf.h>

#include <glib.h>

#include "test-utils.h"

typedef struct
{
  int unused;
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
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
}

static void
test_architecture_get_by_tuple (Fixture *f,
                                gconstpointer context)
{
  const SrtKnownArchitecture *x86_64_arch;
  const SrtKnownArchitecture *i386_arch;
  const SrtKnownArchitecture *arch;

  x86_64_arch = _srt_architecture_get_by_tuple (SRT_ABI_X86_64);
  g_assert_nonnull (x86_64_arch);
  g_assert_cmpstr (x86_64_arch->multiarch_tuple, ==, SRT_ABI_X86_64);
  g_assert_cmpstr (x86_64_arch->interoperable_runtime_linker, ==,
                   "/lib64/ld-linux-x86-64.so.2");
  g_assert_cmpstr (x86_64_arch->openxr_1_architecture, ==, "x86_64");
  g_assert_cmpint (x86_64_arch->machine_type, ==, SRT_MACHINE_TYPE_X86_64);
  g_assert_cmpint (x86_64_arch->elf_class, ==, ELFCLASS64);
  g_assert_cmpint (x86_64_arch->elf_encoding, ==, ELFDATA2LSB);
  g_assert_cmpint (x86_64_arch->sizeof_pointer, ==, 8);

  i386_arch = _srt_architecture_get_by_tuple (SRT_ABI_I386);
  g_assert_nonnull (i386_arch);
  g_assert_cmpstr (i386_arch->multiarch_tuple, ==, SRT_ABI_I386);
  g_assert_cmpstr (i386_arch->interoperable_runtime_linker, ==,
                   "/lib/ld-linux.so.2");
  g_assert_cmpstr (i386_arch->openxr_1_architecture, ==, "i686");
  g_assert_cmpint (i386_arch->machine_type, ==, SRT_MACHINE_TYPE_386);
  g_assert_cmpint (i386_arch->elf_class, ==, ELFCLASS32);
  g_assert_cmpint (i386_arch->elf_encoding, ==, ELFDATA2LSB);
  g_assert_cmpint (i386_arch->sizeof_pointer, ==, 4);

  arch = _srt_architecture_get_by_tuple (SRT_ABI_AARCH64);
  g_assert_nonnull (arch);
  g_assert_cmpstr (arch->multiarch_tuple, ==, SRT_ABI_AARCH64);
  g_assert_cmpstr (arch->interoperable_runtime_linker, ==,
                   "/lib/ld-linux-aarch64.so.1");
  g_assert_cmpstr (arch->openxr_1_architecture, ==, "aarch64");
  g_assert_cmpint (arch->machine_type, ==, SRT_MACHINE_TYPE_AARCH64);
  g_assert_cmpint (arch->elf_class, ==, ELFCLASS64);
  g_assert_cmpint (arch->elf_encoding, ==, ELFDATA2LSB);
  g_assert_cmpint (arch->sizeof_pointer, ==, 8);

  /* Used in unit tests */
  arch = _srt_architecture_get_by_tuple ("x86_64-mock-abi");
  g_assert_nonnull (arch);
  g_assert_cmpstr (arch->multiarch_tuple, ==, "x86_64-mock-abi");
  g_assert_cmpstr (arch->interoperable_runtime_linker, ==, NULL);
  g_assert_cmpint (arch->machine_type, ==, SRT_MACHINE_TYPE_UNKNOWN);
  g_assert_cmpint (arch->elf_class, ==, ELFCLASSNONE);
  g_assert_cmpint (arch->sizeof_pointer, ==, 8);

#ifdef _SRT_MULTIARCH
  arch = _srt_architecture_get_by_tuple (_SRT_MULTIARCH);
  g_assert_nonnull (arch);
#if defined(__x86_64__) && defined(__LP64__)
  g_assert_true (arch == x86_64_arch);
#elif defined(__i386__)
  g_assert_true (arch == i386_arch);
#endif
  g_assert_cmpstr (arch->multiarch_tuple, ==, _SRT_MULTIARCH);
  g_assert_cmpint (arch->sizeof_pointer, ==, sizeof (void *));
#endif
}

int
main (int argc,
      char **argv)
{
  _srt_tests_init (&argc, &argv, NULL);
  g_test_add ("/architecture/get_by_tuple", Fixture, NULL,
              setup, test_architecture_get_by_tuple, teardown);

  return g_test_run ();
}
