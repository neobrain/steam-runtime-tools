/*<private_header>*/
/*
 * Utilities for programs that can't use GLib for whatever reason.
 *
 * Copyright 2019-2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void oom (void) __attribute__((noreturn));

void *steal_pointer (void *pp);
int steal_fd (int *fdp);
void clear_with_free (void *pp);
void clear_with_fclose (void *pp);

/*
 * autoclear:
 * @clear: A function that can clear a variable of the type with this attribute
 *
 * A type attribute marking a variable to be cleared with the given
 * function when it goes out of scope:
 * `autoclear(clear_with_fclose) FILE *fh = fopen (...);`.
 */
#define autoclear(clear) __attribute__((__cleanup__(clear)))

/*
 * autofclose:
 *
 * A type attribute marking a `FILE *` variable to be closed with `fclose()`
 * when it goes out of scope: `autofclose FILE *fh = fopen (...);`.
 */
#define autofclose autoclear(clear_with_fclose)

/*
 * autofree:
 *
 * A type attribute marking a pointer variable to be freed with `free()`
 * when it goes out of scope: `autofree char *str = strdup (...);`.
 */
#define autofree autoclear(clear_with_free)

/*
 * clear_pointer:
 * @p: A pointer to some sort of pointer
 * @destroy: A function that can free `*p`
 *
 * If `*p` is not %NULL, free the pointer that it points to by
 * calling @destroy on it.
 * Set `*p` to %NULL.
 */
#define clear_pointer(p, destroy) \
  do { \
    __typeof__ (*p) clear_temp = steal_pointer ((p)); \
    if (clear_temp != NULL) \
      destroy (clear_temp); \
  } while (0)

/*
 * N_ELEMENTS:
 * @arr: An array (not a pointer! it must be an array)
 *
 * Returns: The number of items in @arr
 */
#define N_ELEMENTS(arr) (sizeof (arr) / sizeof (arr[0]))

/*
 * xasprintf:
 * ...: The same arguments as for `asprintf()`
 *
 * Try to call `asprintf()`. If it fails, print an error and crash.
 */
#define xasprintf(...) \
do { \
    if (asprintf (__VA_ARGS__) < 0) \
      oom (); \
} while (0)

/*
 * xcalloc:
 * @n: Number of items
 * @size: Size of each item
 *
 * Try to call `calloc()`. If it fails, print an error and crash.
 */
static inline void *
xcalloc (size_t n, size_t size)
{
  void *ret = calloc (n, size);

  if (ret == NULL)
    oom ();

  return ret;
}

/*
 * new0:
 * @type: A type, typically a struct
 *
 * Try to allocate one @type with `calloc()`. If it fails, crash.
 *
 * Returns: A newly-allocated, zero-filled `(type *)`
 */
#define new0(type) ((type *) xcalloc (1, sizeof (type)))

/*
 * xrealloc:
 * @buf: (transfer full): A memory buffer allocated with `malloc()` or
 *  compatible, or %NULL
 * @size: A nonzero size to enlarge or shrink @buf
 *
 * Try to reallocate @buf with space for @size bytes. If it fails, crash.
 *
 * Returns: The new location of @buf. Free with `free()`.
 */
static inline void *
xrealloc (void *buf, size_t size)
{
  void *ret = realloc (buf, size);

  if (ret == NULL)
    oom ();

  return ret;
}

/*
 * xstrdup:
 * @s: A non-null string
 *
 * Try to duplicate @s. If it fails, crash.
 *
 * Returns: A copy of @s. Free with `free()`.
 */
static inline char *
xstrdup (const char *s)
{
  char *d = strdup (s);

  if (d == NULL)
    oom ();

  return d;
}

/*
 * strcmp0:
 * @a: A string or %NULL
 * @b: A string or %NULL
 *
 * Compare @a and @b like `strcmp()`, but treating %NULL as a distinct,
 * valid string that compares less than non-%NULL string.
 * This is the same as g_strcmp0().
 *
 * Returns: < 0 if a < b; 0 if a = b; > 0 if a > b
 */
static inline int
strcmp0 (const char *a,
         const char *b)
{
  if (a == b)
    return 0;

  if (a == NULL)
    return -1;

  if (b == NULL)
    return 1;

  return strcmp (a, b);
}

/*
 * str_has_prefix:
 * @str: A non-null string
 * @prefix: A non-null string
 *
 * Returns: true if @str contains @prefix followed by 0 or more bytes.
 */
static inline bool
str_has_prefix (const char *str,
                const char *prefix)
{
  return strncmp (str, prefix, strlen (prefix)) == 0;
}

/*
 * unblock_signals_single_threaded:
 *
 * Unblock all blockable signals.
 * This function is async-signal safe (see signal-safety(7)).
 *
 * To avoid a dependency on libpthread, it is undefined behaviour to call
 * this function in a multi-threaded process. Only call it after fork() or
 * at the beginning of main().
 */
static inline void
unblock_signals_single_threaded (void)
{
  struct sigaction action = { .sa_handler = SIG_DFL };
  sigset_t new_set;
  int sig;

  sigemptyset (&new_set);
  (void) sigprocmask (SIG_SETMASK, &new_set, NULL);

  for (sig = 1; sig < NSIG; sig++)
    {
      if (sig != SIGKILL && sig != SIGSTOP)
        (void) sigaction (sig, &action, NULL);
    }
}

/*
 * getenv_bool:
 * @env: Name of an environment variable
 * @def: Default if unset or empty
 *
 * Try to interpret @env as boolean:
 *
 * - if unset or empty, return @def
 * - if set to `0`, return false
 * - otherwise return true
 */
static inline bool
getenv_bool (const char *env,
             bool def)
{
  const char *value = getenv (env);

  if (value == NULL || *value == '\0')
    return def;

  if (strcmp (value, "0") == 0)
    return false;

  return true;
}
