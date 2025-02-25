---
title: pv-adverb
section: 1
...

<!-- This document:
Copyright © 2020-2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

pv-adverb - wrap processes in various ways

# SYNOPSIS

**pv-adverb**
[**--[no-]exit-with-parent**]
[**--assign-fd** _TARGET_**=**_SOURCE_...]
[**--clear-env**]
[**--env** _VAR_**=**_VALUE_]
[**--env-fd** *FD*]
[**--fd** *FD*...]
[**--[no-]generate-locales**]
[**--inherit-env** *VAR*]
[**--inherit-env-matching** *WILDCARD*]
[**--ld-audit** *MODULE*[**:arch=***TUPLE*]...]
[**--ld-preload** *MODULE*[**:**...]...]
[**--pass-fd** *FD*...]
[[**--add-ld.so-path** *PATH*...]
**--regenerate-ld.so-cache** *PATH*]
[**--set-ld-library-path** *VALUE*]
[**--shell** **none**|**after**|**fail**|**instead**]
[**--subreaper**]
[**--terminal** **none**|**auto**|**tty**|**xterm**]
[**--terminate-timeout** *SECONDS* [**--terminate-idle-timeout** *SECONDS*]]
[**--unset-env** *VAR*]
[[**--[no-]create**]
[**--[no-]wait**]
[**--[no-]write**]
**--lock-file** *FILENAME*...]
[**--verbose**]
[**--**]
*COMMAND* [*ARGUMENTS...*]

# DESCRIPTION

**pv-adverb** runs *COMMAND* as a child process, with
modifications to its execution environment as determined by the options.

By default, it just runs *COMMAND* as a child process and reports its
exit status.

# OPTIONS

<dl>
<dt>

**--add-ld.so-path** *PATH*

</dt><dd>

Add *PATH* to the search path for **--regenerate-ld.so-cache**.
The final search path will consist of all **--add-ld.so-path**
arguments in the order they are given, followed by the lines
from `runtime-ld.so.conf` in order.

</dd>
<dt>

**--assign-fd** _TARGET_**=**_SOURCE_

</dt><dd>

Make file descriptor *TARGET* in the *COMMAND* a copy of file
descriptor *SOURCE* as passed to **pv-adverb**,
similar to writing `TARGET>&SOURCE` as a shell redirection.
For example, **--assign-fd=1=3** is the same as **1>&3**.
The redirection is done at the last possible moment, so the output
of **pv-adverb** will still go to the original standard
error.

</dd>
<dt>

**--create**

</dt><dd>

Create each **--lock-file** that appears on the command-line after
this option if it does not exist, until a **--no-create** option
is seen. **--no-create** reverses this behaviour, and is the default.

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

Arrange for **pv-adverb** to receive **SIGTERM**
(which it will pass on to *COMMAND*, if possible) when its parent
process exits. This is particularly useful when it is wrapped in
**bwrap** by **pressure-vessel-wrap**, to arrange for the
**pv-adverb** command to exit when **bwrap** is killed.
**--no-exit-with-parent** disables this behaviour, and is the default.

</dd>
<dt>

**--fd** *FD*

</dt><dd>

Receive file descriptor *FD* (specified as a small positive integer)
from the parent process, and keep it open until
**pv-adverb** exits. This is most useful if *FD*
is locked with a Linux open file description lock (**F_OFD_SETLK**
or **F_OFD_SETLKW** from **fcntl**(2)), in which case the lock will
be held by **pv-adverb**.

</dd>
<dt>

**--generate-locales**

</dt><dd>

If not all configured locales are available, generate them in a
temporary directory which is passed to the *COMMAND* in the
**LOCPATH** environment variable.
**--no-generate-locales** disables this behaviour, and is the default.

</dd>
<dt>

**--inherit-env** *VAR*

</dt><dd>

Undo the effect of a previous **--env**, **--unset-env**
or similar, returning to the default behaviour of inheriting *VAR*
from the execution environment of **pv-adverb**
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

**--ld-audit** *MODULE*[**:arch=***TUPLE*]

</dt><dd>

Add *MODULE* to **LD_AUDIT** before executing *COMMAND*.
The optional *TUPLE* is the same as for **--ld-preload**, below.

</dd>
<dt>

**--ld-preload** *MODULE*[**:arch=***TUPLE*]

</dt><dd>

Add *MODULE* to **LD_PRELOAD** before executing *COMMAND*.

If the optional **:arch=***TUPLE* is given, the *MODULE* is only used for
the given architecture, and is paired with other modules (if any) that
share its basename; for example,
`/home/me/.steam/root/ubuntu12_32/gameoverlayrenderer.so:arch=i386-linux-gnu`
and
`/home/me/.steam/root/ubuntu12_64/gameoverlayrenderer.so:arch=x86_64-linux-gnu`
will be combined into a single **LD_PRELOAD** entry of the form
`/tmp/pressure-vessel-libs-123456/${PLATFORM}/gameoverlayrenderer.so`.

For a **LD_PRELOAD** module named `gameoverlayrenderer.so` in a directory
named `ubuntu12_32` or `ubuntu12_64`, the architecture is automatically
set to `i386-linux-gnu` or `x86_64-linux-gnu` respectively, if not
otherwise given. Other special-case behaviour might be added in future
if required.

</dd>
<dt>

**--lock-file** *FILENAME*

</dt><dd>

Lock the file *FILENAME* according to the most recently seen
**--[no-]create**, **--[no-]wait** and **--[no-]write** options,
using a Linux open file description lock (**F_OFD_SETLK** or
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

**--pass-fd** *FD*

</dt><dd>

Pass the file descriptor *FD* (specified as a small positive integer)
from the parent process to the *COMMAND*. The default is to only pass
through file descriptors 0, 1 and 2
(**stdin**, **stdout** and **stderr**).

</dd>
<dt>

**--regenerate-ld.so-cache** *PATH*

</dt><dd>

Regenerate "ld.so.cache" in the directory *PATH*.

On entry to **pv-adverb**, *PATH* should
contain `runtime-ld.so.conf`, a symbolic link or copy
of the runtime's original `/etc/ld.so.conf`. It will
usually also contain `ld.so.conf` and `ld.so.cache`
as symbolic links or copies of the runtime's original
`/etc/ld.so.conf` and `/etc/ld.so.cache`.

Before executing the *COMMAND*, **pv-adverb**
will construct a new `ld.so.conf` in *PATH*, consisting of
all **--add-ld.so-path** arguments, followed by the contents
of `runtime-ld.so.conf`; then it will generate a new
`ld.so.cache` from that configuration. Both of these
will atomically replace the original files in *PATH*.

Other filenames in *PATH* will be used temporarily.

To make use of this feature, a container's `/etc/ld.so.conf`
and `/etc/ld.so.cache` should usually be symbolic links into
the *PATH* used here, which will typically be below `/run`.

</dd>
<dt>

**--set-ld-library-path** *VALUE*

</dt><dd>

Set the environment variable `LD_LIBRARY_PATH` to *VALUE* after
processing **--regenerate-ld.so-cache** (if used) and any
environment variable options such as **--env** (if used),
but before executing *COMMAND*.

</dd>
<dt>

**--shell=after**

</dt><dd>

Run an interactive shell after *COMMAND* exits.
In that shell, running **"$@"** will re-run *COMMAND*.

</dd>
<dt>

**--shell=fail**

</dt><dd>

The same as **--shell=after**, but do not run the shell if *COMMAND*
exits with status 0.

</dd>
<dt>

**--shell=instead**

</dt><dd>

The same as **--shell=after**, but do not run *COMMAND* at all.

</dd>
<dt>

**--shell=none**

</dt><dd>

Don't run an interactive shell. This is the default.

</dd>
<dt>

**--subreaper**

</dt><dd>

If the *COMMAND* starts background processes, arrange for them to
be reparented to **pv-adverb** instead of to **init**
when their parent process exits, and do not exit until all such
descendant processes have exited.
A non-negative **--terminate-timeout** implies this option.

</dd>
<dt>

**--terminal=auto**

</dt><dd>

Equivalent to **--terminal=xterm** if a **--shell** option other
than **none** is used, or **--terminal=none** otherwise.
This is the default.

</dd>
<dt>

**--terminal=none**

</dt><dd>

Disable features that would ordinarily use a terminal.

</dd>
<dt>

**--terminal=tty**

</dt><dd>

Use the current execution environment's controlling tty for
the **--shell** options.

</dd>
<dt>

**--terminal=xterm**

</dt><dd>

Start an **xterm**(1), and run *COMMAND* and/or an interactive
shell in that environment.

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
**pv-adverb** will wait for up to the time specified
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

Be more verbose.

</dd>
<dt>

**--wait**

</dt><dd>

For each **--lock-file** that appears on the command-line after
this option until a **--no-wait** option is seen, if the file is
already locked in an incompatible way, **pv-adverb**
will wait for the current holder of the lock to release it.
With **--no-wait**, which is the default, **pv-adverb**
will exit with status 75 (**EX_TEMPFAIL**) if a lock cannot be acquired.

</dd>
<dt>

**--write**

</dt><dd>

Each **--lock-file** that appears on the command-line after
this option will be locked in **F_WRLCK** mode (an exclusive/write
lock), until a **--no-write** option is seen. **--no-write** results
in use of **F_RDLCK** (a shared/read lock), and is the default.

</dd>
</dl>

# ENVIRONMENT

<dl>
<dt>

`PRESSURE_VESSEL_LOG_INFO` (boolean)

</dt><dd>

If set to 1, same as `SRT_LOG=info`

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
Diagnostic messages from **pv-adverb** may also be printed
on standard error.

# SIGNALS

If **pv-adverb** receives signals **SIGHUP**, **SIGINT**,
**SIGQUIT**, **SIGTERM**, **SIGUSR1** or **SIGUSR2**, it immediately
sends the same signal to *COMMAND*, hopefully causing *COMMAND* to
exit gracefully.

# EXIT STATUS

Nonzero (failure) exit statuses are subject to change, and might be
changed to be more like **env**(1) in future.

0
:   The *COMMAND* exited successfully with status 0

1
:   An error occurred while setting up the execution environment or
    starting the *COMMAND*

64 (`EX_USAGE` from `sysexits.h`)
:   Invalid arguments were given

69 (`EX_UNAVAILABLE` from `sysexits.h`)
:   An error occurred while setting up the execution environment

75 (`EX_TEMPFAIL` from `sysexits.h`)
:   A **--lock-file** could not be acquired, and **--wait** was not given

126
:   The *COMMAND* could not be started

127
:   The *COMMAND* was not found

255
:   The *COMMAND* terminated in an unknown way

Any value 1-255
:   The *COMMAND* exited unsuccessfully with the status indicated

128 + *n*
:   The *COMMAND* was killed by signal *n*
    (this is the same encoding used by **bash**(1), **bwrap**(1) and
    **env**(1))

# EXAMPLE

When running a game in a container as a single command,
**pressure-vessel-wrap** uses a pattern similar to:

    bwrap \
        ... \
    /path/to/pv-adverb \
        --exit-with-parent \
        --generate-locales \
        --lock-file=/path/to/runtime/.ref \
        --subreaper \
        -- \
    /path/to/game ...

to preserve Steam's traditional behaviour for native Linux games (where
all background processes must exit before the game is considered to have
exited).

When running **steam-runtime-launcher-service** in a container, the adverb looks
more like this:

    bwrap \
        ... \
    /path/to/pv-adverb \
        --exit-with-parent \
        --generate-locales \
        --lock-file=/path/to/runtime/.ref \
        --subreaper \
        --terminate-timeout=10 \
        -- \
    /path/to/steam-runtime-launcher-service ...

so that when the **steam-runtime-launcher-service** is terminated by
**steam-runtime-launch-client --terminate**, or when the **bwrap** process
receives a fatal signal, the **pv-adverb** process will
gracefully terminate any remaining child/descendant processes before
exiting itself.

<!-- vim:set sw=4 sts=4 et: -->
