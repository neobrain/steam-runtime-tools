/*
 * Copyright © 2019-2025 Collabora Ltd.
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

#include "steam-runtime-tools/graphics-drivers-openxr-1.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/graphics-drivers-internal.h"
#include "steam-runtime-tools/graphics-drivers-json-based-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"

/**
 * SECTION:graphics-drivers-openxr-1
 * @title: OpenXR 1 runtime enumeration
 * @short_description: Get information about the system's OpenXR runtimes
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtOpenXr1Runtime is an opaque object representing the metadata describing
 * an OpenXR runtime.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 */

/**
 * SrtOpenXr1Runtime:
 *
 * Opaque object representing an OpenXR runtime.
 */

struct _SrtOpenXr1RuntimeClass
{
  /*< private >*/
  SrtBaseJsonGraphicsModuleClass parent_class;
};

enum
{
  OPENXR_1_RUNTIME_PROP_0,
  OPENXR_1_RUNTIME_PROP_NAME,
  OPENXR_1_RUNTIME_PROP_JSON_ORIGIN,
  N_OPENXR_1_RUNTIME_PROPERTIES
};

G_DEFINE_TYPE (SrtOpenXr1Runtime, srt_openxr_1_runtime, SRT_TYPE_BASE_JSON_GRAPHICS_MODULE)

static void
srt_openxr_1_runtime_init (SrtOpenXr1Runtime *self)
{
}

static void
srt_openxr_1_runtime_get_property (GObject *object,
                                   guint prop_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
  SrtOpenXr1Runtime *self = SRT_OPENXR_1_RUNTIME (object);

  switch (prop_id)
    {
      case OPENXR_1_RUNTIME_PROP_NAME:
        g_value_set_string (value, self->parent.name);
        break;

      case OPENXR_1_RUNTIME_PROP_JSON_ORIGIN:
        g_value_set_string (value, self->json_origin);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_openxr_1_runtime_set_property (GObject *object,
                                   guint prop_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
  SrtOpenXr1Runtime *self = SRT_OPENXR_1_RUNTIME (object);

  switch (prop_id)
    {
      case OPENXR_1_RUNTIME_PROP_NAME:
        g_return_if_fail (self->parent.name == NULL);
        self->parent.name = g_value_dup_string (value);
        break;

      case OPENXR_1_RUNTIME_PROP_JSON_ORIGIN:
        g_return_if_fail (self->json_origin == NULL);
        self->json_origin = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_openxr_1_runtime_constructed (GObject *object)
{
  SrtBaseGraphicsModule *base = SRT_BASE_GRAPHICS_MODULE (object);
  SrtOpenXr1Runtime *self = SRT_OPENXR_1_RUNTIME (object);

  G_OBJECT_CLASS (srt_openxr_1_runtime_parent_class)->constructed (object);

  g_return_if_fail (self->json_origin != NULL);
  if (base->error != NULL)
    g_return_if_fail (base->library_path == NULL);
  else
    g_return_if_fail (base->library_path != NULL);
}

static void
srt_openxr_1_runtime_finalize (GObject *object)
{
  SrtOpenXr1Runtime *self = SRT_OPENXR_1_RUNTIME (object);

  g_free (self->json_origin);

  G_OBJECT_CLASS (srt_openxr_1_runtime_parent_class)->finalize (object);
}

static GParamSpec *openxr_1_runtime_properties[N_OPENXR_1_RUNTIME_PROPERTIES] = { NULL };

static void
srt_openxr_1_runtime_class_init (SrtOpenXr1RuntimeClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_openxr_1_runtime_get_property;
  object_class->set_property = srt_openxr_1_runtime_set_property;
  object_class->constructed = srt_openxr_1_runtime_constructed;
  object_class->finalize = srt_openxr_1_runtime_finalize;

  openxr_1_runtime_properties[OPENXR_1_RUNTIME_PROP_NAME] =
    g_param_spec_string ("name", "name",
                         "A user-facing name for this runtime.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  openxr_1_runtime_properties[OPENXR_1_RUNTIME_PROP_JSON_ORIGIN] =
    g_param_spec_string ("json-origin", "JSON origin",
                         "The original path to the runtime JSON as found"
                         "  during lookup, before symlink resolution.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_OPENXR_1_RUNTIME_PROPERTIES,
                                     openxr_1_runtime_properties);
}

/*
 * srt_openxr_1_runtime_new:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @name: (transfer none) (nullable): a user-facing name for this runtime
 * @library_path: (transfer none): the path to the library
 * @issues: problems with this runtime
 *
 * Returns: (transfer full): a new runtime
 */
SrtOpenXr1Runtime *
srt_openxr_1_runtime_new (const gchar *json_path,
                          const gchar *json_origin,
                          const gchar *name,
                          const gchar *library_path,
                          SrtLoadableIssues issues)
{
  g_return_val_if_fail (json_origin != NULL, NULL);
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_OPENXR_1_RUNTIME,
                       "name", name,
                       "json-path", json_path,
                       "json-origin", json_origin,
                       "library-path", library_path,
                       "issues", issues,
                       NULL);
}

/*
 * srt_openxr_1_runtime_new_error:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @issues: problems with this runtime
 * @error: (transfer none): Error that occurred when loading the runtime
 *
 * Returns: (transfer full): a new runtime
 */
SrtOpenXr1Runtime *
srt_openxr_1_runtime_new_error (const gchar *json_path,
                                const gchar *json_origin,
                                SrtLoadableIssues issues,
                                const GError *error)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (json_origin != NULL, NULL);
  g_return_val_if_fail (error != NULL, NULL);

  return g_object_new (SRT_TYPE_OPENXR_1_RUNTIME,
                       "error", error,
                       "json-path", json_path,
                       "json-origin", json_origin,
                       "issues", issues,
                       NULL);
}

/**
 * srt_openxr_1_runtime_check_error:
 * @self: The runtime
 * @error: Used to return details if the runtime description could not be loaded
 *
 * Check whether we failed to load the JSON describing this OpenXR runtime.
 * Note that this does not actually `dlopen()` the runtime itself.
 *
 * Returns: %TRUE if the JSON was loaded successfully
 */
gboolean
srt_openxr_1_runtime_check_error (SrtOpenXr1Runtime *self,
                                  GError **error)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return _srt_base_graphics_module_check_error (SRT_BASE_GRAPHICS_MODULE (self), error);
}

/**
 * srt_openxr_1_runtime_get_json_path:
 * @self: The runtime
 *
 * Return the canonicalized path to the JSON file representing this
 * runtime, i.e. a path pointing to the same file as
 * #SrtOpenXr1Runtime:json-origin / srt_openxr_1_runtime_get_json_origin(),
 * but with symlinks expanded. For instance, if #SrtOpenXr1Runtime:json-origin
 * is `/etc/xdg/openxr/1/active_runtime.json`, which is a symlink to
 * `/usr/share/openxr/1/openxr_monado.json`, then this would be the target of
 * the symlink `/usr/share/openxr/1/openxr_monado.json`.
 *
 * Returns: (type filename) (transfer none): #SrtOpenXr1Runtime:json-path
 */
const gchar *
srt_openxr_1_runtime_get_json_path (SrtOpenXr1Runtime *self)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), NULL);
  return self->parent.json_path;
}

/**
 * srt_openxr_1_runtime_get_json_origin:
 * @self: The runtime
 *
 * Return the absolute path to the JSON file representing this runtime, as
 * it was initially found during lookup and without any canonicalization.
 * For instance, if we found a runtime located at
 * `/etc/xdg/openxr/1/active_runtime.json`, which is a symlink to
 * `/usr/share/openxr/1/openxr_monado.json`, then this would be the symlink
 * `/etc/xdg/openxr/1/active_runtime.json` itself and not its target.
 *
 * Returns: (type filename) (transfer none): #SrtOpenXr1Runtime:json-origin
 */
const gchar *
srt_openxr_1_runtime_get_json_origin (SrtOpenXr1Runtime *self)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), NULL);
  return self->json_origin;
}

/**
 * srt_openxr_1_runtime_get_library_path:
 * @self: The runtime
 *
 * Return the library path for this runtime. It is either an absolute path,
 * a path relative to srt_openxr_1_runtime_get_json_path() containing at least one
 * directory separator (slash), or a basename to be loaded from the
 * shared library search path.
 *
 * If the JSON description for this runtime could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtOpenXr1Runtime:library-path
 */
const gchar *
srt_openxr_1_runtime_get_library_path (SrtOpenXr1Runtime *self)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), NULL);
  return SRT_BASE_GRAPHICS_MODULE (self)->library_path;
}

/**
 * srt_openxr_1_runtime_get_name:
 * @self: The runtime
 *
 * Return the optional user-facing name for this runtime.
 *
 * If the JSON description for this runtime could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtOpenXr1Runtime:name
 */
const gchar *
srt_openxr_1_runtime_get_name (SrtOpenXr1Runtime *self)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), NULL);
  return self->parent.name;
}

/**
 * srt_openxr_1_runtime_get_issues:
 * @self: The runtime
 *
 * Return the problems found when parsing and loading @self.
 *
 * Returns: A bitfield containing problems, or %SRT_LOADABLE_ISSUES_NONE
 *  if no problems were found
 */
SrtLoadableIssues
srt_openxr_1_runtime_get_issues (SrtOpenXr1Runtime *self)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), SRT_LOADABLE_ISSUES_UNKNOWN);
  return SRT_BASE_GRAPHICS_MODULE (self)->issues;
}

/**
 * srt_openxr_1_runtime_resolve_library_path:
 * @self: A runtime
 *
 * Return the path that can be passed to `dlopen()` for this runtime.
 *
 * If srt_openxr_1_runtime_get_library_path() is a relative path, return the
 * absolute path that is the result of interpreting it relative to
 * srt_openxr_1_runtime_get_json_path(). Otherwise return a copy of
 * srt_openxr_1_runtime_get_library_path().
 *
 * The result is either the basename of a shared library (to be found
 * relative to some directory listed in `$LD_LIBRARY_PATH`, `/etc/ld.so.conf`,
 * `/etc/ld.so.conf.d` or the hard-coded library search path), or an
 * absolute path.
 *
 * Returns: (transfer full) (type filename) (nullable): A copy
 *  of #SrtOpenXr1Runtime:resolved-library-path. Free with g_free().
 */
gchar *
srt_openxr_1_runtime_resolve_library_path (SrtOpenXr1Runtime *self)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), NULL);
  return _srt_base_graphics_module_resolve_library_path (SRT_BASE_GRAPHICS_MODULE (self));
}

/**
 * srt_openxr_1_runtime_write_to_file:
 * @self: A runtime
 * @path: (type filename): A filename
 * @error: Used to describe the error on failure
 *
 * Serialize @self to the given JSON file.
 *
 * Returns: %TRUE on success
 */
gboolean
srt_openxr_1_runtime_write_to_file (SrtOpenXr1Runtime *self,
                                    const char *path,
                                    GError **error)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return _srt_base_json_graphics_module_write_to_file (&self->parent,
                                                       path,
                                                       SRT_TYPE_OPENXR_1_RUNTIME,
                                                       error);
}

/**
 * srt_openxr_1_runtime_new_replace_library_path:
 * @self: A runtime
 * @path: (type filename) (transfer none): A path
 *
 * Return a copy of @self with the srt_openxr_1_runtime_get_library_path()
 * changed to @path. For example, this is useful when setting up a
 * container where the underlying shared object will be made available
 * at a different absolute path.
 *
 * If @self is in an error state, this returns a new reference to @self.
 *
 * Note that @self issues are copied to the new #SrtOpenXr1Runtime copy, including
 * the eventual %SRT_LOADABLE_ISSUES_DUPLICATED.
 *
 * Returns: (transfer full): A new reference to a #SrtOpenXr1Runtime. Free with
 *  g_object_unref().
 */
SrtOpenXr1Runtime *
srt_openxr_1_runtime_new_replace_library_path (SrtOpenXr1Runtime *self,
                                               const char *path)
{
  SrtBaseGraphicsModule *base;
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), NULL);

  base = SRT_BASE_GRAPHICS_MODULE (self);

  if (base->error != NULL)
    return g_object_ref (self);

  return srt_openxr_1_runtime_new (self->parent.json_path,
                                   self->json_origin,
                                   self->parent.name,
                                   path,
                                   base->issues);
}

static GHashTable *
load_functions_from_json (JsonObject *object)
{
  GHashTable *functions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 g_free, g_free);
  g_autoptr(GList) members = NULL;
  GList *member = NULL;

  members = json_object_get_members (object);
  for (member = members; member != NULL; member = member->next)
    {
      const char *key = member->data;
      const char *value;

      value = json_object_get_string_member (object, key);
      g_warn_if_fail (value != NULL);

      g_hash_table_insert (functions, g_strdup (key), g_strdup (value));
    }

  return functions;
}

SrtOpenXr1Runtime *
srt_openxr_1_runtime_load_json (const gchar *json_path,
                                const gchar *json_origin,
                                const gchar *library_path,
                                JsonObject *runtime_object,
                                SrtLoadableIssues issues)
{
  SrtOpenXr1Runtime *rt = NULL;
  JsonObject *functions_obj = NULL;
  const char *name;

  name = json_object_get_string_member_with_default (runtime_object, "name", NULL);

  rt = srt_openxr_1_runtime_new (json_path, json_origin,
                                 name, library_path, issues);

  if (json_object_has_member (runtime_object, "functions"))
    functions_obj = json_object_get_object_member (runtime_object, "functions");
  if (functions_obj != NULL)
    rt->parent.functions = load_functions_from_json (functions_obj);

  return g_steal_pointer (&rt);
}
