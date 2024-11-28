---
title: steam-runtime-launcher-service
section: 1
...

<!-- This document:
Copyright © 2020-2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-launcher-service - server to launch processes in a container

# SYNOPSIS

**steam-runtime-launcher-service**
[**--alongside-steam**]
[**--exec-fallback**]
[**--exit-on-readable** *FD*]
[**--info-fd** *N*]
[**--inside-app**]
[**--replace**]
[**--[no-]stop-on-exit**]
[**--[no-]stop-on-name-loss**]
[**--verbose**]
{**--session**|**--bus-name** *NAME*...|**--socket** *SOCKET*|**--socket-directory** *PATH*}
[**--** *COMMAND* *ARGUMENTS*]

# DESCRIPTION

**steam-runtime-launcher-service** listens on an `AF_UNIX` socket or the
D-Bus session bus, and executes arbitrary commands as subprocesses.

If a *COMMAND* is specified, it is run as an additional subprocess.
It inherits standard input, standard output and standard error
from **steam-runtime-launcher-service** itself.

If the *COMMAND* exits, then the launcher will also exit (as though the
*COMMAND* had been started via **steam-runtime-launch-client --terminate**),
unless prevented by **--no-stop-on-exit** or
**SRT_LAUNCHER_SERVICE_STOP_ON_EXIT=0**.

# OPTIONS

<dl>
<dt>

**--socket** *PATH*, **--socket** *@ABSTRACT*

</dt><dd>

Listen on the given `AF_UNIX` socket, which can either be an
absolute path, or `@` followed by an arbitrary string. In both cases,
the length is limited to 107 bytes and the value must not contain
ASCII control characters.
An absolute path indicates a filesystem-based socket, which is
associated with the filesystem and can be shared between filesystem
namespaces by bind-mounting.
The path must not exist before **steam-runtime-launcher-service** is run.
`@` indicates an an abstract socket, which is associated with a
network namespace, is shared between all containers that are in
the same network namespace, and cannot be shared across network
namespace boundaries.

This option is deprecated. Using **--session** or **--bus-name** is
preferred.

</dd>
<dt>

**--socket-directory** *PATH*

</dt><dd>

Create a filesystem-based socket with an arbitrary name in the
given directory and listen on that. Clients can use the information
printed on standard output to connect to the socket. Due to
limitations of `AF_UNIX` socket addresses, the absolute path to the
directory must be no more than 64 bytes. It must not contain ASCII
control characters.

This option is deprecated. Using **--session** or **--bus-name** is
preferred.

</dd>
<dt>

**--bus-name** *NAME*

</dt><dd>

Connect to the well-known D-Bus session bus, request the given name
and listen for commands there.
This option may be used more than once.
If it is, each of the names will be requested in the order given.
If at least one name cannot be acquired or is subsequently lost,
**steam-runtime-launcher-service** will behave according to the
**--[no-]stop-on-exit** options.

</dd>
<dt>

**--alongside-steam**

</dt><dd>

Instead of this launcher service providing a way for processes outside
the Steam Linux Runtime container to launch debugging commands inside
the container, advertise it as a way for processes *inside* the container
to launch commands *outside* the container, adjacent to the Steam client
(but possibly inside a larger Flatpak or Snap container, if one was used
to run the Steam client).
This is mutually exclusive with **--inside-app**,
and should not be used inside a Steam Linux Runtime container
(which would make it misleading).
If this option is used, Steam Runtime environment variables will be
removed from the environment of child processes, to give them an
execution environment that most closely resembles the one from which
Steam was launched.
This option implies **--replace**, **--session** and
**--stop-on-parent-exit**.

</dd>
<dt>

**--exec-fallback**

</dt><dd>

If unable to set up the **--socket**, **--socket-directory**,
**--bus-name** or **--session**, fall back to executing the
*COMMAND* directly (replacing the **steam-runtime-launcher-service**
process, similar to **env**(1)).
This option is only allowed if a *COMMAND* is specified.
It is useful if running the *COMMAND* is more important than the ability
to insert additional commands into its execution environment.

</dd>
<dt>

**--exit-on-readable** *FD*

</dt><dd>

Exit when file descriptor *FD* (typically 0, for **stdin**) becomes
readable, meaning either data is available or end-of-file has been
reached.

</dd>
<dt>

**--info-fd** *FD*

</dt><dd>

Print details of the server on *FD* (see **OUTPUT** below).
This fd will be closed (reach end-of-file) when the server is ready.
If a *COMMAND* is not specified, the default is standard output,
equivalent to **--info-fd=1**.
If a *COMMAND* is specified, there is no default.

</dd>
<dt>

**--inside-app**

</dt><dd>

Advertise this launcher service as a way for processes outside a
Steam Linux Runtime per-app (per-game) container to launch commands
inside the container.
This is mutually exclusive with **--alongside-steam**,
and implies **--session**.

</dd>
<dt>

**--replace**

</dt><dd>

When used with **--bus-name**, take over the bus name from
another **steam-runtime-launcher-service** process if any.
The other **steam-runtime-launcher-service** will either exit
or continue to run, depending on whether the **--[no-]stop-on-name-loss**
options were used.
This option is ignored if **--bus-name** is not used.

</dd>
<dt>

**--session**

</dt><dd>

Equivalent to **--bus-name** *NAME*, but *NAME* is chosen automatically.
By default, or with **--inside-app**, if a Steam app ID (game ID) can
be discovered from the environment, then the *NAME* is
**com.steampowered.App** followed by the app ID;
otherwise, **com.steampowered.App0** is used.
If **--alongside-steam** is specified, then
**com.steampowered.PressureVessel.LaunchAlongsideSteam** is used
instead.

Additionally, a second name with an instance-specific suffix like
**com.steampowered.AppX.InstanceY** is generated, to allow multiple
instances of the same app with **--no-stop-on-name-loss**.

If both **--session** and **--bus-name** are used, then **--session**
has no effect.

</dd>
<dt>

**--[no-]stop-on-exit**

</dt><dd>

With **--stop-on-exit** and a *COMMAND*, the server will terminate
other launched processes and prepare to exit when the *COMMAND* exits.
If other launched processes continue to run after receiving the
**SIGTERM** signal, the server will still wait for them to exit
before terminating.
This is the default.

With **--no-stop-on-exit** and a *COMMAND*, do not do this:
the server will still be contactable via D-Bus using its unique bus name
until it is terminated, for example with **SIGTERM** or
**steam-runtime-launch-client --bus-name=:1.xx --terminate**.
Note that if the wrapped *COMMAND* is a Steam game, then Steam will
still consider the game to be running until the
**steam-runtime-launcher-service** is terminated.

</dd>
<dt>

**--[no-]stop-on-name-loss**

</dt><dd>

With **--bus-name** and **--stop-on-name-loss**, the server will
prepare to exit when any of the well-known bus names *NAME* used with
**--bus-name** is lost, most likely because another server was
run with **--replace** and took over ownership.
If other launched processes are active, it will wait for them to exit
before terminating.
This is the default.

With **--bus-name** and **--no-stop-on-name-loss**, do not do this:
the server will still be contactable via D-Bus using its unique bus name.

These parameters have no effect if **--bus-name** is not used.

</dd>
<dt>

**--[no-]stop-on-parent-exit**

</dt><dd>

With **--stop-on-parent-exit**, the server will terminate
other launched processes and prepare to exit when its parent
process exits.
As with **--stop-on-exit**, if launched processes continue to run after
receiving the **SIGTERM** signal, the server will still wait for them
to exit.

With **--no-stop-on-parent-exit**, continue to run when reparented
to init or a subreaper.
This is the default.

</dd>
<dt>

**--verbose**

</dt><dd>

Be more verbose.

</dd>
</dl>

# ENVIRONMENT

## Variables set for the command

Some variables will be set programmatically in the subprocesses started
by **steam-runtime-launcher-service** when a command is sent to it by
**steam-runtime-launch-client**:

<dl>
<dt>

`MAINPID`

</dt><dd>

If **steam-runtime-launcher-service** was run as a wrapper around a
*COMMAND* (for example as
**steam-runtime-launcher-service --bus-name=... -- my-game**),
and the initial process of the wrapped *COMMAND* is still running,
then this variable is set to its process ID (for example, the process
ID of **my-game**). Otherwise, this variable is cleared.

</dd>
<dt>

`PWD`

</dt><dd>

Set to the current working directory for each command executed
inside the container.

</dd>
</dl>

Additionally, **steam-runtime-launch-client** has several options that
manipulate environment variables on a per-command basis.

## Variables read by steam-runtime-launcher-service

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

`SRT_LAUNCHER_SERVICE_STOP_ON_EXIT` (boolean)

</dt><dd>

If set to `0`, the default behaviour changes to be equivalent to
**--no-stop-on-exit**, unless overridden by **--stop-on-exit**.
If set to `1`, no effect.

</dd>
<dt>

`SRT_LAUNCHER_SERVICE_STOP_ON_NAME_LOSS` (boolean)

</dt><dd>

If set to `0`, the default behaviour changes to be equivalent to
**--no-stop-on-name-loss**, unless overridden by **--stop-on-name-loss**.
If set to `1`, no effect.

</dd>
<dt>

`SRT_LOG`

</dt><dd>

A sequence of tokens separated by colons, spaces or commas
affecting how output is recorded. See source code for details.

</dd>
<dt>

`STEAM_COMPAT_APP_ID` (integer)

</dt><dd>

Used by **--session** to identify the Steam app ID (game ID).

</dd>
<dt>

`SteamAppId` (integer)

</dt><dd>

Used by **--session** to identify the Steam app ID (game ID),
if `STEAM_COMPAT_APP_ID` is not also set.

</dd>
</dl>

# OUTPUT

**steam-runtime-launcher-service** prints zero or more lines of
structured text on the file descriptor specified by **--info-fd**,
and then closes the **--info-fd**.

If a *COMMAND* is specified, standard output is used for that command's
standard output, and there is no default **--info-fd**.

Otherwise, standard output acts as the default **--info-fd**, and will
be closed at the same time that the **--info-fd** is closed (even if
different).

The text printed to the **--info-fd** includes one or more of these:

<dl>
<dt>

**socket=**PATH

</dt><dd>

The launcher is listening on *PATH*, and can be contacted by
**steam-runtime-launch-client --socket** _PATH_
or by connecting a peer-to-peer D-Bus client to a D-Bus address
corresponding to _PATH_.

</dd>
<dt>

**dbus_address=**ADDRESS

</dt><dd>

The launcher is listening on *ADDRESS*, and can be contacted by
**steam-runtime-launch-client --dbus-address** _ADDRESS_,
or by connecting another peer-to-peer D-Bus client
(such as **dbus-send --peer=ADDRESS**) to _ADDRESS_.

</dd>
<dt>

**bus_name=**NAME

</dt><dd>

The launcher is listening on the well-known D-Bus session bus,
and can be contacted by
**steam-runtime-launch-client --bus-name** *NAME*,
or by connecting an ordinary D-Bus client
(such as **dbus-send --session**) to the session bus and sending
method calls to _NAME_.

</dd>
</dl>

Before connecting, clients must wait until after at least one of these:

* the **--info-fd** or standard output has been closed
* the bus name (if used) has appeared
* the *COMMAND* (if used) has started

Unstructured diagnostic messages are printed on standard error,
which remains open throughout. If a *COMMAND* is specified, it also
inherits standard error.

# EXIT STATUS

0
:   Terminated gracefully by a signal, by being disconnected from the
    D-Bus session bus after having obtained *NAME* (with **--bus-name**),
    or by *NAME* being replaced by another process
    (with **--bus-name** and **--replace**).

64 (`EX_USAGE` from `sysexits.h`)
:   Invalid arguments were given.

Other nonzero values
:   Startup failed.

# EXAMPLE

Listen on the session bus, and run two commands, and exit:

    name="com.steampowered.PressureVessel.App${SteamAppId}"
    pressure-vessel-wrap \
        ... \
        -- \
        steam-runtime-launcher-service --bus-name "$name" &
    launcher_pid="$!"
    gdbus wait --session --bus-name "$name"

    steam-runtime-launch-client \
        --bus-name "$name" \
        -- \
        ls -al
    steam-runtime-launch-client \
        --bus-name "$name" \
        -- \
        id

    kill -TERM "$launcher_pid"
    wait "$launcher_pid"

<!-- vim:set sw=4 sts=4 et: -->
