---
title: srt-logger
section: 1
...

<!-- This document:
Copyright © 2024 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

srt-logger - record logs

# SYNOPSIS

**srt-logger**
[*OPTIONS*]
[**--**]
[*COMMAND* [*ARGUMENTS...*]]

**source "${STEAM_RUNTIME_SCOUT}/usr/libexec/steam-runtime-tools-0/logger-0.bash"**
[*OPTIONS*]

**srt-logger --mkfifo**

# DESCRIPTION

If run without a *COMMAND*, **srt-logger** reads from standard input
and writes to a log file.
Depending on configuration and execution environment, messages might also
be copied to the terminal and/or the systemd Journal.

If run with a *COMMAND*, **srt-logger** runs the *COMMAND* with its
standard output and standard error connected to a pipe, with a new
instance of **srt-logger** reading from the pipe and logging as above.

The logger holds a shared **F_OFD_SETLK** lock on the log file, which is
temporarily upgraded to an exclusive lock during log rotation.
If running on an older kernel that does not support open file description
locks, then the logger holds a shared **F_SETLK** lock instead, and log
rotation is disabled.
These locks are compatible with those used by **bwrap**(1), **flatpak**(1),
**steam-runtime-supervisor**(1) and **pressure-vessel-adverb**(1).
It is unspecified whether they exclude the **flock**(2) locks used
by util-linux **flock**(1) or not, so using those locks on lock
files used by **steam-runtime-tools** should be avoided.

The name of this tool intentionally does not include **steam** so that
it will not be killed by commands like **pkill steam**, allowing any
final log messages during shutdown to be recorded.

The convenience shell script fragment
**usr/libexec/steam-runtime-tools-0/logger-0.bash**
sets up a **srt-logger** instance to absorb log messages from a **bash**(1)
script as a background subprocess, and sets appropriate environment variables
for the rest of the script.
This implies **--background**, and does not support
**--exec-fallback**, **--sh-syntax** or a *COMMAND*.
It makes use of **bash**(1) features and therefore cannot be used from
a **#!/bin/sh** script.

# OPTIONS

<dl>
<dt>

**--background**

</dt><dd>

Run a new **srt-logger** process in the background (daemonized),
instead of having it become a child of the *COMMAND* or the parent
process.

This can be useful when combined with **steam-runtime-supervisor**,
because it is better for the **srt-logger** not to be killed during
process termination (if it was, then any buffered log output would
be lost).

</dd>
<dt>

**--exec-fallback**

</dt><dd>

If unable to set up logging, run the *COMMAND* anyway.
If there is no *COMMAND*, and **--background** was not used,
mirror messages received on standard input
to standard error by running **cat**(1).
This option should be used in situations where fault-tolerance is
more important than logging.

</dd>
<dt>

**--filename** *FILENAME*

</dt><dd>

Write to the given *FILENAME* in the **--log-directory**,
with rotation when it becomes too large.

An empty *FILENAME* disables logging to flat files.

If no **--filename** is given, the default is to append **.txt**
to the **--identifier**.

</dd>
<dt>

**--identifier** *STRING*, **-t** *STRING*

</dt><dd>

When writing to the systemd Journal, log with the given *STRING*
as an identifier.

An empty *STRING* disables logging to the systemd Journal,
unless a **--journal-fd** is also given.

If no **--identifier** is given, the default is to remove the
extension from the **--filename**.

If no **--filename** is given either, when run with a *COMMAND*,
the default is the basename of the first word of the *COMMAND*,
similar to **systemd-cat**(1).

</dd>
<dt>

**--journal-fd** *FD*

</dt><dd>

Receive an inherited file descriptor instead of opening a new connection
to the Journal.

</dd>
<dt>

**--log-directory** *PATH*

</dt><dd>

Interpret the **--filename** as being in *PATH*.
If `$SRT_LOG_DIR` is set, the default is that directory,
interpreted as being absolute or relative to the current working
directory in the usual way.
Otherwise, if `$STEAM_CLIENT_LOG_FOLDER` is set, the default is
that directory interpreted as relative to `~/.steam/steam` (unusually,
this is done even if it starts with `/`).
Otherwise the default is `~/.steam/steam/logs`.
The log directory must already exist during **srt-logger** startup.

</dd>
<dt>

**--log-fd** *FD*

</dt><dd>

Receive an inherited file descriptor for the log file instead of
opening the log file again.

It is an error to provide a **--log-fd** that does not point to the
**--filename** in the **--log-directory**.
If this is done, it is likely to lead to unintended results and
potentially loss of log messages during log rotation.

</dd>
<dt>

**--mkfifo**

</dt><dd>

Instead of carrying out logging, create a **fifo**(7) (named pipe)
in a subdirectory of any appropriate location, write its path to
standard output followed by a newline and exit.

This option cannot be combined with any other options.
It is primarily intended for use in portable shell scripts, to avoid
relying on external **mktemp**(1) and **mkfifo**(1) commands.

If the fifo is successfully created, the caller is responsible for
deleting both the fifo itself and its parent directory after use.
If **srt-logger** fails to create a fifo, no cleanup is required.
The current implementation for this option creates a subdirectory
with a random name in either `$XDG_RUNTIME_DIR`, `$TMPDIR`
or `/tmp`, similar to `mktemp -d`, then creates the fifo inside
that subdirectory.
If one of those directories cannot hold fifo objects, **srt-logger**
will show a warning and fall back to the next.

</dd>
<dt>

**--no-auto-terminal**

</dt><dd>

Don't copy logged messages to the terminal, if any.
If standard error is a terminal, the default is to copy logged messages
to that terminal, and also provide it to subprocesses via
`$SRT_LOG_TERMINAL`.
Otherwise, if `$SRT_LOG_TERMINAL` is set, the default is to copy logged
messages to that terminal.

</dd>
<dt>

**--sh-syntax**

</dt><dd>

Write machine-readable output to standard output.
The output format is line-oriented, can be parsed by **eval**(1posix),
and on success the last line will be exactly **SRT_LOGGER_READY=1**
followed by a newline.
When this option is used, if standard output is closed without any
output, that indicates that the logger failed to initialize.
If this happens, diagnostic messages will be written to standard error
as usual.
This is mainly used to implement **--background**, but can also be used
elsewhere.

If a line before the last starts with **SRT_LOGGER_PID=**, then it
is a shell-style assignment indicating the process ID of the process
that is responsible for logging (which might be a daemonized background
process).
The associated value might be quoted, and will need to be unquoted
according to **sh**(1) rules before use,
for example with GLib's **g_shell_unquote** or Python's **shlex.split**.

If a line before the last starts with **export** or **unset** followed
by a space, then it indicates an environment variable that should be
set or unset for processes that are writing their output to this
logger, to ensure that any **srt-logger** child processes direct their
log output to appropriate destinations.
The associated value for an **export** might be quoted, as above.

Other output may be produced in future versions of this tool.

</dd>
<dt>

**--rotate** *BYTES*

</dt><dd>

If the **--filename** would exceed *BYTES*, rename it to a different
filename (for example `log.txt` becomes `log.previous.txt`)
and start a new log.

If log rotation fails or another process is holding a lock on the
log file, then rotation is disabled and *BYTES* may be exceeded.

*BYTES* may be suffixed with
`K`, `KiB`, `M` or `MiB` for powers of 1024,
or `kB` or `MB` for powers of 1000.

The default is 8 MiB. **--rotate=0** or environment variable
**SRT_LOG_ROTATION=0** can be used to disable rotation.

</dd>
<dt>

**--terminal-fd** *FD*

</dt><dd>

Receive an inherited file descriptor for the terminal instead of
opening the terminal device again.

</dd>
<dt>

**--timestamps**, **--no-timestamps**

</dt><dd>

Do or don't prepend timestamps to each line in the log file.
The default is to add timestamps, unless environment variable
**SRT_LOGGER_TIMESTAMPS=0** is set.

</dd>
<dt>

**--use-journal**

</dt><dd>

Write messages to the systemd Journal if possible, even if no
**--journal-fd** was given.

</dd>
<dt>

**--parse-level-prefix**

</dt><dd>

If any log lines begin with a syslog priority prefix (an less-than `<`,
a level number 0-7, and a greater-than `>`), then strip that prefix off of
the line and use the number within as the line's log level. This is a
limited subset of RFC 5424's syslog message format, with the following
supported levels (available from C via the `LOG_*` constants in
**syslog**(3)), in order of most to least important:

| Level number | Name      |
| ------------ | --------- |
| 0            | emergency |
| 1            | alert     |
| 2            | critical  |
| 3            | error     |
| 4            | warning   |
| 5            | notice    |
| 6            | info      |
| 7            | debug     |

If an entire log line is `<remaining-lines-assume-level=N>`, where *N* is a
level number from 0-7 as mentioned above, then subsequent lines will be
logged with the level *N* and will not have any specific prefixes from them
parsed.

For example, given these lines:

```
<5>abc
def
<remaining-lines-assume-level=4>
ghi
<3>jkl
```

- The first line will be logged as `abc` with log level 5 / notice.
- The second line will be logged as `def` with the log level given to
  **--default-level**.
- The third line will not be logged but will instead direct the logger to
  use the level 4 / warning for all lines that follow.
- The fourth line will be logged as `ghi` with log level 4 / warning.
- The fifth line will be logged as `<3>jkl` with log level 3 / error.

</dd>
<dt>

**--default-level=**_LEVEL_

</dt><dd>

Use the given level as the default level for all log lines, unless
**--parse-level-prefix** is given and the line has its own prefix. The given
level must be one of the following numbers, corresponding names (in any
case), or aliases:

| Level number | Name      | Alias | Alias |
| ------------ | --------- | ----- | ----- |
| 0            | emergency | emerg |       |
| 1            | alert     | alert |       |
| 2            | critical  | crit  |       |
| 3            | error     | err   | e     |
| 4            | warning   | warn  | w     |
| 5            | notice    |       | n     |
| 6            | info      |       | i     |
| 7            | debug     |       | d     |

Defaults to `info`.

</dd>
<dt>

**--file-level=**_LEVEL_

</dt><dd>

Only send log lines to the log file from **--log-fd** or **--filename** that
have a log level at or more important than _LEVEL_, parsed in the same
format as **--default-level**.

Defaults to `debug` (which will effectively write everything).

</dd>
<dt>

**--journal-level=**_LEVEL_

</dt><dd>

Only send log lines to the systemd journal that have a log level at or more
important than _LEVEL_, parsed in the same format as **--default-level**.

Defaults to `debug` (which will effectively write everything).

</dd>
<dt>

**--terminal-level=**_LEVEL_

</dt><dd>

Only send log lines to the terminal that have a log level at or more
important than _LEVEL_, parsed in the same format as **--default-level**.

Defaults to `info`.

</dd>
<dt>

**--verbose**, **-v**

</dt><dd>

Be more verbose. If used twice, debug messages are shown.

</dd>
<dt>

**--version**

</dt><dd>

Print version information in YAML format.

</dd>
</dl>

# ENVIRONMENT

<dl>
<dt>

`NO_COLOR`

</dt><dd>

If present and non-empty, disables all coloring of logs written to the
terminal.

</dd>
<dt>

`SRT_LOG`

</dt><dd>

A sequence of tokens separated by colons, spaces or commas
affecting how diagnostic output from **srt-logger** itself is recorded.
In particular, `SRT_LOG=journal` has the same effect as
`SRT_LOG_TO_JOURNAL=1`.

</dd>
<dt>

`SRT_LOG_DIR`

</dt><dd>

An absolute or relative path to be used instead of the default
log directory.

</dd>
<dt>

`SRT_LOG_ROTATION` (`0` or `1`)

</dt><dd>

If set to `0`, log rotation is disabled and **--rotate** is ignored.

</dd>
<dt>

`SRT_LOG_TERMINAL`

</dt><dd>

If set to the absolute path of a terminal device such as `/dev/pts/0`,
**srt-logger** will attempt to copy all log messages to that
terminal device.
If set to the empty string, the effect is the same as
**--no-auto-terminal**.
If output to the terminal is enabled, **srt-logger** also sets this
environment variable for the *COMMAND*.

</dd>
<dt>

`SRT_LOG_TO_JOURNAL`, `SRT_LOGGER_USE_JOURNAL`

</dt><dd>

If either is set to `1`, log to the systemd Journal if available.
As well as redirecting diagnostic output from **srt-logger** itself,
this has an effect similar to adding
**--use-journal** to all **srt-logger** invocations.
If output to the Journal is enabled, **srt-logger** also sets
**SRT_LOGGER_USE_JOURNAL=1** for the *COMMAND*,
and may set **SRT_LOG_TO_JOURNAL=0** in order to ensure that child
processes that are part of the steam-runtime-tools suite do not write
directly to the Journal, which would cause them to bypass the
**srt-logger**.

</dd>
<dt>

`SRT_LOGGER`

</dt><dd>

If set, **logger-0.bash** will use this instead of locating an
adjacent **srt-logger** executable automatically.

</dd>
<dt>

`SRT_LOGGER_TIMESTAMPS` (`0` or `1`)

</dt><dd>

Set the behaviour if neither **--timestamps** nor **--no-timestamps**
is specified.

</dd>
<dt>

`STEAM_CLIENT_LOG_FOLDER`

</dt><dd>

A path relative to `~/.steam/steam` to be used as a default log
directory if `$SRT_LOG_DIR` is unset.
The default is `logs`.
Note that unusually, this is interpreted as a relative path, even if
it starts with `/` (this is consistent with the behaviour of the
Steam client).

</dd>
</dl>

# OUTPUT

If a **--filename** could be determined, then log messages from standard
input or the *COMMAND* are written to that file.

If there was a **--journal-fd**, **--use-journal** was given, or standard
error is the systemd Journal, then the same log messages are copied to the
systemd Journal.

If neither of those log destinations are available, then log messages
are written to standard error.

Additionally, if a terminal has been selected for logging, the same
log messages are copied to that terminal.

If **srt-logger --mkfifo** was used, then the only output on standard
output is the absolute path to the fifo.

Unstructured human-readable diagnostic messages are written to standard
error if appropriate.

# EXIT STATUS

The exit status is similar to **env**(1):

0
:   The *COMMAND* exited successfully with status 0.

125
:   Invalid arguments were given, or **srt-logger** failed to start.

126, 127
:   The *COMMAND* was not found or could not be launched.
    These errors are not currently distinguished.

255
:   The *COMMAND* was launched, but its exit status could not be
    determined. This happens if the wait-status was neither
    normal exit nor termination by a signal.

Any value
:   The *COMMAND* exited unsuccessfully with the status indicated.

128 + *n*
:   The *COMMAND* was killed by signal *n*.
    (This is the same encoding used by **bash**(1), **bwrap**(1) and
    **env**(1).)

If invoked as **srt-logger --mkfifo**, instead the exit status is:

0
:   The fifo was created successfully.

Any other value
:   An error was encountered.

# NOTES

If the **srt-logger** is to be combined with **steam-runtime-supervisor**(1)
or another subreaper that will wait for all of its descendant processes to
exit, then the **srt-logger** should normally be run "outside" the control
of the subreaper, using the **--background** option. This avoids situations
that can cause a deadlock or a loss of log messages.

This is particularly important when using **steam-runtime-supervisor**(1)
with the **--terminate-timeout** option, or a similar subreaper that will
terminate all of its child processes.

# EXAMPLES

Run vulkaninfo and send its output to vulkaninfo.txt and the systemd Journal:

    ~/.steam/root/ubuntu12_32/steam-runtime/run.sh \
    srt-logger --use-journal -t vulkaninfo -- vulkaninfo

The best way to combine **srt-logger** with **steam-runtime-supervisor**(1)
is to run the **srt-logger** "outside" the **steam-runtime-supervisor**
and use **--background**:

    ~/.steam/root/ubuntu12_32/steam-runtime/run.sh \
    srt-logger --background --use-journal -t supervised-game -- \
    steam-runtime-supervisor --subreaper --terminate-timeout=5 -- \
    ./your-game

In a **bash**(1) script that will run in the Steam Runtime 1 'scout'
`LD_LIBRARY_PATH` environment, or on the host system with no particular
compatibility tools:

    #!/usr/bin/env bash
    set -eu

    # ... early setup ...

    runtime="${STEAM_RUNTIME-"${STEAM_RUNTIME_SCOUT-"$HOME/.steam/root/ubuntu12_32/steam-runtime"}"}"

    if ! source "$runtime/usr/libexec/steam-runtime-tools-0/logger-0.bash" -t this-script
    then
        echo "Unable to set up log files, continuing anyway" >&2
    fi

    # ... now everything written to stdout or stderr will be logged ...

In a **bash**(1) script that will run in the Steam Linux Runtime 2.0 'soldier'
or 3.0 'sniper' environment, this can be simplified to:

    #!/bin/bash
    set -eu

    # ... early setup ...

    if ! source "/usr/libexec/steam-runtime-tools-0/logger-0.bash" -t this-script
    then
        echo "Unable to set up log files, continuing anyway" >&2
    fi

    # ... now everything written to stdout or stderr will be logged ...

<!-- vim:set sw=4 sts=4 et: -->
