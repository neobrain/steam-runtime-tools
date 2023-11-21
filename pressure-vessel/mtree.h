/*
 * Copyright © 2021 Collabora Ltd.
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

#include <glib.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "libglnx.h"

/*
 * PvMtreeApplyFlags:
 * @PV_MTREE_APPLY_FLAGS_GZIP: Input is compressed with gzip
 * @PV_MTREE_APPLY_FLAGS_EXPECT_HARD_LINKS: Warn if unable to use hard-links
 *  to save space
 * @PV_MTREE_APPLY_FLAGS_CHMOD_MAY_FAIL: If unable to set permissions,
 *  assume that r-x is good enough for directories and executables, and
 *  assume that r-- is good enough for all other files (useful when writing
 *  to NTFS or FAT)
 * @PV_MTREE_APPLY_FLAGS_MINIMIZED_RUNTIME: When verifying, don't check for
 *  existence of files that can be created from the manifest
 * @PV_MTREE_APPLY_FLAGS_NONE: None of the above
 *
 * Flags altering how a mtree manifest is applied to a directory tree.
 */
typedef enum
{
  PV_MTREE_APPLY_FLAGS_GZIP = (1 << 0),
  PV_MTREE_APPLY_FLAGS_EXPECT_HARD_LINKS = (1 << 1),
  PV_MTREE_APPLY_FLAGS_CHMOD_MAY_FAIL = (1 << 2),
  PV_MTREE_APPLY_FLAGS_MINIMIZED_RUNTIME = (1 << 3),
  PV_MTREE_APPLY_FLAGS_NONE = 0
} PvMtreeApplyFlags;

typedef enum
{
  PV_MTREE_ENTRY_KIND_UNKNOWN = '\0',
  PV_MTREE_ENTRY_KIND_BLOCK = 'b',
  PV_MTREE_ENTRY_KIND_CHAR = 'c',
  PV_MTREE_ENTRY_KIND_DIR = 'd',
  PV_MTREE_ENTRY_KIND_FIFO = 'p',
  PV_MTREE_ENTRY_KIND_FILE = '-',
  PV_MTREE_ENTRY_KIND_LINK = 'l',
  PV_MTREE_ENTRY_KIND_SOCKET = 's',
} PvMtreeEntryKind;

/*
 * PvMtreeEntryFlags:
 * @PV_MTREE_ENTRY_FLAGS_IGNORE_BELOW: Anything below this directory (but
 *  not the directory itself!) is to be ignored
 * @PV_MTREE_ENTRY_FLAGS_NO_CHANGE: When applying a manifest to a directory
 *  on disk, don't modify this file or directory
 * @PV_MTREE_ENTRY_FLAGS_OPTIONAL: When applying or verifying a manifest,
 *  it's OK if this item doesn't exist
 */
typedef enum
{
  PV_MTREE_ENTRY_FLAGS_IGNORE_BELOW = (1 << 0),
  PV_MTREE_ENTRY_FLAGS_NO_CHANGE = (1 << 1),
  PV_MTREE_ENTRY_FLAGS_OPTIONAL = (1 << 2),
  PV_MTREE_ENTRY_FLAGS_NONE = 0
} PvMtreeEntryFlags;

typedef struct _PvMtreeEntry PvMtreeEntry;

struct _PvMtreeEntry
{
  gchar *name;
  gchar *contents;
  gchar *link;
  gchar *sha256;
  goffset size;
  GTimeSpan mtime_usec;
  int mode;
  PvMtreeEntryKind kind;
  PvMtreeEntryFlags entry_flags;
};

#define PV_MTREE_ENTRY_BLANK \
{ \
  .size = -1, \
  .mtime_usec = -1, \
  .mode = -1, \
}

gboolean pv_mtree_entry_parse (const char *line,
                               PvMtreeEntry *entry,
                               const char *filename,
                               guint line_number,
                               GError **error);

void pv_mtree_entry_clear (PvMtreeEntry *entry);
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (PvMtreeEntry, pv_mtree_entry_clear)

gboolean pv_mtree_apply (const char *mtree,
                         const char *sysroot,
                         int sysroot_fd,
                         const char *source_files,
                         PvMtreeApplyFlags flags,
                         GError **error);
gboolean pv_mtree_verify (const char *mtree,
                          const char *sysroot,
                          int sysroot_fd,
                          PvMtreeApplyFlags flags,
                          GError **error);
