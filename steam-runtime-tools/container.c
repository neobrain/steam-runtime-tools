/*
 * Copyright © 2021 Collabora Ltd.
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

#include "steam-runtime-tools/container.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include "steam-runtime-tools/container-internal.h"
#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

/**
 * SECTION:container
 * @title: Container info
 * @short_description: Information about the eventual container that is currently in use
 * @include: steam-runtime-tools/steam-runtime-tools.h
 */

struct _SrtContainerInfo
{
  /*< private >*/
  GObject parent;
  gchar *flatpak_version;
  gchar *host_directory;
  SrtContainerType type;
};

struct _SrtContainerInfoClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_FLATPAK_VERSION,
  PROP_HOST_DIRECTORY,
  PROP_TYPE,
  N_PROPERTIES
};

G_DEFINE_TYPE (SrtContainerInfo, srt_container_info, G_TYPE_OBJECT)

static void
srt_container_info_init (SrtContainerInfo *self)
{
}

static void
srt_container_info_get_property (GObject *object,
                                 guint prop_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
  SrtContainerInfo *self = SRT_CONTAINER_INFO (object);

  switch (prop_id)
    {
      case PROP_FLATPAK_VERSION:
        g_value_set_string (value, self->flatpak_version);
        break;

      case PROP_HOST_DIRECTORY:
        g_value_set_string (value, self->host_directory);
        break;

      case PROP_TYPE:
        g_value_set_enum (value, self->type);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_container_info_set_property (GObject *object,
                                 guint prop_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
  SrtContainerInfo *self = SRT_CONTAINER_INFO (object);

  switch (prop_id)
    {
      case PROP_FLATPAK_VERSION:
        /* Construct-only */
        g_return_if_fail (self->flatpak_version == NULL);
        self->flatpak_version = g_value_dup_string (value);
        break;

      case PROP_HOST_DIRECTORY:
        /* Construct-only */
        g_return_if_fail (self->host_directory == NULL);
        self->host_directory = g_value_dup_string (value);
        break;

      case PROP_TYPE:
        /* Construct-only */
        g_return_if_fail (self->type == 0);
        self->type = g_value_get_enum (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_container_info_finalize (GObject *object)
{
  SrtContainerInfo *self = SRT_CONTAINER_INFO (object);

  g_free (self->flatpak_version);
  g_free (self->host_directory);

  G_OBJECT_CLASS (srt_container_info_parent_class)->finalize (object);
}

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
srt_container_info_class_init (SrtContainerInfoClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_container_info_get_property;
  object_class->set_property = srt_container_info_set_property;
  object_class->finalize = srt_container_info_finalize;

  properties[PROP_FLATPAK_VERSION] =
    g_param_spec_string ("flatpak-version", "Flatpak version",
                         "Which Flatpak version, if any, is in use",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_HOST_DIRECTORY] =
    g_param_spec_string ("host-directory", "Host directory",
                         "Absolute path where important files from the host "
                         "system can be found",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_TYPE] =
    g_param_spec_enum ("type", "Container type",
                       "Which container type is currently in use",
                       SRT_TYPE_CONTAINER_TYPE, SRT_CONTAINER_TYPE_UNKNOWN,
                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

typedef struct
{
  SrtContainerType type;
  const char *name;
} ContainerTypeName;

static const ContainerTypeName container_types[] =
{
  { SRT_CONTAINER_TYPE_DOCKER, "docker" },
  { SRT_CONTAINER_TYPE_FLATPAK, "flatpak" },
  { SRT_CONTAINER_TYPE_PODMAN, "podman" },
  { SRT_CONTAINER_TYPE_PRESSURE_VESSEL, "pressure-vessel" },
};

static SrtContainerType
container_type_from_name (const char *name)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (container_types); i++)
    {
      const ContainerTypeName *entry = &container_types[i];

      if (strcmp (entry->name, name) == 0)
        return entry->type;
    }

  return SRT_CONTAINER_TYPE_UNKNOWN;
}

/**
 * _srt_check_container:
 * @sysroot: (not nullable): System root, often `/`
 *
 * Gather and return information about the container that is currently in use.
 *
 * Returns: (transfer full): A new #SrtContainerInfo object.
 *  Free with g_object_unref().
 */
SrtContainerInfo *
_srt_check_container (SrtSysroot *sysroot)
{
  g_autofree gchar *contents = NULL;
  g_autofree gchar *run_host_path = NULL;
  g_autofree gchar *host_directory = NULL;
  g_autofree gchar *flatpak_version = NULL;
  SrtContainerType type = SRT_CONTAINER_TYPE_UNKNOWN;
  glnx_autofd int run_host_fd = -1;

  g_return_val_if_fail (SRT_IS_SYSROOT (sysroot), NULL);

  g_debug ("Finding container info in sysroot %s...", sysroot->path);

  run_host_fd = _srt_sysroot_open (sysroot, "/run/host",
                                   SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY,
                                   &run_host_path, NULL);

  g_debug ("/run/host resolved to %s", run_host_path ?: "(null)");

  /* Toolbx 0.0.99.3 makes /run/host a symlink to .. on the host system,
   * meaning the resolved path relative to the sysroot is ".".
   * We don't want that to be interpreted as being a container. */
  if (run_host_path != NULL && !g_str_equal (run_host_path, "."))
    host_directory = g_build_filename (sysroot->path, run_host_path, NULL);

  if (host_directory != NULL
      && _srt_sysroot_load (sysroot, "/run/host/container-manager",
                            SRT_RESOLVE_FLAGS_NONE,
                            NULL, &contents, NULL, NULL))
    {
      g_strchomp (contents);
      type = container_type_from_name (contents);
      g_debug ("Type %d based on /run/host/container-manager", type);
      goto out;
    }

  if (_srt_sysroot_load (sysroot, "/run/systemd/container",
                         SRT_RESOLVE_FLAGS_NONE,
                         NULL, &contents, NULL, NULL))
    {
      g_strchomp (contents);
      type = container_type_from_name (contents);
      g_debug ("Type %d based on /run/systemd/container", type);
      goto out;
    }

  if (_srt_sysroot_test (sysroot, "/.flatpak-info",
                         SRT_RESOLVE_FLAGS_MUST_BE_REGULAR, NULL))
    {
      type = SRT_CONTAINER_TYPE_FLATPAK;
      g_debug ("Flatpak based on /.flatpak-info");
      goto out;
    }

  if (_srt_sysroot_test (sysroot, "/run/pressure-vessel",
                         SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY, NULL))
    {
      type = SRT_CONTAINER_TYPE_PRESSURE_VESSEL;
      g_debug ("pressure-vessel based on /run/pressure-vessel");
      goto out;
    }

  if (_srt_sysroot_test (sysroot, "/.dockerenv", SRT_RESOLVE_FLAGS_NONE, NULL))
    {
      type = SRT_CONTAINER_TYPE_DOCKER;
      g_debug ("Docker based on /.dockerenv");
      goto out;
    }

  if (_srt_sysroot_test (sysroot, "/run/.containerenv",
                         SRT_RESOLVE_FLAGS_NONE, NULL))
    {
      type = SRT_CONTAINER_TYPE_PODMAN;
      g_debug ("Podman based on /run/.containerenv");
      goto out;
    }

  /* The canonical way to detect Snap is to look for $SNAP, but it's
   * plausible that someone sets that variable for an unrelated reason,
   * so check for more than one variable. This is the same thing
   * WebKitGTK does. */
  if (g_getenv("SNAP") != NULL
      && g_getenv("SNAP_NAME") != NULL
      && g_getenv("SNAP_REVISION") != NULL)
    {
      type = SRT_CONTAINER_TYPE_SNAP;
      g_debug ("Snap based on $SNAP, $SNAP_NAME, $SNAP_REVISION");
      /* The way Snap works means that most of the host filesystem is
       * available in the root directory; but we're not allowed to access
       * it, so it wouldn't be useful to set host_directory to "/". */
      goto out;
    }

  if (_srt_sysroot_load (sysroot, "/proc/1/cgroup",
                         SRT_RESOLVE_FLAGS_NONE,
                         NULL, &contents, NULL, NULL))
    {
      if (strstr (contents, "/docker/") != NULL)
        type = SRT_CONTAINER_TYPE_DOCKER;

      if (type != SRT_CONTAINER_TYPE_UNKNOWN)
        {
          g_debug ("Type %d based on /proc/1/cgroup", type);
          goto out;
        }

      g_clear_pointer (&contents, g_free);
    }

  if (host_directory != NULL)
    {
      g_debug ("Unknown container technology based on /run/host");
      type = SRT_CONTAINER_TYPE_UNKNOWN;
      goto out;
    }

  /* We haven't found any particular evidence of being in a container */
  g_debug ("Probably not a container");
  type = SRT_CONTAINER_TYPE_NONE;

out:
  if (type == SRT_CONTAINER_TYPE_FLATPAK)
    {
      g_autoptr(GKeyFile) info = NULL;
      g_autoptr(GError) local_error = NULL;
      g_autofree gchar *flatpak_info_content = NULL;
      gsize len;

      if (_srt_sysroot_load (sysroot, "/.flatpak-info",
                             SRT_RESOLVE_FLAGS_NONE,
                             NULL, &flatpak_info_content, &len, NULL))
        {
          info = g_key_file_new ();

          if (!g_key_file_load_from_data (info, flatpak_info_content, len,
                                          G_KEY_FILE_NONE, &local_error))
            g_debug ("Unable to load Flatpak instance info: %s", local_error->message);

          flatpak_version = g_key_file_get_string (info,
                                                   FLATPAK_METADATA_GROUP_INSTANCE,
                                                   FLATPAK_METADATA_KEY_FLATPAK_VERSION,
                                                   NULL);
        }
    }

  return _srt_container_info_new (type, flatpak_version, host_directory);
}

/**
 * srt_container_info_get_container_type:
 * @self: A SrtContainerInfo object
 *
 * If the program appears to be running in a container, return what sort
 * of container it is.
 *
 * Implementation of srt_system_info_get_container_type().
 *
 * Returns: A recognised container type, or %SRT_CONTAINER_TYPE_NONE
 *  if a container cannot be detected, or %SRT_CONTAINER_TYPE_UNKNOWN
 *  if unsure.
 */
SrtContainerType
srt_container_info_get_container_type (SrtContainerInfo *self)
{
  g_return_val_if_fail (SRT_IS_CONTAINER_INFO (self), SRT_CONTAINER_TYPE_UNKNOWN);
  return self->type;
}

/**
 * srt_container_info_get_container_host_directory:
 * @self: A SrtContainerInfo object
 *
 * If the program appears to be running in a container, return the
 * directory where host files can be found. For example, if this function
 * returns `/run/host`, it might be possible to load the host system's
 * `/usr/lib/os-release` by reading `/run/host/usr/lib/os-release`.
 *
 * The returned directory is usually not complete. For example,
 * in a Flatpak app, `/run/host` will sometimes contain the host system's
 * `/etc` and `/usr`, but only if suitable permissions flags are set.
 *
 * Implementation of srt_system_info_dup_container_host_directory().
 *
 * Returns: (type filename) (nullable): A path from which at least some
 *  host-system files can be loaded, typically `/run/host`, or %NULL if
 *  unknown or unavailable.
 */
const gchar *
srt_container_info_get_container_host_directory (SrtContainerInfo *self)
{
  g_return_val_if_fail (SRT_IS_CONTAINER_INFO (self), NULL);
  return self->host_directory;
}

/**
 * srt_container_info_get_flatpak_version:
 * @self: A SrtContainerInfo object
 *
 * If the program appears to be running in a container type
 * %SRT_CONTAINER_TYPE_FLATPAK, return the Flatpak version.
 *
 * Returns: (type filename) (nullable): A filename, or %NULL if the container
 *  type is not %SRT_CONTAINER_TYPE_FLATPAK or if it was not able to identify
 *  the Flatpak version.
 */
const gchar *
srt_container_info_get_flatpak_version (SrtContainerInfo *self)
{
  g_return_val_if_fail (SRT_IS_CONTAINER_INFO (self), NULL);

  if (self->type != SRT_CONTAINER_TYPE_FLATPAK)
    return NULL;

  return self->flatpak_version;
}
