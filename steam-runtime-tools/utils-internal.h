/*<private_header>*/
/*
 * Copyright © 2019 Collabora Ltd.
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

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib-unix.h>
#include <glib/gstdio.h>

#include <steam-runtime-tools/macros.h>
#include <steam-runtime-tools/glib-backports-internal.h>

#ifdef __has_feature
#define _srt_compiler_has_feature __has_feature
#else
#define _srt_compiler_has_feature(x) (0)
#endif

G_GNUC_INTERNAL gboolean _srt_check_not_setuid (void);

G_GNUC_INTERNAL gchar *_srt_filter_gameoverlayrenderer (const gchar *input);
G_GNUC_INTERNAL gchar **_srt_filter_gameoverlayrenderer_from_envp (const char * const *envp);
G_GNUC_INTERNAL const char *_srt_find_myself (const char **exe_path_out,
                                              const char **helpers_path_out,
                                              GError **error);
G_GNUC_INTERNAL gchar * _srt_find_executable (GError **error);
G_GNUC_INTERNAL gchar *_srt_find_executable_dir (GError **error);

_SRT_PRIVATE_EXPORT
const char *srt_enum_value_to_nick (GType enum_type,
                                    int value);

_SRT_PRIVATE_EXPORT
gboolean srt_enum_from_nick (GType enum_type,
                             const gchar *nick,
                             gint *value_out,
                             GError **error);

_SRT_PRIVATE_EXPORT
gboolean srt_add_flag_from_nick (GType flags_type,
                                 const gchar *string,
                                 guint *value_out,
                                 GError **error);

G_GNUC_INTERNAL void _srt_child_setup_unblock_signals (gpointer ignored);

_SRT_PRIVATE_EXPORT
void _srt_unblock_signals (void);

G_GNUC_INTERNAL int _srt_indirect_strcmp0 (gconstpointer left,
                                           gconstpointer right);

/*
 * Same as g_strcmp0(), but with the signature of a #GCompareFunc
 */
#define _srt_generic_strcmp0 ((GCompareFunc) (GCallback) g_strcmp0)

_SRT_PRIVATE_EXPORT
gboolean _srt_rm_rf (const char *directory);

_SRT_PRIVATE_EXPORT
FILE *_srt_divert_stdout_to_stderr (GError **error);

G_GNUC_INTERNAL const char * const *_srt_peek_environ_nonnull (void);

G_GNUC_INTERNAL void _srt_setenv_disable_gio_modules (void);

G_GNUC_INTERNAL gboolean _srt_str_is_integer (const char *str);

gboolean _srt_fstatat_is_same_file (int afd, const char *a,
                                    int bfd, const char *b);
guint _srt_struct_stat_devino_hash (gconstpointer p);
gboolean _srt_struct_stat_devino_equal (gconstpointer p1,
                                        gconstpointer p2);

G_GNUC_INTERNAL gboolean _srt_steam_command_via_pipe (const char * const *arguments,
                                                      gssize n_arguments,
                                                      GError **error);

G_GNUC_INTERNAL gchar **_srt_recursive_list_content (const gchar *sysroot,
                                                     int sysroot_fd,
                                                     const gchar *directory,
                                                     int directory_fd,
                                                     const char * const *envp,
                                                     gchar ***messages_out);

G_GNUC_INTERNAL const char *_srt_get_path_after (const char *str,
                                                 const char *prefix);

/*
 * _srt_stat_get_permissions:
 *
 * Return the part of @stat_buf that represents permissions, discarding
 * the file-type bits of `st_mode` (this is the opposite of `S_IFMT`).
 *
 * Returns: the permissions bits
 */
 __attribute__((nonnull)) static inline int
_srt_stat_get_permissions (const struct stat *stat_buf)
{
  return stat_buf->st_mode & 07777;
}

/*
 * _srt_is_same_stat:
 * @a: a stat buffer
 * @b: a stat buffer
 *
 * Returns: %TRUE if a and b identify the same inode
 */
 __attribute__((nonnull)) static inline gboolean
_srt_is_same_stat (const struct stat *a,
                   const struct stat *b)
{
  return (a->st_dev == b->st_dev && a->st_ino == b->st_ino);
}

/*
 * _srt_is_same_file:
 * @a: a path
 * @b: a path
 *
 * Returns: %TRUE if a and b are names for the same inode.
 */
static inline gboolean
_srt_is_same_file (const gchar *a,
                   const gchar *b)
{
  return _srt_fstatat_is_same_file (AT_FDCWD, a,
                                    AT_FDCWD, b);
}

int _srt_set_compatible_resource_limits (pid_t pid);

gboolean _srt_boolean_environment (const gchar *name,
                                   gboolean def);

static inline gboolean
_srt_all_bits_set (unsigned int flags,
                   unsigned int bits)
{
  return (flags == (flags | bits));
}

void _srt_async_signal_safe_error (const char *prgname,
                                   const char *message,
                                   int exit_status) G_GNUC_NORETURN;

void _srt_get_current_dirs (gchar **cwd_p,
                            gchar **cwd_l);

gchar *_srt_get_random_uuid (GError **error);

const char *_srt_get_steam_app_id (void);

/*
 * SrtHashTableIter:
 * @real_iter: The underlying iterator
 *
 * Similar to #GHashTableIter, but optionally sorts the keys in a
 * user-specified order (which is implemented by caching a sorted list
 * of keys the first time _str_hash_table_iter_next() is called).
 *
 * Unlike #GHashTableIter, this data structure allocates resources, which
 * must be cleared after use by using _srt_hash_table_iter_clear() or
 * automatically by using
 * `g_auto(SrtHashTableIter) = SRT_HASH_TABLE_ITER_CLEARED`.
 */
typedef struct
{
  /*< public >*/
  GHashTableIter real_iter;
  /*< private >*/
  GHashTable *table;
  gpointer *sorted_keys;
  guint sorted_n;
  guint sorted_next;
} SrtHashTableIter;

/*
 * SRT_HASH_TABLE_ITER_CLEARED:
 *
 * Constant initializer to set a #SrtHashTableIter to a state from which
 * it can be cleared or initialized, but no other actions.
 */
#define SRT_HASH_TABLE_ITER_CLEARED { {}, NULL, NULL, 0, 0 }

void _srt_hash_table_iter_init (SrtHashTableIter *iter,
                                GHashTable *table);
void _srt_hash_table_iter_init_sorted (SrtHashTableIter *iter,
                                       GHashTable *table,
                                       GCompareFunc cmp);
gboolean _srt_hash_table_iter_next (SrtHashTableIter *iter,
                                    gpointer key_p,
                                    gpointer value_p);

/*
 * _srt_hash_table_iter_clear:
 * @iter: An iterator
 *
 * Free memory used to cache the sorted keys of @iter, if any.
 *
 * Unlike the rest of the #SrtHashTableIter interface, it is valid to call
 * this function on a #SrtHashTableIter that has already been cleared, or
 * was initialized to %SRT_HASH_TABLE_ITER_CLEARED and never subsequently
 * used.
 */
static inline void
_srt_hash_table_iter_clear (SrtHashTableIter *iter)
{
  SrtHashTableIter zero = SRT_HASH_TABLE_ITER_CLEARED;

  g_free (iter->sorted_keys);
  g_clear_pointer (&iter->table, g_hash_table_unref);
  *iter = zero;
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(SrtHashTableIter, _srt_hash_table_iter_clear)

typedef enum
{
  SRT_DIR_ITER_FLAGS_ENSURE_DTYPE = (1 << 0),
  SRT_DIR_ITER_FLAGS_FOLLOW = (1 << 1),
  SRT_DIR_ITER_FLAGS_SORTED = (1 << 2),
  SRT_DIR_ITER_FLAGS_NONE = 0
} SrtDirIterFlags;

/*
 * SrtDirentCompareFunc:
 *
 * Function to compare two `struct dirent` data structures, as used in
 * #SrtDirIter, `scandir` and `scandirat`.
 */
typedef int (*SrtDirentCompareFunc) (const struct dirent **,
                                     const struct dirent **);

/*
 * SrtDirIter:
 * @real_iter: The underlying iterator
 *
 * Similar to `GLnxDirFdIterator`, but optionally sorts the filenames in a
 * user-specified order (which is implemented by caching a sorted list
 * of filenames the first time _srt_dir_iter_next_dent() is called).
 *
 * Like `GLnxDirFdIterator`, this data structure allocates resources, which
 * must be cleared after use by using _srt_dir_iter_clear() or automatically
 * by using `g_auto(SrtDirIter) = SRT_DIR_ITER_CLEARED`.
 */
typedef struct
{
  /*< public >*/
  GLnxDirFdIterator real_iter;
  /*< private >*/
  SrtDirentCompareFunc cmp;
  GPtrArray *members;
  SrtDirIterFlags flags;
  gsize next_member;
} SrtDirIter;

int _srt_dirent_strcmp (const struct dirent **,
                        const struct dirent **);

/*
 * SRT_DIR_ITER_CLEARED:
 *
 * Constant initializer to set a #SrtDirIter to a state from which
 * it can be cleared or initialized, but no other actions.
 */
#define SRT_DIR_ITER_CLEARED { { .initialized = FALSE }, NULL, NULL, 0, 0 }

/*
 * _srt_dir_iter_init_at:
 * @self: (out caller-allocates): A directory iterator
 * @dfd: A directory fd, or AT_FDCWD, or -1
 * @path: A path relative to @dfd
 * @flags: Flags affecting iteration
 * @cmp: (nullable): If non-%NULL, sort the members of the directory
 *  using this function, typically _srt_dirent_strcmp() or `versionsort`
 * @error: Error indicator
 *
 * Start iterating over @path, relative to @dfd.
 *
 * If the #SrtDirIterFlags include %SRT_DIR_ITER_FLAGS_FOLLOW and the
 * last component of @path is a symlink, follow it.
 *
 * Other flags are stored in the iterator and used to modify the result
 * of _srt_dir_iter_next_dent().
 *
 * Returns: %FALSE on I/O error
 */
static inline gboolean
_srt_dir_iter_init_at (SrtDirIter *self,
                       int dfd,
                       const char *path,
                       SrtDirIterFlags flags,
                       SrtDirentCompareFunc cmp,
                       GError **error)
{
  SrtDirIter zero = SRT_DIR_ITER_CLEARED;
  gboolean follow = FALSE;

  *self = zero;
  self->flags = flags;
  self->cmp = cmp;

  if (flags & SRT_DIR_ITER_FLAGS_FOLLOW)
    follow = TRUE;

  if (!glnx_dirfd_iterator_init_at (dfd, path, follow, &self->real_iter, error))
    return FALSE;

  return TRUE;
}

/*
 * _srt_dir_iter_init_take_fd:
 * @self: (out caller-allocates): A directory iterator
 * @dfdp: (inout) (transfer full): A pointer to a directory fd, or AT_FDCWD, or -1
 * @flags: Flags affecting iteration
 * @error: Error indicator
 *
 * Start iterating over @dfdp.
 *
 * %SRT_DIR_ITER_FLAGS_FOLLOW is ignored if set.
 * Other flags are stored in the iterator and used to modify the result
 * of _srt_dir_iter_next_dent().
 *
 * Returns: %FALSE on I/O error
 */
static inline gboolean
_srt_dir_iter_init_take_fd (SrtDirIter *self,
                            int *dfdp,
                            SrtDirIterFlags flags,
                            SrtDirentCompareFunc cmp,
                            GError **error)
{
  SrtDirIter zero = SRT_DIR_ITER_CLEARED;

  *self = zero;
  self->flags = flags;
  self->cmp = cmp;

  if (!glnx_dirfd_iterator_init_take_fd (dfdp, &self->real_iter, error))
    return FALSE;

  return TRUE;
}

gboolean _srt_dir_iter_next_dent (SrtDirIter *self,
                                  struct dirent **out_dent,
                                  GCancellable *cancellable,
                                  GError **error);

/*
 * _srt_dir_iter_rewind:
 * @self: A directory iterator
 *
 * Return to the beginning of @self.
 */
static inline void
_srt_dir_iter_rewind (SrtDirIter *self)
{
  self->next_member = 0;
  g_clear_pointer (&self->members, g_ptr_array_unref);
  glnx_dirfd_iterator_rewind (&self->real_iter);
}

/*
 * _srt_dir_iter_clear:
 * @iter: An iterator
 *
 * Free resources used by the directory iterator.
 *
 * Unlike the rest of the #SrtDirIter interface, it is valid to call
 * this function on a #SrtDirIter that has already been cleared, or
 * was initialized to %SRT_DIR_ITER_CLEARED and never subsequently used.
 */
static inline void
_srt_dir_iter_clear (SrtDirIter *self)
{
  SrtDirIter zero = SRT_DIR_ITER_CLEARED;

  g_clear_pointer (&self->members, g_ptr_array_unref);
  glnx_dirfd_iterator_clear (&self->real_iter);
  *self = zero;
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(SrtDirIter, _srt_dir_iter_clear)

/*
 * _srt_raise_on_parent_death:
 * @signal_number: A signal number, typically `SIGTERM`
 *
 * Wrapper for prctl PR_SET_PDEATHSIG.
 * This function is async-signal-safe if and only if error is NULL.
 *
 * Returns: TRUE on success, FALSE with errno set on failure.
 */
static inline gboolean
_srt_raise_on_parent_death (int signal_number,
                            GError **error)
{
  if (prctl (PR_SET_PDEATHSIG, signal_number, 0, 0, 0) == 0)
    return TRUE;

  if (error != NULL)
    return glnx_throw_errno_prefix (error,
                                    "Unable to set parent death signal");

  return FALSE;
}

/*
 * A Unix pipe. The advantage of this type over int[2] is that it can
 * be closed automatically when it goes out of scope, using g_auto(SrtPipe).
 */
typedef struct
{
  int fds[2];
} SrtPipe;

typedef enum
{
  _SRT_PIPE_END_READ = 0,
  _SRT_PIPE_END_WRITE = 1
} SrtPipeEnd;

/* Initializer for a closed pipe */
#define _SRT_PIPE_INIT { { -1, -1 } }

/*
 * Open a pipe, as if via pipe2 with O_CLOEXEC.
 */
static inline gboolean
_srt_pipe_open (SrtPipe *self,
                GError **error)
{
  return g_unix_open_pipe (self->fds, FD_CLOEXEC, error);
}

/*
 * Return one of the ends of the pipe. It remains owned by @self.
 * This function is async-signal safe and preserves the value of `errno`.
 */
static inline int
_srt_pipe_get (SrtPipe *self,
               SrtPipeEnd end)
{
  return self->fds[end];
}

/*
 * Return one of the ends of the pipe. It becomes owned by the caller,
 * and the file descriptor in the data structure is set to `-1`,
 * similar to g_steal_fd().
 * This function is async-signal safe and preserves the value of `errno`.
 */
static inline int
_srt_pipe_steal (SrtPipe *self,
                 SrtPipeEnd end)
{
  return g_steal_fd (&self->fds[end]);
}

/*
 * Close both ends of the pipe, unless they have already been closed or
 * stolen. Any errors are ignored: use g_clear_fd() if error-handling
 * is required.
 * This function is async-signal safe and preserves the value of `errno`.
 */
static inline void
_srt_pipe_clear (SrtPipe *self)
{
  /* Note that glnx_close_fd preserves errno */
  glnx_close_fd (&self->fds[0]);
  glnx_close_fd (&self->fds[1]);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (SrtPipe, _srt_pipe_clear)

/*
 * Cast @strv to `(const char * const *)` without completely losing
 * type-safety.
 */
static inline const char * const *
_srt_const_strv (char * const *strv)
{
  return (const char * const *) strv;
}

/*
 * Same as g_strdupv(), but takes a const parameter to avoid casts.
 */
static inline GStrv
_srt_strdupv (const char * const *strv)
{
  return g_strdupv ((gchar **) strv);
}

/*
 * Same as g_environ_getenv(), but takes a const parameter to avoid casts.
 */
static inline const char *
_srt_environ_getenv (const char * const *envp,
                     const char *variable)
{
  return g_environ_getenv ((gchar **) envp, variable);
}

gboolean _srt_environ_get_boolean (const char * const *envp,
                                   const char *name,
                                   gboolean *result,
                                   GError **error);

gchar *_srt_describe_fd (int fd) G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;

/*
 * Ignore SIGPIPE, returning -1 with errno set on failure.
 */
static inline int
_srt_ignore_sigpipe (void)
{
  struct sigaction action = { .sa_handler = SIG_IGN };

  return sigaction (SIGPIPE, &action, NULL);
}

/*
 * Set FD_CLOEXEC, returning -1 with errno set on failure.
 * This is async-signal-safe.
 */
static inline int
_srt_fd_set_close_on_exec (int fd)
{
  int flags = fcntl (fd, F_GETFD, 0);

  if (flags < 0)
    return -1;

  if (!(flags & FD_CLOEXEC))
    {
      if (fcntl (fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        return -1;
    }

  return 0;
}

/*
 * Unset FD_CLOEXEC, returning -1 with errno set on failure.
 * This is async-signal-safe.
 */
static inline int
_srt_fd_unset_close_on_exec (int fd)
{
  int flags = fcntl (fd, F_GETFD, 0);

  if (flags < 0)
    return -1;

  if (flags & FD_CLOEXEC)
    {
      if (fcntl (fd, F_SETFD, flags & ~FD_CLOEXEC) < 0)
        return -1;
    }

  return 0;
}

goffset _srt_byte_suffix_to_multiplier (const char *suffix);

gboolean _srt_string_read_fd_until_eof (GString *buf,
                                        int fd,
                                        GError **error);

/*
 * _srt_string_ends_with_len:
 * @str: A #GString
 * @suffix: A suffix
 * @len: Length of suffix
 *
 * Returns: %TRUE if @str ends with @suffix
 */
static inline gboolean
_srt_string_ends_with_len (const GString *str,
                           const char *suffix,
                           gsize len)
{
  return (str->len >= len
          && memcmp (str->str + str->len - len, suffix, len) == 0);
}

/*
 * _srt_string_ends_with:
 * @str: A #GString
 * @suffix: A suffix
 *
 * Returns: %TRUE if @str ends with @suffix
 */
static inline gboolean
_srt_string_ends_with (const GString *str,
                       const char *suffix)
{
  return _srt_string_ends_with_len (str, suffix, strlen (suffix));
}

gboolean _srt_is_identifier (const char *name);

#define _SRT_RECURSIVE_EXEC_GUARD_ENV "SRT_RECURSIVE_EXEC_GUARD"

gboolean _srt_check_recursive_exec_guard (const char *debug_target,
                                          GError **error);

gchar *_srt_find_next_executable (const char *search_path,
                                  const char *exe_name,
                                  GError **error);
