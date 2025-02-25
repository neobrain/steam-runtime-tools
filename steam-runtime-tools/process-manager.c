/*
 * Copyright © 2017-2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "process-manager-internal.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/wait.h>

#include <glib.h>
#include <glib-unix.h>
#include "libglnx.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/launcher-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/missing-internal.h"
#include "steam-runtime-tools/utils-internal.h"

/*
 * _srt_wait_status_to_exit_status:
 * @wait_status: A `wait()` status
 *
 * Returns: An exit status using the same conventions as `env(1)`
 */
int
_srt_wait_status_to_exit_status (int wait_status)
{
  int ret;

  if (WIFEXITED (wait_status))
    {
      ret = WEXITSTATUS (wait_status);
      if (ret == 0)
        g_debug ("Command exited with status %d", ret);
      else
        g_info ("Command exited with status %d", ret);
    }
  else if (WIFSIGNALED (wait_status))
    {
      ret = 128 + WTERMSIG (wait_status);
      g_info ("Command killed by signal %d", ret - 128);
    }
  else
    {
      ret = LAUNCH_EX_CANNOT_REPORT;
      g_info ("Command terminated in an unknown way (wait status %d)",
              wait_status);
    }

  return ret;
}

/*
 * _srt_wait_for_child_processes:
 * @main_process: process for which we will report wait-status;
 *  zero or negative if there is no main process
 * @wait_status_out: (out): Used to store the wait status of `@main_process`,
 *  as if from `wait()`, on success
 * @error: Used to raise an error on failure
 *
 * Wait for child processes of this process to exit, until the @main_process
 * has exited. If there is no main process, wait until there are no child
 * processes at all.
 *
 * If the process is a subreaper (`PR_SET_CHILD_SUBREAPER`),
 * indirect child processes whose parents have exited will be reparented
 * to it, so this will have the effect of waiting for all descendants.
 *
 * If @main_process is positive, return when @main_process has exited.
 * Child processes that exited before @main_process will also have been
 * "reaped", but child processes that exit after @main_process will not
 * (use `_srt_wait_for_child_processes (0, &error)` to resume waiting).
 *
 * If @main_process is zero or negative, wait for all child processes
 * to exit.
 *
 * This function cannot be called in a process that is using
 * g_child_watch_source_new() or similar functions, because it waits
 * for all child processes regardless of their process IDs, and that is
 * incompatible with waiting for individual child processes.
 *
 * Returns: %TRUE when @main_process has exited, or if @main_process
 *  is zero or negative and all child processes have exited
 */
gboolean
_srt_wait_for_child_processes (pid_t main_process,
                               int *wait_status_out,
                               GError **error)
{
  if (wait_status_out != NULL)
    *wait_status_out = -1;

  while (1)
    {
      int wait_status = -1;
      pid_t died = wait (&wait_status);

      if (died < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          else if (errno == ECHILD)
            {
              g_debug ("No more child processes");
              break;
            }
          else
            {
              return glnx_throw_errno_prefix (error, "wait");
            }
        }

      g_debug ("Child %lld exited with wait status %d",
               (long long) died, wait_status);

      if (died == main_process)
        {
          if (wait_status_out != NULL)
            *wait_status_out = wait_status;

          return TRUE;
        }
    }

  if (main_process > 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Process %lld was not seen to exit",
                   (long long) main_process);
      return FALSE;
    }

  return TRUE;
}

typedef struct
{
  GError *error;
  GMainContext *context;
  GSource *wait_source;
  GSource *kill_source;
  GSource *sigchld_source;
  gchar *children_file;
  /* GINT_TO_POINTER (pid) => arbitrary */
  GHashTable *sent_sigterm;
  /* GINT_TO_POINTER (pid) => arbitrary */
  GHashTable *sent_sigkill;
  /* Scratch space to build temporary strings */
  GString *buffer;
  /* 0, SIGTERM or SIGKILL */
  int sending_signal;
  /* Nonzero if wait_source has been attached to context */
  guint wait_source_id;
  guint kill_source_id;
  guint sigchld_source_id;
  /* TRUE if we reach a point where we have no more child processes. */
  gboolean finished;
} TerminationData;

/*
 * Free everything in @data.
 */
static void
termination_data_clear (TerminationData *data)
{
  if (data->wait_source_id != 0)
    {
      g_source_destroy (data->wait_source);
      data->wait_source_id = 0;
    }

  if (data->kill_source_id != 0)
    {
      g_source_destroy (data->kill_source);
      data->kill_source_id = 0;
    }

  if (data->sigchld_source_id != 0)
    {
      g_source_destroy (data->sigchld_source);
      data->sigchld_source_id = 0;
    }

  if (data->wait_source != NULL)
    g_source_unref (g_steal_pointer (&data->wait_source));

  if (data->kill_source != NULL)
    g_source_unref (g_steal_pointer (&data->kill_source));

  if (data->sigchld_source != NULL)
    g_source_unref (g_steal_pointer (&data->sigchld_source));

  g_clear_pointer (&data->children_file, g_free);
  g_clear_pointer (&data->context, g_main_context_unref);
  g_clear_error (&data->error);
  g_clear_pointer (&data->sent_sigterm, g_hash_table_unref);
  g_clear_pointer (&data->sent_sigkill, g_hash_table_unref);

  if (data->buffer != NULL)
    g_string_free (g_steal_pointer (&data->buffer), TRUE);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (TerminationData, termination_data_clear)

/*
 * Sends the configured signal to the given child process, checking the state
 * of it if the signal was already sent.
 */
static void
termination_data_signal_child (TerminationData *data,
                               int child)
{
  GHashTable *already;

  g_return_if_fail (data->sending_signal != 0);

  if (data->sending_signal == SIGKILL)
    already = data->sent_sigkill;
  else
    already = data->sent_sigterm;

  if (!g_hash_table_contains (already, GINT_TO_POINTER (child)))
    {
      g_debug ("Sending signal %d to process %d",
               data->sending_signal, child);
      g_hash_table_add (already, GINT_TO_POINTER (child));

      if (kill (child, data->sending_signal) < 0)
        g_warning ("Unable to send signal %d to process %d: %s",
                   data->sending_signal, child, g_strerror (errno));

      /* In case the child is stopped, wake it up to receive the signal */
      if (kill (child, SIGCONT) < 0)
        g_warning ("Unable to send SIGCONT to process %d: %s",
                   child, g_strerror (errno));

      /* When the child terminates, we will get SIGCHLD and come
       * back to here. */
    }
}

/*
 * Read the PPid for the given pid from the /proc directory opened in proc_dfd.
 */
static gboolean
termination_data_read_ppid (TerminationData *data,
                            int proc_dfd,
                            int pid,
                            int *out_ppid,
                            GError **error)
{
  g_autofree char *contents = NULL;
  char *p;
  gsize len;

  g_string_printf (data->buffer, "%d/status", pid);
  contents = glnx_file_get_contents_utf8_at (proc_dfd, data->buffer->str, &len,
                                             NULL, error);
  if (contents == NULL)
    return FALSE;

  for (p = contents; p != NULL && *p != '\0'; )
    {
      if (sscanf (p, "PPid: %d", out_ppid) >= 1)
        return TRUE;

      p = strchr (p, '\n');
      if (p != NULL)
        p++;
    }

  return glnx_throw (error, "Failed to find PPid in /proc/%s",
                     data->buffer->str);
}

/*
 * Do whatever the next step for srt_terminate_all_child_processes() is.
 *
 * First, reap child processes that already exited, without blocking.
 *
 * Then, act according to the phase we are in:
 * - before wait_period: do nothing
 * - after wait_period but before grace_period: send SIGTERM
 * - after wait_period and grace_period: send SIGKILL
 */
static void
termination_data_refresh (TerminationData *data)
{
  g_autoptr(GError) children_error = NULL;
  g_autofree gchar *contents = NULL;

  if (data->error != NULL)
    return;

  g_debug ("Checking for child processes");

  while (1)
    {
      int wait_status = -1;
      pid_t died = waitpid (-1, &wait_status, WNOHANG);

      if (died < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          else if (errno == ECHILD)
            {
              /* No child processes remain. */
              data->finished = TRUE;
              return;
            }
          else
            {
              glnx_throw_errno_prefix (&data->error, "wait");
              return;
            }
        }
      else if (died == 0)
        {
          /* No more child processes have exited, but at least one is
           * still running. */
          break;
        }

      /* This process has gone away, so remove any record that we have
       * sent it signals. If the pid is reused, we'll want to send
       * the same signals again. */
      g_debug ("Process %d exited", died);
      g_hash_table_remove (data->sent_sigkill, GINT_TO_POINTER (died));
      g_hash_table_remove (data->sent_sigterm, GINT_TO_POINTER (died));
    }

  /* If we are still in the wait_period, nothing more to do yet */
  if (data->sending_signal == 0)
    return;

  /* See whether we have any remaining children. These could be direct
   * child processes, or they could be children we adopted because
   * their parent was one of our descendants and has exited, leaving the
   * child to be reparented to us (their (great)*grandparent) because we
   * are a subreaper. */

  if (g_file_get_contents (data->children_file, &contents, NULL, &children_error))
    {
      const char *p;
      char *endptr;

      g_debug ("Child tasks from %s: %s", data->children_file, contents);

      for (p = contents;
           p != NULL && *p != '\0';
           p = endptr)
        {
          guint64 maybe_child;
          int child;

          while (*p != '\0' && g_ascii_isspace (*p))
            p++;

          if (*p == '\0')
            break;

          maybe_child = g_ascii_strtoull (p, &endptr, 10);

          if (*endptr != '\0' && !g_ascii_isspace (*endptr))
            {
              glnx_throw (&data->error, "Non-numeric string found in %s: %s",
                          data->children_file, p);
              return;
            }

          if (maybe_child > G_MAXINT)
            {
              glnx_throw (&data->error, "Out-of-range number found in %s: %s",
                          data->children_file, p);
              return;
            }

          child = (int) maybe_child;
          g_string_printf (data->buffer, "/proc/%d", child);

          /* If the task is just a thread, it won't have a /proc/%d directory
           * in its own right. We don't kill threads, only processes. */
          if (!g_file_test (data->buffer->str, G_FILE_TEST_IS_DIR))
            {
              g_debug ("Task %d is a thread, not a process", child);
              continue;
            }

          termination_data_signal_child (data, child);
        }
    }
  else
    {
      g_auto(SrtDirIter) iter = SRT_DIR_ITER_CLEARED;
      pid_t our_pid = getpid ();

      if (!g_error_matches (children_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning ("Failed to read %s: %s", data->children_file,
                    children_error->message);

      g_debug ("Finding children by scanning /proc");

      if (!_srt_dir_iter_init_at (&iter, AT_FDCWD, "/proc",
                                  SRT_DIR_ITER_FLAGS_ENSURE_DTYPE, NULL,
                                  &data->error))
        {
          glnx_throw_errno_prefix (&data->error, "opening /proc");
          return;
        }

      while (TRUE)
        {
          g_autoptr(GError) ppid_error = NULL;
          struct dirent *dent;
          char *endptr;
          guint64 maybe_child;
          int child;
          int child_ppid;

          if (!_srt_dir_iter_next_dent (&iter, &dent, NULL, &data->error))
            return;
          else if (dent == NULL)
            break;

          if (dent->d_type != DT_DIR)
            continue;

          maybe_child = g_ascii_strtoull (dent->d_name, &endptr, 10);
          if (*endptr != '\0')
            continue;

          if (maybe_child > G_MAXINT)
            {
              glnx_throw (&data->error, "Out-of-range pid found in /proc: %s",
                          dent->d_name);
              return;
            }

          child = (int) maybe_child;
          if (!termination_data_read_ppid (data, iter.real_iter.fd, child,
                                           &child_ppid, &ppid_error))
            {
              // This is likely just because the process died before we got a
              // chance to check it, so log at the debug level.
              g_debug ("Inspecting potential child pid %d: %s", child,
                       ppid_error->message);
              continue;
            }

          if (child_ppid != our_pid)
            continue;

          termination_data_signal_child (data, child);
        }
    }
}

/*
 * Move from wait period to grace period: start sending SIGTERM to
 * child processes.
 */
static gboolean
start_sending_sigterm (gpointer user_data)
{
  TerminationData *data = user_data;

  g_debug ("Wait period finished, starting to send SIGTERM...");

  if (data->sending_signal == 0)
    data->sending_signal = SIGTERM;

  termination_data_refresh (data);

  data->wait_source_id = 0;
  return G_SOURCE_REMOVE;
}

/*
 * End of grace period: start sending SIGKILL to child processes.
 */
static gboolean
start_sending_sigkill (gpointer user_data)
{
  TerminationData *data = user_data;

  g_debug ("Grace period finished, starting to send SIGKILL...");

  data->sending_signal = SIGKILL;
  termination_data_refresh (data);

  data->kill_source_id = 0;
  return G_SOURCE_REMOVE;
}

/*
 * Called when at least one child process has exited, resulting in
 * SIGCHLD to this process.
 */
static gboolean
sigchld_cb (int sfd,
            G_GNUC_UNUSED GIOCondition condition,
            gpointer user_data)
{
  TerminationData *data = user_data;
  struct signalfd_siginfo info;
  ssize_t size;

  size = read (sfd, &info, sizeof (info));

  if (size < 0)
    {
      if (errno != EINTR && errno != EAGAIN)
        g_warning ("Unable to read struct signalfd_siginfo: %s",
                   g_strerror (errno));
    }
  else if (size != sizeof (info))
    {
      g_warning ("Expected struct signalfd_siginfo of size %"
                 G_GSIZE_FORMAT ", got %" G_GSSIZE_FORMAT,
                 sizeof (info), size);
    }

  g_debug ("One or more child processes exited");
  termination_data_refresh (data);
  return G_SOURCE_CONTINUE;
}

/*
 * _srt_subreaper_terminate_all_child_processes:
 * @wait_period: If greater than 0, wait this many microseconds before
 *  sending `SIGTERM`
 * @grace_period: If greater than 0, after @wait_period plus this many
 *  microseconds, use `SIGKILL` instead of `SIGTERM`. If 0, proceed
 *  directly to sending `SIGKILL`.
 * @error: Used to raise an error on failure
 *
 * Make sure all child processes are terminated.
 *
 * If a child process catches `SIGTERM` but does not exit promptly and
 * does not pass the signal on to its descendants, note that its
 * descendant processes are not guaranteed to be terminated gracefully
 * with `SIGTERM`; they might only receive `SIGKILL`.
 *
 * Return when all child processes have exited or when an error has
 * occurred.
 *
 * This function cannot be called in a process that is using
 * g_child_watch_source_new() or similar functions.
 *
 * The process must be a subreaper, and must have `SIGCHLD` blocked.
 *
 * Returns: %TRUE on success.
 */
gboolean
_srt_subreaper_terminate_all_child_processes (GTimeSpan wait_period,
                                              GTimeSpan grace_period,
                                              GError **error)
{
  g_auto(TerminationData) data = {};
  sigset_t mask;
  int is_subreaper = -1;
  glnx_autofd int sfd = -1;

  if (prctl (PR_GET_CHILD_SUBREAPER, (unsigned long) &is_subreaper, 0, 0, 0) != 0)
    return glnx_throw_errno_prefix (error, "prctl PR_GET_CHILD_SUBREAPER");

  if (is_subreaper != 1)
    return glnx_throw (error, "Process is not a subreaper");

  sigemptyset (&mask);

  if ((errno = pthread_sigmask (SIG_BLOCK, NULL, &mask)) != 0)
    return glnx_throw_errno_prefix (error, "pthread_sigmask");

  if (!sigismember (&mask, SIGCHLD))
    return glnx_throw (error, "Process has not blocked SIGCHLD");

  data.children_file = g_strdup_printf ("/proc/%d/task/%d/children",
                                        getpid (), getpid ());
  data.context = g_main_context_new ();
  data.sent_sigterm = g_hash_table_new (NULL, NULL);
  data.sent_sigkill = g_hash_table_new (NULL, NULL);
  /* Big enough to fit either '/proc/<PID>' or '<PID>/status' without
   * reallocation (the the latter is 1 byte longer than the former). */
  data.buffer = g_string_new ("2345678901/status");

  if (wait_period > 0 && grace_period > 0)
    {
      data.wait_source = g_timeout_source_new (wait_period / G_TIME_SPAN_MILLISECOND);

      g_source_set_callback (data.wait_source, start_sending_sigterm,
                             &data, NULL);
      data.wait_source_id = g_source_attach (data.wait_source, data.context);
    }
  else if (grace_period > 0)
    {
      start_sending_sigterm (&data);
    }

  if (wait_period + grace_period > 0)
    {
      data.kill_source = g_timeout_source_new ((wait_period + grace_period) / G_TIME_SPAN_MILLISECOND);

      g_source_set_callback (data.kill_source, start_sending_sigkill,
                             &data, NULL);
      data.kill_source_id = g_source_attach (data.kill_source, data.context);
    }
  else
    {
      start_sending_sigkill (&data);
    }

  sigemptyset (&mask);
  sigaddset (&mask, SIGCHLD);

  sfd = signalfd (-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

  if (sfd < 0)
    return glnx_throw_errno_prefix (error, "signalfd");

  data.sigchld_source = g_unix_fd_source_new (sfd, G_IO_IN);
  g_source_set_callback (data.sigchld_source,
                         G_SOURCE_FUNC (sigchld_cb), &data, NULL);
  data.sigchld_source_id = g_source_attach (data.sigchld_source, data.context);

  termination_data_refresh (&data);

  while (data.error == NULL
         && !data.finished
         && (data.wait_source_id != 0
             || data.kill_source_id != 0
             || data.sigchld_source_id != 0))
    g_main_context_iteration (data.context, TRUE);

  if (data.error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&data.error));
      return FALSE;
    }

  return TRUE;
}

typedef struct
{
  int target;
  int source;
} AssignFd;

/*
 * _srt_process_manager_options_clear:
 * @self: The process manager options
 *
 * Free all dynamically-allocated contents of @self and reset it
 * to %SRT_PROCESS_MANAGER_OPTIONS_INIT.
 */
void
_srt_process_manager_options_clear (SrtProcessManagerOptions *self)
{
  SrtProcessManagerOptions blank = SRT_PROCESS_MANAGER_OPTIONS_INIT;

  g_clear_pointer (&self->assign_fds, g_array_unref);
  g_clear_pointer (&self->pass_fds, g_array_unref);
  g_clear_pointer (&self->locks, g_ptr_array_unref);

  *self = blank;
}

/*
 * _srt_process_manager_init_single_threaded:
 * @error: Error indicator, see GLib documentation
 *
 * Initialize the process manager.
 *
 * This function carries out non-thread-safe actions such as blocking
 * delivery of signals, so it must be called early in `main()`, before
 * any threads have been created.
 * However, it may also log warnings, so it should be called after
 * initializing logging.
 */
gboolean
_srt_process_manager_init_single_threaded (GError **error)
{
  sigset_t mask;

  _srt_unblock_signals ();

  sigemptyset (&mask);
  sigaddset (&mask, SIGCHLD);

  if ((errno = pthread_sigmask (SIG_BLOCK, &mask, NULL)) != 0)
    return glnx_throw_errno_prefix (error, "pthread_sigmask");

  return TRUE;
}

/*
 * _srt_process_manager_options_take_fd_assignment:
 * @self: The process manager options
 * @target: A file descriptor in the child process's namespace
 * @source: A file descriptor in the current namespace
 *
 * Arrange for @target in the child process to become a copy of @source.
 *
 * Ownership of @source is taken.
 */
void
_srt_process_manager_options_take_fd_assignment (SrtProcessManagerOptions *self,
                                                 int target,
                                                 int source)
{
  AssignFd pair = { .target = target, .source = source };

  if (self->assign_fds == NULL)
    self->assign_fds = g_array_new (FALSE, FALSE, sizeof (AssignFd));

  g_array_append_val (self->assign_fds, pair);
}

/*
 * _srt_process_manager_options_take_original_stdout_stderr:
 * @self: The process manager options
 * @original_stdout: The original standard output, or negative to ignore
 * @original_stderr: The original standard error, or negative to ignore
 *
 * If a file descriptor is already assigned to `STDOUT_FILENO`,
 * close @original_stdout. Otherwise, assign @original_stdout to
 * `STDOUT_FILENO`. Then do the same for `stderr`.
 *
 * Ownership of both file descriptors is taken.
 */
void
_srt_process_manager_options_take_original_stdout_stderr (SrtProcessManagerOptions *self,
                                                          int original_stdout,
                                                          int original_stderr)
{
  size_t i;

  if (self->assign_fds != NULL)
    {
      for (i = 0; i < self->assign_fds->len; i++)
        {
          AssignFd *pair = &g_array_index (self->assign_fds, AssignFd, i);

          if (pair->target == STDOUT_FILENO)
            glnx_close_fd (&original_stdout);

          if (pair->target == STDERR_FILENO)
            glnx_close_fd (&original_stderr);
        }
    }

  if (original_stdout >= 0)
    _srt_process_manager_options_take_fd_assignment (self,
                                                     STDOUT_FILENO,
                                                     original_stdout);

  if (original_stderr >= 0)
    _srt_process_manager_options_take_fd_assignment (self,
                                                     STDERR_FILENO,
                                                     original_stderr);
}

/*
 * _srt_process_manager_options_assign_fd_cli:
 * @self: The process manager options
 * @name: A command-line option name, probably `--assign-fd`
 * @value: A command-line option value of the form `3=4`
 * @error: Error indicator, see GLib documentation
 *
 * Parse a command-line option such as `--assign-fd=3=4` and convert it
 * into a file descriptor assignment analogous to `3>&4`.
 */
gboolean
_srt_process_manager_options_assign_fd_cli (SrtProcessManagerOptions *self,
                                            const char *name,
                                            const char *value,
                                            GError **error)
{
  char *endptr;
  gint64 i64 = g_ascii_strtoll (value, &endptr, 10);
  int source;
  int target;
  int fd_flags;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (i64 < 0 || i64 > G_MAXINT || endptr == value || *endptr != '=')
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Target fd out of range or invalid: %s", value);
      return FALSE;
    }

  /* Note that the target does not need to be a valid fd yet -
   * we can use something like --assign-fd=9=1 to make fd 9
   * a copy of existing fd 1 */
  target = (int) i64;

  value = endptr + 1;
  i64 = g_ascii_strtoll (value, &endptr, 10);

  if (i64 < 0 || i64 > G_MAXINT || endptr == value || *endptr != '\0')
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Source fd out of range or invalid: %s", value);
      return FALSE;
    }

  source = (int) i64;
  fd_flags = fcntl (source, F_GETFD);

  if (fd_flags < 0)
    return glnx_throw_errno_prefix (error, "Unable to receive %s source %d",
                                    name, source);

  _srt_process_manager_options_take_fd_assignment (self, target, source);
  return TRUE;
}

/*
 * _srt_process_manager_options_lock_fd_cli:
 * @self: The process manager options
 * @name: A command-line option name, probably `--lock-fd` or `fd`
 * @value: A command-line option value
 * @error: Error indicator, see GLib documentation
 *
 * Parse a command-line option such as `--lock-fd=3` and keep that
 * file descriptor open until child processes have exited.
 */
gboolean
_srt_process_manager_options_lock_fd_cli (SrtProcessManagerOptions *self,
                                          const char *name,
                                          const char *value,
                                          GError **error)
{
  char *endptr;
  gint64 i64 = g_ascii_strtoll (value, &endptr, 10);
  int fd;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  if (i64 < 0 || i64 > G_MAXINT || endptr == value || *endptr != '\0')
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Integer out of range or invalid: %s", value);
      return FALSE;
    }

  fd = (int) i64;

  if (_srt_fd_set_close_on_exec (fd) < 0)
    return glnx_throw_errno_prefix (error,
                                    "Unable to configure %s %d for "
                                    "close-on-exec",
                                    name, fd);

  /* We don't know whether this is an OFD lock or not. Assume it is:
   * it won't change our behaviour either way, and if it was passed
   * to us across a fork(), it had better be an OFD. */
  _srt_process_manager_options_take_lock (self,
                                          srt_file_lock_new_take (fd, TRUE));
  return TRUE;
}

/*
 * _srt_process_manager_options_pass_fd_cli:
 * @self: The process manager options
 * @name: A command-line option name, probably `--pass-fd`
 * @value: A command-line option value
 * @error: Error indicator, see GLib documentation
 *
 * Parse a command-line option such as `--pass-fd=3` and convert it
 * into an instruction to make file descriptor 3 not be close-on-execute.
 */
gboolean
_srt_process_manager_options_pass_fd_cli (SrtProcessManagerOptions *self,
                                          const char *name,
                                          const char *value,
                                          GError **error)
{
  char *endptr;
  gint64 i64 = g_ascii_strtoll (value, &endptr, 10);
  int fd;
  int fd_flags;

  g_return_val_if_fail (self != NULL, FALSE);
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
    return glnx_throw_errno_prefix (error, "Unable to receive %s %d",
                                    name, fd);

  if (self->pass_fds == NULL)
    self->pass_fds = g_array_new (FALSE, FALSE, sizeof (int));

  g_array_append_val (self->pass_fds, fd);
  return TRUE;
}

/*
 * _srt_process_manager_options_take_lock:
 * @self: The process manager options
 * @lock: (transfer full): Data structure encapsulating a lock file descriptor
 *
 * Take ownership of @lock until child processes have exited.
 */
void
_srt_process_manager_options_take_lock (SrtProcessManagerOptions *self,
                                        SrtFileLock *lock)
{
  if (self->locks == NULL)
    self->locks = g_ptr_array_new_with_free_func ((GDestroyNotify) srt_file_lock_free);

  g_ptr_array_add (self->locks, lock);
}

struct _SrtProcessManager
{
  GObject parent;

  SrtProcessManagerOptions opts;

  const char *prgname;
  size_t n_assign_fds;
  const AssignFd *assign_fds;
  size_t n_pass_fds;
  const int *pass_fds;

  int exit_status;
};

struct _SrtProcessManagerClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (SrtProcessManager, _srt_process_manager, G_TYPE_OBJECT)

static void
_srt_process_manager_init (SrtProcessManager *self)
{
  SrtProcessManagerOptions blank = SRT_PROCESS_MANAGER_OPTIONS_INIT;

  self->opts = blank;
  self->exit_status = -1;
}

static void
_srt_process_manager_finalize (GObject *object)
{
  SrtProcessManager *self = SRT_PROCESS_MANAGER (object);

  self->prgname = NULL;
  self->assign_fds = NULL;
  self->n_assign_fds = 0;
  self->pass_fds = NULL;
  self->n_pass_fds = 0;
  self->exit_status = -1;

  _srt_process_manager_options_clear (&self->opts);

  G_OBJECT_CLASS (_srt_process_manager_parent_class)->finalize (object);
}

static void
_srt_process_manager_class_init (SrtProcessManagerClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->finalize = _srt_process_manager_finalize;
}

/*
 * _srt_process_manager_new:
 * @options: The process manager options
 * @error: Error indicator, see GLib documentation
 *
 * Construct a new process manager from the given @options,
 * which are cleared.
 *
 * Returns: (transfer full): A new process manager
 */
SrtProcessManager *
_srt_process_manager_new (SrtProcessManagerOptions *options,
                          GError **error)
{
  SrtProcessManagerOptions blank = SRT_PROCESS_MANAGER_OPTIONS_INIT;
  g_autoptr(SrtProcessManager) self = NULL;

  g_return_val_if_fail ((options->terminate_grace_usec < 0
                         || options->subreaper),
                        FALSE);

  if (options->exit_with_parent)
    {
      g_debug ("Setting up to exit when parent does");

      if (!_srt_raise_on_parent_death (SIGTERM, error))
        return NULL;
    }

  if (options->subreaper && prctl (PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0)
    return glnx_null_throw_errno_prefix (error,
                                         "Unable to manage background processes");

  self = g_object_new (SRT_TYPE_PROCESS_MANAGER,
                       NULL);

  self->opts = *options;
  *options = blank;

  return g_steal_pointer (&self);
}

static void
_srt_process_manager_child_setup_cb (void *user_data)
{
  SrtProcessManager *self = user_data;
  size_t j;

  /* The adverb should wait for its child before it exits, but if it
   * gets terminated prematurely, we want the child to terminate too.
   * The child could reset this, but we assume it usually won't.
   * This makes it exit even if we are killed by SIGKILL, unless it
   * takes steps not to be.
   * Note that we can't use the GError here, because a GSpawnChildSetupFunc
   * needs to follow signal-safety(7) rules. */
  if (self->opts.exit_with_parent && !_srt_raise_on_parent_death (SIGTERM, NULL))
    _srt_async_signal_safe_error (self->prgname,
                                  "Failed to set up parent-death signal",
                                  LAUNCH_EX_FAILED);

  /* Unblock all signals and reset signal disposition to SIG_DFL */
  _srt_child_setup_unblock_signals (NULL);

  if (self->opts.close_fds)
    g_fdwalk_set_cloexec (3);

  for (j = 0; j < self->n_pass_fds; j++)
    {
      int fd = self->pass_fds[j];
      int fd_flags;

      fd_flags = fcntl (fd, F_GETFD);

      if (fd_flags < 0)
        _srt_async_signal_safe_error (self->prgname,
                                      "Invalid fd?",
                                      LAUNCH_EX_FAILED);

      if ((fd_flags & FD_CLOEXEC) != 0
          && fcntl (fd, F_SETFD, fd_flags & ~FD_CLOEXEC) != 0)
        _srt_async_signal_safe_error (self->prgname,
                                      "Unable to clear close-on-exec",
                                      LAUNCH_EX_FAILED);
    }

  for (j = 0; j < self->n_assign_fds; j++)
    {
      int target = self->assign_fds[j].target;
      int source = self->assign_fds[j].source;

      if (dup2 (source, target) != target)
        _srt_async_signal_safe_error (self->prgname,
                                      "Unable to assign file descriptors",
                                      LAUNCH_EX_FAILED);
    }
}

static void
_srt_process_manager_dump_parameters (SrtProcessManager *self,
                                      const char * const *argv,
                                      const char * const *envp)
{
  size_t i;

  g_debug ("Command-line:");

  for (i = 0; argv != NULL && argv[i] != NULL; i++)
    {
      g_autofree gchar *quoted = NULL;

      quoted = g_shell_quote (argv[i]);
      g_debug ("\t%s", quoted);
    }

  g_debug ("Environment:");

  for (i = 0; envp != NULL && envp[i] != NULL; i++)
    {
      g_autofree gchar *quoted = NULL;

      quoted = g_shell_quote (envp[i]);
      g_debug ("\t%s", quoted);
    }

  g_debug ("Inherited file descriptors:");

  if (self->opts.pass_fds != NULL)
    {
      for (i = 0; i < self->opts.pass_fds->len; i++)
        g_debug ("\t%d", g_array_index (self->opts.pass_fds, int, i));
    }
  else
    {
      g_debug ("\t(none)");
    }

  g_debug ("Redirections:");

  if (self->opts.assign_fds != NULL)
    {
      for (i = 0; i < self->opts.assign_fds->len; i++)
        {
          const AssignFd *item = &g_array_index (self->opts.assign_fds, AssignFd, i);
          g_autofree gchar *description = NULL;

          description = _srt_describe_fd (item->source);

          if (description != NULL)
            g_debug ("\t%d>&%d (%s)", item->target, item->source, description);
          else
            g_debug ("\t%d>&%d", item->target, item->source);
        }
    }
  else
    {
      g_debug ("\t(none)");
    }
}

static pid_t global_child_pid = 0;

static int forwarded_signals[] =
{
  SIGHUP,
  SIGINT,
  SIGQUIT,
  SIGTERM,
  SIGUSR1,
  SIGUSR2,
};

/* Only do async-signal-safe things here: see signal-safety(7) */
static void
terminate_child_cb (int signum)
{
  if (global_child_pid != 0)
    {
      /* pass it on to the child we're going to wait for */
      kill (global_child_pid, signum);
    }
  else
    {
      /* guess I'll just die, then */
      signal (signum, SIG_DFL);
      raise (signum);
    }
}

/*
 * _srt_process_manager_run:
 * @self: The process manager
 * @argv: (array zero-terminated=1): Arguments vector
 * @envp: (array zero-terminated=1): Environment
 *
 * Run the main process given by @argv in an environment given by @envp,
 * and wait for it to exit. If #SrtProcessManagerOptions.subreaper is true,
 * also wait for all descendant processes to exit.
 *
 * This function may alter global state such as signal handlers,
 * and is non-reentrant. Only call it from the main thread.
 *
 * It is an error to call this function more than once.
 * After calling this function, _srt_process_manager_get_exit_status()
 * becomes available.
 *
 * Returns: %TRUE if the process for @argv was started, even if it
 *  subsequently exited unsuccessfully or was killed by a signal.
 */
gboolean
_srt_process_manager_run (SrtProcessManager *self,
                          const char * const *argv,
                          const char * const *envp,
                          GError **error)
{
  g_autoptr(GError) local_error = NULL;
  struct sigaction terminate_child_action = {};
  int wait_status = -1;
  size_t i;

  g_return_val_if_fail (global_child_pid == 0, FALSE);
  g_return_val_if_fail (self->exit_status == -1, FALSE);

  g_debug ("Launching child process...");

  /* Respond to common termination signals by killing the child instead of
   * ourselves */
  if (self->opts.forward_signals)
    {
      terminate_child_action.sa_handler = terminate_child_cb;

      for (i = 0; i < G_N_ELEMENTS (forwarded_signals); i++)
        sigaction (forwarded_signals[i],
                   &terminate_child_action,
                   NULL);
    }

  self->prgname = g_get_prgname ();

  if (self->opts.assign_fds != NULL)
    {
      self->assign_fds = (const AssignFd *) self->opts.assign_fds->data;
      self->n_assign_fds = self->opts.assign_fds->len;
    }
  else
    {
      self->n_assign_fds = 0;
    }

  if (self->opts.pass_fds != NULL)
    {
      self->pass_fds = (const int *) self->opts.pass_fds->data;
      self->n_pass_fds = self->opts.pass_fds->len;
    }
  else
    {
      self->n_pass_fds = 0;
    }

  if (self->opts.dump_parameters && _srt_util_is_debugging ())
    _srt_process_manager_dump_parameters (self, argv, envp);

  fflush (stdout);
  fflush (stderr);

  /* We use LEAVE_DESCRIPTORS_OPEN and set CLOEXEC in the child_setup,
   * to work around a deadlock in GLib < 2.60 */
  if (!g_spawn_async (NULL,   /* working directory */
                      (char **) argv,
                      (char **) envp,
                      (G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD |
                       G_SPAWN_LEAVE_DESCRIPTORS_OPEN |
                       G_SPAWN_CHILD_INHERITS_STDIN),
                      _srt_process_manager_child_setup_cb, self,
                      &global_child_pid,
                      &local_error))
    {
      if (local_error->domain == G_SPAWN_ERROR
          && local_error->code == G_SPAWN_ERROR_NOENT)
        self->exit_status = LAUNCH_EX_NOT_FOUND;
      else
        self->exit_status = LAUNCH_EX_CANNOT_INVOKE;

      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  /* If the parent or child writes to a passed fd and closes it,
   * don't stand in the way of that. Skip fds 0 (stdin),
   * 1 (stdout) and 2 (stderr); this code assumes we have moved our original
   * stdout/stderr to another fd, which will be dealt with as one of the
   * assign_fds, and we want to keep our current stdin, stdout and
   * stderr open. */
  for (i = 0; i < self->n_pass_fds; i++)
    {
      int fd = self->pass_fds[i];

      if (fd > 2)
        close (fd);
    }

  /* Same for reassigned fds, notably our original stdout and stderr */
  for (i = 0; i < self->n_assign_fds; i++)
    {
      int fd = self->assign_fds[i].source;

      if (fd > 2)
        close (fd);
    }

  /* Reap child processes until child_pid exits */
  if (!_srt_wait_for_child_processes (global_child_pid, &wait_status, error))
    {
      self->exit_status = LAUNCH_EX_CANNOT_REPORT;
      return FALSE;
    }

  global_child_pid = 0;
  self->exit_status = _srt_wait_status_to_exit_status (wait_status);

  /* Wait for the other child processes, if any, possibly killing them.
   * Note that this affects whether we return FALSE and set the error,
   * but doesn't affect self->exit_status. */
  if (self->opts.terminate_grace_usec >= 0)
    return _srt_subreaper_terminate_all_child_processes (self->opts.terminate_wait_usec,
                                                         self->opts.terminate_grace_usec,
                                                         error);
  else
    return _srt_wait_for_child_processes (0, NULL, error);
}

/*
 * _srt_process_manager_get_exit_status:
 * @self: The process manager
 *
 * Return an `env(1)`-like exit status representing the result of the
 * process launched by _srt_process_manager_run().
 *
 * It is an error to call this function if _srt_process_manager_run()
 * has not yet returned, but it is valid to call this function after
 * _srt_process_manager_run() fails.
 *
 * Returns: An exit status
 */
int
_srt_process_manager_get_exit_status (SrtProcessManager *self)
{
  g_return_val_if_fail (self->exit_status >= 0, self->exit_status);
  return self->exit_status;
}
