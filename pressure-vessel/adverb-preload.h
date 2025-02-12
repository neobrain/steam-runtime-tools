/*
 * Copyright © 2019-2023 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "libglnx.h"

#include <glib.h>

#include "flatpak-bwrap-private.h"
#include "per-arch-dirs.h"

typedef enum
{
  PV_PRELOAD_VARIABLE_INDEX_LD_AUDIT,
  PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
} PvPreloadVariableIndex;

typedef struct
{
  const char *variable;
  const char *adverb_option;
  const char *separators;
} PvPreloadVariable;

extern const PvPreloadVariable pv_preload_variables[2];

#define PV_UNSPECIFIED_ABI (G_MAXSIZE)

typedef struct
{
  char *name;
  gsize index_in_preload_variables;
  /* An index in pv_multiarch_details, or PV_UNSPECIFIED_ABI if unspecified */
  gsize abi_index;
} PvAdverbPreloadModule;

#define PV_ADVERB_PRELOAD_MODULE_INIT \
{ \
  .name = NULL, \
  .index_in_preload_variables = 0, \
  .abi_index = PV_UNSPECIFIED_ABI, \
}

static inline void
pv_adverb_preload_module_clear (gpointer p)
{
  PvAdverbPreloadModule *self = p;
  PvAdverbPreloadModule blank = PV_ADVERB_PRELOAD_MODULE_INIT;

  g_free (self->name);
  *self = blank;
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (PvAdverbPreloadModule, pv_adverb_preload_module_clear)

gchar *pv_adverb_preload_module_to_adverb_cli (PvAdverbPreloadModule *self);

gboolean pv_adverb_preload_module_parse_adverb_cli (PvAdverbPreloadModule *self,
                                                    const char *option,
                                                    PvPreloadVariableIndex which,
                                                    const char *value,
                                                    GError **error);

gboolean pv_adverb_set_up_preload_modules (FlatpakBwrap *wrapped_command,
                                           PvPerArchDirs *lib_temp_dirs,
                                           const PvAdverbPreloadModule *preload_modules,
                                           gsize n_preload_modules,
                                           GError **error);
