/*
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "steam-runtime-tools/glib-backports-internal.h"

#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include "pressure-vessel/adverb-preload.h"
#include "pressure-vessel/flatpak-exports-private.h"
#include "pressure-vessel/runtime.h"

#include "pressure-vessel/wrap-interactive.h"

typedef struct _PvWrapContext PvWrapContext;
typedef struct _PvWrapContextClass PvWrapContextClass;

/*
 * PvWrapExportFlags:
 * @PV_WRAP_EXPORT_FLAGS_OS_QUIET: Quietly ignore OS paths such as /usr/share
 *  instead of logging a warning
 * @PV_WRAP_EXPORT_FLAGS_NONE: None of the above
 *
 * Flags affecting how we export paths.
 */
typedef enum
{
  PV_WRAP_EXPORT_FLAGS_OS_QUIET = (1 << 0),
  PV_WRAP_EXPORT_FLAGS_NONE = 0
} PvWrapExportFlags;

typedef enum
{
  TRISTATE_NO = 0,
  TRISTATE_YES,
  TRISTATE_MAYBE
} Tristate;

typedef struct
{
  PvPreloadVariableIndex which;
  gchar *preload;
} WrapPreloadModule;

typedef struct
{
  GStrv env_if_host;
  GStrv filesystems;
  gchar *freedesktop_app_id;
  gchar *graphics_provider;
  gchar *home;
  GArray *pass_fds;
  GArray *preload_modules;
  gchar *runtime;
  gchar *runtime_base;
  gchar *steam_app_id;
  gchar *variable_dir;
  gchar *write_final_argv;

  double terminate_idle_timeout;
  double terminate_timeout;

  PvShell shell;
  PvTerminal terminal;
  Tristate share_home;

  gboolean batch;
  gboolean copy_runtime;
  gboolean deterministic;
  gboolean devel;
  gboolean gc_runtimes;
  gboolean generate_locales;
  gboolean import_ca_certs;
  gboolean import_vulkan_layers;
  gboolean launcher;
  gboolean only_prepare;
  gboolean remove_game_overlay;
  gboolean share_pid;
  gboolean single_thread;
  gboolean systemd_scope;
  gboolean test;
  gboolean verbose;
  gboolean version;
  gboolean version_only;
} PvWrapOptions;

struct _PvWrapContext
{
  GObject parent_instance;

  GHashTable *paths_not_exported;
  PvRuntime *runtime;
  SrtSysroot *current_root;
  gchar **original_argv;
  gchar **original_environ;

  PvWrapOptions options;

  gboolean is_flatpak_env;
  int original_argc;
};

#define PV_TYPE_WRAP_CONTEXT \
  (pv_wrap_context_get_type ())
#define PV_WRAP_CONTEXT(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), PV_TYPE_WRAP_CONTEXT, PvWrapContext))
#define PV_IS_WRAP_CONTEXT(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), PV_TYPE_WRAP_CONTEXT))
#define PV_WRAP_CONTEXT_GET_CLASS(o) \
  (G_TYPE_INSTANCE_GET_CLASS ((o), PV_TYPE_WRAP_CONTEXT, PvWrapContextClass))
#define PV_WRAP_CONTEXT_CLASS(c) \
  (G_TYPE_CHECK_CLASS_CAST ((c), PV_TYPE_WRAP_CONTEXT, PvWrapContextClass))
#define PV_IS_WRAP_CONTEXT_CLASS(c) \
  (G_TYPE_CHECK_CLASS_TYPE ((c), PV_TYPE_WRAP_CONTEXT))

GType pv_wrap_context_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PvWrapContext, g_object_unref)

PvWrapContext *pv_wrap_context_new (SrtSysroot *current_root,
                                    GError **error);

gboolean pv_wrap_options_parse_environment (PvWrapOptions *self,
                                            GError **error);

gboolean pv_wrap_context_parse_argv (PvWrapContext *self,
                                     int *argcp,
                                     char ***argvp,
                                     GError **error);

gboolean pv_wrap_options_parse_argv (PvWrapOptions *self,
                                     int *argcp,
                                     char ***argvp,
                                     GError **error);

gboolean pv_wrap_options_parse_environment_after_argv (PvWrapOptions *self,
                                                       SrtSysroot *interpreter_root,
                                                       GError **error);

gboolean pv_wrap_context_export_if_allowed (PvWrapContext *self,
                                            FlatpakExports *exports,
                                            FlatpakFilesystemMode export_mode,
                                            const char *path,
                                            const char *host_path,
                                            const char *source,
                                            const char *before,
                                            const char *after,
                                            PvWrapExportFlags flags);
