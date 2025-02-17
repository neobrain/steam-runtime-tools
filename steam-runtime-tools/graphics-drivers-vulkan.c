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

#include "steam-runtime-tools/graphics.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/graphics-drivers-json-based-internal.h"
#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"

#include "steam-runtime-tools/graphics-drivers-internal.h"
/**
 * SECTION:graphics-drivers-vulkan
 * @title: Vulkan graphics driver and layer enumeration
 * @short_description: Get information about the system's Vulkan drivers
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtVulkanIcd is an opaque object representing the metadata describing
 * a Vulkan ICD.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 */

/**
 * SrtVulkanIcd:
 *
 * Opaque object representing a Vulkan ICD.
 */

struct _SrtVulkanIcdClass
{
  /*< private >*/
  SrtBaseJsonGraphicsModuleClass parent_class;
};

enum
{
  VULKAN_ICD_PROP_0,
  VULKAN_ICD_PROP_API_VERSION,
  VULKAN_ICD_PROP_LIBRARY_ARCH,
  VULKAN_ICD_PROP_PORTABILITY_DRIVER,
  N_VULKAN_ICD_PROPERTIES
};

G_DEFINE_TYPE (SrtVulkanIcd, srt_vulkan_icd, SRT_TYPE_BASE_JSON_GRAPHICS_MODULE)

static void
srt_vulkan_icd_init (SrtVulkanIcd *self)
{
}

static void
srt_vulkan_icd_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  SrtVulkanIcd *self = SRT_VULKAN_ICD (object);

  switch (prop_id)
    {
      case VULKAN_ICD_PROP_API_VERSION:
        g_value_set_string (value, self->parent.api_version);
        break;

      case VULKAN_ICD_PROP_LIBRARY_ARCH:
        g_value_set_string (value, self->parent.library_arch);
        break;

      case VULKAN_ICD_PROP_PORTABILITY_DRIVER:
        g_value_set_boolean (value, self->parent.portability_driver);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_vulkan_icd_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  SrtVulkanIcd *self = SRT_VULKAN_ICD (object);

  switch (prop_id)
    {
      case VULKAN_ICD_PROP_API_VERSION:
        g_return_if_fail (self->parent.api_version == NULL);
        self->parent.api_version = g_value_dup_string (value);
        break;

      case VULKAN_ICD_PROP_LIBRARY_ARCH:
        g_return_if_fail (self->parent.library_arch == NULL);
        self->parent.library_arch = g_value_dup_string (value);
        break;

      case VULKAN_ICD_PROP_PORTABILITY_DRIVER:
        self->parent.portability_driver = g_value_get_boolean (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_vulkan_icd_constructed (GObject *object)
{
  SrtVulkanIcd *self = SRT_VULKAN_ICD (object);
  SrtBaseGraphicsModule *base = SRT_BASE_GRAPHICS_MODULE (object);

  G_OBJECT_CLASS (srt_vulkan_icd_parent_class)->constructed (object);

  if (base->error != NULL)
    {
      g_return_if_fail (self->parent.api_version == NULL);
      g_return_if_fail (base->library_path == NULL);
    }
  else
    {
      g_return_if_fail (self->parent.api_version != NULL);
      g_return_if_fail (base->library_path != NULL);
    }
}

static GParamSpec *vulkan_icd_properties[N_VULKAN_ICD_PROPERTIES] = { NULL };

static void
srt_vulkan_icd_class_init (SrtVulkanIcdClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_vulkan_icd_get_property;
  object_class->set_property = srt_vulkan_icd_set_property;
  object_class->constructed = srt_vulkan_icd_constructed;

  vulkan_icd_properties[VULKAN_ICD_PROP_API_VERSION] =
    g_param_spec_string ("api-version", "API version",
                         "Vulkan API version implemented by this ICD",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_icd_properties[VULKAN_ICD_PROP_LIBRARY_ARCH] =
    g_param_spec_string ("library-arch", "Library architecture",
                         "Architecture of the library implementing this ICD. "
                         "The values allowed by the specification are \"32\" "
                         "and \"64\", but other values are possible.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_icd_properties[VULKAN_ICD_PROP_PORTABILITY_DRIVER] =
    g_param_spec_boolean ("portability-driver", "Is a portability driver?",
                          "TRUE if the ICD is for a portability driver",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_VULKAN_ICD_PROPERTIES,
                                     vulkan_icd_properties);
}

/*
 * srt_vulkan_icd_new:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @api_version: (transfer none): the API version
 * @library_path: (transfer none): the path to the library
 * @library_arch: (transfer none) (nullable): the architecture of
 *  @library_path
 * @portability_driver: Whether the ICD is a portability driver or not
 * @issues: problems with this ICD
 *
 * Returns: (transfer full): a new ICD
 */
SrtVulkanIcd *
srt_vulkan_icd_new (const gchar *json_path,
                    const gchar *api_version,
                    const gchar *library_path,
                    const gchar *library_arch,
                    gboolean portability_driver,
                    SrtLoadableIssues issues)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (api_version != NULL, NULL);
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_VULKAN_ICD,
                       "api-version", api_version,
                       "json-path", json_path,
                       "library-path", library_path,
                       "library-arch", library_arch,
                       "portability-driver", portability_driver,
                       "issues", issues,
                       NULL);
}

/*
 * srt_vulkan_icd_new_error:
 * @issues: problems with this ICD
 * @error: (transfer none): Error that occurred when loading the ICD
 *
 * Returns: (transfer full): a new ICD
 */
SrtVulkanIcd *
srt_vulkan_icd_new_error (const gchar *json_path,
                          SrtLoadableIssues issues,
                          const GError *error)
{
  return (SrtVulkanIcd *) _srt_base_json_graphics_module_new_error (SRT_TYPE_VULKAN_ICD,
                                                                    json_path,
                                                                    issues,
                                                                    error);
}

/**
 * srt_vulkan_icd_check_error:
 * @self: The ICD
 * @error: Used to return details if the ICD description could not be loaded
 *
 * Check whether we failed to load the JSON describing this Vulkan ICD.
 * Note that this does not actually `dlopen()` the ICD itself.
 *
 * Returns: %TRUE if the JSON was loaded successfully
 */
gboolean
srt_vulkan_icd_check_error (SrtVulkanIcd *self,
                            GError **error)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return _srt_base_graphics_module_check_error (SRT_BASE_GRAPHICS_MODULE (self), error);
}

/**
 * srt_vulkan_icd_get_api_version:
 * @self: The ICD
 *
 * Return the Vulkan API version of this ICD.
 *
 * If the JSON description for this ICD could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type utf8) (transfer none) (nullable): The API version as a string
 */
const gchar *
srt_vulkan_icd_get_api_version (SrtVulkanIcd *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), NULL);
  return self->parent.api_version;
}

/**
 * srt_vulkan_icd_get_json_path:
 * @self: The ICD
 *
 * Return the absolute path to the JSON file representing this ICD.
 *
 * Returns: (type filename) (transfer none): #SrtVulkanIcd:json-path
 */
const gchar *
srt_vulkan_icd_get_json_path (SrtVulkanIcd *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), NULL);
  return self->parent.json_path;
}

/**
 * srt_vulkan_icd_get_library_path:
 * @self: The ICD
 *
 * Return the library path for this ICD. It is either an absolute path,
 * a path relative to srt_vulkan_icd_get_json_path() containing at least one
 * directory separator (slash), or a basename to be loaded from the
 * shared library search path.
 *
 * If the JSON description for this ICD could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtVulkanIcd:library-path
 */
const gchar *
srt_vulkan_icd_get_library_path (SrtVulkanIcd *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), NULL);
  return SRT_BASE_GRAPHICS_MODULE (self)->library_path;
}

/**
 * srt_vulkan_icd_get_library_arch:
 * @self: The ICD
 *
 * Return a string that describes the architecture of this ICD.
 * The values allowed by the Vulkan specification are `32` and `64`,
 * indicating the size of a pointer, but the reference Vulkan-Loader
 * accepts any value (and therefore so does steam-runtime-tools).
 *
 * This is an optional field, so if it was not available in the JSON,
 * or if the ICD could not be loaded, %NULL will be returned.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtVulkanIcd:library-arch
 */
const gchar *
srt_vulkan_icd_get_library_arch (SrtVulkanIcd *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), NULL);
  return self->parent.library_arch;
}

/**
 * srt_vulkan_icd_get_issues:
 * @self: The ICD
 *
 * Return the problems found when parsing and loading @self.
 *
 * Returns: A bitfield containing problems, or %SRT_LOADABLE_ISSUES_NONE
 *  if no problems were found
 */
SrtLoadableIssues
srt_vulkan_icd_get_issues (SrtVulkanIcd *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), SRT_LOADABLE_ISSUES_UNKNOWN);
  return SRT_BASE_GRAPHICS_MODULE (self)->issues;
}

/**
 * srt_vulkan_icd_resolve_library_path:
 * @self: An ICD
 *
 * Return the path that can be passed to `dlopen()` for this ICD.
 *
 * If srt_vulkan_icd_get_library_path() is a relative path, return the
 * absolute path that is the result of interpreting it relative to
 * srt_vulkan_icd_get_json_path(). Otherwise return a copy of
 * srt_vulkan_icd_get_library_path().
 *
 * The result is either the basename of a shared library (to be found
 * relative to some directory listed in `$LD_LIBRARY_PATH`, `/etc/ld.so.conf`,
 * `/etc/ld.so.conf.d` or the hard-coded library search path), or an
 * absolute path.
 *
 * Returns: (transfer full) (type filename) (nullable): A copy
 *  of #SrtVulkanIcd:resolved-library-path. Free with g_free().
 */
gchar *
srt_vulkan_icd_resolve_library_path (SrtVulkanIcd *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), NULL);
  return _srt_base_graphics_module_resolve_library_path (SRT_BASE_GRAPHICS_MODULE (self));
}

/**
 * srt_vulkan_icd_write_to_file:
 * @self: An ICD
 * @path: (type filename): A filename
 * @error: Used to describe the error on failure
 *
 * Serialize @self to the given JSON file.
 *
 * Returns: %TRUE on success
 */
gboolean
srt_vulkan_icd_write_to_file (SrtVulkanIcd *self,
                              const char *path,
                              GError **error)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return _srt_base_json_graphics_module_write_to_file (&self->parent,
                                                       path,
                                                       SRT_TYPE_VULKAN_ICD,
                                                       error);
}

/**
 * srt_vulkan_icd_new_replace_library_path:
 * @self: An ICD
 * @path: (type filename) (transfer none): A path
 *
 * Return a copy of @self with the srt_vulkan_icd_get_library_path()
 * changed to @path. For example, this is useful when setting up a
 * container where the underlying shared object will be made available
 * at a different absolute path.
 *
 * If @self is in an error state, this returns a new reference to @self.
 *
 * Note that @self issues are copied to the new #SrtVulkanIcd copy, including
 * the eventual %SRT_LOADABLE_ISSUES_DUPLICATED.
 *
 * Returns: (transfer full): A new reference to a #SrtVulkanIcd. Free with
 *  g_object_unref().
 */
SrtVulkanIcd *
srt_vulkan_icd_new_replace_library_path (SrtVulkanIcd *self,
                                         const char *path)
{
  SrtBaseGraphicsModule *base;
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), NULL);

  base = SRT_BASE_GRAPHICS_MODULE (self);

  if (base->error != NULL)
    return g_object_ref (self);

  return srt_vulkan_icd_new (self->parent.json_path,
                             self->parent.api_version,
                             path,
                             self->parent.library_arch,
                             self->parent.portability_driver,
                             base->issues);
}

static void
vulkan_icd_load_json_cb (SrtSysroot *sysroot,
                         const char *filename,
                         void *user_data)
{
  load_manifest_from_json (SRT_TYPE_VULKAN_ICD, sysroot, filename,
                           MANIFEST_JSON_MEMBER_NAME_ICD, user_data);
}

/*
 * Return the ${sysconfdir} that we assume the Vulkan loader has.
 * See get_glvnd_sysconfdir().
 */
static const char *
get_vulkan_sysconfdir (void)
{
  return "/etc";
}

/* Reference:
 * https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198.1/docs/LoaderLayerInterface.md#linux-layer-discovery
 * https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198.1/docs/LoaderDriverInterface.md#driver-discovery-on-linux
 *
 * ICDs (drivers) and loaders are currently exactly the same, except for
 * the suffix used. If they diverge in future, this function will need more
 * parameters. */
gchar **
_srt_graphics_get_vulkan_search_paths (SrtSysroot *sysroot,
                                       const char * const *envp,
                                       const char * const *multiarch_tuples,
                                       const char *suffix)
{
  GPtrArray *search_paths = g_ptr_array_new_with_free_func (g_free);
  g_auto(GStrv) dirs = NULL;
  const char *home;
  const gchar *value;
  gsize i;

  home = _srt_environ_getenv (envp, "HOME");

  if (home == NULL)
    home = g_get_home_dir ();

  /* 1. $XDG_CONFIG_HOME or $HOME/.config (since 1.2.198) */
  value = _srt_environ_getenv (envp, "XDG_CONFIG_HOME");

  if (value != NULL)
    g_ptr_array_add (search_paths, g_build_filename (value, suffix, NULL));
  else if (home != NULL)
    g_ptr_array_add (search_paths, g_build_filename (home, ".config", suffix, NULL));

  /* 1a. $XDG_CONFIG_DIRS or /etc/xdg */
  value = _srt_environ_getenv (envp, "XDG_CONFIG_DIRS");

  /* Constant and non-configurable fallback, as per
   * https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html */
  if (value == NULL)
    value = "/etc/xdg";

  dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);
  for (i = 0; dirs[i] != NULL; i++)
    g_ptr_array_add (search_paths, g_build_filename (dirs[i], suffix, NULL));

  g_clear_pointer (&dirs, g_strfreev);

  /* 2. SYSCONFDIR */
  value = get_vulkan_sysconfdir ();
  g_ptr_array_add (search_paths, g_build_filename (value, suffix, NULL));

  /* 3. EXTRASYSCONFDIR.
   * This is hard-coded in the reference loader: if its own sysconfdir
   * is not /etc, it searches /etc afterwards. (In practice this
   * won't trigger at the moment, because we assume the Vulkan
   * loader's sysconfdir *is* /etc.) */
  if (g_strcmp0 (value, "/etc") != 0)
    g_ptr_array_add (search_paths, g_build_filename ("/etc", suffix, NULL));

  /* freedesktop-sdk patches the Vulkan loader to look here for ICDs,
   * after EXTRASYSCONFDIR but before XDG_DATA_HOME.
   * https://gitlab.com/freedesktop-sdk/freedesktop-sdk/-/blob/master/patches/vulkan/vulkan-libdir-path.patch */
  if (_srt_sysroot_test (sysroot, "/.flatpak-info", SRT_RESOLVE_FLAGS_NONE, NULL))
    {
      g_debug ("Flatpak detected: assuming freedesktop-based runtime");

      for (i = 0; multiarch_tuples != NULL && multiarch_tuples[i] != NULL; i++)
        {
          /* GL extensions */
          g_ptr_array_add (search_paths, g_build_filename ("/usr/lib",
                                                           multiarch_tuples[i],
                                                           "GL",
                                                           suffix,
                                                           NULL));
          /* Built-in Mesa stack */
          g_ptr_array_add (search_paths, g_build_filename ("/usr/lib",
                                                           multiarch_tuples[i],
                                                           suffix,
                                                           NULL));
        }

      /* https://gitlab.com/freedesktop-sdk/freedesktop-sdk/-/merge_requests/3398 */
      g_ptr_array_add (search_paths, g_build_filename ("/usr/lib/extensions/vulkan/share",
                                                       suffix,
                                                       NULL));
    }

  /* 4. $XDG_DATA_HOME or $HOME/.local/share.
   * In previous versions of steam-runtime-tools, we misinterpreted the
   * Vulkan-Loader code and thought it was loading $XDG_DATA_HOME *and*
   * $HOME/.local/share (inconsistent with the basedir spec). This was
   * incorrect: it only used $HOME/.local/share as a fallback, consistent
   * with the basedir spec.
   *
   * Unfortunately, Steam currently relies on layers in $HOME/.local/share
   * being found, even if $XDG_DATA_HOME is set to something else:
   * https://github.com/ValveSoftware/steam-for-linux/issues/8337
   * So for now we continue to follow the misinterpretation, to make the
   * Steam Overlay more likely to work in pressure-vessel containers. */
  value = _srt_environ_getenv (envp, "XDG_DATA_HOME");

  if (value != NULL)
    g_ptr_array_add (search_paths, g_build_filename (value, suffix, NULL));

  /* When steam-for-linux#8337 has been fixed, this should become an 'else if' */
  if (home != NULL)
    {
      g_ptr_array_add (search_paths,
                       g_build_filename (home, ".local", "share", suffix, NULL));

      if (value != NULL)
        {
          /* Avoid searching the same directory twice if a fully
           * spec-compliant Vulkan loader would not */
          const char *in_xdh;
          const char *in_fallback;

          g_assert (search_paths->len > 2);
          in_xdh = search_paths->pdata[search_paths->len - 2];
          in_fallback = search_paths->pdata[search_paths->len - 1];

          if (_srt_fstatat_is_same_file (-1, in_xdh, -1, in_fallback))
            g_ptr_array_remove_index (search_paths, search_paths->len - 1);
        }
    }

  /* 5. $XDG_DATA_DIRS or /usr/local/share:/usr/share */
  value = _srt_environ_getenv (envp, "XDG_DATA_DIRS");

  /* Constant and non-configurable fallback, as per
   * https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html */
  if (value == NULL)
    value = "/usr/local/share" G_SEARCHPATH_SEPARATOR_S "/usr/share";

  dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);
  for (i = 0; dirs[i] != NULL; i++)
    g_ptr_array_add (search_paths, g_build_filename (dirs[i], suffix, NULL));

  g_ptr_array_add (search_paths, NULL);

  return (GStrv) g_ptr_array_free (search_paths, FALSE);
}

/*
 * _srt_load_vulkan_icds:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @runner: The execution environment
 * @multiarch_tuples: (nullable): If not %NULL, and a Flatpak environment
 *  is detected, assume a freedesktop-sdk-based runtime and look for
 *  GL extensions for these multiarch tuples
 * @multiarch_tuples: (nullable): If not %NULL, and a Flatpak environment
 *  is detected, assume a freedesktop-sdk-based runtime and look for
 *  GL extensions for these multiarch tuples. Also if not %NULL, duplicated
 *  Vulkan ICDs are searched by their absolute path, obtained using
 *  "inspect-library" in the provided multiarch tuples, instead of just their
 *  resolved library path.
 * @check_flags: Whether to check for problems
 *
 * Implementation of srt_system_info_list_vulkan_icds().
 *
 * Returns: (transfer full) (element-type SrtVulkanIcd): A list of ICDs,
 *  most-important first
 */
GList *
_srt_load_vulkan_icds (SrtSysroot *sysroot,
                       SrtSubprocessRunner *runner,
                       const char * const *multiarch_tuples,
                       SrtCheckFlags check_flags)
{
  const char * const *envp;
  const gchar *value;
  gsize i;
  /* To avoid O(n**2) performance, we build this list in reverse order,
   * then reverse it at the end. */
  GList *ret = NULL;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (SRT_IS_SYSROOT (sysroot), NULL);
  g_return_val_if_fail (SRT_IS_SUBPROCESS_RUNNER (runner), NULL);

  /* Reference:
   * https://github.com/KhronosGroup/Vulkan-Loader/blob/v1.3.207/docs/LoaderDriverInterface.md#overriding-the-default-driver-discovery
   * https://github.com/KhronosGroup/Vulkan-Loader/pull/873
   */
  envp = _srt_subprocess_runner_get_environ (runner);
  value = _srt_environ_getenv (envp, "VK_DRIVER_FILES");

  if (value == NULL)
    value = _srt_environ_getenv (envp, "VK_ICD_FILENAMES");

  if (value != NULL)
    {
      gchar **filenames = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);

      g_debug ("Vulkan driver search path overridden to: %s", value);

      for (i = 0; filenames[i] != NULL; i++)
        load_manifest_from_json (SRT_TYPE_VULKAN_ICD, sysroot, filenames[i],
                                 MANIFEST_JSON_MEMBER_NAME_ICD, &ret);

      g_strfreev (filenames);
    }
  else
    {
      const gchar *add;
      g_auto(GStrv) search_paths = NULL;

      add = _srt_environ_getenv (envp, "VK_ADD_DRIVER_FILES");
      search_paths = _srt_graphics_get_vulkan_search_paths (sysroot, envp,
                                                            multiarch_tuples,
                                                            _SRT_GRAPHICS_VULKAN_ICD_SUFFIX);

      if (add != NULL)
        {
          g_auto(GStrv) filenames = g_strsplit (add, G_SEARCHPATH_SEPARATOR_S, -1);

          g_debug ("Vulkan additional driver search path: %s", add);

          for (i = 0; filenames[i] != NULL; i++)
            load_manifest_from_json (SRT_TYPE_VULKAN_ICD, sysroot, filenames[i],
                                     MANIFEST_JSON_MEMBER_NAME_ICD, &ret);
        }

      g_debug ("Using normal Vulkan driver search path");
      load_json_dirs (sysroot, search_paths, NULL, READDIR_ORDER,
                      vulkan_icd_load_json_cb, &ret);
    }

  if (!(check_flags & SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS))
    _srt_loadable_flag_duplicates (SRT_TYPE_VULKAN_ICD, runner,
                                   multiarch_tuples, ret);

  return g_list_reverse (ret);
}

/*
 * Set library_arch field, increasing the file_format_version to the
 * minimum version that described library_arch if necessary.
 */
void
_srt_vulkan_icd_set_library_arch (SrtVulkanIcd *self,
                                  const char *library_arch)
{
  g_return_if_fail (SRT_IS_VULKAN_ICD (self));
  _srt_base_json_graphics_module_set_library_arch (&self->parent,
                                                   library_arch,
                                                   "1.0.1");
}

/**
 * SrtVulkanLayer:
 *
 * Opaque object representing a Vulkan layer.
 */

struct _SrtVulkanLayer
{
  /*< private >*/
  SrtBaseJsonGraphicsModule parent;
};

struct _SrtVulkanLayerClass
{
  /*< private >*/
  SrtBaseJsonGraphicsModuleClass parent;
};

enum
{
  VULKAN_LAYER_PROP_0,
  VULKAN_LAYER_PROP_NAME,
  VULKAN_LAYER_PROP_TYPE,
  VULKAN_LAYER_PROP_LIBRARY_ARCH,
  VULKAN_LAYER_PROP_API_VERSION,
  VULKAN_LAYER_PROP_IMPLEMENTATION_VERSION,
  VULKAN_LAYER_PROP_DESCRIPTION,
  VULKAN_LAYER_PROP_COMPONENT_LAYERS,
  N_VULKAN_LAYER_PROPERTIES
};

G_DEFINE_TYPE (SrtVulkanLayer, srt_vulkan_layer, SRT_TYPE_BASE_JSON_GRAPHICS_MODULE)

static void
srt_vulkan_layer_init (SrtVulkanLayer *self)
{
}

static void
srt_vulkan_layer_get_property (GObject *object,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SrtVulkanLayer *self = SRT_VULKAN_LAYER (object);

  switch (prop_id)
    {
      case VULKAN_LAYER_PROP_NAME:
        g_value_set_string (value, self->parent.name);
        break;

      case VULKAN_LAYER_PROP_TYPE:
        g_value_set_string (value, self->parent.type);
        break;

      case VULKAN_LAYER_PROP_LIBRARY_ARCH:
        g_value_set_string (value, self->parent.library_arch);
        break;

      case VULKAN_LAYER_PROP_API_VERSION:
        g_value_set_string (value, self->parent.api_version);
        break;

      case VULKAN_LAYER_PROP_IMPLEMENTATION_VERSION:
        g_value_set_string (value, self->parent.implementation_version);
        break;

      case VULKAN_LAYER_PROP_DESCRIPTION:
        g_value_set_string (value, self->parent.description);
        break;

      case VULKAN_LAYER_PROP_COMPONENT_LAYERS:
        g_value_set_boxed (value, self->parent.component_layers);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_vulkan_layer_set_property (GObject *object,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  SrtVulkanLayer *self = SRT_VULKAN_LAYER (object);

  switch (prop_id)
    {
      case VULKAN_LAYER_PROP_NAME:
        g_return_if_fail (self->parent.name == NULL);
        self->parent.name = g_value_dup_string (value);
        break;

      case VULKAN_LAYER_PROP_TYPE:
        g_return_if_fail (self->parent.type == NULL);
        self->parent.type = g_value_dup_string (value);
        break;

      case VULKAN_LAYER_PROP_LIBRARY_ARCH:
        g_return_if_fail (self->parent.library_arch == NULL);
        self->parent.library_arch = g_value_dup_string (value);
        break;

      case VULKAN_LAYER_PROP_API_VERSION:
        g_return_if_fail (self->parent.api_version == NULL);
        self->parent.api_version = g_value_dup_string (value);
        break;

      case VULKAN_LAYER_PROP_IMPLEMENTATION_VERSION:
        g_return_if_fail (self->parent.implementation_version == NULL);
        self->parent.implementation_version = g_value_dup_string (value);
        break;

      case VULKAN_LAYER_PROP_DESCRIPTION:
        g_return_if_fail (self->parent.description == NULL);
        self->parent.description = g_value_dup_string (value);
        break;

      case VULKAN_LAYER_PROP_COMPONENT_LAYERS:
        g_return_if_fail (self->parent.component_layers == NULL);
        self->parent.component_layers = g_value_dup_boxed (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_vulkan_layer_constructed (GObject *object)
{
  SrtVulkanLayer *self = SRT_VULKAN_LAYER (object);
  SrtBaseGraphicsModule *base = SRT_BASE_GRAPHICS_MODULE (object);

  G_OBJECT_CLASS (srt_vulkan_layer_parent_class)->constructed (object);

  if (base->error != NULL)
    {
      g_return_if_fail (self->parent.name == NULL);
      g_return_if_fail (self->parent.type == NULL);
      g_return_if_fail (self->parent.api_version == NULL);
      g_return_if_fail (self->parent.implementation_version == NULL);
      g_return_if_fail (self->parent.description == NULL);
      g_return_if_fail (self->parent.component_layers == NULL);
    }
  else
    {
      g_return_if_fail (self->parent.name != NULL);
      g_return_if_fail (self->parent.type != NULL);
      g_return_if_fail (self->parent.api_version != NULL);
      g_return_if_fail (self->parent.implementation_version != NULL);
      g_return_if_fail (self->parent.description != NULL);

      if (base->library_path == NULL)
        g_return_if_fail (self->parent.component_layers != NULL && self->parent.component_layers[0] != NULL);
      else if (self->parent.component_layers == NULL || self->parent.component_layers[0] == NULL)
        g_return_if_fail (base->library_path != NULL);
      else
        g_return_if_reached ();
    }
}

static GParamSpec *vulkan_layer_properties[N_VULKAN_LAYER_PROPERTIES] = { NULL };

static void
srt_vulkan_layer_class_init (SrtVulkanLayerClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_vulkan_layer_get_property;
  object_class->set_property = srt_vulkan_layer_set_property;
  object_class->constructed = srt_vulkan_layer_constructed;

  vulkan_layer_properties[VULKAN_LAYER_PROP_NAME] =
    g_param_spec_string ("name", "name",
                         "The name that uniquely identify this layer to "
                         "applications.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_TYPE] =
    g_param_spec_string ("type", "type",
                         "The type of this layer. It is expected to be either "
                         "GLOBAL or INSTANCE.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_LIBRARY_ARCH] =
    g_param_spec_string ("library-arch", "Library architecture",
                         "Architecture of the library binary that implements "
                         "this layer. The values allowed by the specification "
                         "are \"32\" and \"64\", but other values are possible.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_API_VERSION] =
    g_param_spec_string ("api-version", "API version",
                         "The version number of the Vulkan API that the "
                         "shared library was built against.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_IMPLEMENTATION_VERSION] =
    g_param_spec_string ("implementation-version", "Implementation version",
                         "Version of the implemented layer.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_DESCRIPTION] =
    g_param_spec_string ("description", "Description",
                         "Brief description of the layer.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_COMPONENT_LAYERS] =
    g_param_spec_boxed ("component-layers", "Component layers",
                        "Component layer names that are part of a meta-layer.",
                        G_TYPE_STRV,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_VULKAN_LAYER_PROPERTIES,
                                     vulkan_layer_properties);
}

/*
 * srt_vulkan_layer_new:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @name: (transfer none): the layer unique name
 * @type: (transfer none): the type of the layer
 * @library_path: (transfer none): the path to the library
 * @library_arch: (transfer none) (nullable): the architecture of the binary
 *  in @library_path
 * @api_version: (transfer none): the API version
 * @implementation_version: (transfer none): the version of the implemented
 *  layer
 * @description: (transfer none): the description of the layer
 * @component_layers: (transfer none): the component layer names part of a
 *  meta-layer
 * @issues: problems with this layer
 *
 * @component_layers must be %NULL if @library_path has been defined.
 * @library_path must be %NULL if @component_layers has been defined.
 *
 * Returns: (transfer full): a new SrtVulkanLayer
 */
SrtVulkanLayer *
srt_vulkan_layer_new (const gchar *json_path,
                      const gchar *name,
                      const gchar *type,
                      const gchar *library_path,
                      const gchar *library_arch,
                      const gchar *api_version,
                      const gchar *implementation_version,
                      const gchar *description,
                      GStrv component_layers,
                      SrtLoadableIssues issues)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (type != NULL, NULL);
  g_return_val_if_fail (api_version != NULL, NULL);
  g_return_val_if_fail (implementation_version != NULL, NULL);
  g_return_val_if_fail (description != NULL, NULL);
  if (library_path == NULL)
    g_return_val_if_fail (component_layers != NULL && *component_layers != NULL, NULL);
  else if (component_layers == NULL || *component_layers == NULL)
    g_return_val_if_fail (library_path != NULL, NULL);
  else
    g_return_val_if_reached (NULL);

  return g_object_new (SRT_TYPE_VULKAN_LAYER,
                       "json-path", json_path,
                       "name", name,
                       "type", type,
                       "library-path", library_path,
                       "library-arch", library_arch,
                       "api-version", api_version,
                       "implementation-version", implementation_version,
                       "description", description,
                       "component-layers", component_layers,
                       "issues", issues,
                       NULL);
}

/*
 * srt_vulkan_layer_new_error:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @issues: problems with this layer
 * @error: (transfer none): Error that occurred when loading the layer
 *
 * Returns: (transfer full): a new SrtVulkanLayer
 */
SrtVulkanLayer *
srt_vulkan_layer_new_error (const gchar *json_path,
                            SrtLoadableIssues issues,
                            const GError *error)
{
  return (SrtVulkanLayer *) _srt_base_json_graphics_module_new_error (SRT_TYPE_VULKAN_LAYER,
                                                                      json_path,
                                                                      issues,
                                                                      error);
}

static void
_vulkan_layer_parse_json_environment_field (const gchar *member_name,
                                            EnvironmentVariable *env_var,
                                            JsonObject* json_layer)
{
  JsonObject *env_obj = NULL;
  g_autoptr(GList) members = NULL;

  g_return_if_fail (member_name != NULL);
  g_return_if_fail (env_var != NULL);
  g_return_if_fail (env_var->name == NULL);
  g_return_if_fail (env_var->value == NULL);
  g_return_if_fail (json_layer != NULL);

  if (json_object_has_member (json_layer, member_name))
    env_obj = json_object_get_object_member (json_layer, member_name);
  if (env_obj != NULL)
    {
      members = json_object_get_members (env_obj);
      if (members != NULL)
        {
          const gchar *value = json_object_get_string_member_with_default (env_obj,
                                                                           members->data,
                                                                           NULL);

          if (value == NULL)
            {
              g_debug ("The Vulkan layer property '%s' has an element with an "
                       "invalid value, trying to continue...", member_name);
            }
          else
            {
              env_var->name = g_strdup (members->data);
              env_var->value = g_strdup (value);
            }

          if (members->next != NULL)
            g_debug ("The Vulkan layer property '%s' has more than the expected "
                     "number of elements, trying to continue...", member_name);
        }
    }
}

static SrtVulkanLayer *
vulkan_layer_parse_json (const gchar *path,
                         const gchar *file_format_version,
                         JsonObject *json_layer)
{
  const gchar *name = NULL;
  const gchar *type = NULL;
  const gchar *library_path = NULL;
  const gchar *library_arch = NULL;
  const gchar *api_version = NULL;
  const gchar *implementation_version = NULL;
  const gchar *description = NULL;
  g_auto(GStrv) component_layers = NULL;
  JsonArray *instance_json_array = NULL;
  JsonArray *device_json_array = NULL;
  JsonNode *arr_node;
  JsonObject *functions_obj = NULL;
  JsonObject *pre_instance_obj = NULL;
  SrtVulkanLayer *vulkan_layer = NULL;
  g_autoptr(GError) error = NULL;
  guint array_length;
  gsize i;
  GList *l;

  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (file_format_version != NULL, NULL);
  g_return_val_if_fail (json_layer != NULL, NULL);

  name = json_object_get_string_member_with_default (json_layer, "name", NULL);

  type = json_object_get_string_member_with_default (json_layer, "type", NULL);
  library_path = json_object_get_string_member_with_default (json_layer, "library_path", NULL);
  /* In theory only "32" and "64" are valid values here. However the Vulkan-Loader
   * doesn't enforce it, so we don't do that either. */
  library_arch = json_object_get_string_member_with_default (json_layer, "library_arch", NULL);
  api_version = json_object_get_string_member_with_default (json_layer, "api_version", NULL);
  implementation_version = json_object_get_string_member_with_default (json_layer,
                                                                       "implementation_version",
                                                                       NULL);
  description = json_object_get_string_member_with_default (json_layer, "description", NULL);

  component_layers = _srt_json_object_dup_strv_member (json_layer, "component_layers", NULL);

  /* Don't distinguish between absent, and present with empty value */
  if (component_layers != NULL && component_layers[0] == NULL)
    g_clear_pointer (&component_layers, g_free);

  if (library_path != NULL && component_layers != NULL)
    {
      g_debug ("The parsed JSON layer has both 'library_path' and 'component_layers' "
               "fields. This is not allowed.");
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Vulkan layer in \"%s\" cannot be parsed because it is not allowed to list "
                   "both 'library_path' and 'component_layers' fields",
                   path);
      return srt_vulkan_layer_new_error (path, SRT_LOADABLE_ISSUES_CANNOT_LOAD, error);
    }

  if (name == NULL ||
      type == NULL ||
      api_version == NULL ||
      implementation_version == NULL ||
      description == NULL ||
      (library_path == NULL && component_layers == NULL))
    {
      g_debug ("A required field is missing from the JSON layer");
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Vulkan layer in \"%s\" cannot be parsed because it is missing a required field",
                   path);
      return srt_vulkan_layer_new_error (path, SRT_LOADABLE_ISSUES_CANNOT_LOAD, error);
    }

  vulkan_layer = srt_vulkan_layer_new (path, name, type, library_path, library_arch,
                                       api_version, implementation_version, description,
                                       component_layers, SRT_LOADABLE_ISSUES_NONE);

  vulkan_layer->parent.file_format_version = g_strdup (file_format_version);

  if (json_object_has_member (json_layer, "functions"))
    functions_obj = json_object_get_object_member (json_layer, "functions");
  if (functions_obj != NULL)
    {
      g_autoptr(GList) members = NULL;
      vulkan_layer->parent.functions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                              g_free, g_free);
      members = json_object_get_members (functions_obj);
      for (l = members; l != NULL; l = l->next)
        {
          const gchar *value = json_object_get_string_member_with_default (functions_obj, l->data,
                                                                           NULL);
          if (value == NULL)
            g_debug ("The Vulkan layer property 'functions' has an element with an invalid "
                     "value, trying to continue...");
          else
            g_hash_table_insert (vulkan_layer->parent.functions,
                                 g_strdup (l->data), g_strdup (value));
        }
    }

  if (json_object_has_member (json_layer, "pre_instance_functions"))
    pre_instance_obj = json_object_get_object_member (json_layer, "pre_instance_functions");
  if (pre_instance_obj != NULL)
    {
      g_autoptr(GList) members = NULL;
      vulkan_layer->parent.pre_instance_functions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                           g_free, g_free);
      members = json_object_get_members (pre_instance_obj);
      for (l = members; l != NULL; l = l->next)
        {
          const gchar *value = json_object_get_string_member_with_default (pre_instance_obj,
                                                                           l->data, NULL);
          if (value == NULL)
            g_debug ("The Vulkan layer property 'pre_instance_functions' has an "
                     "element with an invalid value, trying to continue...");
          else
            g_hash_table_insert (vulkan_layer->parent.pre_instance_functions,
                                 g_strdup (l->data), g_strdup (value));
        }
    }

  arr_node = json_object_get_member (json_layer, "instance_extensions");
  if (arr_node != NULL && JSON_NODE_HOLDS_ARRAY (arr_node))
    instance_json_array = json_node_get_array (arr_node);
  if (instance_json_array != NULL)
    {
      array_length = json_array_get_length (instance_json_array);
      for (i = 0; i < array_length; i++)
        {
          InstanceExtension *ie = g_slice_new0 (InstanceExtension);
          JsonObject *instance_extension = json_array_get_object_element (instance_json_array, i);
          ie->name = g_strdup (json_object_get_string_member_with_default (instance_extension,
                                                                           "name", NULL));
          ie->spec_version = g_strdup (json_object_get_string_member_with_default (instance_extension,
                                                                                   "spec_version",
                                                                                   NULL));

          if (ie->name == NULL || ie->spec_version == NULL)
            {
              g_debug ("The Vulkan layer property 'instance_extensions' is "
                       "missing some expected values, trying to continue...");
              instance_extension_free (ie);
            }
          else
            {
              vulkan_layer->parent.instance_extensions = g_list_prepend (vulkan_layer->parent.instance_extensions,
                                                                         ie);
            }
        }
      vulkan_layer->parent.instance_extensions = g_list_reverse (vulkan_layer->parent.instance_extensions);
    }

  arr_node = json_object_get_member (json_layer, "device_extensions");
  if (arr_node != NULL && JSON_NODE_HOLDS_ARRAY (arr_node))
    device_json_array = json_node_get_array (arr_node);
  if (device_json_array != NULL)
    {
      array_length = json_array_get_length (device_json_array);
      for (i = 0; i < array_length; i++)
        {
          DeviceExtension *de = g_slice_new0 (DeviceExtension);
          JsonObject *device_extension = json_array_get_object_element (device_json_array, i);
          de->name = g_strdup (json_object_get_string_member_with_default (device_extension,
                                                                           "name", NULL));
          de->spec_version = g_strdup (json_object_get_string_member_with_default (device_extension,
                                                                                   "spec_version",
                                                                                   NULL));
          de->entrypoints = _srt_json_object_dup_strv_member (device_extension, "entrypoints", NULL);

          if (de->name == NULL || de->spec_version == NULL)
            {
              g_debug ("The Vulkan layer json is missing some expected values");
              device_extension_free (de);
            }
          else
            {
              vulkan_layer->parent.device_extensions = g_list_prepend (vulkan_layer->parent.device_extensions,
                                                                       de);
            }
        }
    }

  _vulkan_layer_parse_json_environment_field ("enable_environment",
                                              &vulkan_layer->parent.enable_env_var,
                                              json_layer);

  _vulkan_layer_parse_json_environment_field ("disable_environment",
                                              &vulkan_layer->parent.disable_env_var,
                                              json_layer);

  return vulkan_layer;
}

/**
 * load_vulkan_layer_json:
 * @sysroot: (not nullable): Sysroot in which to load the layer
 * @path: (not nullable): Path to a Vulkan layer JSON file
 *
 * Returns: (transfer full) (element-type SrtVulkanLayer): A list of Vulkan
 *  layers, least-important first
 */
static GList *
load_vulkan_layer_json (SrtSysroot *sysroot,
                        const gchar *path)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(JsonParser) parser = NULL;
  JsonNode *node = NULL;
  JsonNode *arr_node = NULL;
  JsonObject *object = NULL;
  JsonObject *json_layer = NULL;
  JsonArray *json_layers = NULL;
  const gchar *file_format_version = NULL;
  g_autofree gchar *contents = NULL;
  g_autofree gchar *canon = NULL;
  gsize contents_len = 0;
  guint length;
  gsize i;
  GList *ret_list = NULL;
  SrtLoadableIssues issues = SRT_LOADABLE_ISSUES_CANNOT_LOAD;
  g_autoptr(SrtVulkanLayer) layer = NULL;

  g_return_val_if_fail (SRT_IS_SYSROOT (sysroot), NULL);
  g_return_val_if_fail (path != NULL, NULL);

  if (!g_path_is_absolute (path))
    {
      canon = g_canonicalize_filename (path, NULL);
      path = canon;
    }

  g_debug ("Attempting to load JSON layer from %s%s",
           sysroot->path, path);

  if (!_srt_sysroot_load (sysroot, path, SRT_RESOLVE_FLAGS_NONE,
                          NULL, &contents, &contents_len, &error))
    goto return_error;

  if (G_UNLIKELY (contents_len > G_MAXSSIZE))
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unreasonably large JSON file \"%s%s\"",
                   sysroot->path, path);
      goto return_error;
    }

  if (G_UNLIKELY (strnlen (contents, contents_len + 1) < contents_len))
    {
      /* In practice json-glib does diagnose this as an error, but the
       * error message is misleading (it claims the file isn't UTF-8). */
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "JSON file \"%s%s\" contains \\0",
                   sysroot->path, path);
      goto return_error;
    }

  parser = json_parser_new ();

  if (!json_parser_load_from_data (parser, contents, contents_len, &error))
    {
      g_debug ("error %s", error->message);
      goto return_error;
    }

  node = json_parser_get_root (parser);

  if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Expected to find a JSON object in \"%s\"", path);
      goto return_error;
    }

  object = json_node_get_object (node);

  file_format_version = json_object_get_string_member_with_default (object,
                                                                    "file_format_version",
                                                                    NULL);

  if (file_format_version == NULL)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "file_format_version in \"%s\" is missing or not a string", path);
      goto return_error;
    }

  /* At the time of writing the latest layer manifest file version is
   * 1.2.1 and forward compatibility is not guaranteed */
  if (strverscmp (file_format_version, "1.2.1") <= 0)
    {
      g_debug ("file_format_version is \"%s\"", file_format_version);
    }
  else
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Vulkan layer file_format_version \"%s\" in \"%s\" is not supported",
                   file_format_version, path);
      issues = SRT_LOADABLE_ISSUES_UNSUPPORTED;
      goto return_error;
    }

  if (json_object_has_member (object, "layers"))
    {
      arr_node = json_object_get_member (object, "layers");
      if (arr_node != NULL && JSON_NODE_HOLDS_ARRAY (arr_node))
        json_layers = json_node_get_array (arr_node);

      if (json_layers == NULL)
        {
          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "\"layers\" in \"%s\" is not an array as expected", path);
          goto return_error;
        }
      length = json_array_get_length (json_layers);
      for (i = 0; i < length; i++)
        {
          json_layer = json_array_get_object_element (json_layers, i);
          if (json_layer == NULL)
            {
              /* Try to continue parsing */
              g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "the layer in \"%s\" is not an object as expected", path);
              layer = srt_vulkan_layer_new_error (path,
                                                  SRT_LOADABLE_ISSUES_CANNOT_LOAD,
                                                  error);
              g_clear_error (&error);
            }
          else
            {
              layer = vulkan_layer_parse_json (path, file_format_version, json_layer);
            }

          ret_list = g_list_prepend (ret_list, g_steal_pointer (&layer));
        }
    }
  else if (json_object_has_member (object, "layer"))
    {
      json_layer = json_object_get_object_member (object, "layer");
      if (json_layer == NULL)
        {
          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "\"layer\" in \"%s\" is not an object as expected", path);
          goto return_error;
        }

      layer = vulkan_layer_parse_json (path, file_format_version, json_layer);
      ret_list = g_list_prepend (ret_list, g_steal_pointer (&layer));
    }
  else
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "The layer definitions in \"%s\" is missing both the \"layer\" and \"layers\" fields",
                   path);
      goto return_error;
    }

  return ret_list;

return_error:
  g_assert (error != NULL);
  layer = srt_vulkan_layer_new_error (path, issues, error);
  return g_list_prepend (ret_list, g_steal_pointer (&layer));
}

static void
vulkan_layer_load_json (SrtSysroot *sysroot,
                        const char *filename,
                        GList **list)
{
  g_return_if_fail (SRT_IS_SYSROOT (sysroot));
  g_return_if_fail (filename != NULL);
  g_return_if_fail (list != NULL);

  *list = g_list_concat (load_vulkan_layer_json (sysroot, filename), *list);
}

static void
vulkan_layer_load_json_cb (SrtSysroot *sysroot,
                           const char *filename,
                           void *user_data)
{
  vulkan_layer_load_json (sysroot, filename, user_data);
}

/*
 * _srt_load_vulkan_layers_extended:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @runner: The execution environment
 * @multiarch_tuples: (nullable): If not %NULL, duplicated
 *  Vulkan layers are searched by their absolute path, obtained using
 *  'inspect-library' in the provided multiarch tuples, instead of just their
 *  resolved library path.
 * @explicit: If %TRUE, load explicit layers, otherwise load implicit layers.
 * @check_flags: Whether to check for problems
 *
 * Implementation of srt_system_info_list_explicit_vulkan_layers() and
 * srt_system_info_list_implicit_vulkan_layers().
 *
 * Returns: (transfer full) (element-type SrtVulkanLayer): A list of Vulkan
 *  layers, most-important first
 */
GList *
_srt_load_vulkan_layers_extended (SrtSysroot *sysroot,
                                  SrtSubprocessRunner *runner,
                                  const char * const *multiarch_tuples,
                                  gboolean explicit,
                                  SrtCheckFlags check_flags)
{
  GList *ret = NULL;
  g_auto(GStrv) search_paths = NULL;
  const gchar *value = NULL;
  const gchar *suffix;
  const char * const *envp;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (SRT_IS_SYSROOT (sysroot), NULL);
  g_return_val_if_fail (SRT_IS_SUBPROCESS_RUNNER (runner), NULL);

  if (explicit)
    suffix = _SRT_GRAPHICS_EXPLICIT_VULKAN_LAYER_SUFFIX;
  else
    suffix = _SRT_GRAPHICS_IMPLICIT_VULKAN_LAYER_SUFFIX;

  envp = _srt_subprocess_runner_get_environ (runner);
  value = _srt_environ_getenv (envp, "VK_LAYER_PATH");

  /* As in the Vulkan-Loader implementation, implicit layers are not
   * overridden by "VK_LAYER_PATH"
   * https://github.com/KhronosGroup/Vulkan-Loader/blob/v1.3.207/docs/LoaderApplicationInterface.md#forcing-layer-source-folders
   */
  if (value != NULL && explicit)
    {
      g_auto(GStrv) dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);
      g_debug ("Vulkan explicit layer search path overridden to: %s", value);
      load_json_dirs (sysroot, dirs, NULL, _srt_indirect_strcmp0,
                      vulkan_layer_load_json_cb, &ret);
    }
  else
    {
      const gchar *add = NULL;

      if (explicit)
        add = _srt_environ_getenv (envp, "VK_ADD_LAYER_PATH");

      if (add != NULL)
        {
          g_auto(GStrv) dirs = g_strsplit (add, G_SEARCHPATH_SEPARATOR_S, -1);
          g_debug ("Vulkan additional explicit layer search path: %s", add);
          load_json_dirs (sysroot, dirs, NULL, _srt_indirect_strcmp0,
                          vulkan_layer_load_json_cb, &ret);
        }

      search_paths = _srt_graphics_get_vulkan_search_paths (sysroot, envp,
                                                            multiarch_tuples,
                                                            suffix);
      g_debug ("Using normal Vulkan layer search path");
      g_debug ("SEARCH PATHS %s", search_paths[0]);
      load_json_dirs (sysroot, search_paths, NULL, _srt_indirect_strcmp0,
                      vulkan_layer_load_json_cb, &ret);
    }

  if (!(check_flags & SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS))
    _srt_loadable_flag_duplicates (SRT_TYPE_VULKAN_LAYER, runner,
                                   multiarch_tuples, ret);

  return g_list_reverse (ret);
}

/*
 * _srt_load_vulkan_layers:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ`
 *  was this array
 * @explicit: If %TRUE, load explicit layers, otherwise load implicit layers.
 *
 * This function has been deprecated, use _srt_load_vulkan_layers_extended()
 * instead
 *
 * Returns: (transfer full) (element-type SrtVulkanLayer): A list of Vulkan
 *  layers, most-important first
 */
GList *
_srt_load_vulkan_layers (const char *sysroot,
                         gchar **envp,
                         gboolean explicit)
{
  g_autoptr(SrtSysroot) sysroot_object = NULL;
  g_autoptr(SrtSubprocessRunner) runner = NULL;
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail (sysroot != NULL, NULL);
  g_return_val_if_fail (envp != NULL, NULL);

  sysroot_object = _srt_sysroot_new (sysroot, &local_error);

  if (sysroot_object == NULL)
    {
      g_warning ("%s", local_error->message);
      return NULL;
    }

  runner = _srt_subprocess_runner_new_full (_srt_const_strv (envp),
                                            NULL,
                                            NULL,
                                            SRT_TEST_FLAGS_NONE);
  return _srt_load_vulkan_layers_extended (sysroot_object, runner, NULL,
                                           explicit, SRT_CHECK_FLAGS_NONE);
}

static SrtVulkanLayer *
vulkan_layer_dup (SrtVulkanLayer *self)
{
  SrtBaseGraphicsModule *base;
  GHashTableIter iter;
  gpointer key;
  gpointer value;
  const GList *l;

  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);

  base = SRT_BASE_GRAPHICS_MODULE (self);

  SrtVulkanLayer *ret = srt_vulkan_layer_new (self->parent.json_path,
                                              self->parent.name,
                                              self->parent.type,
                                              base->library_path,
                                              self->parent.library_arch,
                                              self->parent.api_version,
                                              self->parent.implementation_version,
                                              self->parent.description,
                                              self->parent.component_layers,
                                              base->issues);

  ret->parent.file_format_version = g_strdup (self->parent.file_format_version);

  if (self->parent.functions != NULL)
    {
      ret->parent.functions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
      g_hash_table_iter_init (&iter, self->parent.functions);
      while (g_hash_table_iter_next (&iter, &key, &value))
        g_hash_table_insert (ret->parent.functions, g_strdup (key), g_strdup (value));
    }

  if (self->parent.pre_instance_functions != NULL)
    {
      ret->parent.pre_instance_functions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                  g_free, g_free);
      g_hash_table_iter_init (&iter, self->parent.pre_instance_functions);
      while (g_hash_table_iter_next (&iter, &key, &value))
        g_hash_table_insert (ret->parent.pre_instance_functions, g_strdup (key), g_strdup (value));
    }

  for (l = self->parent.instance_extensions; l != NULL; l = l->next)
    {
      InstanceExtension *ie = g_slice_new0 (InstanceExtension);
      InstanceExtension *self_ie = l->data;
      ie->name = g_strdup (self_ie->name);
      ie->spec_version = g_strdup (self_ie->spec_version);
      ret->parent.instance_extensions = g_list_prepend (ret->parent.instance_extensions, ie);
    }
  ret->parent.instance_extensions = g_list_reverse (ret->parent.instance_extensions);

  for (l = self->parent.device_extensions; l != NULL; l = l->next)
    {
      DeviceExtension *de = g_slice_new0 (DeviceExtension);
      DeviceExtension *self_de = l->data;
      de->name = g_strdup (self_de->name);
      de->spec_version = g_strdup (self_de->spec_version);
      de->entrypoints = g_strdupv (self_de->entrypoints);
      ret->parent.device_extensions = g_list_prepend (ret->parent.device_extensions, de);
    }
  ret->parent.device_extensions = g_list_reverse (ret->parent.device_extensions);

  ret->parent.enable_env_var.name = g_strdup (self->parent.enable_env_var.name);
  ret->parent.enable_env_var.value = g_strdup (self->parent.enable_env_var.value);

  ret->parent.disable_env_var.name = g_strdup (self->parent.disable_env_var.name);
  ret->parent.disable_env_var.value = g_strdup (self->parent.disable_env_var.value);

  return ret;
}

/**
 * srt_vulkan_layer_new_replace_library_path:
 * @self: A Vulkan layer
 * @path: (type filename) (transfer none): A path
 *
 * Return a copy of @self with the srt_vulkan_layer_get_library_path()
 * changed to @path. For example, this is useful when setting up a
 * container where the underlying shared object will be made available
 * at a different absolute path.
 *
 * If @self does not have #SrtVulkanLayer:library-path set, or if it
 * is in an error state, this returns a new reference to @self.
 *
 * Returns: (transfer full): A new reference to a #SrtVulkanLayer. Free with
 *  g_object_unref().
 */
SrtVulkanLayer *
srt_vulkan_layer_new_replace_library_path (SrtVulkanLayer *self,
                                           const gchar *library_path)
{
  SrtBaseGraphicsModule *base;
  SrtVulkanLayer *ret = NULL;

  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  g_return_val_if_fail (library_path != NULL, NULL);

  base = SRT_BASE_GRAPHICS_MODULE (self);

  if (base->error != NULL || base->library_path == NULL)
    return g_object_ref (self);

  ret = vulkan_layer_dup (self);
  g_return_val_if_fail (ret != NULL, NULL);

  g_free (((SrtBaseGraphicsModule *) ret)->library_path);
  ((SrtBaseGraphicsModule *) ret)->library_path = g_strdup (library_path);

  return ret;
}

/**
 * srt_vulkan_layer_write_to_file:
 * @self: The Vulkan layer
 * @path: (type filename): A filename
 * @error: Used to describe the error on failure
 *
 * Serialize @self to the given JSON file.
 *
 * Returns: %TRUE on success
 */
gboolean
srt_vulkan_layer_write_to_file (SrtVulkanLayer *self,
                                const char *path,
                                GError **error)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return _srt_base_json_graphics_module_write_to_file (&self->parent,
                                                       path,
                                                       SRT_TYPE_VULKAN_LAYER,
                                                       error);
}

/**
 * srt_vulkan_layer_get_json_path:
 * @self: The Vulkan layer
 *
 * Return the absolute path to the JSON file representing this layer.
 *
 * Returns: (type filename) (transfer none) (not nullable):
 *  #SrtVulkanLayer:json-path
 */
const gchar *
srt_vulkan_layer_get_json_path (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->parent.json_path;
}

/**
 * srt_vulkan_layer_get_library_path:
 * @self: The Vulkan layer
 *
 * Return the library path for this layer. It is either an absolute path,
 * a path relative to srt_vulkan_layer_get_json_path() containing at least one
 * directory separator (slash), or a basename to be loaded from the
 * shared library search path.
 *
 * If the JSON description for this layer could not be loaded, or if
 * #SrtVulkanLayer:component_layers is used, return %NULL instead.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtVulkanLayer:library-path
 */
const gchar *
srt_vulkan_layer_get_library_path (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return SRT_BASE_GRAPHICS_MODULE (self)->library_path;
}

/**
 * srt_vulkan_layer_get_library_arch:
 * @self: The Vulkan layer
 *
 * Return a string that describes the architecture of the binary
 * associated with #SrtVulkanLayer:library-path.
 * The meaning is the same as for srt_vulkan_icd_get_library_arch().
 *
 * This is an optional field, so if it was not available in the JSON,
 * or if the layer description could not be loaded, %NULL will be returned.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtVulkanLayer:library-arch
 */
const gchar *
srt_vulkan_layer_get_library_arch (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->parent.library_arch;
}

/**
 * srt_vulkan_layer_get_name:
 * @self: The Vulkan layer
 *
 * Return the name that uniquely identify this layer.
 *
 * If the JSON description for this layer could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type utf8) (transfer none) (nullable): #SrtVulkanLayer:name
 */
const gchar *
srt_vulkan_layer_get_name (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->parent.name;
}

/**
 * srt_vulkan_layer_get_description:
 * @self: The Vulkan layer
 *
 * Return the description of this layer.
 *
 * If the JSON description for this layer could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type utf8) (transfer none) (nullable): #SrtVulkanLayer:description
 */
const gchar *
srt_vulkan_layer_get_description (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->parent.description;
}

/**
 * srt_vulkan_layer_get_api_version:
 * @self: The Vulkan layer
 *
 * Return the Vulkan API version of this layer.
 *
 * If the JSON description for this layer could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type utf8) (transfer none) (nullable): #SrtVulkanLayer:api_version
 */
const gchar *
srt_vulkan_layer_get_api_version (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->parent.api_version;
}

/**
 * srt_vulkan_layer_get_type_value:
 * @self: The Vulkan layer
 *
 * Return the type of this layer.
 * The expected values should be either "GLOBAL" or "INSTANCE".
 *
 * If the JSON description for this layer could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type utf8) (transfer none) (nullable): #SrtVulkanLayer:type
 */
const gchar *
srt_vulkan_layer_get_type_value (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->parent.type;
}

/**
 * srt_vulkan_layer_get_implementation_version:
 * @self: The Vulkan layer
 *
 * Return the version of the implemented layer.
 *
 * If the JSON description for this layer could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type utf8) (transfer none) (nullable):
 *  #SrtVulkanLayer:implementation_version
 */
const gchar *
srt_vulkan_layer_get_implementation_version (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->parent.implementation_version;
}

/**
 * srt_vulkan_layer_get_component_layers:
 * @self: The Vulkan layer
 *
 * Return the component layer names that are part of a meta-layer.
 *
 * If the JSON description for this layer could not be loaded, or if
 * #SrtVulkanLayer:library-path is used, return %NULL instead.
 *
 * Returns: (array zero-terminated=1) (transfer none) (element-type utf8) (nullable):
 *  #SrtVulkanLayer:component_layers
 */
const char * const *
srt_vulkan_layer_get_component_layers (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return (const char * const *) self->parent.component_layers;
}

/**
 * srt_vulkan_layer_get_issues:
 * @self: The Vulkan layer
 *
 * Return the problems found when parsing and loading @self.
 *
 * Returns: A bitfield containing problems, or %SRT_LOADABLE_ISSUES_NONE
 *  if no problems were found
 */
SrtLoadableIssues
srt_vulkan_layer_get_issues (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), SRT_LOADABLE_ISSUES_UNKNOWN);
  return SRT_BASE_GRAPHICS_MODULE (self)->issues;
}

/**
 * srt_vulkan_layer_resolve_library_path:
 * @self: A Vulkan layer
 *
 * Return the path that can be passed to `dlopen()` for this layer.
 *
 * If srt_vulkan_layer_get_library_path() is a relative path, return the
 * absolute path that is the result of interpreting it relative to
 * srt_vulkan_layer_get_json_path(). Otherwise return a copy of
 * srt_vulkan_layer_get_library_path().
 *
 * The result is either the basename of a shared library (to be found
 * relative to some directory listed in `$LD_LIBRARY_PATH`, `/etc/ld.so.conf`,
 * `/etc/ld.so.conf.d` or the hard-coded library search path), or an
 * absolute path.
 *
 * Returns: (transfer full) (type filename) (nullable): The basename of a
 *  shared library or an absolute path. Free with g_free().
 */
gchar *
srt_vulkan_layer_resolve_library_path (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return _srt_base_graphics_module_resolve_library_path (SRT_BASE_GRAPHICS_MODULE (self));
}

/**
 * srt_vulkan_layer_check_error:
 * @self: The layer
 * @error: Used to return details if the layer description could not be loaded
 *
 * Check whether we failed to load the JSON describing this Vulkan layer.
 * Note that this does not actually `dlopen()` the layer itself.
 *
 * Returns: %TRUE if the JSON was loaded successfully
 */
gboolean
srt_vulkan_layer_check_error (const SrtVulkanLayer *self,
                              GError **error)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return _srt_base_graphics_module_check_error (SRT_BASE_GRAPHICS_MODULE (self), error);
}

void
_srt_vulkan_layer_set_library_arch (SrtVulkanLayer *self,
                                    const char *library_arch)
{
  g_return_if_fail (SRT_IS_VULKAN_LAYER (self));
  _srt_base_json_graphics_module_set_library_arch (&self->parent,
                                                   library_arch,
                                                   "1.2.1");
}
