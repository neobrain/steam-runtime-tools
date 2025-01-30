/*
 * Copyright © 2019-2023 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"
#include "adverb-preload.h"

#include "supported-architectures.h"
#include "utils.h"

const PvPreloadVariable pv_preload_variables[] =
{
  [PV_PRELOAD_VARIABLE_INDEX_LD_AUDIT] = {
      .variable = "LD_AUDIT",
      .adverb_option = "--ld-audit",
      /* "The items in the list are colon-separated, and there is no support
       * for escaping the separator." —ld.so(8) */
      .separators = ":",
  },
  [PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD] = {
      .variable = "LD_PRELOAD",
      .adverb_option = "--ld-preload",
      /* "The items of the list can be separated by spaces or colons, and
       * there is no support for escaping either separator." —ld.so(8) */
      .separators = ": ",
  },
};

static gpointer
generic_strdup (gpointer p)
{
  return g_strdup (p);
}

static void
ptr_array_add_unique (GPtrArray *arr,
                      const void *item,
                      GEqualFunc equal_func,
                      GBoxedCopyFunc copy_func)
{
  if (!g_ptr_array_find_with_equal_func (arr, item, equal_func, NULL))
    g_ptr_array_add (arr, copy_func ((gpointer) item));
}

static void
append_to_search_path (const gchar *item, GString *search_path)
{
  pv_search_path_append (search_path, item);
}

static void
do_one_preload_module (const PvAdverbPreloadModule *module,
                       GPtrArray *search_path,
                       PvPerArchDirs *lib_temp_dirs)
{
  const char *preload = module->name;
  const char *base;
  gsize abi_index = module->abi_index;

  g_assert (preload != NULL);

  if (*preload == '\0')
    return;

  base = glnx_basename (preload);

  /* If we were not able to create the temporary library
   * directories, we simply avoid any adjustment and try to continue */
  if (lib_temp_dirs == NULL)
    {
      g_ptr_array_add (search_path, g_strdup (preload));
      return;
    }

  if (abi_index == PV_UNSPECIFIED_ABI
      && module->index_in_preload_variables == PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD
      && strcmp (base, "gameoverlayrenderer.so") == 0)
    {
      for (gsize abi = 0; abi < PV_N_SUPPORTED_ARCHITECTURES; abi++)
        {
          g_autofree gchar *expected_suffix = g_strdup_printf ("/%s/gameoverlayrenderer.so",
                                                               pv_multiarch_details[abi].gameoverlayrenderer_dir);

          if (g_str_has_suffix (preload, expected_suffix))
            {
              abi_index = abi;
              break;
            }
        }

      if (abi_index == PV_UNSPECIFIED_ABI)
        {
          g_debug ("Preloading %s from an unexpected path \"%s\", "
                   "just leave it as is without adjusting",
                   base, preload);
        }
    }

  if (abi_index != PV_UNSPECIFIED_ABI)
    {
      g_autofree gchar *link = NULL;
      g_autofree gchar *platform_path = NULL;
      int saved_errno;

      g_debug ("Module %s is for %s",
               preload, pv_multiarch_details[abi_index].tuple);
      platform_path = g_build_filename (lib_temp_dirs->libdl_token_path,
                                        base, NULL);
      link = g_build_filename (lib_temp_dirs->abi_paths[abi_index],
                               base, NULL);

      if (symlink (preload, link) == 0)
        {
          g_debug ("created symlink %s -> %s", link, preload);
          ptr_array_add_unique (search_path, platform_path,
                                g_str_equal, generic_strdup);
          return;
        }

      saved_errno = errno;

      if (saved_errno == EEXIST)
        {
          g_autofree gchar *found = NULL;

          found = glnx_readlinkat_malloc (-1, link, NULL, NULL);

          if (g_strcmp0 (found, preload) == 0)
            {
              g_debug ("Already created symlink %s -> %s", link, preload);
              ptr_array_add_unique (search_path, platform_path,
                                    g_str_equal, generic_strdup);
              return;
            }
        }

      /* Use the object as-is instead of trying to do anything clever */
      g_debug ("Unable to create symlink %s -> %s: %s",
               link, preload, g_strerror (saved_errno));
      g_ptr_array_add (search_path, g_strdup (preload));
    }
  else
    {
      g_debug ("Module %s is for all architectures", preload);
      g_ptr_array_add (search_path, g_strdup (preload));
    }
}

gboolean
pv_adverb_set_up_preload_modules (FlatpakBwrap *wrapped_command,
                                  PvPerArchDirs *lib_temp_dirs,
                                  const PvAdverbPreloadModule *preload_modules,
                                  gsize n_preload_modules,
                                  GError **error)
{
  GPtrArray *preload_search_paths[G_N_ELEMENTS (pv_preload_variables)] = { NULL };
  gsize i;

  /* Iterate through all modules, populating preload_search_paths */
  for (i = 0; i < n_preload_modules; i++)
    {
      const PvAdverbPreloadModule *module = preload_modules + i;
      GPtrArray *search_path = preload_search_paths[module->index_in_preload_variables];

      if (search_path == NULL)
        {
          preload_search_paths[module->index_in_preload_variables]
            = search_path
            = g_ptr_array_new_full (n_preload_modules, g_free);
        }

      do_one_preload_module (module, search_path, lib_temp_dirs);
    }

  /* Serialize search_paths[PRELOAD_VARIABLE_INDEX_LD_AUDIT] into
   * LD_AUDIT, etc. */
  for (i = 0; i < G_N_ELEMENTS (pv_preload_variables); i++)
    {
      GPtrArray *search_path = preload_search_paths[i];
      g_autoptr(GString) buffer = g_string_new ("");
      const char *variable = pv_preload_variables[i].variable;

      if (search_path != NULL)
        g_ptr_array_foreach (search_path, (GFunc) append_to_search_path, buffer);

      if (buffer->len != 0)
        flatpak_bwrap_set_env (wrapped_command, variable, buffer->str, TRUE);

      g_clear_pointer (&preload_search_paths[i], g_ptr_array_unref);
    }

  return TRUE;
}

/*
 * pv_adverb_preload_module_parse_adverb_cli:
 * @self: The module, which must be initialized
 *  to %PV_ADVERB_PRELOAD_MODULE_INIT
 * @option: The command-line option that we are parsing, for
 *  example `--ld-preload`
 * @which: The corresponding type of module
 * @value: The argument to the command-line option
 * @error: Used to report an error if unsuccessful
 *
 * Parse the `--ld-preload` or `--ld-audit` option of `pv-adverb`.
 *
 * Returns: %TRUE if the argument was parsed successfully
 */
gboolean
pv_adverb_preload_module_parse_adverb_cli (PvAdverbPreloadModule *self,
                                           const char *option,
                                           PvPreloadVariableIndex which,
                                           const char *value,
                                           GError **error)
{
  g_auto(GStrv) parts = NULL;
  const char *architecture = NULL;
  gsize abi_index = PV_UNSPECIFIED_ABI;
  gsize abi;

  g_return_val_if_fail (self->name == NULL, FALSE);
  g_return_val_if_fail (self->abi_index == PV_UNSPECIFIED_ABI, FALSE);

  parts = g_strsplit (value, ":", 0);

  if (parts[0] != NULL)
    {
      gsize i;

      for (i = 1; parts[i] != NULL; i++)
        {
          if (g_str_has_prefix (parts[i], "abi="))
            {
              architecture = parts[i] + strlen ("abi=");

              for (abi = 0; abi < PV_N_SUPPORTED_ARCHITECTURES; abi++)
                {
                  if (strcmp (architecture, pv_multiarch_tuples[abi]) == 0)
                    {
                      abi_index = abi;
                      break;
                    }
                }

              if (abi_index == PV_UNSPECIFIED_ABI)
                {
                  g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                               "Unsupported ABI %s",
                               architecture);
                  return FALSE;
                }

              continue;
            }
          else
            {
              g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                           "Unexpected option in %s=\"%s\": %s",
                           option, value, parts[i]);
              return FALSE;
            }
        }

      value = parts[0];
    }

  self->index_in_preload_variables = which;
  self->name = g_strdup (value);
  self->abi_index = abi_index;
  return TRUE;
}

gchar *
pv_adverb_preload_module_to_adverb_cli (PvAdverbPreloadModule *self)
{
  g_autoptr(GString) buf = NULL;
  const PvPreloadVariable *variable;

  g_return_val_if_fail (self->index_in_preload_variables < G_N_ELEMENTS (pv_preload_variables), NULL);
  g_return_val_if_fail (self->name != NULL, NULL);
  g_return_val_if_fail (self->abi_index == PV_UNSPECIFIED_ABI
                        || self->abi_index < PV_N_SUPPORTED_ARCHITECTURES,
                        NULL);

  variable = &pv_preload_variables[self->index_in_preload_variables];
  buf = g_string_new (variable->adverb_option);
  g_string_append_c (buf, '=');
  g_string_append (buf, self->name);

  if (self->abi_index != PV_UNSPECIFIED_ABI)
    {
      g_string_append (buf, ":abi=");
      g_string_append (buf, pv_multiarch_tuples[self->abi_index]);
    }

  return g_string_free (g_steal_pointer (&buf), FALSE);
}
