/*
 * Copyright © 2024-2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "steam-runtime-tools/libc-utils-internal.h"

static const char this_module[] = THIS_MODULE;

/* Originally contributed to MangoHud in
 * https://github.com/flightlessmango/MangoHud/pull/1540 */
static void *
load_adjacent_fallback (char **message)
{
  autofree char *location = NULL;
  autofree char *lib = NULL;
  Dl_info info = {};
  void *extra_info = NULL;
  void *handle = NULL;
  const struct link_map *map;
  char *slash;

  /* The first argument can be any symbol in this shared library,
   * this_module is a convenient one */
  if (!dladdr1 (this_module, &info, &extra_info, RTLD_DL_LINKMAP))
    {
      xasprintf (message, "Unable to find my own location: %s", dlerror ());
      return NULL;
    }

  map = extra_info;

  if (map == NULL)
    {
      xasprintf (message, "Unable to find my own location: NULL link_map");
      return NULL;
    }

  if (map->l_name == NULL)
    {
      xasprintf (message, "Unable to find my own location: NULL l_name");
      return NULL;
    }

  location = realpath (map->l_name, NULL);

  if (location == NULL)
    {
      xasprintf (message, "realpath: %s", strerror (errno));
      return NULL;
    }

  slash = strrchr (location, '/');

  if (slash == NULL)
    {
      xasprintf (message, "Unable to find my own location: no directory separator");
      return NULL;
    }

  *slash = '\0';

  xasprintf (&lib, "%s/loaded-from-origin.so", location);

  handle = dlopen (lib, RTLD_NOW | RTLD_LOCAL);

  if (handle == NULL)
    {
      xasprintf (message, "Failed to load \"%s\": %s", lib, dlerror ());
      return NULL;
    }

  return handle;
}

static void
call_get_version_or_warn (void *handle,
                          const char *suffix)
{
  const char *(*get_version_ptr) (void);

  get_version_ptr = (__typeof__ (get_version_ptr)) dlsym (handle, "get_version");

  if (get_version_ptr != NULL)
    {
      if (getenv ("STEAM_RUNTIME_DEBUG") != NULL)
        fprintf (stderr, "%s[%d]/%s: found loaded-from-origin.so version %s%s\n",
                 program_invocation_short_name, getpid (), this_module, get_version_ptr (), suffix);
    }
  else
    {
      fprintf (stderr, "%s[%d]/%s: dlsym failed to find get_version: %s\n",
               program_invocation_short_name, getpid (), this_module, dlerror ());
    }
}

static void
dlclose_or_warn (void *handle)
{
  if (dlclose (handle) != 0)
    fprintf (stderr, "%s[%d]/%s: dlclose ${ORIGIN}/loaded-from-origin.so failed: %s\n",
             program_invocation_short_name, getpid (), this_module, dlerror ());
}

__attribute__((__constructor__)) static void
ctor (void)
{
  autofree char *message = NULL;
  void *handle;

  handle = dlopen ("${ORIGIN}/loaded-from-origin.so",
                   RTLD_NOW | RTLD_LOCAL);

  if (handle != NULL)
    {
      call_get_version_or_warn (handle, " via dlopen");
      dlclose_or_warn (handle);
    }
  else
    {
      fprintf (stderr, "%s[%d]/%s: dlopen ${ORIGIN}/loaded-from-origin.so failed: %s\n",
               program_invocation_short_name, getpid (), this_module, dlerror ());
    }

  handle = load_adjacent_fallback (&message);

  if (handle != NULL)
    {
      call_get_version_or_warn (handle, " via dladdr1");
      dlclose_or_warn (handle);
    }
  else
    {
      fprintf (stderr, "%s[%d]/%s: %s\n",
               program_invocation_short_name, getpid (), this_module, message);
    }
}
