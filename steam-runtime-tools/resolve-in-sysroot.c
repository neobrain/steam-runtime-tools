/*
 * Copyright © 2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "steam-runtime-tools/resolve-in-sysroot-internal.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "libglnx.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"

/* Enabling debug logging for this is rather too verbose, so only
 * enable it when actively debugging this module */
#if 0
#define trace(...) g_debug (__VA_ARGS__)
#else
#define trace(...) do { } while (0)
#endif

G_DEFINE_TYPE (SrtSysroot, _srt_sysroot, G_TYPE_OBJECT)

static void
_srt_sysroot_init (SrtSysroot *self)
{
  self->fd = -1;
}

static void
_srt_sysroot_finalize (GObject *object)
{
  SrtSysroot *self = SRT_SYSROOT (object);

  glnx_close_fd (&self->fd);
  g_free (self->path);

  G_OBJECT_CLASS (_srt_sysroot_parent_class)->finalize (object);
}

static void
_srt_sysroot_class_init (SrtSysrootClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->finalize = _srt_sysroot_finalize;
}

/*
 * _srt_sysroot_new_take:
 * @path: (transfer full): A path
 * @fd: A file descriptor opened on @path
 *
 * Return a sysroot object, taking ownership of @path and @fd.
 * This function cannot fail.
 *
 * Returns: (transfer full): The new object
 */
SrtSysroot *
_srt_sysroot_new_take (gchar *path,
                       int fd)
{
  g_autoptr(SrtSysroot) self = g_object_new (SRT_TYPE_SYSROOT, NULL);

  self->path = path;
  self->fd = fd;
  return g_steal_pointer (&self);
}

/*
 * _srt_sysroot_new:
 * @path: A path
 * @error: Used to report an error if @path cannot be opened
 *
 * Return a sysroot object representing @path.
 *
 * Returns: (transfer full): The new object, or %NULL on failure
 */
SrtSysroot *
_srt_sysroot_new (const char *path,
                  GError **error)
{
  glnx_autofd int fd = -1;

  if (!glnx_opendirat (-1, path, TRUE, &fd, error))
    return NULL;

  return _srt_sysroot_new_take (g_strdup (path), g_steal_fd (&fd));
}

/*
 * _srt_sysroot_new_direct:
 * @error: Used to report an error if the root directory cannot be opened
 *
 * Return a sysroot object representing the root directory.
 * I/O for this sysroot should be done via naive filesystem functions
 * rather than resolving paths from first principles, giving frameworks
 * like FEX-Emu the opportunity to change the meaning of filesystem paths.
 *
 * Returns: (transfer full): The new object, or %NULL on failure
 */
SrtSysroot *
_srt_sysroot_new_direct (GError **error)
{
  g_autoptr(SrtSysroot) self = _srt_sysroot_new ("/", error);

  if (self == NULL)
    return NULL;

  self->mode = SRT_SYSROOT_MODE_DIRECT;
  return g_steal_pointer (&self);
}

/*
 * _srt_sysroot_new_real_root:
 * @error: Used to report an error if the root directory cannot be opened
 *
 * Return a sysroot object representing the real root directory.
 * Unlike _srt_sysroot_new_direct(), this will usually bypass user-space
 * filesystem virtualization like FEX-Emu.
 *
 * Returns: (transfer full): The new object, or %NULL on failure
 */
SrtSysroot *
_srt_sysroot_new_real_root (GError **error)
{
  return _srt_sysroot_new ("/proc/self/root", error);
}

/*
 * _srt_sysroot_new_flatpak_host:
 * @error: Used to report an error if the host directory cannot be opened
 *
 * Return a sysroot object representing the filesystem of the host outside
 * a Flatpak sandbox or similar container.
 *
 * Returns: (transfer full): The new object, or %NULL on failure
 */
SrtSysroot *
_srt_sysroot_new_flatpak_host (GError **error)
{
  return _srt_sysroot_new ("/run/host", error);
}

/*
 * clear_fd:
 * @p: A pointer into the data array underlying a #GArray.
 *
 * Close the fd pointed to by @p, and set `*(int *) p` to -1.
 *
 * This wraps glnx_close_fd() with the signature required by
 * g_array_set_clear_func().
 */
static void
clear_fd (void *p)
{
  glnx_close_fd (p);
}

/*
 * Steal `*fdp` and append it to @fds.
 *
 * We can't just use g_array_append_val (fds, g_steal_fd (&fd))
 * because g_array_append_val is a macro that takes a pointer to its
 * argument.
 */
static inline void
fd_array_take (GArray *fds,
               int *fdp)
{
  int fd = g_steal_fd (fdp);

  g_array_append_val (fds, fd);
}

static gboolean
check_fd_is_regular_file (const char *path,
                          int fd,
                          GError **error)
{
  struct stat stat_buf;

  if (!glnx_fstatat (fd, "", &stat_buf, AT_EMPTY_PATH, error))
    {
      g_prefix_error (error,
                      "Unable to determine whether \"%s\" "
                      "is a regular file",
                      path);
      return FALSE;
    }

  if (!S_ISREG (stat_buf.st_mode))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE,
                   "\"%s\" is not a regular file (type 0o%o)",
                   path, stat_buf.st_mode & S_IFMT);
      return FALSE;
    }

  return TRUE;
}

/*
 * _srt_resolve_in_sysroot:
 * @sysroot: (transfer none): A file descriptor representing the root
 * @descendant: (type filename): A path below the root directory, either
 *  absolute or relative (to the root)
 * @flags: Flags affecting how we resolve the path
 * @real_path_out: (optional) (out) (type filename): If not %NULL, used to
 *  return the path to @descendant below @sysroot
 * @error: Used to raise an error on failure
 *
 * Open @descendant as though @sysroot was the root directory.
 *
 * If %SRT_RESOLVE_FLAGS_MKDIR_P is in @flags, each path segment in
 * @descendant must be a directory, a symbolic link to a directory,
 * or nonexistent (in which case a directory will be created, currently
 * with hard-coded 0700 permissions).
 *
 * If %SRT_RESOLVE_FLAGS_RETURN_ABSOLUTE is in @flags, @real_path_out
 * will be set to a canonicalized absolute path, which will be `/` if the
 * returned file descriptor points to @sysroot, otherwise does not
 * end with a `/`, and in any case does not have any `.` or `..` path
 * segments.
 *
 * Otherwise, it will be set to a canonicalized relative path, which will
 * be `.` if the returned file descriptor points to @sysroot and otherwise
 * does not have any `.` path segments, and in any case does not have any
 * `..` path segments.
 *
 * Returns: An `O_PATH` file descriptor pointing to @descendant,
 *  or -1 on error
 */
int
_srt_resolve_in_sysroot (int sysroot,
                         const char *descendant,
                         SrtResolveFlags flags,
                         gchar **real_path_out,
                         GError **error)
{
  g_autoptr(GString) current_path = g_string_new ("");
  /* Array of fds pointing to directories beneath @sysroot.
   * The 0'th element is sysroot itself, the 1st element is a direct
   * child of sysroot and so on. The last element can be a
   * non-directory. */
  g_autoptr(GArray) fds = NULL;
  /* @buffer contains parts of @descendant. We edit it in-place to replace
   * each directory separator we have dealt with by \0. */
  g_autofree gchar *buffer = g_strdup (descendant);
  /* @remaining points to the remaining path to traverse. For example,
   * if we are trying to resolve a/b/c/d, we have already opened a, and we
   * will open b next, then @buffer contains "a\0b/c" and remaining
   * points to b. */
  gchar *remaining;

  g_return_val_if_fail (sysroot > 0, -1);
  g_return_val_if_fail (descendant != NULL, -1);
  g_return_val_if_fail (real_path_out == NULL || *real_path_out == NULL, -1);
  g_return_val_if_fail (error == NULL || *error == NULL, -1);
  g_return_val_if_fail (!_srt_all_bits_set (flags,
                                             SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY
                                             | SRT_RESOLVE_FLAGS_MUST_BE_REGULAR),
                        -1);
  g_return_val_if_fail (!_srt_all_bits_set (flags,
                                            SRT_RESOLVE_FLAGS_MKDIR_P
                                            | SRT_RESOLVE_FLAGS_MUST_BE_REGULAR),
                        -1);

    {
      glnx_autofd int fd = -1;

      fd = TEMP_FAILURE_RETRY (fcntl (sysroot, F_DUPFD_CLOEXEC, 0));

      if (fd < 0)
        {
          glnx_throw_errno_prefix (error, "Unable to duplicate fd \"%d\"",
                                   sysroot);
          return -1;
        }

      fds = g_array_new (FALSE, FALSE, sizeof (int));
      g_array_set_clear_func (fds, clear_fd);
      fd_array_take (fds, &fd);
    }

  if (flags & SRT_RESOLVE_FLAGS_MKDIR_P)
    flags |= SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY;

  remaining = buffer;

  while (remaining != NULL)
    {
      g_autofree gchar *target = NULL;
      glnx_autofd int fd = -1;
      const gchar *next;
      gchar *slash;   /* Points into @buffer */
      int open_flags;

      /* Ignore excess slashes */
      while (remaining[0] == '/')
        remaining++;

      next = remaining;

      if (next[0] == '\0')
        break;

      slash = strchr (remaining, '/');

      if (slash == NULL)
        {
          trace ("Done so far: \"%s\"; next: \"%s\"; remaining: nothing",
                 current_path->str, next);
          remaining = NULL;
        }
      else
        {
          *slash = '\0';
          remaining = slash + 1;
          trace ("Done so far: \"%s\"; next: \"%s\"; remaining: \"%s\"",
                 current_path->str, next, remaining);
        }

      /* Ignore ./ path segments */
      if (strcmp (next, ".") == 0)
        continue;

      /* Implement ../ by going up a level - unless we would escape
       * from the sysroot, in which case do nothing */
      if (strcmp (next, "..") == 0)
        {
          const gchar *last_slash;

          if (fds->len >= 2)
            g_array_set_size (fds, fds->len - 1);
          /* else silently ignore ../ when already at the root, the same
           * as the kernel would */

          last_slash = strrchr (current_path->str, '/');

          if (last_slash != NULL)
            g_string_truncate (current_path, last_slash - current_path->str);
          else
            g_string_truncate (current_path, 0);

          continue;
        }

      /* Open @next with O_NOFOLLOW, so that if it's a symbolic link,
       * we open the symbolic link itself and not whatever it points to */
      open_flags = O_CLOEXEC | O_NOFOLLOW | O_PATH;
      fd = TEMP_FAILURE_RETRY (openat (g_array_index (fds, int, fds->len - 1),
                                       next, open_flags));

      if (fd < 0 && errno == ENOENT && (flags & SRT_RESOLVE_FLAGS_MKDIR_P) != 0)
        {
          if (TEMP_FAILURE_RETRY (mkdirat (g_array_index (fds, int, fds->len - 1),
                                           next, 0700)) != 0)
            {
              glnx_throw_errno_prefix (error, "Unable to create \"%s/%s\"",
                                       current_path->str, next);
              return -1;
            }

          g_debug ("Created \"%s/%s\" in /proc/self/fd/%d",
                   current_path->str, next, sysroot);

          fd = TEMP_FAILURE_RETRY (openat (g_array_index (fds, int, fds->len - 1),
                                           next, open_flags | O_DIRECTORY));
        }

      if (fd < 0)
        {
          glnx_throw_errno_prefix (error, "Unable to open \"%s/%s\"",
                                   current_path->str, next);
          return -1;
        }

      /* Maybe it's a symlink? */
      target = glnx_readlinkat_malloc (fd, "", NULL, NULL);

      if (target != NULL)   /* Yes, it's a symlink */
        {
          if (flags & SRT_RESOLVE_FLAGS_REJECT_SYMLINKS)
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_TOO_MANY_LINKS,
                           "\"%s/%s\" is a symlink", current_path->str, next);
              return -1;
            }
          else if ((flags & SRT_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK) != 0 &&
                   remaining == NULL)
            {
              /* Treat as though not a symlink. */
              g_clear_pointer (&target, g_free);
            }
        }

      if (target != NULL)
        {
          /* This isn't g_autofree because clang would warn about it
           * being unused. We need to keep it alive until we change
           * @remaining to point into the new buffer. */
          gchar *old_buffer = NULL;

          if (target[0] == '/')
            {
              /* For example if we were asked to resolve foo/bar/a/b,
               * but bar is a symlink to /x/y, we restart from the beginning as though
               * we had been asked to resolve x/y/a/b */
              trace ("Absolute symlink to \"%s\"", target);
              g_string_set_size (current_path, 0);
              g_array_set_size (fds, 1);
            }
          else
            {
              /* For example if we were asked to resolve foo/bar/a/b,
               * but bar is a symlink to ../x/y, we continue as though
               * we had been asked to resolve foo/../x/y/baz */
              trace ("Relative symlink to \"%s\"/\"%s\"",
                       current_path->str, target);
            }

          old_buffer = g_steal_pointer (&buffer);
          buffer = g_build_filename (target, remaining, NULL);
          remaining = buffer;
          g_free (old_buffer);
        }
      else  /* Not a symlink, or a symlink but we are returning it anyway. */
        {
          if ((flags & SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY) != 0 ||
              remaining != NULL)
            {
              struct stat stat_buf;

              if (!glnx_fstatat (fd, "", &stat_buf, AT_EMPTY_PATH, error))
                {
                  g_prefix_error (error,
                                  "Unable to determine whether \"%s/%s\" "
                                  "is a directory",
                                  current_path->str, next);
                  return -1;
                }

              if (!S_ISDIR (stat_buf.st_mode))
                {
                  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY,
                               "\"%s/%s\" is not a directory (type 0o%o)",
                               current_path->str, next,
                               stat_buf.st_mode & S_IFMT);
                  return -1;
                }
            }

          if (current_path->len != 0)
            g_string_append_c (current_path, '/');

          g_string_append (current_path, next);
          fd_array_take (fds, &fd);
        }
    }

  /* Avoid returning an empty path */
  if (flags & SRT_RESOLVE_FLAGS_RETURN_ABSOLUTE)
    g_string_prepend_c (current_path, '/');
  else if (current_path->len == 0)
    g_string_append_c (current_path, '.');

  if ((flags & SRT_RESOLVE_FLAGS_MUST_BE_REGULAR) != 0
      && !check_fd_is_regular_file (current_path->str,
                                    g_array_index (fds, int, fds->len - 1),
                                    error))
    return -1;

  if (flags & SRT_RESOLVE_FLAGS_READABLE)
    {
      g_autofree char *proc_fd_name = g_strdup_printf ("/proc/self/fd/%d",
                                                       g_array_index (fds, int,
                                                                      fds->len - 1));
      glnx_autofd int fd = -1;

      if (flags & SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY)
        {
          if (!glnx_opendirat (-1, proc_fd_name, TRUE, &fd, error))
            {
              g_prefix_error (error, "Unable to open \"%s\" as directory: ",
                              current_path->str);
              return -1;
            }
        }
      else
        {
          if (!glnx_openat_rdonly (-1, proc_fd_name, TRUE, &fd, error))
            {
              g_prefix_error (error, "Unable to open \"%s\": ",
                              current_path->str);
              return -1;
            }
        }

      if (real_path_out != NULL)
        *real_path_out = g_string_free (g_steal_pointer (&current_path), FALSE);

      return g_steal_fd (&fd);
    }

  if (real_path_out != NULL)
    *real_path_out = g_string_free (g_steal_pointer (&current_path), FALSE);

  /* Taking the address might look like nonsense here, but it's
   * documented to work: g_array_index expands to fds->data[some_offset].
   * We need to steal ownership of the fd back from @fds so it won't be
   * closed with the rest of them when @fds is freed. */
  return g_steal_fd (&g_array_index (fds, int, fds->len - 1));
}

int
_srt_sysroot_open (SrtSysroot *sysroot,
                   const char *path,
                   SrtResolveFlags flags,
                   gchar **resolved,
                   GError **error)
{
  glnx_autofd int fd = -1;

  /*
   * Only a subset of flags are supported here:
   *
   * - MKDIR_P is unimplemented (difficult to implement for direct I/O
   *   with semantics that match _srt_resolve_in_sysroot)
   * - REJECT_SYMLINKS is unimplemented (ditto)
   */
  g_return_val_if_fail ((flags & ~(SRT_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK
                                   | SRT_RESOLVE_FLAGS_READABLE
                                   | SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY
                                   | SRT_RESOLVE_FLAGS_MUST_BE_REGULAR
                                   | SRT_RESOLVE_FLAGS_RETURN_ABSOLUTE)) == 0,
                        -1);
  g_return_val_if_fail (!_srt_all_bits_set (flags,
                                             SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY
                                             | SRT_RESOLVE_FLAGS_MUST_BE_REGULAR),
                        -1);

  if (_srt_sysroot_is_direct (sysroot))
    {
      g_autofree gchar *abs_path = NULL;

      /* We want to use normal path resolution that gives FEX-Emu an
       * opportunity to fake the filesystem layout, so turn relative paths
       * into absolute. We have to pass AT_FDCWD to openat() when we do this:
       * <https://github.com/FEX-Emu/FEX/issues/3204>. */
      if (path[0] != '/')
        {
          abs_path = g_build_filename ("/", path, NULL);
          path = abs_path;
        }

      if (flags & SRT_RESOLVE_FLAGS_READABLE)
        {
          gboolean follow = !(flags & SRT_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK);

          if (flags & SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY)
            {
              if (!glnx_opendirat (AT_FDCWD, path, follow, &fd, error))
                return -1;
            }
          else
            {
              if (!glnx_openat_rdonly (AT_FDCWD, path, follow, &fd, error))
                return -1;
            }
        }
      else
        {
          int open_flags = O_CLOEXEC | O_PATH;

          if (flags & SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY)
            open_flags |= O_DIRECTORY;

          if (flags & SRT_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK)
            open_flags |= O_NOFOLLOW;

          fd = TEMP_FAILURE_RETRY (openat (AT_FDCWD, path, open_flags));

          if (fd < 0)
            {
              glnx_throw_errno_prefix (error, "Unable to open \"%s\"", path);
              return -1;
            }
        }

      if ((flags & SRT_RESOLVE_FLAGS_MUST_BE_REGULAR) != 0
          && !check_fd_is_regular_file (path, fd, error))
        return -1;

      if (resolved != NULL)
        {
          g_autofree gchar *proc_path = NULL;

          proc_path = g_strdup_printf ("/proc/self/fd/%d", fd);
          *resolved = glnx_readlinkat_malloc (AT_FDCWD, proc_path, NULL, error);

          if (*resolved == NULL)
            return -1;

          if (!(flags & SRT_RESOLVE_FLAGS_RETURN_ABSOLUTE))
            {
              if ((*resolved)[0] == '/' && (*resolved)[1] == '\0')
                {
                  /* Replace "/" with "." to avoid returning empty string */
                  (*resolved)[0] = '.';
                }
              else
                {
                  /* Replace "/foo/bar" by shifting "foo/bar\0" 1 byte
                   * to the left */
                  memmove (*resolved, (*resolved) + 1, strlen (*resolved));
                }
            }
          /* ... else leave it as "/" or "/foo/bar" */
        }
    }
  else
    {
      fd = _srt_resolve_in_sysroot (sysroot->fd, path, flags, resolved, error);

      if (fd < 0)
        return -1;
    }

  g_assert (fd >= 0);
  return g_steal_fd (&fd);
}

gboolean
_srt_sysroot_load (SrtSysroot *sysroot,
                   const char *path,
                   SrtResolveFlags flags,
                   gchar **resolved,
                   gchar **contents_out,
                   gsize *len_out,
                   GError **error)
{
  glnx_autofd int fd = -1;

  if (contents_out != NULL || len_out != NULL)
    flags |= SRT_RESOLVE_FLAGS_READABLE;

  fd = _srt_sysroot_open (sysroot, path, flags, resolved, error);

  if (fd < 0)
    return FALSE;

  if (contents_out != NULL || len_out != NULL)
    {
      g_autoptr(GBytes) bytes = NULL;
      gsize len;

      bytes = glnx_fd_readall_bytes (fd, NULL, error);

      if (bytes == NULL)
        return FALSE;

      len = g_bytes_get_size (bytes);

      if (contents_out != NULL)
        {
          *contents_out = g_new0 (char, len + 1);

          if (len > 0)
            memcpy (*contents_out, g_bytes_get_data (bytes, NULL), len);
        }

      if (len_out != NULL)
        *len_out = len;
    }

  return TRUE;
}
