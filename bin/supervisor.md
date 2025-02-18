---
title: steam-runtime-supervisor
section: 1
...

<!-- This document:
Copyright © 2020-2024 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-supervisor - run and supervise a subprocess

# SYNOPSIS

**steam-runtime-supervisor**
[**--assign-fd** _TARGET_**=**_SOURCE_...]
[**--clear-env**]
[**--[no-]-close-fds**]
[**--env** _VAR_**=**_VALUE_]
[**--env-fd** *FD*]
[**--[no-]exit-with-parent**]
[**--inherit-env** *VAR*]
[**--inherit-env-matching** *WILDCARD*]
[**--lock-fd** *FD*...]
[**--pass-fd** *FD*...]
[**--[no-]subreaper**]
[**--terminate-timeout** *SECONDS* [**--terminate-idle-timeout** *SECONDS*]]
[**--unset-env** *VAR*]
[**--verbose** [**--verbose**]]
[[**--[no-]lock-create**]
[**--[no-]lock-wait**]
[**--[no-]lock-exclusive**]
**--lock-file** *FILENAME*...]
[**--**]
[*COMMAND* [*ARGUMENTS...*]]

# DESCRIPTION

**steam-runtime-supervisor** runs *COMMAND* as a child process and waits
for it to exit, with modifications to its execution environment as
determined by the options.

# OPTIONS

<dl>
<dt>

**--assign-fd** _TARGET_**=**_SOURCE_

</dt><dd>

Make file descriptor *TARGET* in the *COMMAND* a copy of file
descriptor *SOURCE* as passed to **steam-runtime-supervisor**,
similar to writing `TARGET>&SOURCE` as a shell redirection.
For example, **--assign-fd=1=3** is the same as **1>&3**.
The redirection is done at the last possible moment, so the output
of **steam-runtime-supervisor** (if any) will still go to the
original standard error.

</dd>
<dt>

**--close-fds**

</dt><dd>

Do not pass inherited file descriptors to the *COMMAND*,
except for file descriptors 0, 1 and 2
(**stdin**, **stdout** and **stderr**)
and any file descriptors passed to **--pass-fd** or as the target
of **--assign-fd**.
**--no-close-fds** disables this behaviour, and is the default.

</dd>
<dt>

**--env** _VAR=VALUE_

</dt><dd>

Set environment variable _VAR_ to _VALUE_.
This is mostly equivalent to using
**env** _VAR=VALUE_ *COMMAND* *ARGUMENTS...*
as the command.

</dd>
<dt>

**--env-fd** _FD_

</dt><dd>

Parse zero-terminated environment variables from _FD_, and set each
one as if via **--env**.
The format of _FD_ is the same as the output of `$(env -0)` or the
pseudo-file `/proc/PID/environ`.

</dd>
<dt>

**--exit-with-parent**

</dt><dd>

Arrange for **steam-runtime-supervisor** to receive **SIGTERM**
(which it will pass on to *COMMAND*, if possible) when its parent
process exits.
**--no-exit-with-parent** disables this behaviour, and is the default.

</dd>
<dt>

**--inherit-env** *VAR*

</dt><dd>

Undo the effect of a previous **--env**, **--unset-env**
or similar, returning to the default behaviour of inheriting *VAR*
from the execution environment of **steam-runtime-supervisor**
(unless **--clear-env** was used, in which case this option becomes
effectively equivalent to **--unset-env**).

</dd>
<dt>

**--inherit-env-matching** *WILDCARD*

</dt><dd>

Do the same as for **--inherit-env** for any environment variable
whose name matches *WILDCARD*.
If this command is run from a shell, the wildcard will usually need
to be quoted, for example **--inherit-env-matching="FOO&#x2a;"**.

</dd>
<dt>

**--lock-create**

</dt><dd>

Create each **--lock-file** that appears on the command-line after
this option if it does not exist, until a **--no-lock-create** option
is seen.
**--no-lock-create** reverses this behaviour, and is the default.

</dd>
<dt>

**--lock-exclusive**

</dt><dd>

Each **--lock-file** that appears on the command-line after
this option will be locked in **F_WRLCK** mode (an exclusive/write
lock), until a **--no-lock-exclusive** or **--lock-shared**
option is seen.
**--no-lock-exclusive** or **--lock-shared** results
in use of **F_RDLCK** (a shared/read lock), and is the default.

</dd>
<dt>

**--lock-fd** *FD*

</dt><dd>

Receive file descriptor *FD* (specified as a small positive integer)
from the parent process, and keep it open until
**steam-runtime-supervisor** exits. This is most useful if *FD*
is locked with a Linux open file description lock (**F_OFD_SETLK**
or **F_OFD_SETLKW** from **fcntl**(2)), in which case the lock will
be held by **steam-runtime-supervisor**.

</dd>
<dt>

**--lock-file** *FILENAME*

</dt><dd>

Lock the file *FILENAME* according to the most recently seen
**--[no-]lock-create**, **--[no-]lock-wait** and **--[no-]-lock-exclusive**
options, using a Linux open file description lock (**F_OFD_SETLK** or
**F_OFD_SETLKW** from **fcntl**(2)) if possible, or a POSIX
process-associated record lock (**F_SETLK** or **F_SETLKW**) on older
kernels.

These locks interact in the expected way with **bwrap**(1),
**flatpak**(1) and other parts of **steam-runtime-tools**.
It is unspecified whether they exclude the **flock**(2) locks used
by util-linux **flock**(1) or not, so using those locks on lock
files used by **steam-runtime-tools** should be avoided.

</dd>
<dt>

**--lock-wait**

</dt><dd>

For each **--lock-file** that appears on the command-line after
this option until a **--no-lock-wait** option is seen, if the file is
already locked in an incompatible way, **steam-runtime-supervisor**
will wait for the current holder of the lock to release it.
With **--no-lock-wait**, which is the default,
**steam-runtime-supervisor** will exit with status 125
if a lock cannot be acquired.

</dd>
<dt>

**--lock-wait-verbose**

</dt><dd>

Same as **--lock-wait**, but if the lock cannot be acquired immediately,
log a message before waiting for it and another message after it is
acquired.

</dd>
<dt>

**--pass-fd** *FD*

</dt><dd>

Pass the file descriptor *FD* (specified as a small positive integer)
from the parent process to the *COMMAND*, even if **--close-fds**
was specified.
This option has no practical effect if **--close-fds** was not used.

</dd>
<dt>

**--subreaper**

</dt><dd>

If the *COMMAND* starts background processes, arrange for them to
be reparented to **steam-runtime-supervisor** instead of to **init**
when their parent process exits, and do not exit until all such
descendant processes have exited.

</dd>
<dt>

**--terminate-idle-timeout** *SECONDS*

</dt><dd>

If a non-negative **--terminate-timeout** is specified, wait this
many seconds before sending **SIGTERM** to child processes.
Non-integer decimal values are allowed.
0 or negative means send **SIGTERM** immediately, which is the
default.
This option is ignored if **--terminate-timeout** is not used.

</dd>
<dt>

**--terminate-timeout** *SECONDS*

</dt><dd>

If non-negative, terminate background processes after the *COMMAND*
exits. This implies **--subreaper**.
Non-integer decimal values are allowed.
When this option is enabled, after *COMMAND* exits,
**steam-runtime-supervisor** will wait for up to the time specified
by **--terminate-idle-timeout**, then send **SIGTERM** to any
remaining child processes and wait for them to exit gracefully.
If child processes continue to run after a further time specified
by this option, send **SIGKILL** to force them to be terminated,
and continue to send **SIGKILL** until there are no more descendant
processes. If *SECONDS* is 0, **SIGKILL** is sent immediately.
A negative number means signals are not sent, which is the default.

</dd>
<dt>

**--unset-env** *VAR*

</dt><dd>

Unset *VAR* when running the command.
This is mostly equivalent to using
**env -u** *VAR* *COMMAND* *ARGUMENTS...*
as the command.

</dd>
<dt>

**--verbose**

</dt><dd>

Be more verbose. If used twice, debug messages are shown.

</dd>
</dl>

# ENVIRONMENT

<dl>
<dt>

`PRESSURE_VESSEL_LOG_INFO` (boolean)

</dt><dd>

If set to 1, same as `SRT_LOG=info` or **--verbose**

</dd>
<dt>

`PRESSURE_VESSEL_LOG_WITH_TIMESTAMP` (boolean)

</dt><dd>

If set to 1, same as `SRT_LOG=timestamp`

</dd>
<dt>

`SRT_LOG`

</dt><dd>

A sequence of tokens separated by colons, spaces or commas
affecting how output is recorded. See source code for details.

</dd>
</dl>

# OUTPUT

The standard output from *COMMAND* is printed on standard output.

The standard error from *COMMAND* is printed on standard error.
Diagnostic messages from **steam-runtime-supervisor** may also be printed
on standard error.

# SIGNALS

If **steam-runtime-supervisor** receives signals **SIGHUP**, **SIGINT**,
**SIGQUIT**, **SIGTERM**, **SIGUSR1** or **SIGUSR2**, it immediately
sends the same signal to *COMMAND*, hopefully causing *COMMAND* to
exit gracefully.

# EXIT STATUS

The exit status is similar to **env**(1):

0
:   The *COMMAND* exited successfully with status 0.

125
:   Invalid arguments were given, or **steam-runtime-supervisor**
    failed to start.

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

<!-- vim:set sw=4 sts=4 et: -->
