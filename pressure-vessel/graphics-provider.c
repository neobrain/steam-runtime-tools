/*
 * Copyright © 2020-2021 Collabora Ltd.
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

#include "graphics-provider.h"

#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/system-info-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include "utils.h"

enum {
  PROP_0,
  PROP_IN_CURRENT_NS,
  PROP_PATH_IN_CONTAINER_NS,
  PROP_USE_SRT_HELPERS,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

G_DEFINE_TYPE (PvGraphicsProvider, pv_graphics_provider, G_TYPE_OBJECT)

static void
pv_graphics_provider_init (PvGraphicsProvider *self)
{
}

static void
pv_graphics_provider_get_property (GObject *object,
                                   guint prop_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
  PvGraphicsProvider *self = PV_GRAPHICS_PROVIDER (object);

  switch (prop_id)
    {
      case PROP_IN_CURRENT_NS:
        g_value_set_object (value, self->in_current_ns);
        break;

      case PROP_PATH_IN_CONTAINER_NS:
        g_value_set_string (value, self->path_in_container_ns);
        break;

      case PROP_USE_SRT_HELPERS:
        g_value_set_boolean (value, self->use_srt_helpers);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
pv_graphics_provider_set_property (GObject *object,
                                   guint prop_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
  PvGraphicsProvider *self = PV_GRAPHICS_PROVIDER (object);

  switch (prop_id)
    {
      case PROP_IN_CURRENT_NS:
        /* Construct-only */
        g_return_if_fail (self->in_current_ns == NULL);
        self->in_current_ns = g_value_dup_object (value);
        break;

      case PROP_PATH_IN_CONTAINER_NS:
        /* Construct-only */
        g_return_if_fail (self->path_in_container_ns == NULL);
        self->path_in_container_ns = g_value_dup_string (value);
        break;

      case PROP_USE_SRT_HELPERS:
        /* Construct-only */
        self->use_srt_helpers = g_value_get_boolean (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
pv_graphics_provider_constructed (GObject *object)
{
  PvGraphicsProvider *self = PV_GRAPHICS_PROVIDER (object);

  G_OBJECT_CLASS (pv_graphics_provider_parent_class)->constructed (object);

  g_return_if_fail (SRT_IS_SYSROOT (self->in_current_ns));
  g_return_if_fail (self->in_current_ns->path != NULL);
  g_return_if_fail (self->in_current_ns->fd >= 0);
  g_return_if_fail (self->path_in_container_ns != NULL);

  /* Path that, when resolved in the host namespace, points to us */
  self->path_in_host_ns = pv_current_namespace_path_to_host_path (self->in_current_ns->path);
}

static void
pv_graphics_provider_dispose (GObject *object)
{
  PvGraphicsProvider *self = PV_GRAPHICS_PROVIDER (object);

  g_clear_object (&self->in_current_ns);

  G_OBJECT_CLASS (pv_graphics_provider_parent_class)->dispose (object);
}

static void
pv_graphics_provider_finalize (GObject *object)
{
  PvGraphicsProvider *self = PV_GRAPHICS_PROVIDER (object);

  g_free (self->path_in_host_ns);
  g_free (self->path_in_container_ns);

  G_OBJECT_CLASS (pv_graphics_provider_parent_class)->finalize (object);
}

static void
pv_graphics_provider_class_init (PvGraphicsProviderClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = pv_graphics_provider_get_property;
  object_class->set_property = pv_graphics_provider_set_property;
  object_class->constructed = pv_graphics_provider_constructed;
  object_class->dispose = pv_graphics_provider_dispose;
  object_class->finalize = pv_graphics_provider_finalize;

  properties[PROP_IN_CURRENT_NS] =
    g_param_spec_object ("in-current-ns", "Representation in current namespace",
                         ("Path and file descriptor for this provider in the "
                          "current execution environment"),
                         SRT_TYPE_SYSROOT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_PATH_IN_CONTAINER_NS] =
    g_param_spec_string ("path-in-container-ns", "Path in container namespace",
                         ("Path to the graphics provider in the container "
                          "namespace, typically /run/host"),
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_USE_SRT_HELPERS] =
    g_param_spec_boolean ("use-srt-helpers", "Use SRT helpers?",
                          "TRUE to use the SRT architecture-specific helpers, or"
                          "FALSE to assume hard-coded paths instead",
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/*
 * search_paths (nullable): List of colon separated paths where the program
 *  should be searched into, in addition to the hardcoded common directories.
 */
gchar *
pv_graphics_provider_search_in_path_and_bin (PvGraphicsProvider *self,
                                             const gchar *search_paths,
                                             const gchar *program_name)
{
  g_autofree gchar *cwd = g_get_current_dir ();
  g_autoptr(GPtrArray) paths_array = NULL;
  g_auto(GStrv) paths = NULL;
  gsize i;

  g_return_val_if_fail (PV_IS_GRAPHICS_PROVIDER (self), NULL);
  g_return_val_if_fail (program_name != NULL, NULL);
  g_return_val_if_fail (strchr (program_name, G_DIR_SEPARATOR) == NULL, NULL);

  /* Start with a large enough array to avoid frequent reallocations */
  paths_array = g_ptr_array_sized_new (16);

  if (search_paths != NULL)
    {
      paths = g_strsplit (search_paths, ":", -1);
      for (i = 0; paths[i] != NULL; i++)
        g_ptr_array_add (paths_array, (gpointer) paths[i]);
    }

  /* Hardcoded common binary paths */
  g_ptr_array_add (paths_array, (gpointer) "/usr/bin");
  g_ptr_array_add (paths_array, (gpointer) "/bin");
  g_ptr_array_add (paths_array, (gpointer) "/usr/sbin");
  g_ptr_array_add (paths_array, (gpointer) "/sbin");

  for (i = 0; i < paths_array->len; i++)
    {
      g_autofree gchar *test_path = NULL;
      const gchar *path = g_ptr_array_index(paths_array, i);
      if (strstr (path, "/.linuxbrew/") != NULL)
        {
          g_debug ("Skipping over Homebrew's \"%s\" from PATH", path);
          continue;
        }

      if (!g_path_is_absolute (path))
        test_path = g_build_filename (cwd, path, program_name, NULL);
      else
        test_path = g_build_filename (path, program_name, NULL);

      if (_srt_file_test_in_sysroot (self->in_current_ns->path,
                                     self->in_current_ns->fd,
                                     test_path, G_FILE_TEST_IS_EXECUTABLE))
        return g_steal_pointer (&test_path);
    }

  return NULL;
}

PvGraphicsProvider *
pv_graphics_provider_new (const char *path_in_current_ns,
                          const char *path_in_container_ns,
                          gboolean use_srt_helpers,
                          GError **error)
{
  g_autoptr(SrtSysroot) sysroot = NULL;

  g_return_val_if_fail (path_in_current_ns != NULL, NULL);
  g_return_val_if_fail (path_in_container_ns != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (g_str_equal (path_in_current_ns, "/"))
    sysroot = _srt_sysroot_new_direct (error);
  else
    sysroot = _srt_sysroot_new (path_in_current_ns, error);

  if (sysroot == NULL)
    return NULL;

  return g_object_new (PV_TYPE_GRAPHICS_PROVIDER,
                       "in-current-ns", sysroot,
                       "path-in-container-ns", path_in_container_ns,
                       "use-srt-helpers", use_srt_helpers,
                       NULL);
}

/*
 * Create a new SrtSystemInfo, suitable for use in a separate thread.
 */
SrtSystemInfo *
pv_graphics_provider_create_system_info (PvGraphicsProvider *self)
{
  g_autoptr(SrtSystemInfo) system_info = NULL;
  SrtCheckFlags flags = SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS
                        | SRT_CHECK_FLAGS_SKIP_EXTRAS;

  g_return_val_if_fail (PV_IS_GRAPHICS_PROVIDER (self), NULL);

  if (!self->use_srt_helpers)
    flags |= SRT_CHECK_FLAGS_NO_HELPERS;

  system_info = srt_system_info_new (NULL);
  _srt_system_info_set_sysroot (system_info, self->in_current_ns);
  _srt_system_info_set_check_flags (system_info, flags);
  return g_steal_pointer (&system_info);
}
