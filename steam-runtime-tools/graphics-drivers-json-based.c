/*
 * Copyright © 2019-2022 Collabora Ltd.
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

#include "steam-runtime-tools/glib-backports-internal.h"

#include "steam-runtime-tools/graphics-drivers-json-based-internal.h"
#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"
#include "steam-runtime-tools/library-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"

enum
{
  BASE_PROP_0,
  BASE_PROP_JSON_PATH,
  N_BASE_PROPERTIES
};

static GParamSpec *base_properties[N_BASE_PROPERTIES] = { NULL };

G_DEFINE_TYPE (SrtBaseJsonGraphicsModule,
               _srt_base_json_graphics_module,
               SRT_TYPE_BASE_GRAPHICS_MODULE)

static void
_srt_base_json_graphics_module_init (SrtBaseJsonGraphicsModule *self)
{
}

static void
srt_base_json_graphics_module_constructed (GObject *object)
{
  SrtBaseJsonGraphicsModule *self = SRT_BASE_JSON_GRAPHICS_MODULE (object);

  G_OBJECT_CLASS (_srt_base_json_graphics_module_parent_class)->constructed (object);

  g_return_if_fail (self->json_path != NULL);
  g_return_if_fail (g_path_is_absolute (self->json_path));
}

static void
srt_base_json_graphics_module_get_property (GObject *object,
                                            guint prop_id,
                                            GValue *value,
                                            GParamSpec *pspec)
{
  SrtBaseJsonGraphicsModule *self = SRT_BASE_JSON_GRAPHICS_MODULE (object);

  switch (prop_id)
    {
      case BASE_PROP_JSON_PATH:
        g_value_set_string (value, self->json_path);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_base_json_graphics_module_set_property (GObject *object,
                                            guint prop_id,
                                            const GValue *value,
                                            GParamSpec *pspec)
{
  SrtBaseJsonGraphicsModule *self = SRT_BASE_JSON_GRAPHICS_MODULE (object);
  const char *tmp;

  switch (prop_id)
    {
      case BASE_PROP_JSON_PATH:
        g_return_if_fail (self->json_path == NULL);
        tmp = g_value_get_string (value);
        self->json_path = g_canonicalize_filename (tmp, NULL);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_base_json_graphics_module_finalize (GObject *object)
{
  SrtBaseJsonGraphicsModule *self = SRT_BASE_JSON_GRAPHICS_MODULE (object);

  g_clear_pointer (&self->api_version, g_free);
  g_clear_pointer (&self->json_path, g_free);
  g_clear_pointer (&self->library_arch, g_free);
  g_clear_pointer (&self->file_format_version, g_free);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->type, g_free);
  g_clear_pointer (&self->implementation_version, g_free);
  g_clear_pointer (&self->description, g_free);
  g_clear_pointer (&self->component_layers, g_strfreev);
  g_clear_pointer (&self->functions, g_hash_table_unref);
  g_list_free_full (g_steal_pointer (&self->instance_extensions), instance_extension_free);
  g_clear_pointer (&self->pre_instance_functions, g_hash_table_unref);
  g_list_free_full (g_steal_pointer (&self->device_extensions), device_extension_free);
  g_clear_pointer (&self->enable_env_var.name, g_free);
  g_clear_pointer (&self->enable_env_var.value, g_free);
  g_clear_pointer (&self->disable_env_var.name, g_free);
  g_clear_pointer (&self->disable_env_var.value, g_free);
  g_clear_pointer (&self->original_json, g_free);

  G_OBJECT_CLASS (_srt_base_json_graphics_module_parent_class)->finalize (object);
}

SrtBaseJsonGraphicsModule *
_srt_base_json_graphics_module_new_error (GType type,
                                          const gchar *json_path,
                                          SrtLoadableIssues issues,
                                          const GError *error)
{
  g_return_val_if_fail (g_type_is_a (type, SRT_TYPE_BASE_JSON_GRAPHICS_MODULE),
                        NULL);
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (error != NULL, NULL);

  return g_object_new (type,
                       "error", error,
                       "json-path", json_path,
                       "issues", issues,
                       NULL);
}

/*
 * See srt_egl_icd_resolve_library_path(),
 * srt_vulkan_icd_resolve_library_path() or
 * srt_vulkan_layer_resolve_library_path()
 */
static gchar *
srt_base_json_graphics_module_resolve_library_path (SrtBaseGraphicsModule *base)
{
  SrtBaseJsonGraphicsModule *self = SRT_BASE_JSON_GRAPHICS_MODULE (base);
  gchar *dir;
  gchar *ret;

  /*
   * In Vulkan, this function behaves according to the specification:
   *
   * The "library_path" specifies either a filename, a relative pathname,
   * or a full pathname to an ICD shared library file. If "library_path"
   * specifies a relative pathname, it is relative to the path of the
   * JSON manifest file. If "library_path" specifies a filename, the
   * library must live in the system's shared object search path.
   * — https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198.1/docs/LoaderDriverInterface.md#driver-manifest-file-format
   * — https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198.1/docs/LoaderLayerInterface.md#layer-manifest-file-format
   *
   * In GLVND, EGL ICDs with relative pathnames are currently passed
   * directly to dlopen(), which will interpret them as relative to
   * the current working directory - but upstream acknowledge in
   * https://github.com/NVIDIA/libglvnd/issues/187 that this is not
   * actually very useful, and have indicated that they would consider
   * a patch to give it the same behaviour as Vulkan instead.
   */

  if (base->library_path == NULL)
    return NULL;

  if (base->library_path[0] == '/')
    return g_strdup (base->library_path);

  if (strchr (base->library_path, '/') == NULL)
    return g_strdup (base->library_path);

  dir = g_path_get_dirname (self->json_path);
  ret = g_build_filename (dir, base->library_path, NULL);
  g_free (dir);
  g_return_val_if_fail (g_path_is_absolute (ret), ret);
  return ret;
}

static void
_srt_base_json_graphics_module_class_init (SrtBaseJsonGraphicsModuleClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  SrtBaseGraphicsModuleClass *base_class = SRT_BASE_GRAPHICS_MODULE_CLASS (cls);

  object_class->constructed = srt_base_json_graphics_module_constructed;
  object_class->get_property = srt_base_json_graphics_module_get_property;
  object_class->set_property = srt_base_json_graphics_module_set_property;
  object_class->finalize = srt_base_json_graphics_module_finalize;

  base_class->resolve_library_path = srt_base_json_graphics_module_resolve_library_path;

  base_properties[BASE_PROP_JSON_PATH] =
    g_param_spec_string ("json-path", "JSON path",
                         "Absolute path to JSON file describing this module. "
                         "If examining a sysroot, this path is set as though "
                         "the sysroot was the root directory. "
                         "When constructing the object, a relative path can "
                         "be given: it will be converted to an absolute path.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_BASE_PROPERTIES,
                                     base_properties);
}

/* See srt_egl_icd_write_to_file(), srt_vulkan_icd_write_to_file() and
 * srt_vulkan_layer_write_to_file() */
gboolean
_srt_base_json_graphics_module_write_to_file (SrtBaseJsonGraphicsModule *self,
                                              const char *path,
                                              GType which,
                                              GError **error)
{
  SrtBaseGraphicsModule *base = &self->parent;
  JsonBuilder *builder;
  JsonGenerator *generator;
  JsonNode *root;
  gchar *json_output;
  gboolean ret = FALSE;
  const gchar *member;
  gpointer key;
  gpointer value;
  const GList *l;

  /* EGL external platforms have { "ICD": ... } in their JSON file,
   * even though you might have expected a different string. */
  if (which == SRT_TYPE_EGL_ICD
      || which == SRT_TYPE_VULKAN_ICD
      || which == SRT_TYPE_EGL_EXTERNAL_PLATFORM)
    member = "ICD";
  else if (which == SRT_TYPE_VULKAN_LAYER)
    member = "layer";
  else if (which == SRT_TYPE_OPENXR_1_RUNTIME)
    member = _SRT_GRAPHICS_MANIFEST_MEMBER_OPENXR_1_RUNTIME;
  else
    g_return_val_if_reached (FALSE);

  if (!_srt_base_graphics_module_check_error (base, error))
    {
      g_prefix_error (error,
                      "Cannot save %s metadata to file because it is invalid: ",
                      member);
      return FALSE;
    }

  if (self->original_json != NULL)
    {
      ret = g_file_set_contents (path, self->original_json, -1, error);

      if (!ret)
        g_prefix_error (error, "Cannot save %s metadata to file :", member);

      return ret;
    }

  builder = json_builder_new ();
  json_builder_begin_object (builder);
    {
      if (which == SRT_TYPE_VULKAN_ICD)
        {
          json_builder_set_member_name (builder, "file_format_version");

          /* We parse and store all the information defined in file format
           * version 1.0.0 and 1.0.1. We use the file format 1.0.1 only if
           * either the field "is_portability_driver" or "library_arch" are
           * set, because those are the only changes that have been
           * introduced with 1.0.1. */
          if (self->portability_driver || self->library_arch)
            json_builder_add_string_value (builder, "1.0.1");
          else
            json_builder_add_string_value (builder, "1.0.0");

           json_builder_set_member_name (builder, member);
           json_builder_begin_object (builder);
            {
              json_builder_set_member_name (builder, "library_path");
              json_builder_add_string_value (builder, base->library_path);

              json_builder_set_member_name (builder, "api_version");
              json_builder_add_string_value (builder, self->api_version);

              if (self->library_arch)
                {
                  json_builder_set_member_name (builder, "library_arch");
                  json_builder_add_string_value (builder, self->library_arch);
                }

              if (self->portability_driver)
                {
                  json_builder_set_member_name (builder, "is_portability_driver");
                  json_builder_add_boolean_value (builder, self->portability_driver);
                }
            }
           json_builder_end_object (builder);
        }
      else if (which == SRT_TYPE_EGL_ICD
               || which == SRT_TYPE_EGL_EXTERNAL_PLATFORM)
        {
          /* We parse and store all the information defined in file format
           * version 1.0.0, but nothing beyond that, so we use this version
           * in our output instead of quoting whatever was in the input. */
          json_builder_set_member_name (builder, "file_format_version");
          json_builder_add_string_value (builder, "1.0.0");

           json_builder_set_member_name (builder, member);
           json_builder_begin_object (builder);
            {
              json_builder_set_member_name (builder, "library_path");
              json_builder_add_string_value (builder, base->library_path);
            }
           json_builder_end_object (builder);
        }
      else if (which == SRT_TYPE_VULKAN_LAYER)
        {
          json_builder_set_member_name (builder, "file_format_version");
          /* In the Vulkan layer specs the file format version is a required field.
           * However it might happen that we are not aware of its value, e.g. when we
           * parse an s-r-s-i report. Because of that, if the file format version info
           * is missing, we don't consider it a fatal error and we just set it to the
           * lowest version that is required, based on the fields we have. */
          if (self->file_format_version == NULL)
            {
              if (self->library_arch != NULL)
                json_builder_add_string_value (builder, "1.2.1");
              else if (self->pre_instance_functions != NULL)
                json_builder_add_string_value (builder, "1.1.2");
              else if (self->component_layers != NULL && self->component_layers[0] != NULL)
                json_builder_add_string_value (builder, "1.1.1");
              else
                json_builder_add_string_value (builder, "1.1.0");
            }
          else
            {
              json_builder_add_string_value (builder, self->file_format_version);
            }

          json_builder_set_member_name (builder, "layer");
          json_builder_begin_object (builder);
            {
              json_builder_set_member_name (builder, "name");
              json_builder_add_string_value (builder, self->name);

              json_builder_set_member_name (builder, "type");
              json_builder_add_string_value (builder, self->type);

              if (base->library_path != NULL)
                {
                  json_builder_set_member_name (builder, "library_path");
                  json_builder_add_string_value (builder, base->library_path);
                }

              if (self->library_arch != NULL)
                {
                  json_builder_set_member_name (builder, "library_arch");
                  json_builder_add_string_value (builder, self->library_arch);
                }

              json_builder_set_member_name (builder, "api_version");
              json_builder_add_string_value (builder, self->api_version);

              json_builder_set_member_name (builder, "implementation_version");
              json_builder_add_string_value (builder, self->implementation_version);

              json_builder_set_member_name (builder, "description");
              json_builder_add_string_value (builder, self->description);

              _srt_json_builder_add_strv_value (builder, "component_layers",
                                                (const gchar * const *) self->component_layers,
                                                FALSE);

              if (self->functions != NULL)
                {
                  g_auto(SrtHashTableIter) iter = SRT_HASH_TABLE_ITER_CLEARED;

                  json_builder_set_member_name (builder, "functions");
                  json_builder_begin_object (builder);
                  _srt_hash_table_iter_init_sorted (&iter,
                                                    self->functions,
                                                    _srt_generic_strcmp0);
                  while (_srt_hash_table_iter_next (&iter, &key, &value))
                    {
                      json_builder_set_member_name (builder, key);
                      json_builder_add_string_value (builder, value);
                    }
                  json_builder_end_object (builder);
                }

              if (self->pre_instance_functions != NULL)
                {
                  g_auto(SrtHashTableIter) iter = SRT_HASH_TABLE_ITER_CLEARED;

                  json_builder_set_member_name (builder, "pre_instance_functions");
                  json_builder_begin_object (builder);
                  _srt_hash_table_iter_init_sorted (&iter,
                                                    self->pre_instance_functions,
                                                    _srt_generic_strcmp0);
                  while (_srt_hash_table_iter_next (&iter, &key, &value))
                    {
                      json_builder_set_member_name (builder, key);
                      json_builder_add_string_value (builder, value);
                    }
                  json_builder_end_object (builder);
                }

              if (self->instance_extensions != NULL)
                {
                  json_builder_set_member_name (builder, "instance_extensions");
                  json_builder_begin_array (builder);
                  for (l = self->instance_extensions; l != NULL; l = l->next)
                    {
                      InstanceExtension *ie = l->data;
                      json_builder_begin_object (builder);
                      json_builder_set_member_name (builder, "name");
                      json_builder_add_string_value (builder, ie->name);
                      json_builder_set_member_name (builder, "spec_version");
                      json_builder_add_string_value (builder, ie->spec_version);
                      json_builder_end_object (builder);
                    }
                  json_builder_end_array (builder);
                }

              if (self->device_extensions != NULL)
                {
                  json_builder_set_member_name (builder, "device_extensions");
                  json_builder_begin_array (builder);
                  for (l = self->device_extensions; l != NULL; l = l->next)
                    {
                      DeviceExtension *de = l->data;
                      json_builder_begin_object (builder);
                      json_builder_set_member_name (builder, "name");
                      json_builder_add_string_value (builder, de->name);
                      json_builder_set_member_name (builder, "spec_version");
                      json_builder_add_string_value (builder, de->spec_version);
                      _srt_json_builder_add_strv_value (builder, "entrypoints",
                                                        (const gchar * const *) de->entrypoints,
                                                        FALSE);
                      json_builder_end_object (builder);
                    }
                  json_builder_end_array (builder);
                }

              if (self->enable_env_var.name != NULL)
                {
                  json_builder_set_member_name (builder, "enable_environment");
                  json_builder_begin_object (builder);
                  json_builder_set_member_name (builder, self->enable_env_var.name);
                  json_builder_add_string_value (builder, self->enable_env_var.value);
                  json_builder_end_object (builder);
                }

              if (self->disable_env_var.name != NULL)
                {
                  json_builder_set_member_name (builder, "disable_environment");
                  json_builder_begin_object (builder);
                  json_builder_set_member_name (builder, self->disable_env_var.name);
                  json_builder_add_string_value (builder, self->disable_env_var.value);
                  json_builder_end_object (builder);
                }
            }
          json_builder_end_object (builder);
        }
      else if (which == SRT_TYPE_OPENXR_1_RUNTIME)
        {
          /* We parse and store all the information defined in file format
           * version 1.0.0, but nothing beyond that, so we use this version
           * in our output instead of quoting whatever was in the input. */
          json_builder_set_member_name (builder, "file_format_version");
          json_builder_add_string_value (builder, "1.0.0");

          json_builder_set_member_name (builder, member);
          json_builder_begin_object (builder);
            {
              json_builder_set_member_name (builder, "library_path");
              json_builder_add_string_value (builder, base->library_path);

              if (self->name != NULL)
                {
                  json_builder_set_member_name (builder, "name");
                  json_builder_add_string_value (builder, self->name);
                }

              if (self->functions != NULL)
                {
                  g_auto(SrtHashTableIter) iter = SRT_HASH_TABLE_ITER_CLEARED;

                  json_builder_set_member_name (builder, "functions");
                  json_builder_begin_object (builder);
                  _srt_hash_table_iter_init_sorted (&iter,
                                                    self->functions,
                                                    _srt_generic_strcmp0);
                  while (_srt_hash_table_iter_next (&iter, &key, &value))
                    {
                      json_builder_set_member_name (builder, key);
                      json_builder_add_string_value (builder, value);
                    }
                  json_builder_end_object (builder);
                }
            }
          json_builder_end_object (builder);
        }
    }
  json_builder_end_object (builder);

  root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);
  json_generator_set_pretty (generator, TRUE);
  json_output = json_generator_to_data (generator, NULL);

  ret = g_file_set_contents (path, json_output, -1, error);

  if (!ret)
    g_prefix_error (error, "Cannot save %s metadata to file :", member);

  g_free (json_output);
  g_object_unref (generator);
  json_node_free (root);
  g_object_unref (builder);
  return ret;
}

/*
 * Use 'inspect-library' to get the absolute path of @library_path,
 * resolving also its eventual symbolic links.
 */
static gchar *
_get_library_canonical_path (SrtSubprocessRunner *runner,
                             const char *multiarch,
                             const gchar *library_path)
{
  g_autoptr(SrtLibrary) library = NULL;
  _srt_check_library_presence (runner, library_path, multiarch, NULL,
                               NULL, SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS,
                               SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN, &library);

  /* Use realpath() because the path might still be a symbolic link or it can
   * contains ./ or ../
   * The absolute path is gathered using 'inspect-library', so we don't have
   * to worry about still having special tokens, like ${LIB}, in the path. */
  return realpath (srt_library_get_absolute_path (library), NULL);
}

static void
_update_duplicated_value (SrtBaseGraphicsModule *self,
                          GHashTable *loadable_seen,
                          const gchar *key)
{
  if (key == NULL)
    return;

  if (g_hash_table_contains (loadable_seen, key))
    {
      SrtBaseGraphicsModule *other = g_hash_table_lookup (loadable_seen, key);

      other->issues |= SRT_LOADABLE_ISSUES_DUPLICATED;
      self->issues |= SRT_LOADABLE_ISSUES_DUPLICATED;
    }
  else
    {
      g_hash_table_replace (loadable_seen,
                            g_strdup (key),
                            self);
    }
}

/*
 * @runner: The execution environment
 * @loadable: (inout) (element-type SrtBaseJsonGraphicsModule):
 *
 * Iterate the provided @loadable list and update their "issues" property
 * to include the SRT_LOADABLE_ISSUES_DUPLICATED bit if they are duplicated.
 * Two ICDs are considered to be duplicated if they have the same absolute
 * library path.
 * Two Vulkan layers are considered to be duplicated if they have the same
 * name and absolute library path.
 */
void
_srt_loadable_flag_duplicates (GType which,
                               SrtSubprocessRunner *runner,
                               const char * const *multiarch_tuples,
                               GList *loadable)
{
  g_autoptr(GHashTable) loadable_seen = NULL;
  gsize i;
  GList *l;

  g_return_if_fail (which == SRT_TYPE_VULKAN_ICD
                    || which == SRT_TYPE_EGL_ICD
                    || which == SRT_TYPE_EGL_EXTERNAL_PLATFORM
                    || which == SRT_TYPE_VULKAN_LAYER
                    || which == SRT_TYPE_OPENXR_1_RUNTIME);

  loadable_seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (l = loadable; l != NULL; l = l->next)
    {
      SrtBaseGraphicsModule *module = l->data;
      g_autofree gchar *resolved_path = NULL;
      const gchar *name = NULL;

      g_assert (G_TYPE_CHECK_INSTANCE_TYPE (module, which));
      resolved_path = _srt_base_graphics_module_resolve_library_path (module);

      if (which == SRT_TYPE_VULKAN_ICD
          || which == SRT_TYPE_EGL_ICD
          || which == SRT_TYPE_EGL_EXTERNAL_PLATFORM
          || which == SRT_TYPE_OPENXR_1_RUNTIME)
        {
          if (resolved_path == NULL)
            continue;

          if (multiarch_tuples == NULL)
            {
              /* If we don't have the multiarch_tuples just use the
               * resolved_path as is */
              _update_duplicated_value (module, loadable_seen, resolved_path);
            }
          else
            {
              for (i = 0; multiarch_tuples[i] != NULL; i++)
                {
                  g_autofree gchar *canonical_path = NULL;
                  canonical_path = _get_library_canonical_path (runner,
                                                                multiarch_tuples[i],
                                                                resolved_path);

                  if (canonical_path == NULL)
                    {
                      /* Either the library is of a different ELF class or it is missing */
                      g_debug ("Unable to get the absolute path of \"%s\" via inspect-library",
                               resolved_path);
                      continue;
                    }

                  _update_duplicated_value (module, loadable_seen, canonical_path);
                }
            }
        }
      else if (which == SRT_TYPE_VULKAN_LAYER)
        {
          name = srt_vulkan_layer_get_name (l->data);

          if (resolved_path == NULL && name == NULL)
            continue;

          if (multiarch_tuples == NULL || resolved_path == NULL)
            {
              g_autofree gchar *hash_key = NULL;
              /* We need a key for the hashtable that includes the name and
               * the path in it. We use '//' as a separator between the two
               * values, because we don't expect to have '//' in the
               * path, nor in the name. In the very unlikely event where
               * a collision happens, we will just consider two layers
               * as duplicated when in reality they weren't. */
              hash_key = g_strdup_printf ("%s//%s", name, resolved_path);
              _update_duplicated_value (module, loadable_seen, hash_key);
            }
          else
            {
              for (i = 0; multiarch_tuples[i] != NULL; i++)
                {
                  g_autofree gchar *canonical_path = NULL;
                  g_autofree gchar *hash_key = NULL;
                  canonical_path = _get_library_canonical_path (runner,
                                                                multiarch_tuples[i],
                                                                resolved_path);

                  if (canonical_path == NULL)
                    {
                      /* Either the library is of a different ELF class or it is missing */
                      g_debug ("Unable to get the absolute path of \"%s\" via inspect-library",
                               resolved_path);
                      continue;
                    }

                  /* We need a key for the hashtable that includes the name and
                   * the canonical path in it. We use '//' as a separator
                   * between the two values, because we don't expect to have
                   * '//' in the path, nor in the name. In the very unlikely
                   * event where a collision happens, we will just consider
                   * two layers as duplicated when in reality they weren't. */
                  hash_key = g_strdup_printf ("%s//%s", name, canonical_path);
                  _update_duplicated_value (module, loadable_seen, hash_key);
                }
            }
        }
      else
        {
          g_return_if_reached ();
        }
    }
}

/*
 * load_json_dir:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @dir: A directory to search
 * @suffix: (nullable): A path to append to @dir, such as `"vulkan/icd.d"`
 * @sort: (nullable): If not %NULL, load ICDs sorted by filename in this order
 * @load_json_cb: Called for each potential ICD found
 * @user_data: Passed to @load_json_cb
 */
void
load_json_dir (SrtSysroot *sysroot,
               const char *dir,
               const char *suffix,
               GCompareFunc sort,
               void (*load_json_cb) (SrtSysroot *, const char *, void *),
               void *user_data)
{
  g_autoptr(GError) error = NULL;
  g_auto(GLnxDirFdIterator) iter = { .initialized = FALSE };
  g_autofree gchar *canon = NULL;
  g_autofree gchar *suffixed_dir = NULL;
  glnx_autofd int dirfd = -1;
  const char *member;
  g_autoptr(GPtrArray) members = NULL;
  gsize i;

  g_return_if_fail (SRT_IS_SYSROOT (sysroot));
  g_return_if_fail (load_json_cb != NULL);

  if (dir == NULL)
    return;

  if (!g_path_is_absolute (dir))
    {
      canon = g_canonicalize_filename (dir, NULL);
      dir = canon;
    }

  if (suffix != NULL)
    {
      suffixed_dir = g_build_filename (dir, suffix, NULL);
      dir = suffixed_dir;
    }

  g_debug ("Looking for ICDs in %s (in sysroot %s)...", dir, sysroot->path);

  dirfd = _srt_sysroot_open (sysroot, dir,
                             (SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY
                              | SRT_RESOLVE_FLAGS_READABLE),
                             NULL, &error);

  if (dirfd < 0 ||
      !glnx_dirfd_iterator_init_take_fd (&dirfd, &iter, &error))
    {
      g_debug ("Failed to open \"%s%s\": %s",
               sysroot->path, dir, error->message);
      return;
    }

  members = g_ptr_array_new_with_free_func (g_free);

  while (TRUE)
    {
      struct dirent *dent = NULL;

      if (!glnx_dirfd_iterator_next_dent (&iter, &dent, NULL, &error))
        {
          g_warning ("I/O error reading members of \"%s%s\": %s",
                     sysroot->path, dir, error->message);
          g_clear_error (&error);
          break;
        }

      if (dent == NULL)
        break;

      if (!g_str_has_suffix (dent->d_name, ".json"))
        continue;

      g_ptr_array_add (members, g_strdup (dent->d_name));
    }

  if (sort != READDIR_ORDER)
    g_ptr_array_sort (members, sort);

  for (i = 0; i < members->len; i++)
    {
      gchar *path;

      member = g_ptr_array_index (members, i);
      path = g_build_filename (dir, member, NULL);
      load_json_cb (sysroot, path, user_data);
      g_free (path);
    }
}

/*
 * load_json_dir:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @search_paths: Directories to search
 * @suffix: (nullable): A path to append to @dir, such as `"vulkan/icd.d"`
 * @sort: (nullable): If not %NULL, load ICDs sorted by filename in this order
 * @load_json_cb: Called for each potential ICD found
 * @user_data: Passed to @load_json_cb
 *
 * If @search_paths contains duplicated directories they'll be filtered out
 * to prevent loading the same JSONs multiple times.
 */
void
load_json_dirs (SrtSysroot *sysroot,
                GStrv search_paths,
                const char *suffix,
                GCompareFunc sort,
                void (*load_json_cb) (SrtSysroot *, const char *, void *),
                void *user_data)
{
  gchar **iter;
  g_autoptr(GHashTable) searched_set = NULL;
  g_autoptr(GError) error = NULL;

  g_return_if_fail (SRT_IS_SYSROOT (sysroot));
  g_return_if_fail (load_json_cb != NULL);

  searched_set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (iter = search_paths;
       iter != NULL && *iter != NULL;
       iter++)
    {
      glnx_autofd int file_fd = -1;
      g_autofree gchar *file_realpath_in_sysroot = NULL;

      file_fd = _srt_sysroot_open (sysroot, *iter,
                                   SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY,
                                   &file_realpath_in_sysroot, &error);

      if (file_realpath_in_sysroot == NULL)
        {
          /* Skip it if the path doesn't exist or is not reachable */
          g_debug ("An error occurred while resolving \"%s\": %s", *iter, error->message);
          g_clear_error (&error);
          continue;
        }

      if (!g_hash_table_contains (searched_set, file_realpath_in_sysroot))
        {
          g_hash_table_add (searched_set, g_steal_pointer (&file_realpath_in_sysroot));
          load_json_dir (sysroot, *iter, suffix, sort, load_json_cb, user_data);
        }
      else
        {
          g_debug ("Skipping \"%s\" because we already loaded the JSONs from it",
                   file_realpath_in_sysroot);
        }
    }
}

/*
 * load_manifest_from_json:
 * @type: %SRT_TYPE_EGL_ICD or %SRT_TYPE_EGL_EXTERNAL_PLATFORM or %SRT_TYPE_VULKAN_ICD
 * @sysroot: (not nullable): The root directory, usually `/`
 * @filename: The filename of the metadata
 * @json_member_name: Name of the JSON object beneath the root, containing the
 *  main body of the manifest.
 *
 * Load an EGL or Vulkan ICD or OpenXR runtime from a JSON metadata file.
 *
 * Returns: (transfer full): The resulting object of type @type
 */
GObject *
load_manifest_from_json (GType type,
                         SrtSysroot *sysroot,
                         const char *filename,
                         const char *json_member_name)
{
  g_autoptr(JsonParser) parser = NULL;
  g_autofree gchar *canon = NULL;
  g_autofree gchar *resolved_filename = NULL;
  g_autofree gchar *contents = NULL;
  g_autoptr(GError) error = NULL;
  /* These are all borrowed from the parser */
  JsonNode *node;
  JsonObject *object;
  JsonNode *subnode;
  JsonObject *member_object = NULL;
  const char *file_format_version;
  const char *api_version = NULL;
  const char *library_path = NULL;
  const char *library_arch = NULL;
  gboolean portability_driver = FALSE;
  SrtLoadableIssues issues = SRT_LOADABLE_ISSUES_NONE;
  gsize len = 0;

  g_return_val_if_fail (type == SRT_TYPE_VULKAN_ICD
                        || type == SRT_TYPE_EGL_ICD
                        || type == SRT_TYPE_EGL_EXTERNAL_PLATFORM
                        || type == SRT_TYPE_OPENXR_1_RUNTIME,
                        NULL);
  g_return_val_if_fail (SRT_IS_SYSROOT (sysroot), NULL);

  if (!g_path_is_absolute (filename))
    {
      canon = g_canonicalize_filename (filename, NULL);
      filename = canon;
    }

  g_debug ("Attempting to load %s from \"%s/%s\"",
           g_type_name (type), sysroot->path, filename);

  if (!_srt_sysroot_load (sysroot, filename,
                          SRT_RESOLVE_FLAGS_RETURN_ABSOLUTE,
                          /* Make sure to get a fully resolved path for OpenXR
                             runtimes, because that's the path used to resolve
                             relative library paths. */
                          type == SRT_TYPE_OPENXR_1_RUNTIME
                            ? &resolved_filename
                            : NULL,
                          &contents, &len, &error))
    {
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  if (G_UNLIKELY (len > G_MAXSSIZE))
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unreasonably large JSON file \"%s%s\"",
                   sysroot->path, filename);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  if (G_UNLIKELY (strnlen (contents, len + 1) < len))
    {
      /* In practice json-glib does diagnose this as an error, but the
       * error message is misleading (it claims the file isn't UTF-8);
       * and we want to check for this explicitly anyway, because if
       * the content could contain \0 then it would be wrong to store it
       * as a gchar * and not a (content,length) pair. */
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "JSON file \"%s%s\" contains \\0",
                   sysroot->path, filename);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  parser = json_parser_new ();

  if (!json_parser_load_from_data (parser, contents, len, &error))
    {
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  node = json_parser_get_root (parser);

  if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Expected to find a JSON object in \"%s%s\"",
                   sysroot->path, filename);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  object = json_node_get_object (node);

  file_format_version = _srt_json_object_get_string_member (object,
                                                            "file_format_version");
  if (file_format_version == NULL)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "file_format_version in \"%s%s\" is either missing or not a string",
                   sysroot->path, filename);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  if (type == SRT_TYPE_VULKAN_ICD)
    {
      /*
       * The compatibility rules for Vulkan ICDs are not clear.
       * See https://github.com/KhronosGroup/Vulkan-Loader/issues/248
       *
       * The reference loader currently logs a warning, but carries on
       * anyway, if the file format version is not 1.0.0 or 1.0.1.
       * However, on #248 there's a suggestion that all the format versions
       * that are valid for layer JSON (1.0.x up to 1.0.1 and 1.1.x up
       * to 1.1.2) should also be considered valid for ICD JSON. For now
       * we assume that the rule is the same as for EGL, below.
       */
      if (!g_str_has_prefix (file_format_version, "1.0."))
        {
          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Vulkan file_format_version in \"%s%s\" is not 1.0.x",
                       sysroot->path, filename);
          issues |= SRT_LOADABLE_ISSUES_UNSUPPORTED;
          goto out;
        }
    }
  else if (type == SRT_TYPE_EGL_ICD || type == SRT_TYPE_EGL_EXTERNAL_PLATFORM)
    {
      /*
       * For EGL, all 1.0.x versions are officially backwards compatible
       * with 1.0.0.
       * https://github.com/NVIDIA/libglvnd/blob/HEAD/src/EGL/icd_enumeration.md
       * There's no specification or public loader for external platforms,
       * but we assume the same is true for those.
       */
      if (!g_str_has_prefix (file_format_version, "1.0."))
        {
          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "EGL file_format_version in \"%s%s\" is not 1.0.x",
                       sysroot->path, filename);
          issues |= SRT_LOADABLE_ISSUES_UNSUPPORTED;
          goto out;
        }
    }
  else if (type == SRT_TYPE_OPENXR_1_RUNTIME)
    {
      /*
       * Currently, the OpenXR loader rejects all versions other than 1.0.0:
       * https://github.com/KhronosGroup/OpenXR-SDK/blob/release-1.1.43/src/loader/manifest_file.cpp#L479-L487
       * so for now we follow that behavior.
       */
      if (!g_str_equal (file_format_version, "1.0.0"))
        {
          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "OpenXR file_format_version in \"%s%s\" is not 1.0.0",
                       sysroot->path, filename);
          issues |= SRT_LOADABLE_ISSUES_UNSUPPORTED;
          goto out;
        }
    }
  else
    {
      g_assert_not_reached ();
    }

  subnode = json_object_get_member (object, json_member_name);

  if (subnode == NULL
      || !JSON_NODE_HOLDS_OBJECT (subnode))
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No \"%s\" object in \"%s%s\"",
                   json_member_name, sysroot->path, filename);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  member_object = json_node_get_object (subnode);

  if (type == SRT_TYPE_VULKAN_ICD)
    {
      library_arch = _srt_json_object_get_string_member (member_object, "library_arch");
      api_version = _srt_json_object_get_string_member (member_object, "api_version");
      if (api_version == NULL)
        {
          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "%s.api_version in \"%s%s\" is either missing or not a string",
                       json_member_name, sysroot->path, filename);
          issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
          goto out;
        }
      portability_driver = json_object_get_boolean_member_with_default (member_object,
                                                                        "is_portability_driver",
                                                                        FALSE);
      if (portability_driver)
        issues |= SRT_LOADABLE_ISSUES_API_SUBSET;
    }

  library_path = _srt_json_object_get_string_member (member_object, "library_path");
  if (library_path == NULL)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "%s.library_path in \"%s%s\" is either missing or not a string",
                   json_member_name, sysroot->path, filename);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

out:
  if (type == SRT_TYPE_VULKAN_ICD)
    {
      SrtVulkanIcd *icd;

      if (error == NULL)
        {
          icd = srt_vulkan_icd_new (filename, api_version,
                                    library_path, library_arch,
                                    portability_driver, issues);
          _srt_base_json_graphics_module_take_original_json (SRT_BASE_JSON_GRAPHICS_MODULE (icd),
                                                             g_steal_pointer (&contents));
        }
      else
        {
          icd = srt_vulkan_icd_new_error (filename, issues, error);
        }

      return G_OBJECT (icd);
    }
  else if (type == SRT_TYPE_EGL_ICD)
    {
      SrtEglIcd *icd;

      if (error == NULL)
        {
          icd = srt_egl_icd_new (filename, library_path, issues);
          _srt_base_json_graphics_module_take_original_json (SRT_BASE_JSON_GRAPHICS_MODULE (icd),
                                                             g_steal_pointer (&contents));
        }
      else
        {
          icd = srt_egl_icd_new_error (filename, issues, error);
        }

      return G_OBJECT (icd);
    }
  else if (type == SRT_TYPE_EGL_EXTERNAL_PLATFORM)
    {
      SrtEglExternalPlatform *ep;

      if (error == NULL)
        {
          ep = srt_egl_external_platform_new (filename, library_path, issues);
          _srt_base_json_graphics_module_take_original_json (SRT_BASE_JSON_GRAPHICS_MODULE (ep),
                                                             g_steal_pointer (&contents));
        }
      else
        {
          ep = srt_egl_external_platform_new_error (filename, issues, error);
        }

      return G_OBJECT (ep);
    }
  else if (type == SRT_TYPE_OPENXR_1_RUNTIME)
    {
      SrtOpenXr1Runtime *rt;

      if (error == NULL)
        {
          rt = srt_openxr_1_runtime_load_json (resolved_filename,
                                               filename, library_path,
                                               member_object, issues);
        }
      else
        {
          rt = srt_openxr_1_runtime_new_error (resolved_filename ?: filename,
                                               filename,
                                               issues, error);
        }

      return G_OBJECT (rt);
    }
  else
    {
      g_return_val_if_reached (NULL);
    }
}

/*
 * prepend_manifest_from_json:
 * @type: %SRT_TYPE_EGL_ICD or %SRT_TYPE_EGL_EXTERNAL_PLATFORM or %SRT_TYPE_VULKAN_ICD
 * @sysroot: (not nullable): The root directory, usually `/`
 * @filename: The filename of the metadata
 * @json_member_name: Name of the JSON object beneath the root, containing the
 *  main body of the manifest.
 * @out_list: (element-type GObject) (transfer full) (inout): Prepend the
 *  resulting object of type @type to this list
 *
 * Load an EGL or Vulkan ICD or OpenXR runtime from a JSON metadata file.
 * Equivalent to load_manifest_from_json(), but prepends the loaded manifest to
 * a list rather than returning it directly.
 */
void
prepend_manifest_from_json (GType type,
                            SrtSysroot *sysroot,
                            const char *filename,
                            const char *json_member_name,
                            GList **out_list)
{
  GObject *ret = load_manifest_from_json (type, sysroot, filename,
                                          json_member_name);
  g_return_if_fail (ret != NULL);
  *out_list = g_list_prepend (*out_list, ret);
}

void
_srt_base_json_graphics_module_set_library_arch (SrtBaseJsonGraphicsModule *self,
                                                 const char *library_arch,
                                                 const char *min_file_format_version)
{
  g_return_if_fail (library_arch != NULL);
  g_return_if_fail (min_file_format_version != NULL);

  g_clear_pointer (&self->library_arch, g_free);
  self->library_arch = g_strdup (library_arch);

  if (self->file_format_version == NULL
      || strverscmp (self->file_format_version, min_file_format_version) < 0)
    {
      g_clear_pointer (&self->file_format_version, g_free);
      self->file_format_version = g_strdup (min_file_format_version);
    }
}

void
_srt_base_json_graphics_module_take_original_json (SrtBaseJsonGraphicsModule *self,
                                                   gchar *contents)
{
  g_clear_pointer (&self->original_json, g_free);
  self->original_json = contents;
}
