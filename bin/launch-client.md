---
title: steam-runtime-launch-client
section: 1
...

<!-- This document:
Copyright © 2020-2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-launch-client - launch processes across a container boundary

# SYNOPSIS

**steam-runtime-launch-client**
[**--clear-env**]
[**--directory** *DIR*]
[**--env** _VAR_**=**_VALUE_]
[**--env-fd** *FD*]
[**--forward-fd** *FD*]
[**--inherit-env** *VAR*]
[**--inherit-env-matching** *WILDCARD*]
[**--pass-env** *VAR*]
[**--pass-env-matching** *WILDCARD*]
[**--unset-env** *VAR*]
[**--terminate**]
[**--verbose**]
{**--bus-name** *NAME*|**--dbus-address** *ADDRESS*|**--socket** *SOCKET*|**--host**|**--inside-app** *ID*|**--alongside-steam**}
[**--**]
[*COMMAND* [*ARGUMENTS...*]]

**steam-runtime-launch-client**
*OPTIONS*
**-c** *SHELL_COMMAND*
[**--**]
[*$0* *ARGUMENTS...*]

**steam-runtime-launch-client**
[**--verbose**]
**--list**

# DESCRIPTION

**steam-runtime-launch-client** connects to a D-Bus service or an `AF_UNIX`
socket established by **steam-runtime-launcher-service**(1),
and executes an arbitrary command as a subprocess of
**steam-runtime-launcher-service**.

If no *COMMAND* is specified, and the **--terminate** option is not given,
then the default is to run an interactive shell.
This uses **$SHELL** if available in the container, falling back to
**bash**(1) or **sh**(1) if necessary.

If **steam-runtime-launcher-service** is seen as analogous to the
**sshd**(8) server
(but for communication between container environments rather than
between machines),
then **steam-runtime-launch-client** is the equivalent of the
**ssh**(1) client.

There are two major use-cases for **steam-runtime-launch-client**:

1. The Steam Linux Runtime container framework launches games in a per-game
    container.
    During development, it can be configured to run a
    **steam-runtime-launcher-service** inside each of these containers.
    If this is done, a **steam-runtime-launch-client** outside the
    container can be used to run debugging or diagnostic commands inside
    the container environment.

2. Conversely, because the Steam Linux Runtime container is not designed
    to be a security boundary, a developer tool or an interactive debugging
    session inside the per-game container can use
    **steam-runtime-launch-client** to reach outside the container and run
    debugging or diagnostic commands outside the container environment.

See **EXAMPLES**, below, for practical examples of each of these.

# OPTIONS

<dl>
<dt>

**--socket** *PATH*, **--socket** *@ABSTRACT*

</dt><dd>

Connect to the given `AF_UNIX` socket, which can either be an
absolute path, or `@` followed by an arbitrary string.
An absolute path indicates a filesystem-based socket, which is
associated with the filesystem and can be shared between filesystem
namespaces by bind-mounting.
`@` indicates an an abstract socket, which is associated with a
network namespace, is shared between all containers that are in
the same network namespace, and cannot be shared across network
namespace boundaries.
This option is deprecated: using a bus name on the well-known D-Bus
session bus is preferred.

</dd>
<dt>

**--dbus-address** *ADDRESS*

</dt><dd>

The same as **--socket**, but the socket is specified in the form
of a D-Bus address.
This option is deprecated: using a bus name on the well-known D-Bus
session bus is preferred.

</dd>
<dt>

**--alongside-steam**

</dt><dd>

Attempt to launch the *COMMAND* on behalf of a Steam app or game
that is running inside a Steam Linux Runtime container, so that
the *COMMAND* is run outside the container in approximately the
same execution environment as the script that launches the Steam client.
The Steam Runtime 1 'scout' library stack that is
used to run major components of the Steam client will
usually **not** be present in the **LD_LIBRARY_PATH** for commands
run like this.

This is currently equivalent to
**--bus-name=**_$SRT_LAUNCHER_SERVICE_ALONGSIDE_STEAM_
(if that variable is set), followed by
**--bus-name=com.steampowered.PressureVessel.LaunchAlongsideSteam**
(unconditionally) and
**--bus-name=org.freedesktop.portal.Flatpak** (if running inside the
Flatpak per-app container for the unofficial Steam Flatpak app).
It can be combined with other options that select a bus name:
in particular it is valid to use
**--alongside-steam --host**
to run a command in the context of the Steam client if possible or
on the host system otherwise, or
**--host --alongside-steam**
to use the opposite fallback sequence.

Unlike **--host**, if the Steam client was run as a Flatpak or Snap
application, the *COMMAND* will be run as part of that same Flatpak
or Snap application, but possibly in a Flatpak subsandbox environment.

The *COMMAND* will run in an unspecified execution environment which
will frequently change between different host operating systems and
over time, so this should only be used to run diagnostic commands
and similar things that are not part of the critical path for a game
to be run successfully.
Executables which are intended to be run like this should be compiled
against a glibc version that is as old as possible, and should
either use a relative **DT_RPATH** or **DT_RUNPATH** to find bundled
dependencies relative to the special token **${ORIGIN}**, or use
**dlopen**(3) to tolerate dependencies not being loaded successfully.

Official binary releases of the **pressure-vessel** tools are a good
example of what is required to run successfully on an arbitrary host
system: they are compiled in the Steam Runtime 1 'scout'
environment, all required dependencies except for glibc are bundled
with the pressure-vessel tools and loaded from
**${ORIGIN}/../lib/x86_64-linux-gnu** using a **DT_RPATH**, and the
optional **sd-journal**(3) library is loaded using **dlopen**(3).

As currently implemented, this feature requires a working D-Bus
session bus, the same as for **--bus-name**.

</dd>
<dt>

**--bus-name** *NAME*

</dt><dd>

Connect to the well-known D-Bus session bus and send commands to
the given *NAME*, which is normally assumed to be owned by
**steam-runtime-launcher-service**.

If used more than once, each *NAME* is tried in sequence and the
first that is owned on the session bus will be used.

As a special case, if the *NAME* is
**org.freedesktop.Flatpak**, then it is assumed to be
owned by the **flatpak-session-helper** per-user service, and the
*COMMAND* is launched on the host system, similar to the
**--host** option of **flatpak-spawn**(1).
The **--terminate** option is not allowed in this mode.

As another special case, if the *NAME* is
**org.freedesktop.portal.Flatpak**, then it is assumed to be
owned by the **flatpak-portal** per-user service, and the
*COMMAND* is launched in a Flatpak sub-sandbox, similar to
**flatpak-spawn**(1) without the **--host** option.
Most options that alter how the sub-sandbox is created, such as
**--sandbox-flag**, are not currently supported.
As with **org.freedesktop.Flatpak**, the
**--terminate** option is not allowed in this mode.

This option requires a working D-Bus session bus, which is typically
provided by the **dbus.service** per-user service on systemd systems,
or by **dbus-launch**(1) or **dbus-run-session**(1) on non-systemd
systems.

</dd>
<dt>

**--host**

</dt><dd>

Attempt to launch the *COMMAND* on the host system, similar to the
**--host** option of **flatpak-spawn**(1).
This is currently equivalent to
**--bus-name=org.freedesktop.Flatpak**, and can be combined with
other options that select a bus name, such as **--alongside-steam**.

Similar to **--alongside-steam**, the *COMMAND* will run in an unspecified
execution environment which will frequently change between different
host operating systems and over time.
See the description of **--alongside-steam** for more details on how to
compile suitable executables.

As currently implemented, this feature requires a working D-Bus
session bus, the same as for **--bus-name**.

</dd>
<dt>

**--inside-app** *ID*

</dt><dd>

Attempt to launch the *COMMAND* inside a pressure-vessel container
that is currently running Steam app ID *ID*.
*ID* may be **0**, for containers that are not running a Steam app.
This is currently equivalent to
**--bus-name=com.steampowered.App**_ID_, and can be combined with
other options that select a bus name, although this is unlikely to be
practically useful.

As currently implemented, this feature requires a working D-Bus
session bus, the same as for **--bus-name**.

</dd>
<dt>

**--app-path** *PATH*, **--app-path=**

</dt><dd>

When creating a Flatpak subsandbox, mount *PATH* as the `/app` in
the new subsandbox. If *PATH* is specified as the empty string,
place an empty directory at `/app`.

This option is only valid when used with
**--bus-name=org.freedesktop.portal.Flatpak**, and requires
Flatpak 1.12 or later.

</dd>
<dt>

**--clear-env**

</dt><dd>

Run the *COMMAND* in an empty environment, apart from any environment
variables set by **--env** and similar options, and environment
variables such as `PWD` that are set programmatically (see
[**ENVIRONMENT** section, below][ENVIRONMENT]).
If this option is not used, instead it inherits environment variables
from **steam-runtime-launcher-service**, with **--env** and
similar options overriding or unsetting individual variables.

</dd>
<dt>

**-c** *SHELL_COMMAND*, **--shell-command** *SHELL_COMMAND*

</dt><dd>

Run the *SHELL_COMMAND* using **sh**(1).
If additional arguments are given, the first is used to set `$0`
in the resulting shell, and the remaining arguments are the
shell's positional parameters `$@`.
This is a shortcut for using
**-- sh -c** *SHELL_COMMAND* [*$0* [*ARGUMENT*...]]
as the command and arguments.

</dd>
<dt>

**--directory** *DIR*

</dt><dd>

Arrange for the *COMMAND* to run with *DIR* as its current working
directory.

An empty string (typically written as **--directory=**) results in
inheriting the current working directory from the service that
will run the *COMMAND*.

When using **--alongside-steam**, **--host**,
**--bus-name=org.freedesktop.portal.Flatpak** or
**--bus-name=org.freedesktop.Flatpak**,
the default is to attempt to use the same working directory from
which **steam-runtime-launch-client**(1) was run, similar to the
effect of using **--directory="$(pwd)"** in a shell.
This matches the behaviour of **flatpak-spawn**(1).

Otherwise, the default is to inherit the current working directory
from the service, equivalent to **--directory=**.

</dd>
<dt>

**--forward-fd** *FD*

</dt><dd>

Arrange for the *COMMAND* to receive file descriptor number *FD*
from the current execution environment.
File descriptors 0, 1 and 2
(standard input, standard output and standard error) are always
forwarded.

If *FD* is a terminal, **steam-runtime-launch-client** will allocate
a pseudo-terminal (pty) and pass the terminal end of the pty to the
*COMMAND*, forwarding input and output between *FD* and the ptmx
end of the pty.

</dd>
<dt>

**--list**

</dt><dd>

Instead of running a *COMMAND*, list the services that
**steam-runtime-launch-client** could connect to.
Each line of output is an option that could be passed to
**steam-runtime-launch-client** to select a service.
This uses a heuristic to identify possible targets from their bus
names, so it is possible that not all possible targets are listed.
In the current implementation, it lists bus names starting with
**com.steampowered.App**, plus
**com.steampowered.PressureVessel.LaunchAlongsideSteam** and Flatpak
if available.

As currently implemented, this feature requires a working D-Bus
session bus, the same as for **--bus-name**.

</dd>
<dt>

**--share-pids**

</dt><dd>

If used with **--bus-name=org.freedesktop.portal.Flatpak**, use the
same process ID namespace for the new subsandbox as for the calling
process. This requires Flatpak 1.10 or later, running on a host
operating system where **bwrap**(1) is not and does not need to be
setuid root.

When communicating with a different API, this option is ignored.

</dd>
<dt>

**--terminate**

</dt><dd>

Terminate the **steam-runtime-launcher-service** server after the
*COMMAND* finishes running.
If no *COMMAND* is specified, terminate the server immediately.
This option is not available when communicating with Flatpak.

</dd>
<dt>

**--usr-path** *PATH*

</dt><dd>

When creating a Flatpak subsandbox, mount *PATH* as the `/usr` in
the new subsandbox.

This option is only valid when used with
**--bus-name=org.freedesktop.portal.Flatpak**, and requires
Flatpak 1.12 or later.

</dd>
<dt>

**--verbose**

</dt><dd>

Be more verbose.

</dd>
</dl>

# ENVIRONMENT OPTIONS

Options from this group are processed in order, with each option taking
precedence over any earlier options that affect the same environment variable.
For example,
`--pass-env-matching="FO&#x2a;" --env=FOO=bar --unset-env=FOCUS`
will set **FOO** to **bar**, unset **FOCUS** even if the caller has
it set, and pass through **FONTS** from the caller.

If standard input, standard output or standard error is a terminal, then
the **TERM** environment variable is passed through by default, as if
**--pass-env=TERM** had been used at the beginning of the command line.
This behaviour can be overridden by other options that affect **TERM**,
or disabled with **--inherit-env=TERM**.

<dl>
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

**--inherit-env** *VAR*

</dt><dd>

Undo the effect of a previous **--env**, **--unset-env**, **--pass-env**
or similar, returning to the default behaviour of inheriting *VAR*
from the execution environment of the service that is used to run
the *COMMAND* (unless **--clear-env** was used).

</dd>
<dt>

**--inherit-env-matching** *WILDCARD*

</dt><dd>

Do the same as for **--inherit-env** for any environment variable
whose name matches the **fnmatch**(3) pattern *WILDCARD*,
case-sensitively.
If this command is run from a shell, the wildcard will usually need
to be quoted, for example `--inherit-env-matching="FOO*"`.

</dd>
<dt>

**--pass-env** *VAR*

</dt><dd>

If the environment variable *VAR* is set, pass its current value
into the container as if via **--env**. Otherwise, unset it as if
via **--unset-env**.

</dd>
<dt>

**--pass-env-matching** *WILDCARD*

</dt><dd>

For each environment variable that is set and has a name matching
the **fnmatch**(3) pattern *WILDCARD*, pass its current value
into the container as if via **--env**.
For example, `--pass-env-matching=Steam*` copies some Steam-related
environment variables.
If this command is run from a shell, the wildcard will usually need
to be quoted, for example `--pass-env-matching="Steam*"`.

</dd>
<dt>

**--unset-env** *VAR*

</dt><dd>

Unset *VAR* when running the command.
This is mostly equivalent to using
**env -u** *VAR* *COMMAND* *ARGUMENTS...*
as the command.

</dd>
</dl>

# ENVIRONMENT

## Variables set for the command

Some variables will be set programmatically by
**steam-runtime-launcher-service** when the *COMMAND* is launched:

<dl>
<dt>

`MAINPID`

</dt><dd>

If **steam-runtime-launcher-service**(1) was run as a wrapper around a
command (for example as
`steam-runtime-launcher-service --bus-name=... -- my-game`),
and the initial process of the wrapped command is still running,
then this variable is set to its process ID (for example, the process
ID of **my-game**). Otherwise, this variable is cleared.
[The environment options shown above][ENVIRONMENT OPTIONS]
will override this behaviour.

</dd>
<dt>

`PWD`

</dt><dd>

**steam-runtime-launcher-service**(1) sets this to the current working
directory (as specified by **--directory**, or inherited from the
launcher) for each command executed inside the container,
overriding
[the environment options shown above][ENVIRONMENT OPTIONS].

</dd>
</dl>

## Variables read by steam-runtime-launch-client

Some variables affect the behaviour of **steam-runtime-launch-client**:

<dl>
<dt>

`DBUS_SESSION_BUS_ADDRESS`

</dt><dd>

Used to contact the well-known D-Bus session bus.

</dd>
<dt>

`PRESSURE_VESSEL_LOG_INFO` (boolean)

</dt><dd>

If set to 1, same as `SRT_LOG=info`

</dd>
<dt>

</dd>
<dt>

`PRESSURE_VESSEL_LOG_WITH_TIMESTAMP` (boolean)

</dt><dd>

If set to 1, same as `SRT_LOG=timestamp`

</dd>
<dt>

`SHELL`

</dt><dd>

If set to a non-empty value, it is used as the default shell when
no *COMMAND* is provided.

</dd>
<dt>

`SRT_LAUNCHER_SERVICE_ALONGSIDE_STEAM`

</dt><dd>

If set, it is assumed to be the well-known or unique bus name of a
**steam-runtime-launcher-service**(1) outside the Steam Linux Runtime
container, used when implementing **--alongside-steam**.

</dd>
<dt>

`SRT_LOG`

</dt><dd>

A sequence of tokens separated by colons, spaces or commas
affecting how output is recorded. See source code for details.

</dd>
<dt>

`TERM`

</dt><dd>

If standard input, standard output or standard error is a terminal,
then this environment variable is passed to the *COMMAND* by default.

</dd>
<dt>

`XDG_RUNTIME_DIR`

</dt><dd>

Used to contact the well-known D-Bus session bus.

</dd>
</dl>

# OUTPUT

The standard output from *COMMAND* is printed on standard output.

The standard error from *COMMAND* is printed on standard error.
Diagnostic messages from **steam-runtime-launch-client** may also be printed
on standard error.

# EXIT STATUS

The exit status is similar to **env**(1):

<dl>
<dt>

0

</dt><dd>

The *COMMAND* exited successfully with status 0,
or there was no *COMMAND* and **--terminate** succeeded.

</dd>
<dt>

125

</dt><dd>

Invalid arguments were given, or **steam-runtime-launch-client** failed
to start.

</dd>
<dt>

126

</dt><dd>

Reserved to indicate inability to launch the *COMMAND*.
This is not currently distinguished from exit status 125.

</dd>
<dt>

127

</dt><dd>

Reserved to indicate that the *COMMAND* was not found.
This is not currently distinguished from exit status 125.

</dd>
<dt>

255

</dt><dd>

The *COMMAND* was launched, but its exit status could not be
determined. This happens if the wait-status was neither
normal exit nor termination by a signal. It also happens if
**steam-runtime-launch-client** was disconnected from the D-Bus
session bus or the **--socket** before the exit status could be
determined.

</dd>
<dt>

Any value

</dt><dd>

The *COMMAND* exited unsuccessfully with the status indicated.

</dd>
<dt>

128 + *n*

</dt><dd>

The *COMMAND* was killed by signal *n*.
(This is the same encoding used by **bash**(1), **bwrap**(1) and
**env**(1).)

</dd>
</dl>

# EXAMPLES

## Inserting debugging commands into a container

A copy of **steam-runtime-launch-client** can be found in
`~/.steam/root/ubuntu12_32/steam-runtime/amd64/usr/bin/`,
`.../steamapps/common/SteamLinuxRuntime_soldier/pressure-vessel/bin/`
or
`.../steamapps/common/SteamLinuxRuntime_sniper/pressure-vessel/bin/`.

For a Steam game that runs under the "Steam Linux Runtime 3.0 (sniper)"
(or newer) compatibility tool,
such as Team Fortress 2 (app ID 440),
if you set its Steam Launch Options to

    STEAM_COMPAT_LAUNCHER_SERVICE=container-runtime %command%

you can run debugging commands inside the container,
in this example **wflinfo**(1):

    $ steam-runtime-launch-client --list
    --bus-name=com.steampowered.App440
    --bus-name=com.steampowered.App440.Instance54321

    $ steam-runtime-launch-client \
        --bus-name=com.steampowered.App440 \
        -- \
        wflinfo --platform=glx --api=gl

`wflinfo` and its arguments can be replaced by any command and arguments
that will run successfully inside the container,
for example `steam-runtime-system-info`, `xterm` or `bash -i`.

The same mechanism can be used to attach an interactive debugger,
with commands like:

    $ steam-runtime-launch-client --list
    --bus-name=com.steampowered.App440
    --bus-name=com.steampowered.App440.Instance54321

    $ pgrep hl2_linux
    12345

    $ gdb ./hl2_linux
    (gdb) set sysroot /proc/12345/root
    (gdb) target remote | \
        steam-runtime-launch-client \
        --bus-name=com.steampowered.App440 \
        -- gdbserver --attach - 12345
    (gdb) thread apply all bt
    (gdb) detach

For a Steam game that runs under the "Steam Linux Runtime 1.0 (scout)"
compatibility tool, the procedure is similar, but you would usually set
its Steam Launch Options to

    STEAM_COMPAT_LAUNCHER_SERVICE=scout-in-container %command%

so that the debugging commands are run in the `scout` compatibility
environment.

For a Steam game that runs under Proton 7.0 or later, if you set its
Steam Launch Options to

    STEAM_COMPAT_LAUNCHER_SERVICE=proton %command%

then you can run commands in its execution environment with commands
like:

    $ steam-runtime-launch-client --list
    --bus-name=com.steampowered.App312990
    --bus-name=com.steampowered.App312990.Instance123

    $ steam-runtime-launch-client \
        --bus-name=com.steampowered.App312990 \
        -- \
        wine winedbg notepad.exe

If a Proton game is crashing on startup, this can be debugged by setting
its Steam Launch Options to

    SRT_LAUNCHER_SERVICE_STOP_ON_EXIT=0 STEAM_COMPAT_LAUNCHER_SERVICE=proton %command%

and then running debugging commands in its Proton environment with
commands like:

    $ steam-runtime-launch-client \
        --bus-name=com.steampowered.App312990 \
        -- \
        wine64 winedbg Expendabros.exe

To exit the command server when finished, use a command like:

    $ steam-runtime-launch-client \
        --bus-name=com.steampowered.App312990 \
        --terminate

A similar procedure can be used for non-Proton games,
by adding `SRT_LAUNCHER_SERVICE_STOP_ON_EXIT=0` to the launch options in
a similar way.

## Running debugging commands outside the per-game container

While inside the Steam Linux Runtime container,
there is a copy of **steam-runtime-launch-client** available in the **PATH**.

This can be used to run diagnostic commands that are not available inside
the container,
such as **ping**(8):

    $ steam-runtime-launch-client --alongside-steam --host -- \
        ping store.steampowered.com

This works by contacting an instance of **steam-runtime-launcher-service**(1)
that is run by the Steam client (`--alongside-steam`),
or if that is not available,
by attempting to use a similar interface provided by Flatpak (`--host`).

App and game developers should avoid using this facility for normal
app/game functionality, because the command does not benefit from any of
Steam's usual mechanisms for providing cross-distribution compatibility,
and some distributions are very unusual and will break most typical
assumptions.

<!-- vim:set sw=4 sts=4 et: -->
