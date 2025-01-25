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

/**
 * SECTION:graphics-drivers-egl
 * @title: EGL graphics driver enumeration
 * @short_description: Get information about the system's EGL drivers
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtEglIcd is an opaque object representing the metadata describing
 * an EGL ICD.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 *
 * Similarly, #SrtEglExternalPlatform is an opaque object representing
 * an EGL external platform module, as used with the NVIDIA proprietary
 * driver.
 */

/**
 * SrtEglExternalPlatform:
 *
 * Opaque object representing an EGL external platform module.
 */

struct _SrtEglExternalPlatformClass
{
  /*< private >*/
  SrtBaseJsonGraphicsModuleClass parent_class;
};

G_DEFINE_TYPE (SrtEglExternalPlatform,
               srt_egl_external_platform,
               SRT_TYPE_BASE_JSON_GRAPHICS_MODULE)

static void
srt_egl_external_platform_init (SrtEglExternalPlatform *self)
{
}

static void
srt_egl_external_platform_constructed (GObject *object)
{
  SrtEglExternalPlatform *self = SRT_EGL_EXTERNAL_PLATFORM (object);
  SrtBaseGraphicsModule *base = SRT_BASE_GRAPHICS_MODULE (object);

  G_OBJECT_CLASS (srt_egl_external_platform_parent_class)->constructed (object);

  g_return_if_fail (self->parent.api_version == NULL);

  if (base->error != NULL)
    g_return_if_fail (base->library_path == NULL);
  else
    g_return_if_fail (base->library_path != NULL);
}

static void
srt_egl_external_platform_class_init (SrtEglExternalPlatformClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->constructed = srt_egl_external_platform_constructed;
}

/*
 * srt_egl_external_platform_new:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @library_path: (transfer none): the path to the library
 * @issues: problems with this module
 *
 * Returns: (transfer full): a new module
 */
SrtEglExternalPlatform *
srt_egl_external_platform_new (const gchar *json_path,
                               const gchar *library_path,
                               SrtLoadableIssues issues)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_EGL_EXTERNAL_PLATFORM,
                       "json-path", json_path,
                       "library-path", library_path,
                       "issues", issues,
                       NULL);
}

/*
 * srt_egl_external_platform_new_error:
 * @issues: problems with this module
 * @error: (transfer none): Error that occurred when loading the module
 *
 * Returns: (transfer full): a new module
 */
SrtEglExternalPlatform *
srt_egl_external_platform_new_error (const gchar *json_path,
                                     SrtLoadableIssues issues,
                                     const GError *error)
{
  return (SrtEglExternalPlatform *) _srt_base_json_graphics_module_new_error (SRT_TYPE_EGL_EXTERNAL_PLATFORM,
                                                                              json_path,
                                                                              issues,
                                                                              error);
}

/**
 * srt_egl_external_platform_check_error:
 * @self: The module
 * @error: Used to return #SrtEglExternalPlatform:error if the module description could
 *  not be loaded
 *
 * Check whether we failed to load the JSON describing this EGL external
 * platform module.
 * Note that this does not actually `dlopen()` the module itself.
 *
 * Returns: %TRUE if the JSON was loaded successfully
 */
gboolean
srt_egl_external_platform_check_error (SrtEglExternalPlatform *self,
                                       GError **error)
{
  g_return_val_if_fail (SRT_IS_EGL_EXTERNAL_PLATFORM (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return _srt_base_graphics_module_check_error (SRT_BASE_GRAPHICS_MODULE (self), error);
}

/**
 * srt_egl_external_platform_get_json_path:
 * @self: The module
 *
 * Return the absolute path to the JSON file representing this module.
 *
 * Returns: (type filename) (transfer none): #SrtEglExternalPlatform:json-path
 */
const gchar *
srt_egl_external_platform_get_json_path (SrtEglExternalPlatform *self)
{
  g_return_val_if_fail (SRT_IS_EGL_EXTERNAL_PLATFORM (self), NULL);
  return self->parent.json_path;
}

/**
 * srt_egl_external_platform_get_library_path:
 * @self: The module
 *
 * Return the library path for this module. It is either an absolute path,
 * a path relative to srt_egl_external_platform_get_json_path() containing at least one
 * directory separator (slash), or a basename to be loaded from the
 * shared library search path.
 *
 * If the JSON description for this module could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtEglExternalPlatform:library-path
 */
const gchar *
srt_egl_external_platform_get_library_path (SrtEglExternalPlatform *self)
{
  g_return_val_if_fail (SRT_IS_EGL_EXTERNAL_PLATFORM (self), NULL);
  return SRT_BASE_GRAPHICS_MODULE (self)->library_path;
}

/**
 * srt_egl_external_platform_get_issues:
 * @self: The module
 *
 * Return the problems found when parsing and loading @self.
 *
 * Returns: A bitfield containing problems, or %SRT_LOADABLE_ISSUES_NONE
 *  if no problems were found
 */
SrtLoadableIssues
srt_egl_external_platform_get_issues (SrtEglExternalPlatform *self)
{
  g_return_val_if_fail (SRT_IS_EGL_EXTERNAL_PLATFORM (self), SRT_LOADABLE_ISSUES_UNKNOWN);
  return SRT_BASE_GRAPHICS_MODULE (self)->issues;
}

/**
 * srt_egl_external_platform_resolve_library_path:
 * @self: A module
 *
 * Return the path that can be passed to `dlopen()` for this module.
 *
 * If srt_egl_external_platform_get_library_path() is a relative path, return the
 * absolute path that is the result of interpreting it relative to
 * an appropriate location (the exact interpretation is subject to change,
 * depending on upstream decisions).
 *
 * Otherwise return a copy of srt_egl_external_platform_get_library_path().
 *
 * The result is either the basename of a shared library (to be found
 * relative to some directory listed in `$LD_LIBRARY_PATH`, `/etc/ld.so.conf`,
 * `/etc/ld.so.conf.d` or the hard-coded library search path), or an
 * absolute path.
 *
 * Returns: (transfer full) (type filename) (nullable): A copy
 *  of #SrtEglExternalPlatform:resolved-library-path. Free with g_free().
 */
gchar *
srt_egl_external_platform_resolve_library_path (SrtEglExternalPlatform *self)
{
  g_return_val_if_fail (SRT_IS_EGL_EXTERNAL_PLATFORM (self), NULL);
  return _srt_base_graphics_module_resolve_library_path (SRT_BASE_GRAPHICS_MODULE (self));
}

/**
 * srt_egl_external_platform_new_replace_library_path:
 * @self: A module
 * @path: (type filename) (transfer none): A path
 *
 * Return a copy of @self with the srt_egl_external_platform_get_library_path()
 * changed to @path. For example, this is useful when setting up a
 * container where the underlying shared object will be made available
 * at a different absolute path.
 *
 * If @self is in an error state, this returns a new reference to @self.
 *
 * Returns: (transfer full): A new reference to a #SrtEglExternalPlatform. Free with
 *  g_object_unref().
 */
SrtEglExternalPlatform *
srt_egl_external_platform_new_replace_library_path (SrtEglExternalPlatform *self,
                                                    const char *path)
{
  SrtBaseGraphicsModule *base;

  g_return_val_if_fail (SRT_IS_EGL_EXTERNAL_PLATFORM (self), NULL);

  base = SRT_BASE_GRAPHICS_MODULE (self);

  if (base->error != NULL)
    return g_object_ref (self);

  return srt_egl_external_platform_new (self->parent.json_path, path, base->issues);
}

/**
 * srt_egl_external_platform_write_to_file:
 * @self: A module
 * @path: (type filename): A filename
 * @error: Used to describe the error on failure
 *
 * Serialize @self to the given JSON file.
 *
 * Returns: %TRUE on success
 */
gboolean
srt_egl_external_platform_write_to_file (SrtEglExternalPlatform *self,
                                         const char *path,
                                         GError **error)
{
  g_return_val_if_fail (SRT_IS_EGL_EXTERNAL_PLATFORM (self), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return _srt_base_json_graphics_module_write_to_file (&self->parent,
                                                       path,
                                                       SRT_TYPE_EGL_EXTERNAL_PLATFORM,
                                                       error);
}

/**
 * SrtEglIcd:
 *
 * Opaque object representing an EGL ICD.
 */

struct _SrtEglIcdClass
{
  /*< private >*/
  SrtBaseJsonGraphicsModuleClass parent_class;
};

G_DEFINE_TYPE (SrtEglIcd, srt_egl_icd, SRT_TYPE_BASE_JSON_GRAPHICS_MODULE)

static void
srt_egl_icd_init (SrtEglIcd *self)
{
}

static void
srt_egl_icd_constructed (GObject *object)
{
  SrtEglIcd *self = SRT_EGL_ICD (object);
  SrtBaseGraphicsModule *base = SRT_BASE_GRAPHICS_MODULE (object);

  G_OBJECT_CLASS (srt_egl_icd_parent_class)->constructed (object);

  g_return_if_fail (self->parent.api_version == NULL);

  if (base->error != NULL)
    g_return_if_fail (base->library_path == NULL);
  else
    g_return_if_fail (base->library_path != NULL);
}

static void
srt_egl_icd_class_init (SrtEglIcdClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->constructed = srt_egl_icd_constructed;
}

/*
 * srt_egl_icd_new:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @library_path: (transfer none): the path to the library
 * @issues: problems with this ICD
 *
 * Returns: (transfer full): a new ICD
 */
SrtEglIcd *
srt_egl_icd_new (const gchar *json_path,
                 const gchar *library_path,
                 SrtLoadableIssues issues)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_EGL_ICD,
                       "json-path", json_path,
                       "library-path", library_path,
                       "issues", issues,
                       NULL);
}

/*
 * srt_egl_icd_new_error:
 * @issues: problems with this ICD
 * @error: (transfer none): Error that occurred when loading the ICD
 *
 * Returns: (transfer full): a new ICD
 */
SrtEglIcd *
srt_egl_icd_new_error (const gchar *json_path,
                       SrtLoadableIssues issues,
                       const GError *error)
{
  return (SrtEglIcd *) _srt_base_json_graphics_module_new_error (SRT_TYPE_EGL_ICD,
                                                                 json_path,
                                                                 issues,
                                                                 error);
}

/**
 * srt_egl_icd_check_error:
 * @self: The ICD
 * @error: Used to return #SrtEglIcd:error if the ICD description could
 *  not be loaded
 *
 * Check whether we failed to load the JSON describing this EGL ICD.
 * Note that this does not actually `dlopen()` the ICD itself.
 *
 * Returns: %TRUE if the JSON was loaded successfully
 */
gboolean
srt_egl_icd_check_error (SrtEglIcd *self,
                         GError **error)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return _srt_base_graphics_module_check_error (SRT_BASE_GRAPHICS_MODULE (self), error);
}

/**
 * srt_egl_icd_get_json_path:
 * @self: The ICD
 *
 * Return the absolute path to the JSON file representing this ICD.
 *
 * Returns: (type filename) (transfer none): #SrtEglIcd:json-path
 */
const gchar *
srt_egl_icd_get_json_path (SrtEglIcd *self)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), NULL);
  return self->parent.json_path;
}

/**
 * srt_egl_icd_get_library_path:
 * @self: The ICD
 *
 * Return the library path for this ICD. It is either an absolute path,
 * a path relative to srt_egl_icd_get_json_path() containing at least one
 * directory separator (slash), or a basename to be loaded from the
 * shared library search path.
 *
 * If the JSON description for this ICD could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtEglIcd:library-path
 */
const gchar *
srt_egl_icd_get_library_path (SrtEglIcd *self)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), NULL);
  return SRT_BASE_GRAPHICS_MODULE (self)->library_path;
}

/**
 * srt_egl_icd_get_issues:
 * @self: The ICD
 *
 * Return the problems found when parsing and loading @self.
 *
 * Returns: A bitfield containing problems, or %SRT_LOADABLE_ISSUES_NONE
 *  if no problems were found
 */
SrtLoadableIssues
srt_egl_icd_get_issues (SrtEglIcd *self)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), SRT_LOADABLE_ISSUES_UNKNOWN);
  return SRT_BASE_GRAPHICS_MODULE (self)->issues;
}

/**
 * srt_egl_icd_resolve_library_path:
 * @self: An ICD
 *
 * Return the path that can be passed to `dlopen()` for this ICD.
 *
 * If srt_egl_icd_get_library_path() is a relative path, return the
 * absolute path that is the result of interpreting it relative to
 * an appropriate location (the exact interpretation is subject to change,
 * depending on upstream decisions).
 *
 * Otherwise return a copy of srt_egl_icd_get_library_path().
 *
 * The result is either the basename of a shared library (to be found
 * relative to some directory listed in `$LD_LIBRARY_PATH`, `/etc/ld.so.conf`,
 * `/etc/ld.so.conf.d` or the hard-coded library search path), or an
 * absolute path.
 *
 * Returns: (transfer full) (type filename) (nullable): A copy
 *  of #SrtEglIcd:resolved-library-path. Free with g_free().
 */
gchar *
srt_egl_icd_resolve_library_path (SrtEglIcd *self)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), NULL);
  return _srt_base_graphics_module_resolve_library_path (SRT_BASE_GRAPHICS_MODULE (self));
}

/**
 * srt_egl_icd_new_replace_library_path:
 * @self: An ICD
 * @path: (type filename) (transfer none): A path
 *
 * Return a copy of @self with the srt_egl_icd_get_library_path()
 * changed to @path. For example, this is useful when setting up a
 * container where the underlying shared object will be made available
 * at a different absolute path.
 *
 * If @self is in an error state, this returns a new reference to @self.
 *
 * Returns: (transfer full): A new reference to a #SrtEglIcd. Free with
 *  g_object_unref().
 */
SrtEglIcd *
srt_egl_icd_new_replace_library_path (SrtEglIcd *self,
                                      const char *path)
{
  SrtBaseGraphicsModule *base;

  g_return_val_if_fail (SRT_IS_EGL_ICD (self), NULL);

  base = SRT_BASE_GRAPHICS_MODULE (self);

  if (base->error != NULL)
    return g_object_ref (self);

  return srt_egl_icd_new (self->parent.json_path, path, base->issues);
}

/**
 * srt_egl_icd_write_to_file:
 * @self: An ICD
 * @path: (type filename): A filename
 * @error: Used to describe the error on failure
 *
 * Serialize @self to the given JSON file.
 *
 * Returns: %TRUE on success
 */
gboolean
srt_egl_icd_write_to_file (SrtEglIcd *self,
                           const char *path,
                           GError **error)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return _srt_base_json_graphics_module_write_to_file (&self->parent,
                                                       path,
                                                       SRT_TYPE_EGL_ICD,
                                                       error);
}

static void
egl_icd_load_json_cb (SrtSysroot *sysroot,
                      const char *filename,
                      void *user_data)
{
  load_manifest_from_json (SRT_TYPE_EGL_ICD, sysroot, filename,
                           MANIFEST_JSON_MEMBER_NAME_ICD, user_data);
}

static void
egl_external_platform_load_json_cb (SrtSysroot *sysroot,
                                    const char *filename,
                                    void *user_data)
{
  load_manifest_from_json (SRT_TYPE_EGL_EXTERNAL_PLATFORM, sysroot, filename,
                           MANIFEST_JSON_MEMBER_NAME_ICD, user_data);
}

#define EGL_VENDOR_SUFFIX "glvnd/egl_vendor.d"

/*
 * Return the ${sysconfdir} that we assume GLVND has.
 *
 * steam-runtime-tools is typically installed in the Steam Runtime,
 * which is not part of the operating system, so we cannot assume
 * that our own prefix is the same as GLVND. Assume a conventional
 * OS-wide installation of GLVND.
 */
static const char *
get_glvnd_sysconfdir (void)
{
  return "/etc";
}

/*
 * Return the ${datadir} that we assume GLVND has. See above.
 */
static const char *
get_glvnd_datadir (void)
{
  return "/usr/share";
}

/*
 * _srt_load_egl_things:
 * @which: either %SRT_EGL_ICD or %SRT_EGL_EXTERNAL_PLATFORM
 * @helpers_path: (nullable): An optional path to find "inspect-library"
 *  helper, PATH is used if %NULL
 * @sysroot: (not nullable): The root directory, usually `/`
 * @runner: The execution environment
 * @multiarch_tuples: (nullable): If not %NULL, and a Flatpak environment
 *  is detected, assume a freedesktop-sdk-based runtime and look for
 *  GL extensions for these multiarch tuples. Also if not %NULL, duplicated
 *  EGL ICDs are searched by their absolute path, obtained using
 *  "inspect-library" in the provided multiarch tuples, instead of just their
 *  resolved library path.
 * @check_flags: Whether to check for problems
 *
 * Implementation of srt_system_info_list_egl_icds()
 * and srt_system_info_list_egl_external_platforms().
 *
 * Returns: (transfer full) (element-type GObject): A list of ICDs
 *  or external platform modules, most-important first
 */
GList *
_srt_load_egl_things (GType which,
                      SrtSysroot *sysroot,
                      SrtSubprocessRunner *runner,
                      const char * const *multiarch_tuples,
                      SrtCheckFlags check_flags)
{
  const gchar *value;
  gsize i;
  /* To avoid O(n**2) performance, we build this list in reverse order,
   * then reverse it at the end. */
  GList *ret = NULL;
  void (*loader_cb) (SrtSysroot *, const char *, void *);
  const char *filenames_var;
  const char *dirs_var;
  const char *suffix;
  const char *sysconfdir;
  const char *datadir;
  const char * const *envp;

  g_return_val_if_fail (which == SRT_TYPE_EGL_ICD
                        || which == SRT_TYPE_EGL_EXTERNAL_PLATFORM,
                        NULL);
  g_return_val_if_fail (SRT_IS_SYSROOT (sysroot), NULL);
  g_return_val_if_fail (SRT_IS_SUBPROCESS_RUNNER (runner), NULL);
  g_return_val_if_fail (_srt_check_not_setuid (), NULL);

  /* See
   * https://github.com/NVIDIA/libglvnd/blob/HEAD/src/EGL/icd_enumeration.md
   * for details of the search order for ICDs, and
   * https://github.com/NVIDIA/eglexternalplatform/issues/3,
   * https://github.com/NVIDIA/egl-wayland/issues/39 for attempts to
   * determine the search order for external platform modules. */

  if (which == SRT_TYPE_EGL_ICD)
    {
      filenames_var = "__EGL_VENDOR_LIBRARY_FILENAMES";
      dirs_var = "__EGL_VENDOR_LIBRARY_DIRS";
      loader_cb = egl_icd_load_json_cb;
      suffix = EGL_VENDOR_SUFFIX;
      sysconfdir = get_glvnd_sysconfdir ();
      datadir = get_glvnd_datadir ();
    }
  else if (which == SRT_TYPE_EGL_EXTERNAL_PLATFORM)
    {
      filenames_var = "__EGL_EXTERNAL_PLATFORM_CONFIG_FILENAMES";
      dirs_var = "__EGL_EXTERNAL_PLATFORM_CONFIG_DIRS";
      loader_cb = egl_external_platform_load_json_cb;
      suffix = "egl/egl_external_platform.d";
      /* These are hard-coded in libEGL_nvidia.so.0 and so do not vary
       * with ${prefix}, even if we could determine the prefix. */
      sysconfdir = "/etc";
      datadir = "/usr/share";
    }
  else
    {
      g_return_val_if_reached (NULL);
    }

  envp = _srt_subprocess_runner_get_environ (runner);
  value = _srt_environ_getenv (envp, filenames_var);

  if (value != NULL)
    {
      g_auto(GStrv) filenames = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);

      for (i = 0; filenames[i] != NULL; i++)
        load_manifest_from_json (which, sysroot, filenames[i],
                                 MANIFEST_JSON_MEMBER_NAME_ICD, &ret);
    }
  else
    {
      value = _srt_environ_getenv (envp, dirs_var);

      if (value != NULL)
        {
          g_auto(GStrv) dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);

          load_json_dirs (sysroot, dirs, NULL, _srt_indirect_strcmp0,
                          loader_cb, &ret);
        }
      else if (which == SRT_TYPE_EGL_ICD
               && _srt_sysroot_test (sysroot, "/.flatpak-info",
                                     SRT_RESOLVE_FLAGS_NONE, NULL)
               && multiarch_tuples != NULL)
        {
          g_debug ("Flatpak detected: assuming freedesktop-based runtime");

          for (i = 0; multiarch_tuples[i] != NULL; i++)
            {
              g_autofree gchar *tmp = NULL;

              /* freedesktop-sdk reconfigures the EGL loader to look here. */
              tmp = g_strdup_printf ("/usr/lib/%s/GL/" EGL_VENDOR_SUFFIX,
                                     multiarch_tuples[i]);
              load_json_dir (sysroot, tmp, NULL, _srt_indirect_strcmp0,
                             loader_cb, &ret);
            }
        }
      else
        {
          load_json_dir (sysroot, sysconfdir, suffix,
                         _srt_indirect_strcmp0, loader_cb, &ret);
          load_json_dir (sysroot, datadir, suffix,
                         _srt_indirect_strcmp0, loader_cb, &ret);
        }
    }

  if (!(check_flags & SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS))
    _srt_loadable_flag_duplicates (which, runner, multiarch_tuples, ret);

  return g_list_reverse (ret);
}
