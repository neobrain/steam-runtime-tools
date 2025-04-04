/*<private_header>*/
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

#pragma once

#include "steam-runtime-tools/steam-runtime-tools.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/graphics-drivers-internal.h"
#include "steam-runtime-tools/subprocess-internal.h"
#include "steam-runtime-tools/utils-internal.h"

typedef struct _SrtBaseJsonGraphicsModule SrtBaseJsonGraphicsModule;
typedef struct _SrtBaseJsonGraphicsModuleClass SrtBaseJsonGraphicsModuleClass;

#define SRT_TYPE_BASE_JSON_GRAPHICS_MODULE \
  (_srt_base_json_graphics_module_get_type ())
#define SRT_BASE_JSON_GRAPHICS_MODULE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_BASE_JSON_GRAPHICS_MODULE, SrtBaseJsonGraphicsModule))
#define SRT_BASE_JSON_GRAPHICS_MODULE_CLASS(cls) \
  (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_BASE_JSON_GRAPHICS_MODULE, SrtBaseJsonGraphicsModuleClass))
#define SRT_IS_BASE_JSON_GRAPHICS_MODULE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_BASE_JSON_GRAPHICS_MODULE))
#define SRT_IS_BASE_JSON_GRAPHICS_MODULE_CLASS(cls) \
  (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_BASE_JSON_GRAPHICS_MODULE))
#define SRT_BASE_JSON_GRAPHICS_MODULE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_BASE_JSON_GRAPHICS_MODULE, SrtBaseJsonGraphicsModuleClass)
GType _srt_base_json_graphics_module_get_type (void);

typedef struct
{
  gchar *name;
  gchar *spec_version;
  gchar **entrypoints;
} DeviceExtension;

typedef struct
{
  gchar *name;
  gchar *spec_version;
} InstanceExtension;

typedef struct
{
  gchar *name;
  gchar *value;
} EnvironmentVariable;

struct _SrtBaseJsonGraphicsModule
{
  SrtBaseGraphicsModule parent;

  gchar *api_version;   /* Always NULL when found in a SrtEglIcd */
  gchar *json_path;
  gchar *library_arch;
  gchar *file_format_version;
  gchar *name;
  gchar *type;
  gchar *implementation_version;
  gchar *description;
  GStrv component_layers;
  gboolean portability_driver;
  /* Standard name => dlsym() name to call instead
   * (element-type utf8 utf8) */
  GHashTable *functions;
  /* (element-type InstanceExtension) */
  GList *instance_extensions;
  /* Standard name to intercept => dlsym() name to call instead
   * (element-type utf8 utf8) */
  GHashTable *pre_instance_functions;
  /* (element-type DeviceExtension) */
  GList *device_extensions;
  EnvironmentVariable enable_env_var;
  EnvironmentVariable disable_env_var;
  gchar *original_json;
};

struct _SrtBaseJsonGraphicsModuleClass
{
  SrtBaseGraphicsModuleClass parent_class;
};

struct _SrtVulkanIcd
{
  /*< private >*/
  SrtBaseJsonGraphicsModule parent;
};

struct _SrtEglIcd
{
  /*< private >*/
  SrtBaseJsonGraphicsModule parent;
};

struct _SrtEglExternalPlatform
{
  /*< private >*/
  SrtBaseJsonGraphicsModule parent;
};

struct _SrtOpenXr1Runtime
{
  /*< private >*/
  SrtBaseJsonGraphicsModule parent;

  char *json_origin;
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtBaseJsonGraphicsModule, g_object_unref)

static inline void
device_extension_free (gpointer p)
{
  DeviceExtension *self = p;

  g_free (self->name);
  g_free (self->spec_version);
  g_strfreev (self->entrypoints);
  g_slice_free (DeviceExtension, self);
}

static inline void
instance_extension_free (gpointer p)
{
  InstanceExtension *self = p;

  g_free (self->name);
  g_free (self->spec_version);
  g_slice_free (InstanceExtension, self);
}

SrtBaseJsonGraphicsModule *_srt_base_json_graphics_module_new_error (GType type,
                                                                     const char *json_path,
                                                                     SrtLoadableIssues issues,
                                                                     const GError *error);
gboolean _srt_base_json_graphics_module_write_to_file (SrtBaseJsonGraphicsModule *self,
                                                       const char *path,
                                                       GType which,
                                                       GError **error);
void _srt_loadable_flag_duplicates (GType which,
                                    SrtSubprocessRunner *runner,
                                    const char * const *multiarch_tuples,
                                    GList *loadable);
void _srt_base_json_graphics_module_set_library_arch (SrtBaseJsonGraphicsModule *self,
                                                      const char *library_arch,
                                                      const char *min_file_format_version);
void _srt_base_json_graphics_module_take_original_json (SrtBaseJsonGraphicsModule *self,
                                                        gchar *contents);

/*
 * A #GCompareFunc that does not sort the members of the directory.
 */
#define READDIR_ORDER ((GCompareFunc) NULL)

void load_json_dir (SrtSysroot *sysroot,
                    const char *dir,
                    const char *suffix,
                    GCompareFunc sort,
                    void (*load_json_cb) (SrtSysroot *, const char *, void *),
                    void *user_data);
void load_json_dirs (SrtSysroot *sysroot,
                     const char * const *search_paths,
                     const char *suffix,
                     GCompareFunc sort,
                     void (*load_json_cb) (SrtSysroot *, const char *, void *),
                     void *user_data);
GObject *load_manifest_from_json (GType type,
                                  SrtSysroot *sysroot,
                                  const char *filename,
                                  const char *json_member_name);
void prepend_manifest_from_json (GType type,
                                 SrtSysroot *sysroot,
                                 const char *filename,
                                 const char *json_member_name,
                                 GList **list);

#define MANIFEST_JSON_MEMBER_NAME_ICD "ICD"
