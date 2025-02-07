/*
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "pressure-vessel/wrap-context.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include "pressure-vessel/exports.h"
#include "pressure-vessel/utils.h"

static Tristate
tristate_environment (const gchar *name)
{
  const gchar *value = g_getenv (name);

  if (g_strcmp0 (value, "1") == 0)
    return TRISTATE_YES;

  if (g_strcmp0 (value, "0") == 0)
    return TRISTATE_NO;

  if (value != NULL && value[0] != '\0')
    g_warning ("Unrecognised value \"%s\" for $%s", value, name);

  return TRISTATE_MAYBE;
}

static void
wrap_preload_module_clear (gpointer p)
{
  WrapPreloadModule *self = p;

  g_clear_pointer (&self->preload, g_free);
}

static void
pv_wrap_options_init (PvWrapOptions *self)
{
  self->pass_fds = g_array_new (FALSE, FALSE, sizeof (int));

  self->preload_modules = g_array_new (FALSE, FALSE,
                                       sizeof (WrapPreloadModule));
  g_array_set_clear_func (self->preload_modules, wrap_preload_module_clear);

  /* Set defaults */
  self->batch = FALSE;
  self->copy_runtime = FALSE;
  self->deterministic = FALSE;
  self->devel = FALSE;
  self->env_if_host = NULL;
  self->filesystems = NULL;
  self->gc_runtimes = TRUE;
  self->generate_locales = TRUE;
  self->graphics_provider = FALSE;
  self->import_ca_certs = FALSE;
  self->import_vulkan_layers = TRUE;
  self->launcher = FALSE;
  self->only_prepare = FALSE;
  self->remove_game_overlay = FALSE;
  self->runtime = NULL;
  self->runtime_base = NULL;
  self->share_home = TRISTATE_MAYBE;
  self->share_pid = TRUE;
  self->shell = PV_SHELL_NONE;
  self->single_thread = FALSE;
  self->systemd_scope = FALSE;
  self->terminal = PV_TERMINAL_AUTO;
  self->terminate_idle_timeout = 0.0;
  self->terminate_timeout = -1.0;
  self->test = FALSE;
  self->variable_dir = NULL;
  self->verbose = FALSE;
  self->version = FALSE;
  self->version_only = FALSE;
  self->write_final_argv = NULL;
}

static void
pv_wrap_options_clear (PvWrapOptions *self)
{
  g_clear_pointer (&self->env_if_host, g_strfreev);
  g_clear_pointer (&self->filesystems, g_strfreev);
  g_clear_pointer (&self->freedesktop_app_id, g_free);
  g_clear_pointer (&self->graphics_provider, g_free);
  g_clear_pointer (&self->home, g_free);
  g_clear_pointer (&self->pass_fds, g_array_unref);
  g_clear_pointer (&self->preload_modules, g_array_unref);
  g_clear_pointer (&self->runtime, g_free);
  g_clear_pointer (&self->runtime_base, g_free);
  g_clear_pointer (&self->steam_app_id, g_free);
  g_clear_pointer (&self->variable_dir, g_free);
  g_clear_pointer (&self->write_final_argv, g_free);
}

enum {
  PROP_0,
  PROP_CURRENT_ROOT,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

struct _PvWrapContextClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (PvWrapContext, pv_wrap_context, G_TYPE_OBJECT)

static void
pv_wrap_context_init (PvWrapContext *self)
{
  self->is_flatpak_env = g_file_test ("/.flatpak-info", G_FILE_TEST_IS_REGULAR);
  self->original_environ = g_get_environ ();

  pv_wrap_options_init (&self->options);

  /* Some defaults are conditional */
  self->options.copy_runtime = self->is_flatpak_env;
}

static void
pv_wrap_context_get_property (GObject *object,
                              guint prop_id,
                              GValue *value,
                              GParamSpec *pspec)
{
  PvWrapContext *self = PV_WRAP_CONTEXT (object);

  switch (prop_id)
    {
      case PROP_CURRENT_ROOT:
        g_value_set_object (value, self->current_root);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
pv_wrap_context_set_property (GObject *object,
                              guint prop_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
  PvWrapContext *self = PV_WRAP_CONTEXT (object);

  switch (prop_id)
    {
      case PROP_CURRENT_ROOT:
        /* Construct-only */
        g_return_if_fail (self->current_root == NULL);
        self->current_root = g_value_dup_object (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
pv_wrap_context_dispose (GObject *object)
{
  PvWrapContext *self = PV_WRAP_CONTEXT (object);

  g_clear_object (&self->current_root);

  G_OBJECT_CLASS (pv_wrap_context_parent_class)->dispose (object);
}

static void
pv_wrap_context_finalize (GObject *object)
{
  PvWrapContext *self = PV_WRAP_CONTEXT (object);

  pv_wrap_options_clear (&self->options);

  g_clear_pointer (&self->paths_not_exported, g_hash_table_unref);
  g_strfreev (self->original_argv);
  g_strfreev (self->original_environ);

  G_OBJECT_CLASS (pv_wrap_context_parent_class)->finalize (object);
}

static void
pv_wrap_context_class_init (PvWrapContextClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = pv_wrap_context_get_property;
  object_class->set_property = pv_wrap_context_set_property;
  object_class->dispose = pv_wrap_context_dispose;
  object_class->finalize = pv_wrap_context_finalize;

  properties[PROP_CURRENT_ROOT] =
    g_param_spec_object ("current-root", "Current root",
                         ("Real or mock root directory for the current "
                          "filesystem namespace"),
                         SRT_TYPE_SYSROOT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

PvWrapContext *
pv_wrap_context_new (SrtSysroot *current_root,
                     GError **error)
{
  /* Can't actually fail right now */
  return g_object_new (PV_TYPE_WRAP_CONTEXT,
                       "current-root", current_root,
                       NULL);
}

static gboolean
opt_copy_runtime_into_cb (const gchar *option_name,
                          const gchar *value,
                          gpointer data,
                          GError **error)
{
  PvWrapOptions *self = data;

  if (value == NULL)
    {
      /* Do nothing, keep previous setting */
    }
  else if (value[0] == '\0')
    {
      g_warning ("%s is deprecated, disable with --no-copy-runtime instead",
                 option_name);
      self->copy_runtime = FALSE;
    }
  else
    {
      g_warning ("%s is deprecated, use --copy-runtime and "
                 "--variable-dir instead",
                 option_name);
      self->copy_runtime = TRUE;
      g_free (self->variable_dir);
      self->variable_dir = g_strdup (value);
    }

  return TRUE;
}

static gboolean
opt_ignored_cb (const gchar *option_name,
                const gchar *value,
                gpointer data,
                GError **error)
{
  g_warning ("%s is deprecated and no longer has any effect", option_name);
  return TRUE;
}

static gboolean
opt_ld_something (PvWrapOptions *self,
                  PvPreloadVariableIndex which,
                  const char *value,
                  gboolean split,
                  GError **error)
{
  g_auto(GStrv) tokens = NULL;
  size_t i;

  if (split)
    {
      tokens = g_strsplit_set (value, pv_preload_variables[which].separators, 0);
    }
  else
    {
      tokens = g_new0 (char *, 2);
      tokens[0] = g_strdup (value);
      tokens[1] = NULL;
    }

  for (i = 0; tokens[i] != NULL; i++)
    {
      WrapPreloadModule module = { which, g_steal_pointer (&tokens[i]) };

      if (module.preload[0] == '\0')
        wrap_preload_module_clear (&module);
      else
        g_array_append_val (self->preload_modules, module);
    }

  return TRUE;
}

static gboolean
opt_ld_audit_cb (const gchar *option_name,
                 const gchar *value,
                 gpointer data,
                 GError **error)
{
  return opt_ld_something (data, PV_PRELOAD_VARIABLE_INDEX_LD_AUDIT,
                           value, FALSE, error);
}

static gboolean
opt_ld_audits_cb (const gchar *option_name,
                  const gchar *value,
                  gpointer data,
                  GError **error)
{
  return opt_ld_something (data, PV_PRELOAD_VARIABLE_INDEX_LD_AUDIT,
                           value, TRUE, error);
}

static gboolean
opt_ld_preload_cb (const gchar *option_name,
                   const gchar *value,
                   gpointer data,
                   GError **error)
{
  return opt_ld_something (data, PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
                           value, FALSE, error);
}

static gboolean
opt_ld_preloads_cb (const gchar *option_name,
                    const gchar *value,
                    gpointer data,
                    GError **error)
{
  return opt_ld_something (data, PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
                           value, TRUE, error);
}

static gboolean
opt_host_ld_preload_cb (const gchar *option_name,
                        const gchar *value,
                        gpointer data,
                        GError **error)
{
  g_warning ("%s is deprecated, use --ld-preload=%s instead",
             option_name, value);
  return opt_ld_preload_cb (option_name, value, data, error);
}

static gboolean
opt_pass_fd_cb (const char *name,
                const char *value,
                gpointer data,
                GError **error)
{
  PvWrapOptions *self = data;
  char *endptr;
  gint64 i64 = g_ascii_strtoll (value, &endptr, 10);
  int fd;
  int fd_flags;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  if (i64 < 0 || i64 > G_MAXINT || endptr == value || *endptr != '\0')
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Integer out of range or invalid: %s", value);
      return FALSE;
    }

  fd = (int) i64;

  fd_flags = fcntl (fd, F_GETFD);

  if (fd_flags < 0)
    return glnx_throw_errno_prefix (error, "Unable to receive --fd %d", fd);

  g_array_append_val (self->pass_fds, fd);
  return TRUE;
}

static gboolean
opt_share_home_cb (const gchar *option_name,
                   const gchar *value,
                   gpointer data,
                   GError **error)
{
  PvWrapOptions *self = data;

  if (g_strcmp0 (option_name, "--share-home") == 0)
    self->share_home = TRISTATE_YES;
  else if (g_strcmp0 (option_name, "--unshare-home") == 0)
    self->share_home = TRISTATE_NO;
  else
    g_return_val_if_reached (FALSE);

  return TRUE;
}

static gboolean
opt_shell_cb (const gchar *option_name,
              const gchar *value,
              gpointer data,
              GError **error)
{
  PvWrapOptions *self = data;

  if (g_strcmp0 (option_name, "--shell-after") == 0)
    value = "after";
  else if (g_strcmp0 (option_name, "--shell-fail") == 0)
    value = "fail";
  else if (g_strcmp0 (option_name, "--shell-instead") == 0)
    value = "instead";

  if (value == NULL || *value == '\0')
    {
      self->shell = PV_SHELL_NONE;
      return TRUE;
    }

  switch (value[0])
    {
      case 'a':
        if (g_strcmp0 (value, "after") == 0)
          {
            self->shell = PV_SHELL_AFTER;
            return TRUE;
          }
        break;

      case 'f':
        if (g_strcmp0 (value, "fail") == 0)
          {
            self->shell = PV_SHELL_FAIL;
            return TRUE;
          }
        break;

      case 'i':
        if (g_strcmp0 (value, "instead") == 0)
          {
            self->shell = PV_SHELL_INSTEAD;
            return TRUE;
          }
        break;

      case 'n':
        if (g_strcmp0 (value, "none") == 0 || g_strcmp0 (value, "no") == 0)
          {
            self->shell = PV_SHELL_NONE;
            return TRUE;
          }
        break;

      default:
        /* fall through to error */
        break;
    }

  g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
               "Unknown choice \"%s\" for %s", value, option_name);
  return FALSE;
}

static gboolean
opt_terminal_cb (const gchar *option_name,
                 const gchar *value,
                 gpointer data,
                 GError **error)
{
  PvWrapOptions *self = data;

  if (g_strcmp0 (option_name, "--tty") == 0)
    value = "tty";
  else if (g_strcmp0 (option_name, "--xterm") == 0)
    value = "xterm";

  if (value == NULL || *value == '\0')
    {
      self->terminal = PV_TERMINAL_AUTO;
      return TRUE;
    }

  switch (value[0])
    {
      case 'a':
        if (g_strcmp0 (value, "auto") == 0)
          {
            self->terminal = PV_TERMINAL_AUTO;
            return TRUE;
          }
        break;

      case 'n':
        if (g_strcmp0 (value, "none") == 0 || g_strcmp0 (value, "no") == 0)
          {
            self->terminal = PV_TERMINAL_NONE;
            return TRUE;
          }
        break;

      case 't':
        if (g_strcmp0 (value, "tty") == 0)
          {
            self->terminal = PV_TERMINAL_TTY;
            return TRUE;
          }
        break;

      case 'x':
        if (g_strcmp0 (value, "xterm") == 0)
          {
            self->terminal = PV_TERMINAL_XTERM;
            return TRUE;
          }
        break;

      default:
        /* fall through to error */
        break;
    }

  g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
               "Unknown choice \"%s\" for %s", value, option_name);
  return FALSE;
}

static gboolean
opt_with_host_graphics_cb (const gchar *option_name,
                           const gchar *value,
                           gpointer data,
                           GError **error)
{
  PvWrapOptions *self = data;

  /* This is the old way to get the graphics from the host system */
  if (g_strcmp0 (option_name, "--with-host-graphics") == 0)
    {
      if (g_file_test ("/run/host/usr", G_FILE_TEST_IS_DIR)
          && g_file_test ("/run/host/etc", G_FILE_TEST_IS_DIR))
        self->graphics_provider = g_strdup ("/run/host");
      else
        self->graphics_provider = g_strdup ("/");
    }
  /* This is the old way to avoid using graphics from the host */
  else if (g_strcmp0 (option_name, "--without-host-graphics") == 0)
    {
      self->graphics_provider = g_strdup ("");
    }
  else
    {
      g_return_val_if_reached (FALSE);
    }

  g_warning ("\"--with-host-graphics\" and \"--without-host-graphics\" have "
             "been deprecated and could be removed in future releases. Please use "
             "use \"--graphics-provider=/\", \"--graphics-provider=/run/host\" or "
             "\"--graphics-provider=\" instead.");

  return TRUE;
}

gboolean
pv_wrap_options_parse_environment (PvWrapOptions *self,
                                   GError **error)
{
  const char *value;

  self->batch = _srt_boolean_environment ("PRESSURE_VESSEL_BATCH",
                                          self->batch);

  /* Process COPY_RUNTIME_INFO first so that COPY_RUNTIME and VARIABLE_DIR
   * can override it */
  opt_copy_runtime_into_cb ("$PRESSURE_VESSEL_COPY_RUNTIME_INTO",
                            g_getenv ("PRESSURE_VESSEL_COPY_RUNTIME_INTO"),
                            self, NULL);

  self->copy_runtime = _srt_boolean_environment ("PRESSURE_VESSEL_COPY_RUNTIME",
                                                 self->copy_runtime);

  self->deterministic = _srt_boolean_environment ("PRESSURE_VESSEL_DETERMINISTIC",
                                                  self->deterministic);
  self->devel = _srt_boolean_environment ("PRESSURE_VESSEL_DEVEL",
                                          self->devel);

  value = g_getenv ("PRESSURE_VESSEL_VARIABLE_DIR");

  if (value != NULL)
    {
      g_free (self->variable_dir);
      self->variable_dir = g_strdup (value);
    }

  self->freedesktop_app_id = g_strdup (g_getenv ("PRESSURE_VESSEL_FDO_APP_ID"));

  if (self->freedesktop_app_id != NULL && self->freedesktop_app_id[0] == '\0')
    g_clear_pointer (&self->freedesktop_app_id, g_free);

  self->home = g_strdup (g_getenv ("PRESSURE_VESSEL_HOME"));

  if (self->home != NULL && self->home[0] == '\0')
    g_clear_pointer (&self->home, g_free);

  self->remove_game_overlay = _srt_boolean_environment ("PRESSURE_VESSEL_REMOVE_GAME_OVERLAY",
                                                        self->remove_game_overlay);
  self->systemd_scope = _srt_boolean_environment ("PRESSURE_VESSEL_SYSTEMD_SCOPE",
                                                  self->systemd_scope);
  self->import_ca_certs = _srt_boolean_environment ("PRESSURE_VESSEL_IMPORT_CA_CERTS",
                                                    self->import_ca_certs);
  self->import_vulkan_layers = _srt_boolean_environment ("PRESSURE_VESSEL_IMPORT_VULKAN_LAYERS",
                                                         self->import_vulkan_layers);

  self->share_home = tristate_environment ("PRESSURE_VESSEL_SHARE_HOME");

  self->gc_runtimes = _srt_boolean_environment ("PRESSURE_VESSEL_GC_RUNTIMES",
                                                self->gc_runtimes);
  self->generate_locales = _srt_boolean_environment ("PRESSURE_VESSEL_GENERATE_LOCALES",
                                                     self->generate_locales);

  self->share_pid = _srt_boolean_environment ("PRESSURE_VESSEL_SHARE_PID",
                                              self->share_pid);
  self->single_thread = _srt_boolean_environment ("PRESSURE_VESSEL_SINGLE_THREAD",
                                                self->single_thread);
  self->verbose = _srt_boolean_environment ("PRESSURE_VESSEL_VERBOSE",
                                            self->verbose);

  if (!opt_shell_cb ("$PRESSURE_VESSEL_SHELL",
                     g_getenv ("PRESSURE_VESSEL_SHELL"), self, error))
    return FALSE;

  if (!opt_terminal_cb ("$PRESSURE_VESSEL_TERMINAL",
                        g_getenv ("PRESSURE_VESSEL_TERMINAL"), self, error))
    return FALSE;

  return TRUE;
}

gboolean
pv_wrap_options_parse_argv (PvWrapOptions *self,
                            int *argcp,
                            char ***argvp,
                            GError **error)
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GOptionGroup) main_group = NULL;
  GOptionEntry options[] =
  {
    { "batch", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->batch,
      "Disable all interactivity and redirection: ignore --shell*, "
      "--terminal, --xterm, --tty. [Default: if $PRESSURE_VESSEL_BATCH]", NULL },
    { "copy-runtime", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->copy_runtime,
      "If a --runtime is used, copy it into --variable-dir and edit the "
      "copy in-place.",
      NULL },
    { "no-copy-runtime", '\0',
      G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &self->copy_runtime,
      "Don't behave as described for --copy-runtime. "
      "[Default unless $PRESSURE_VESSEL_COPY_RUNTIME is 1 or running in Flatpak]",
      NULL },
    { "copy-runtime-into", '\0',
      G_OPTION_FLAG_FILENAME|G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_CALLBACK,
      opt_copy_runtime_into_cb,
      "Deprecated alias for --copy-runtime and --variable-dir", "DIR" },
    { "deterministic", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->deterministic,
      "Enforce a deterministic sort order on arbitrarily-ordered things, "
      "even if that's slower. [Default if $PRESSURE_VESSEL_DETERMINISTIC is 1]",
      NULL },
    { "devel", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->devel,
      "Use a more permissive configuration that is helpful during development "
      "but not intended for production use. "
      "[Default if $PRESSURE_VESSEL_DEVEL is 1]",
      NULL },
    { "env-if-host", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME_ARRAY, &self->env_if_host,
      "Set VAR=VAL if COMMAND is run with /usr from the host system, "
      "but not if it is run with /usr from RUNTIME.", "VAR=VAL" },
    { "filesystem", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME_ARRAY, &self->filesystems,
      "Share filesystem directories with the container. "
      "They must currently be given as absolute paths.",
      "PATH" },
    { "freedesktop-app-id", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &self->freedesktop_app_id,
      "Make --unshare-home use ~/.var/app/ID as home directory, where ID "
      "is com.example.MyApp or similar. This interoperates with Flatpak. "
      "[Default: $PRESSURE_VESSEL_FDO_APP_ID if set]",
      "ID" },
    { "steam-app-id", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &self->steam_app_id,
      "Make --unshare-home use ~/.var/app/com.steampowered.AppN "
      "as home directory. [Default: $STEAM_COMPAT_APP_ID or $SteamAppId]",
      "N" },
    { "gc-legacy-runtimes", '\0',
      G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_CALLBACK, opt_ignored_cb,
      NULL, NULL },
    { "no-gc-legacy-runtimes", '\0',
      G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_CALLBACK, opt_ignored_cb,
      NULL, NULL },
    { "gc-runtimes", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->gc_runtimes,
      "If using --variable-dir, garbage-collect old temporary "
      "runtimes. [Default, unless $PRESSURE_VESSEL_GC_RUNTIMES is 0]",
      NULL },
    { "no-gc-runtimes", '\0',
      G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &self->gc_runtimes,
      "If using --variable-dir, don't garbage-collect old "
      "temporary runtimes.", NULL },
    { "generate-locales", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->generate_locales,
      "If using --runtime, attempt to generate any missing locales. "
      "[Default, unless $PRESSURE_VESSEL_GENERATE_LOCALES is 0]",
      NULL },
    { "no-generate-locales", '\0',
      G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &self->generate_locales,
      "If using --runtime, don't generate any missing locales.", NULL },
    { "home", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &self->home,
      "Use HOME as home directory. Implies --unshare-home. "
      "[Default: $PRESSURE_VESSEL_HOME if set]", "HOME" },
    { "host-ld-preload", '\0',
      G_OPTION_FLAG_FILENAME | G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_CALLBACK,
      opt_host_ld_preload_cb,
      "Deprecated alias for --ld-preload=MODULE, which despite its name "
      "does not necessarily take the module from the host system",
      "MODULE" },
    { "graphics-provider", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &self->graphics_provider,
      "If using --runtime, use PATH as the graphics provider. "
      "The path is assumed to be relative to the current namespace, "
      "and will be adjusted for use on the host system if pressure-vessel "
      "is run in a container. The empty string means use the graphics "
      "stack from container."
      "[Default: $PRESSURE_VESSEL_GRAPHICS_PROVIDER or '/']", "PATH" },
    { "launcher", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->launcher,
      "Instead of specifying a command with its arguments to execute, all the "
      "elements after '--' will be used as arguments for "
      "'steam-runtime-launcher-service'. All the environment variables that are "
      "edited by pressure-vessel, or that are known to be wrong in the new "
      "container, or that needs to inherit the value from the host system, "
      "will be locked. This option implies --batch.", NULL },
    { "ld-audit", '\0',
      G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_ld_audit_cb,
      "Add MODULE from current execution environment to LD_AUDIT when "
      "executing COMMAND.",
      "MODULE" },
    { "ld-audits", '\0',
      G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_ld_audits_cb,
      "Add MODULEs from current execution environment to LD_AUDIT when "
      "executing COMMAND. Modules are separated by colons.",
      "MODULE[:MODULE...]" },
    { "ld-preload", '\0',
      G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_ld_preload_cb,
      "Add MODULE from current execution environment to LD_PRELOAD when "
      "executing COMMAND.",
      "MODULE" },
    { "ld-preloads", '\0',
      G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_ld_preloads_cb,
      "Add MODULEs from current execution environment to LD_PRELOAD when "
      "executing COMMAND. Modules are separated by colons and/or spaces.",
      "MODULE[:MODULE...]" },
    { "pass-fd", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_pass_fd_cb,
      "Let the launched process inherit the given fd.",
      "FD" },
    { "remove-game-overlay", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->remove_game_overlay,
      "Disable the Steam Overlay. "
      "[Default if $PRESSURE_VESSEL_REMOVE_GAME_OVERLAY is 1]",
      NULL },
    { "keep-game-overlay", '\0',
      G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &self->remove_game_overlay,
      "Do not disable the Steam Overlay. "
      "[Default unless $PRESSURE_VESSEL_REMOVE_GAME_OVERLAY is 1]",
      NULL },
    { "import-vulkan-layers", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->import_vulkan_layers,
      "Import Vulkan layers from the host system. "
      "[Default unless $PRESSURE_VESSEL_IMPORT_VULKAN_LAYERS is 0]",
      NULL },
    { "no-import-vulkan-layers", '\0',
      G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &self->import_vulkan_layers,
      "Do not import Vulkan layers from the host system. Please note that "
      "certain Vulkan layers might still continue to be reachable from inside "
      "the container. This could be the case for all the layers located in "
      " `~/.local/share/vulkan` for example, because we usually share the real "
      "home directory."
      "[Default if $PRESSURE_VESSEL_IMPORT_VULKAN_LAYERS is 0]",
      NULL },
    { "runtime", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &self->runtime,
      "Mount the given sysroot or merged /usr in the container, and augment "
      "it with the provider's graphics stack. The empty string "
      "means don't use a runtime. [Default: $PRESSURE_VESSEL_RUNTIME or '']",
      "RUNTIME" },
    { "runtime-base", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &self->runtime_base,
      "If a --runtime is a relative path, look for "
      "it relative to BASE. "
      "[Default: $PRESSURE_VESSEL_RUNTIME_BASE or '.']",
      "BASE" },
    { "share-home", '\0',
      G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_share_home_cb,
      "Use the real home directory. "
      "[Default unless $PRESSURE_VESSEL_HOME is set or "
      "$PRESSURE_VESSEL_SHARE_HOME is 0]",
      NULL },
    { "unshare-home", '\0',
      G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_share_home_cb,
      "Use an app-specific home directory chosen according to --home, "
      "--freedesktop-app-id, --steam-app-id or $STEAM_COMPAT_APP_ID. "
      "[Default if $PRESSURE_VESSEL_HOME is set or "
      "$PRESSURE_VESSEL_SHARE_HOME is 0]",
      NULL },
    { "share-pid", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->share_pid,
      "Do not create a new process ID namespace for the app. "
      "[Default, unless $PRESSURE_VESSEL_SHARE_PID is 0]",
      NULL },
    { "unshare-pid", '\0',
      G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &self->share_pid,
      "Create a new process ID namespace for the app. "
      "[Default if $PRESSURE_VESSEL_SHARE_PID is 0]",
      NULL },
    { "shell", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_shell_cb,
      "--shell=after is equivalent to --shell-after, and so on. "
      "[Default: $PRESSURE_VESSEL_SHELL or 'none']",
      "{none|after|fail|instead}" },
    { "shell-after", '\0',
      G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_shell_cb,
      "Run an interactive shell after COMMAND. Executing \"$@\" in that "
      "shell will re-run COMMAND [ARGS].",
      NULL },
    { "shell-fail", '\0',
      G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_shell_cb,
      "Run an interactive shell after COMMAND, but only if it fails.",
      NULL },
    { "shell-instead", '\0',
      G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_shell_cb,
      "Run an interactive shell instead of COMMAND. Executing \"$@\" in that "
      "shell will run COMMAND [ARGS].",
      NULL },
    { "single-thread", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->single_thread,
      "Disable multi-threaded code paths, for debugging",
      NULL },
    { "systemd-scope", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->systemd_scope,
      "Attempt to run the game in a systemd scope", NULL },
    { "no-systemd-scope", '\0',
      G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &self->systemd_scope,
      "Do not run the game in a systemd scope", NULL },
    { "terminal", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_terminal_cb,
      "none: disable features that would use a terminal; "
      "auto: equivalent to xterm if a --shell option is used, or none; "
      "xterm: put game output (and --shell if used) in an xterm; "
      "tty: put game output (and --shell if used) on Steam's "
      "controlling tty "
      "[Default: $PRESSURE_VESSEL_TERMINAL or 'auto']",
      "{none|auto|xterm|tty}" },
    { "tty", '\0',
      G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_terminal_cb,
      "Equivalent to --terminal=tty", NULL },
    { "xterm", '\0',
      G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_terminal_cb,
      "Equivalent to --terminal=xterm", NULL },
    { "terminate-idle-timeout", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE, &self->terminate_idle_timeout,
      "If --terminate-timeout is used, wait this many seconds before "
      "sending SIGTERM. [default: 0.0]",
      "SECONDS" },
    { "terminate-timeout", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE, &self->terminate_timeout,
      "Send SIGTERM and SIGCONT to descendant processes that didn't "
      "exit within --terminate-idle-timeout. If they don't all exit within "
      "this many seconds, send SIGKILL and SIGCONT to survivors. If 0.0, "
      "skip SIGTERM and use SIGKILL immediately. Implies --subreaper. "
      "[Default: -1.0, meaning don't signal].",
      "SECONDS" },
    { "variable-dir", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &self->variable_dir,
      "If a runtime needs to be unpacked or copied, put it in DIR.",
      "DIR" },
    { "verbose", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->verbose,
      "Be more verbose.", NULL },
    { "version", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->version,
      "Print version number and exit.", NULL },
    { "version-only", '\0',
      G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &self->version_only,
      "Print version number (no other information) and exit.", NULL },
    { "with-host-graphics", '\0',
      G_OPTION_FLAG_NO_ARG | G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_CALLBACK,
      opt_with_host_graphics_cb,
      "Deprecated alias for \"--graphics-provider=/\" or "
      "\"--graphics-provider=/run/host\"", NULL },
    { "without-host-graphics", '\0',
      G_OPTION_FLAG_NO_ARG | G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_CALLBACK,
      opt_with_host_graphics_cb,
      "Deprecated alias for \"--graphics-provider=\"", NULL },
    { "write-final-argv", '\0',
      G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_FILENAME, &self->write_final_argv,
      "Write the final argument vector, as null terminated strings, to the "
      "given file path.", "PATH" },
    { "test", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->test,
      "Smoke test pressure-vessel-wrap and exit.", NULL },
    { "only-prepare", '\0',
      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &self->only_prepare,
      "Prepare runtime, but do not actually run anything.", NULL },
    { NULL }
  };

  context = g_option_context_new ("[--] COMMAND [ARGS]\n"
                                  "Run COMMAND [ARGS] in a container.\n");
  main_group = g_option_group_new ("pressure-vessel-wrap",
                                   "Application Options:",
                                   "Application options",
                                   self,
                                   NULL);
  g_option_group_add_entries (main_group, options);
  g_option_context_set_main_group (context, g_steal_pointer (&main_group));

  if (!g_option_context_parse (context, argcp, argvp, error))
    return FALSE;

  return TRUE;
}

gboolean
pv_wrap_context_parse_argv (PvWrapContext *self,
                            int *argcp,
                            char ***argvp,
                            GError **error)
{
  int argc = *argcp;
  char **argv = *argvp;
  int i;

  self->original_argc = argc;
  g_clear_pointer (&self->original_argv, g_strfreev);
  self->original_argv = g_new0 (char *, argc + 1);

  for (i = 0; i < argc; i++)
    self->original_argv[i] = g_strdup (argv[i]);

  return pv_wrap_options_parse_argv (&self->options, argcp, argvp, error);
}

gboolean
pv_wrap_options_parse_environment_after_argv (PvWrapOptions *self,
                                              SrtSysroot *interpreter_root,
                                              GError **error)
{
  if (self->runtime == NULL)
    {
      self->runtime = g_strdup (g_getenv ("PRESSURE_VESSEL_RUNTIME"));

      /* Normalize empty string to NULL to simplify later code */
      if (self->runtime != NULL && self->runtime[0] == '\0')
        g_clear_pointer (&self->runtime, g_free);
    }

  if (self->runtime_base == NULL)
    self->runtime_base = g_strdup (g_getenv ("PRESSURE_VESSEL_RUNTIME_BASE"));

  if (self->graphics_provider == NULL)
    self->graphics_provider = g_strdup (g_getenv ("PRESSURE_VESSEL_GRAPHICS_PROVIDER"));

  if (self->graphics_provider == NULL)
    {
      /* Also check the deprecated 'PRESSURE_VESSEL_HOST_GRAPHICS' */
      Tristate value = tristate_environment ("PRESSURE_VESSEL_HOST_GRAPHICS");

      if (value == TRISTATE_MAYBE)
        {
          if (interpreter_root != NULL)
            self->graphics_provider = g_strdup (interpreter_root->path);
          else
            self->graphics_provider = g_strdup ("/");
        }
      else
        {
          g_warning ("$PRESSURE_VESSEL_HOST_GRAPHICS is deprecated, "
                     "please use PRESSURE_VESSEL_GRAPHICS_PROVIDER instead");

          if (value == TRISTATE_NO)
            self->graphics_provider = g_strdup ("");
          else if (interpreter_root != NULL)
            self->graphics_provider = g_strdup (interpreter_root->path);
          else if (g_file_test ("/run/host/usr", G_FILE_TEST_IS_DIR)
                   && g_file_test ("/run/host/etc", G_FILE_TEST_IS_DIR))
            self->graphics_provider = g_strdup ("/run/host");
          else
            self->graphics_provider = g_strdup ("/");
        }
    }

  g_assert (self->graphics_provider != NULL);

  if (self->graphics_provider[0] != '\0' && self->graphics_provider[0] != '/')
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "--graphics-provider path must be absolute, not \"%s\"",
                   self->graphics_provider);
      return FALSE;
    }

  return TRUE;
}

/*
 * Return %TRUE if @path might appear in `XDG_DATA_DIRS`, etc. as part of
 * the operating system, and should not trigger warnings on that basis.
 */
static gboolean
is_os_path (const char *path)
{
  static const char * const os_paths[] =
  {
    "/etc",
    "/run",
    "/var",
    "/usr",
  };
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (os_paths); i++)
    {
      if (_srt_get_path_after (path, os_paths[i]) != NULL)
        return TRUE;
    }

  return FALSE;
}

/*
 * export_not_allowed:
 * @self: The context
 * @path: The path we propose to export
 * @reserved_path: The reserved path preventing us from exporting @path
 * @source: The environment variable or --option where we found @path
 * @before: "...:" to represent other entries in a colon-delimited
 *  environment variable, or empty
 * @after: ":..." to represent other entries in a colon-delimited
 *  environment variable, or empty
 * @flags: Flags affecting how we log
 *
 * Log either a warning or an informational message saying that @path
 * will not be exported.
 */
static void
export_not_allowed (PvWrapContext *self,
                    const char *path,
                    const char *reserved_path,
                    const char *source,
                    const char *before,
                    const char *after,
                    PvWrapExportFlags flags)
{
  GLogLevelFlags log_level;

  if ((flags & PV_WRAP_EXPORT_FLAGS_OS_QUIET) && is_os_path (path))
    {
      /* Don't warn loudly about e.g. /usr/share in XDG_DATA_DIRS */
      log_level = G_LOG_LEVEL_INFO;
    }
  else
    {
      if (self->paths_not_exported == NULL)
        self->paths_not_exported = g_hash_table_new_full (g_str_hash,
                                                          g_str_equal,
                                                          g_free,
                                                          NULL);

      if (g_hash_table_lookup_extended (self->paths_not_exported,
                                        path, NULL, NULL))
        {
          /* We already warned about this path, de-escalate subsequent
           * warnings to INFO level */
          log_level = G_LOG_LEVEL_INFO;
        }
      else
        {
          /* Warn the first time */
          log_level = G_LOG_LEVEL_WARNING;
          g_hash_table_add (self->paths_not_exported, g_strdup (path));
        }
    }

  g_log (G_LOG_DOMAIN, log_level,
         "Not sharing path %s=\"%s%s%s\" with container because "
         "\"%s\" is reserved by the container framework",
         source, before, path, after, reserved_path);
}

/*
 * pv_wrap_context_export_if_allowed:
 * @self: The context
 * @exports: List of exported paths
 * @export_mode: Mode with which to add @path
 * @path: The path we propose to export, as an absolute path within the
 *  current execution environment
 * @host_path: @path represented as an absolute path on the host system
 * @source: The environment variable or --option where we found @path
 * @before: (nullable): "...:" to represent other entries in a colon-delimited
 *  environment variable, or empty or %NULL
 * @after: (nullable): ":..." to represent other entries in a colon-delimited
 *  environment variable, or empty or %NULL
 * @flags: Flags affecting how we do it
 *
 * If @path can be exported (shared with the container), do so.
 * Otherwise, log a warning or informational message as appropriate.
 *
 * Returns: %TRUE if exporting the path is allowed
 */
gboolean
pv_wrap_context_export_if_allowed (PvWrapContext *self,
                                   FlatpakExports *exports,
                                   FlatpakFilesystemMode export_mode,
                                   const char *path,
                                   const char *host_path,
                                   const char *source,
                                   const char *before,
                                   const char *after,
                                   PvWrapExportFlags flags)
{
  const char * const *reserved_paths = pv_get_reserved_paths ();
  size_t i;

  g_return_val_if_fail (PV_IS_WRAP_CONTEXT (self), FALSE);
  g_return_val_if_fail (exports != NULL, FALSE);
  g_return_val_if_fail (export_mode > FLATPAK_FILESYSTEM_MODE_NONE, FALSE);
  g_return_val_if_fail (export_mode <= FLATPAK_FILESYSTEM_MODE_LAST, FALSE);
  g_return_val_if_fail (g_path_is_absolute (path), FALSE);
  g_return_val_if_fail (g_path_is_absolute (host_path), FALSE);
  g_return_val_if_fail (source != NULL, FALSE);

  if (before == NULL)
    before = "";

  if (after == NULL)
    after = "";

  for (i = 0; reserved_paths[i] != NULL; i++)
    {
      if (_srt_get_path_after (path, reserved_paths[i]) != NULL)
        {
          export_not_allowed (self, path, reserved_paths[i],
                              source, before, after, flags);
          return FALSE;
        }

      if (_srt_get_path_after (reserved_paths[i], path) != NULL)
        {
          export_not_allowed (self, path, reserved_paths[i],
                              source, before, after, flags);
          return FALSE;
        }
    }

  if (g_strcmp0 (path, host_path) == 0)
    g_info ("Bind-mounting %s=\"%s%s%s\" into the container",
            source, before, path, after);
  else
    g_info ("Bind-mounting %s=\"%s%s%s\" from the current environment "
            "as %s=\"%s%s%s\" on the host and in the container",
            source, before, path, after,
            source, before, host_path, after);

  /* This generally shouldn't fail in practice, because we already checked
   * against reserved_paths[] above */
  pv_exports_expose_or_log (exports, export_mode, path);

  return TRUE;
}
