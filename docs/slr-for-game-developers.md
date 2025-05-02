# Steam Linux Runtime - guide for game developers

<!-- This document:
Copyright 2021-2025 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

[[_TOC_]]

## Audience

This document is primarily intended for game developers intending to
release their games on Steam.
It might also be interesting to new Steam Linux Runtime developers,
and to Steam-on-Linux enthusiasts with an interest in tweaking settings.

Please note that most of the configurations described in this document
should be considered to be unsupported: they are intended to be used by
game developers while debugging a new game, and are not intended to be
used by Steam customers to play released games.
Please consult official [Steam support documentation][] for help with
playing released games on Linux.

## Introduction

The *Steam Linux Runtime* is a collection of container environments
which can be used to run Steam games on Linux in a relatively predictable
container environment, instead of running directly on an unknown Linux
distribution which might be old, new or unusually set up.

It is implemented as a collection of Steam
[compatibility tools][], but can also be used outside Steam for
development and debugging.

The Steam Linux Runtime consists of a series of scripts that wrap a
container-launching tool written in C, *pressure-vessel*.
pressure-vessel normally creates containers using an included copy of the
third-party [bubblewrap][] container-runner.

If Steam or the Steam Linux Runtime is run inside a Flatpak sandbox,
then pressure-vessel cannot create new containers directly.
Instead, it communicates with the Flatpak service on the host system,
and asks the Flatpak service to launch new containers on its behalf.

[compatibility tools]: steam-compat-tool-interface.md
[bubblewrap]: https://github.com/containers/bubblewrap

Unlike more typical container launchers such as Flatpak and Docker,
pressure-vessel is a special-purpose container launcher designed
specifically for Steam games.
It combines the host system's graphics drivers with the
container runtime's library stack, to get an environment that is as
similar to the container runtime as possible, but has graphics drivers
matching the host system. Lower-level libraries such as `libc`, `libdrm`
and `libX11` are taken from either the host system or the container runtime,
whichever one appears to be newer.

The Steam Linux Runtime can be used to run three categories of games:

  * Native Linux games on newer runtimes such as sniper
  * Native Linux games on scout
  * Windows games, using Proton

All games distributed on Steam use a compatibility tool of some sort,
for the widest possible cross-distribution compatibility.
There is no option for running directly on the host system with no
compatibility tool.

### <a name="sniper"></a>Native Linux games targeting Steam Runtime 3 'sniper'

pressure-vessel is able to run games in a runtime that is newer than
scout.
[Steam Runtime version 3, codenamed sniper][sniper],
is the first such runtime available to developers of native Linux games
on Steam.
It is the recommended runtime for new native Linux games,
and is based on Debian 11 (2021).
Most of its libraries are taken directly from Debian, and can benefit
from Debian's long-term security support.
Selected libraries that are particularly important for games, such as
SDL and Vulkan-Loader, have been upgraded to newer versions backported
from newer branches of Debian.
Games that use this runtime can be tested for Steam Deck compatibility.

Games that target sniper should be compiled in the [sniper SDK][].

For backwards compatibility,
the default runtime environment when configuring a game in the Steamworks
partner web interface is
[Steam Linux Runtime 1.0 (scout)](#scout).
To opt-in to using sniper,
your app must first set up a Launch Option that supports Linux.
Once that is set up, you can use the Installation → Linux Runtime
menu item to select a runtime.
This can be done during initial setup for an unreleased game,
or as part of an update from a version that targeted scout to a
newer version that targets sniper.

Games that use sniper include Valve's
Counter-Strike 2,
Dota 2 and
Team Fortress 2,
and third-party titles like
Battle for Wesnoth,
Endless Sky and
Retroarch.

#### <a name="soldier"></a>Native Linux games targeting Steam Runtime 2 'soldier'

Native Linux games that require soldier cannot be released on Steam.
The next-generation runtime for native Linux games is intended to be
[Steam Runtime 3 `sniper`](#sniper).
All older native Linux games should be compiled for
[Steam Runtime 1 `scout`](#scout).

However, for development, debugging and experiments, if it is useful
to run a game under `soldier`, replacing `sniper` with `soldier` in
instructions that refer to `sniper` should usually work.

### <a name="scout"></a>Native Linux games targeting Steam Runtime 1 'scout'

All pre-2022 native Linux games on Steam are meant to be built to target
Steam Runtime version 1, codenamed scout, which is based on
Ubuntu 12.04 (2012).
For new or actively-maintained games,
consider using [Steam Runtime 3 `sniper`](#sniper) instead.
For historical reasons,
there are two different scout-compatible compatibility tools.

Since [November 2024][Steam client 2024-11-05],
games targeting scout are
run under *Steam Linux Runtime 1.0 (scout)* by default.
This tool runs games in a hybrid environment where the majority of
libraries are taken from a Steam Runtime 2 'soldier' container,
which is based on Debian 10 (2019).
Older libraries that are necessary for ABI compatibility with scout, such
as `libssl.so.1.0.0`, are also available via the
[`LD_LIBRARY_PATH`-based scout runtime][ldlp-runtime];
A small number of libraries from soldier, such as `libcurl.so.3`, are
overridden by their scout equivalents to provide ABI compatibility.
This provides a predictable environment for older games,
even when the host operating system changes.
Games that use this runtime can be tested for Steam Deck compatibility.
It is referred to internally as [scout-on-soldier][scout-on-soldier].

The *Legacy Runtime 1.0* is a non-default runtime environment for scout
games.
This means that Steam will use libraries from the host operating system
(which vary widely between Linux distributions,
between distribution versions and over time),
with the [`LD_LIBRARY_PATH`-based scout runtime][ldlp-runtime] added for
backward compatibility.
This is equivalent to Steam's pre-2024 default behaviour.
Because this environment is not long-term-stable,
games that rely on this runtime cannot pass Steam Deck compatibility testing.

The user can select the *Steam Linux Runtime 1.0 (scout)* or
*Legacy Runtime 1.0* compatibility tool in the game's properties,
overriding the default set by the game developer.

Games targeting either of these environments should be built in the
Steam Runtime 1 'scout' Docker container provided by the [scout SDK][].

### Windows games, using Proton

Recent versions of Proton require recent Linux shared library stacks.
To ensure that these are available, even when running on an older
operating system, Steam automatically runs Proton 8.0 or later
inside a *Steam Linux Runtime 3.0 (sniper)* container.

Similarly, Proton versions 5.13 to 7.0 use a
*Steam Linux Runtime 2.0 (soldier)* container.

Future versions of Proton might switch to Steam Runtime 4 or later.

## Suggested Steam configuration

You can move compatibility tools between Steam libraries through
the Steam user interface, in the same way as if they were games.
When developing with compatibility tools, it is usually most convenient
to [add a Steam Library folder][] in an easy-to-access location such as
`~/steamlibrary`, set it as the default, and move all compatibility
tools and games into that folder.

It is sometimes useful to try beta versions of the various compatibility
tools.
This is the same as [switching a game to a beta branch][], except that
instead of accessing the properties of the game, you would access the
properties of a compatibility tool such as *Steam Linux Runtime 2.0 (soldier)*
or *Proton 6.3*.

## Launching Steam games in a Steam Linux Runtime container

To run Windows games using Proton in a Steam Linux Runtime container:

  * Edit the Properties of the game in the Steam client
  * Select `Force the use of a specific Steam Play compatibility tool`
  * Select Proton 5.13 or later

To run Linux games in a *Steam Linux Runtime 1.0 (scout)* container:

  * Edit the Properties of the game in the Steam client
  * Select `Force the use of a specific Steam Play compatibility tool`
  * Select `Steam Linux Runtime 1.0 (scout)`

This will automatically download *Steam Linux Runtime 2.0 (soldier)*
or *Steam Linux Runtime 3.0 (sniper)*,
together with Proton and/or *Steam Linux Runtime 1.0 (scout)*,
into your default Steam library.

## <a name="s-r-launch-options"></a>Using steam-runtime-launch-options

[steam-runtime-launch-options]: #s-r-launch-options

The Steam Runtime provides a developer tool called
`steam-runtime-launch-options` which can adjust how Steam games are
launched.
To use this tool, ensure that Python 3, GTK 3, GObject-Introspection
and PyGI are installed
(for example `sudo apt install python3-gi gir1.2-gtk-3.0` on Debian,
or `sudo pacman -Syu pygobject gtk3` on Arch Linux),
then set a Steam game's [launch options][set launch options] to:

```
steam-runtime-launch-options -- %command%
```

The special token `%command%` should be typed literally: it changes Steam's
interpretation of the launch options so that instead of appending the
given launch options to the game's command-line, Steam will replace
`%command%` with the complete command-line for the game, including any
compatibility tool wrappers.
See the [compatibility tool interface][] for more information on how
this works.

Then launch the game.
Instead of the game itself, you will see a GUI window with various options
that can be adjusted.
Change whatever options are necessary, and then launch the game.

This tool intentionally does not save configuration: every time it is
run, it defaults to running the game in the same way that Steam
normally would.
Any special settings will need to be selected every time.

This tool looks for possible runtimes and pressure-vessel versions in
several likely locations including your Steam library directory,
the current working directory, and `~/tmp`.

## Launching non-Steam games in a Steam Linux Runtime container

First, install a Steam game and configure it to use the required
compatibility tool, as above.
This ensures that the compatibility tool will be downloaded, and provides
an easy way to test that the compatibility tool is working correctly.

For a more scriptable version of this, run one of these commands:

  * Steam Linux Runtime 1.0 (scout): `steam steam://install/1070560`
  * Steam Linux Runtime 2.0 (soldier): `steam steam://install/1391110`
  * Steam Linux Runtime 3.0 (sniper): `steam steam://install/1628350`
  * Proton Experimental: `steam steam://install/1493710`
  * Proton 10.0: `steam steam://install/3658110`
  * Proton 9.0: `steam steam://install/2805730`
  * Proton 8.0: `steam steam://install/2348590`
  * Proton 7.0: `steam steam://install/1887720`
  * Proton 6.3: `steam steam://install/1580130`
  * Proton 5.13: `steam steam://install/1420170`

### <a name="commands"></a>Running commands in sniper, soldier, etc.

The simplest scenario for using the Steam Linux Runtime framework is to
run commands in a newer runtime such as sniper.
This mimics what Steam would do for a game that has been
[configured to run in sniper](#sniper).

To do this, run a command like:

```
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_sniper/run \
    -- \
    xterm
```

or more realistically for a game,

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_sniper/run \
    $pressure_vessel_options \
    -- \
    ./my-game.sh \
    $game_options
```

Like many Unix commands, pressure-vessel uses the special option `--`
as a divider between its own options and the game's options.
Anything before `--` will be parsed as a pressure-vessel option.
Anything after `--` will be ignored by pressure-vessel, but will be
passed to the game unaltered.

The [steam-runtime-launch-options][] tool can be used from outside Steam
by prefixing it to the command, like this:

```
$ ~/.steam/root/ubuntu12_32/steam-runtime/amd64/usr/bin/steam-runtime-launch-options \
    -- \
    /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_sniper/run \
    $pressure_vessel_options \
    -- \
    ./my-game.sh \
    $game_options
```

By default, the command to be run in the container gets `/dev/null` as
its standard input, so it cannot be an interactive shell like `bash`.
To pass through standard input from the shell where you are running the
command, you can either use [developer mode][],
use the `--terminal=tty` option:

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_sniper/run \
    --terminal=tty \
    -- \
    bash
```

Exporting the environment variable `PRESSURE_VESSEL_TERMINAL=tty` is
equivalent to using the `--terminal=tty` option.

### <a name="commands-in-scout-on-soldier"></a>Running commands in the Steam Linux Runtime 1.0 (scout) environment

Running a game that was compiled for Steam Runtime 1 'scout' in the
scout-on-soldier container is similar to a pure soldier container, but an
extra step is needed: the *Steam Linux Runtime 1.0 (scout)* compatibility tool
needs to make older libraries like `libssl.so.1.0.0` available
to the game.
You will also need to ensure that the *Steam Linux Runtime 1.0 (scout)*
compatibility
tool is visible in the container environment: Steam normally does this
automatically, but outside Steam it can be necessary to do this yourself.
This means the commands required are not the same as for soldier or
sniper.

To enter this environment, use commands like this:

```
$ export STEAM_COMPAT_MOUNTS=/path/to/steamlibrary
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    $pressure_vessel_options \
    -- \
    /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime/scout-on-soldier-entry-point-v2 \
    -- \
    ./my-game.sh \
    $game_options
```

See [Making more files available in the container][],
below, for more information on `STEAM_COMPAT_MOUNTS`.

Similar to the `run` script, the `scout-on-soldier-entry-point-v2` script
uses `--` as a divider between its own options and the game to be run.

### Running a game under Proton in the Steam Linux Runtime environment

To run a Windows game under Proton 5.13 or later, again, an
extra step is needed to add Proton to the command-line.

Several extra environment variables starting with `STEAM_COMPAT_`
need to be set to make Proton work. They are usually set by Steam itself.

Something like this should generally work:

```
$ gameid=123            # replace with your numeric Steam app ID
$ export STEAM_COMPAT_CLIENT_INSTALL_PATH=$(readlink -f "$HOME/.steam/root")
$ export STEAM_COMPAT_DATA_PATH="/path/to/steamlibrary/compatdata/$gameid"
$ export STEAM_COMPAT_INSTALL_PATH=$(pwd)
$ export STEAM_COMPAT_LIBRARY_PATHS=/path/to/steamlibrary:/path/to/otherlibrary
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_sniper/run \
    $pressure_vessel_options \
    -- \
    /path/to/steamlibrary/steamapps/common/"Proton - Experimental"/proton \
    run \
    my-game.exe \
    $game_options
```

Use `soldier` instead of `sniper` for Proton 7.0 or older.

## Logging

By default, anything that the game writes to standard output or
standard error will appear on Steam's standard output or standard error.
Depending on the operating system, this might mean that it appears in
the systemd Journal, in a log file, or on an interactive terminal, or
it might be discarded.

Setting the environment variable `STEAM_LINUX_RUNTIME_LOG=1` makes
the Steam Linux Runtime infrastructure write more verbose output to a
log file, matching the pattern
`steamapps/common/SteamLinuxRuntime_*/var/slr-*.log`.
The log file's name will include the Steam app ID, if available.
The game's standard output and standard error are also redirected to
this log file.
A symbolic link `steamapps/common/SteamLinuxRuntime_*/var/slr-latest.log`
is also created, pointing to the most recently-created log.

The environment variable `STEAM_LINUX_RUNTIME_VERBOSE=1` can be exported
to make the Steam Linux Runtime even more verbose, which is useful when
debugging an issue.
This variable does not change the logging destination: if
`STEAM_LINUX_RUNTIME_LOG` is set to `1`, the Steam Linux Runtime will
write messages to its log file, or if not, it will write messages to
whatever standard error stream it inherits from Steam.

For Proton games, the environment variable `PROTON_LOG=1` makes Proton
write more verbose output to a log file, usually `~/steam-<appid>.log`.
The game's standard output and standard error will also appear in this
log file.
If both this and `STEAM_LINUX_RUNTIME_LOG` are used, this takes precedence:
the container runtime's own output will still appear in the container
runtime's log file, but Proton's output will not, and neither will the
game's output.
See [Proton documentation][] for more details.

## <a name="shell"></a>Running in an interactive shell

By default, the Steam Linux Runtime will just launch the game, but this
is not always convenient.

You can get an interactive shell inside the container instead of running
your game, by using [steam-runtime-launch-options][] and
setting the *Interactive shell* option to *Instead of running the command*,
or by exporting the environment variable `PRESSURE_VESSEL_SHELL=instead`,
or by using the equivalent command-line option `--shell=instead`.

When the interactive shell starts, the game's command-line is placed
in the special variable `"$@"`, as though you had run a command similar
to `set -- ./my-game.sh $game_options`.
You can run the game by entering `"$@"` at the prompt, including the
double quotes.
The game's standard output and standard error file descriptors will be
connected to the `xterm`, if used.

If you are using a Debian-derived system for development, the contents
of the container's `/etc/debian_chroot` file appear in the default shell
prompt to help you to recognise the container shell, for example:

```
(steamrt soldier 0.20211013.0)user@host:~$
```

Code similar to [Debian's /etc/bash.bashrc][] can be used to provide
this behaviour on other distributions, if desired.

When running games through Steam, you can either export
`PRESSURE_VESSEL_SHELL=instead` for the whole Steam process, or
[change an individual game's launch options][set launch options] to
`PRESSURE_VESSEL_SHELL=instead %command%`.
As with [steam-runtime-launch-options][],
The special token `%command%` should be typed literally.

<details><summary>The equivalent for non-Steam games</summary>

When launching the Steam Linux Runtime separately, you can either set
the same environment variable, or use the command-line option like this:

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_sniper/run \
    --shell=instead \
    -- \
    ./my-game.sh \
    $game_options
```

</details>

By default, the interactive shell runs in an `xterm` terminal emulator
which is included in the container runtime.
If you ran Steam or the game from a terminal or `ssh` session, you can
use `PRESSURE_VESSEL_TERMINAL=tty` or `--terminal=tty` to put the
interactive shell in the same place as your previous shell session.

It is also possible to ask for an interactive shell after running the
command (replace `instead` with `after`), or only if the command exits
with a nonzero status (replace `instead` with `fail`).

## <a name="command-injection"></a>Inserting debugging commands into the container

Recent versions of the various container runtimes include a feature that
can be used to run arbitrary debugging commands inside the container.
This feature requires a working D-Bus session bus.

If using [steam-runtime-launch-options][], this can be activated by
setting the *Command injection* option to *SteamLinuxRuntime_...*,
*any Proton version* or _any layered scout-on-* runtime_.

Or, to activate this programmatically, set the `STEAM_COMPAT_LAUNCHER_SERVICE`
environment variable to the `compatmanager_layer_name` listed in the
`toolmanifest.vdf` of the compatibility tool used to run a game:

* `container-runtime` for "Steam Linux Runtime 2.0 (soldier)" or
    "Steam Linux Runtime 3.0 (sniper)"

* `proton` for any version of Proton that supports it (7.0 or later)

* `scout-in-container` for "Steam Linux Runtime 1.0 (scout)"

When running games through Steam, you can either export something like
`STEAM_COMPAT_LAUNCHER_SERVICE=container-runtime` for the whole Steam
process, or [change an individual game's launch options][set launch options]
to `STEAM_COMPAT_LAUNCHER_SERVICE=container-runtime %command%`.
The special token `%command%` should be typed literally.

<details><summary>The equivalent for non-Steam games</summary>

The `SteamLinuxRuntime_sniper/run` and
`SteamLinuxRuntime_soldier/run` scripts also accept this environment
variable, so it can be used in commands like these:

```
$ export STEAM_COMPAT_MOUNTS=/path/to/steamlibrary
$ export STEAM_COMPAT_LAUNCHER_SERVICE=container-runtime
$ cd /builds/native-linux-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_sniper/run \
    $pressure_vessel_options \
    -- \
    ./my-game.sh \
    $game_options
```

or for scout-on-soldier

```
$ export STEAM_COMPAT_MOUNTS=/path/to/steamlibrary
$ export STEAM_COMPAT_LAUNCHER_SERVICE=scout-in-container
$ cd /builds/native-linux-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    $pressure_vessel_options \
    -- \
    /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime/scout-on-soldier-entry-point-v2 \
    -- \
    ./my-game.sh \
    $game_options
```

or for Proton

```
$ gameid=123            # replace with your numeric Steam app ID
$ cd /builds/proton-game
$ export STEAM_COMPAT_LAUNCHER_SERVICE=proton
$ export STEAM_COMPAT_CLIENT_INSTALL_PATH=$(readlink -f "$HOME/.steam/root")
$ export STEAM_COMPAT_DATA_PATH="/path/to/steamlibrary/compatdata/$gameid"
$ export STEAM_COMPAT_INSTALL_PATH=$(pwd)
$ export STEAM_COMPAT_LIBRARY_PATHS=/path/to/steamlibrary:/path/to/otherlibrary
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    $pressure_vessel_options \
    -- \
    /path/to/steamlibrary/steamapps/common/"Proton - Experimental"/proton \
    run \
    my-game.exe \
    $game_options
```

</details>

After configuring this, while a game is running, you can list game sessions
where this has taken effect like this:

```
$ .../SteamLinuxRuntime_sniper/pressure-vessel/bin/steam-runtime-launch-client --list
--bus-name=com.steampowered.App123
--bus-name=com.steampowered.App123.Instance31679
```

and then connect to one of them with a command like:

```
$ .../SteamLinuxRuntime_sniper/pressure-vessel/bin/steam-runtime-launch-client \
    --bus-name=com.steampowered.App123 \
    -- \
    bash
```

Commands that are run like this will run inside the container, but their
standard input, standard output and standard error are connected to
the `steam-runtime-launch-client` command, similar to `ssh` or `docker exec`.
For example, `bash` can be used to get an interactive shell inside the
container, or an interactive tool like `gdb` or `python3` or a
non-interactive tool like `ls` can be placed directly after the `--`
separator.

### <a name="crash-on-startup"></a>Debugging a game that is crashing on startup

Normally, the debug interface used by `steam-runtime-launch-client`
exits when the game does.
However, this is not useful if the game exits or crashes on startup
and the opportunity to debug it is lost.

To debug a game that is in this situation, in addition to
`STEAM_COMPAT_LAUNCHER_SERVICE`, you can export
`SRT_LAUNCHER_SERVICE_STOP_ON_EXIT=0`.
With this variable set, the command-launching service will *not* exit when
the game does, allowing debugging commands to be sent to it by using
`steam-runtime-launch-client`.
For example, it is possible to re-run the crashed game under [gdbserver][]
with a command like:

```
$ .../SteamLinuxRuntime_sniper/pressure-vessel/bin/steam-runtime-launch-client \
    --bus-name=com.steampowered.App123 \
    -- \
    gdbserver 127.0.0.1:12345 ./my-game-executable
```

Steam will behave as though the game is still running, because from
Steam's point of view, the debugging service has replaced the game.
To exit the "game" when you have finished debugging, instruct the
command server to terminate:

```
$ .../SteamLinuxRuntime_sniper/pressure-vessel/bin/steam-runtime-launch-client \
    --bus-name=com.steampowered.App123 \
    --terminate
```

## <a name="layout"></a>Layout of the container runtime

In general, the container runtime is similar to Debian and Ubuntu.
In particular, the standard directories for C/C++ libraries are
`/usr/lib/x86_64-linux-gnu` and `/usr/lib/i386-linux-gnu`.
The `lib64` or `lib32` directories are not used.

The host system's `/usr`, `/bin`, `/sbin` and `/lib*` appear below
`/run/host` in the container.
For example, a Fedora host system might provide
`/run/host/usr/lib64/libz.so.1`.
However, these executables and libraries cannot normally be used directly
within the container,
because the container's shared library stack is not compatible with the host's.
See [Running commands outside the container][], below,
for the closest equivalent.

Files imported from the host system appear as symbolic links in the
`/usr/lib/pressure-vessel/overrides` hierarchy.
For example, if we are using the 64-bit `libz.so.1` from the host system,
it is found via the symbolic link
`/usr/lib/pressure-vessel/overrides/lib/x86_64-linux-gnu/libz.so.1`.

Non-OS directories such as `/home` and `/media` either do not appear
in the container, or appear in the container with the same paths that
they have on the host system. For example, `/home/me/.steam/root` on the
host system becomes `/home/me/.steam/root` in the container.

### Exploring the container from the host

The container's root directory can be seen from the host system by using
`ps` to find the process ID of any game or shell process inside the
container, and then using

```
ls -l /proc/$game_pid/root/
```

You'll see that the graphics drivers and possibly their dependencies
are available in `/overrides` inside that filesystem, while selected
files from the host are visible in `/run/host`.

You can also access a temporary copy of the container runtime in a
subdirectory of `steamapps/common/SteamLinuxRuntime_*/var/`
with a name similar to
`steamapps/common/SteamLinuxRuntime_*/var/tmp-1234567`.
These temporary copies use hard-links to avoid consuming additional
disk space and I/O bandwidth.
To avoid these temporary copies building up forever, they will be
deleted the next time you run a game in a container, unless you create a
file `steamapps/common/SteamLinuxRuntime_*/var/tmp-1234567/keep`
to flag that particular root directory to be kept for future reference.

## Access to filesystems

By default, `pressure-vessel` makes a limited set of files available in
the container, including:

  * the user's home directory
  * the Steam installation directory, if found
  * the current working directory

When running the Steam Linux Runtime via Steam, it also uses the environment
variables set by the [compatibility tool interface][] to find additional
files and directories that should be shared with the container.

Please see [Paths shared between host system and container runtime][]
for full details.

### Private home directory

The Steam Linux Runtime has experimental support for giving each game
a private (virtualized) home directory.
In this mode, the user's real home directory is *not* shared with the game.
Instead, a directory on the host system is used as a "fake" home directory
for the game to write into.

This mode is not yet documented here.
Please see pressure-vessel source code for more details.

### Making more files available in the container

[Making more files available in the container]: #making-more-files-available-in-the-container

When running outside Steam, or when loading files from elsewhere in the
filesystem during debugging, it might be necessary to share additional
paths.
This can be done by setting the `STEAM_COMPAT_MOUNTS`,
`PRESSURE_VESSEL_FILESYSTEMS_RO` and/or `PRESSURE_VESSEL_FILESYSTEMS_RW`
environment variables.

For example, to share `/builds` and `/resources` with the container, you
might use a command like this:

```
$ export STEAM_COMPAT_MOUNTS=/builds:/resources
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_sniper/run \
    -- \
    ./my-game.sh \
    +set extra_texture_path /resources/my-game/textures
```

## <a name="launch-alongside-steam"></a>Running commands outside the container

[Running commands outside the container]: #launch-alongside-steam

For some development and debugging use-cases,
it can be desirable to run commands that are not part of the container.

The `steam-runtime-launch-client` utility can be used to run commands
that are not available inside the container,
such as `ping`, like this:

    $ steam-runtime-launch-client --alongside-steam --host -- \
        ping store.steampowered.com

This can be used
[from an interactive shell](#shell),
or it can be used programmatically via normal APIs for running external
commands,
for example `posix_spawnp()`, `GSubprocess` or `system()`.

App and game developers should avoid using this mechanism for normal
app/game functionality,
because the command will not benefit from any of Steam's usual mechanisms
for providing cross-distribution compatibility.
As a result, there are many limitations to be aware of, such as:

* the command might not be installed
* the command might be installed in an unexpected location
* the command might not work correctly
* the command or the OS might be a version 10 years older than you expect
* the command or the OS might be a version 10 years *newer* than you expect
* the OS distribution might be structured in an unexpected way
* the user might have made extensive customizations to the OS distribution

However,
with some appropriate expectations-management,
this mechanism can be useful for non-core use-cases such as game mod
development tools.

You can think of this as being like connecting to a remote machine
using `ssh`:
the command runs outside the container,
"in a different world".
For example,
the meaning of some filesystem paths is different:
`cat /etc/os-release` will show you the [os-release(5)][] file inside the
container,
but
`steam-runtime-launch-client --alongside-steam --host -- cat /etc/os-release`
will show you the equivalent file outside the container.
Similarly,
process parameters such as environment variables,
the current working directory
and resource limits will be different.

As a special case,
many of the [paths that are shared with the container][shared-paths]
will normally have the same meaning inside and outside the container.
In particular,
the paths used inside the container for
the game itself (`$STEAM_COMPAT_INSTALL_PATH`),
the Steam client (`$STEAM_COMPAT_CLIENT_INSTALL_PATH`),
and the user's Steam libraries (`$STEAM_COMPAT_LIBRARY_PATHS`)
will normally be equally valid outside the container.

Various options can be placed before the `--` separator,
for example to select which environment variables from inside the
container are sent to the command.
See the
[steam-runtime-launch-client documentation][steam-runtime-launch-client]
for full details of the options that are available.

This mechanism works by contacting an instance of
`steam-runtime-launcher-service` that is run automatically by the Steam
client (`--alongside-steam`),
or if that is not available,
by attempting to use a similar interface provided by Flatpak (`--host`).

As currently implemented,
this mechanism requires a working D-Bus session bus.
This is a facility that is available on all typical Linux desktop systems,
as well as on the Steam Deck,
but might not always be available on enthusiasts' heavily-customized
Linux systems.

## <a name="developer-mode"></a>Developer mode

[developer mode]: #developer-mode

The `--devel` option puts `pressure-vessel` into a "developer mode"
which enables experimental or developer-oriented features.
It should be passed to the `run` script before the `--` marker,
like this:

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_sniper/run \
    --devel \
    -- \
    ./my-game.sh
```

Exporting `PRESSURE_VESSEL_DEVEL=1` is equivalent to using the `--devel`
option.

Currently, the features enabled by this option are:

  * The standard input file descriptor is inherited from the parent
    process, the same as `--terminal=tty`.
    This is useful when running an interactive shell like `bash`, or a
    game that accepts developer console commands on standard input.

  * pressure-vessel doesn't call `setsid()` to create a new terminal
    session, so that Ctrl+C and Ctrl+Z will work as expected when
    inheriting a terminal file descriptor as standard input.

  * `/sys` is mounted read-write instead of read-only, so that game
    developers can use advanced profiling and debugging mechanisms that
    might require writing to `/sys/kernel` or similar pseudo-filesystems.

This option is likely to have more effects in future pressure-vessel releases.

## Running in a SDK environment

By default, the various *Steam Linux Runtime* tools use a variant of the
container runtime that is identified as the *Platform*.
This is the same naming convention used in Flatpak.
The Platform runtime contains shared libraries needed by the games
themselves, as well as some very basic debugging tools, but to keep its
size manageable it does not contain a complete suite of debugging and
development tools.

A larger variant of each container runtime, the *SDK*, contains all the
same debugging and development tools that are provided in our official
Docker images.

To use the SDK, first identify the version of the Platform that you are
using.
This information can be found in `SteamLinuxRuntime_sniper/VERSIONS.txt`,
in the row starting with `sniper`.
Next, visit the corresponding numbered directory in
<https://repo.steampowered.com/steamrt-images-sniper/snapshots/>
and download the large archive named
`com.valvesoftware.SteamRuntime.Sdk-amd64,i386-sniper-runtime.tar.gz`.

In the `SteamLinuxRuntime_sniper` directory in your
Steam library, create a directory `SteamLinuxRuntime_sniper/sdk` and unpack the
archive into it, so that you have files like
`steamapps/common/SteamLinuxRuntime_sniper/sdk/files/lib/os-release` and
`steamapps/common/SteamLinuxRuntime_sniper/sdk/metadata`:

```
$ cd .../SteamLinuxRuntime_sniper
$ mkdir -p sdk
$ tar -C sdk -xf ~/Downloads/com.valvesoftware.SteamRuntime.Sdk-amd64,i386-sniper-runtime.tar.gz
```

You can now use this runtime by selecting it from the *Container runtime*
drop-down list in [steam-runtime-launch-options][].

<details><summary>The equivalent for non-Steam games</summary>

For non-Steam games, this can be selected by
passing the option `--runtime=sdk` to the `SteamLinuxRuntime_sniper/run`
script, for example:

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_sniper/run \
    $pressure_vessel_options \
    --runtime=sdk \
    -- \
    ./my-game.sh \
    $game_options
```

</details>

You will find that tools like `gdb` and `strace` are available in the SDK
environment.

[soldier][] works in the same way, but with `soldier` instead of `sniper`.

## Running in a modified Platform or SDK environment

The default `Platform` environment provided by
*Steam Linux Runtime 2.0 (soldier)*
and *Steam Linux Runtime 3.0 (sniper)* in the `soldier_platform_*` or
`sniper_platform_*` directory is in a format that has been optimized
for distribution through the Steampipe CDN, and cannot easily be modified:
most files' names, permissions and checksums are checked against a manifest
file during container setup, and some files do not exist in `*_platform_*`
at all and are dynamically created from the manifest file during container
setup.

During game or runtime development, it is sometimes useful to use a
modified runtime.
This is unsupported, and should not be used as a production environment.

To use a locally-modified SDK environment, start by downloading and
unpacking the SDK as described above.
You can modify the `sdk` directory before running the game, for example
by unpacking a `.deb` file with `dpkg-deb -x` and copying the necessary
files into place.

To use a locally-modified Platform environment, proceed as if for the SDK,
but download
`com.valvesoftware.SteamRuntime.Platform-amd64,i386-sniper-runtime.tar.gz`
and unpack it into `SteamLinuxRuntime_sniper/platform`,
so that you have files like
`steamapps/common/SteamLinuxRuntime_sniper/platform/files/lib/os-release` and
`steamapps/common/SteamLinuxRuntime_sniper/platform/metadata`.
Then you can proceed as if for the SDK, but use `--runtime=platform`
instead of `--runtime=sdk`.

[soldier][] works in the same way, but with `soldier` instead of `sniper`.

## Upgrading pressure-vessel

[Upgrading pressure-vessel]: #upgrading-pressure-vessel

The recommended version of `pressure-vessel` is the one that is included
in the *Steam Linux Runtime 3.0 (sniper)* depot, and other versions are
not necessarily compatible with the container runtime and scripts in
the depot.
However, it can sometimes be useful for developers and testers to upgrade
their version of the `pressure-vessel` container tool, so that they can
make use of new features or try out new bug-fixes.

To do this, you can download an archive named `pressure-vessel-bin.tar.gz`
or `pressure-vessel-bin+src.tar.gz`, unpack it, and use it to replace the
`steamapps/common/SteamLinuxRuntime_sniper/pressure-vessel/` directory.

Alternatively, [steam-runtime-launch-options][] will look for copies of
pressure-vessel in several likely locations, including `./pressure-vessel`
and `~/tmp/pressure-vessel`, and offer them as choices.

Official releases of pressure-vessel are available from
<https://repo.steampowered.com/pressure-vessel/snapshots/>.
If you are comfortable with using untested pre-release software, it is
also possible to download unofficial builds of pressure-vessel from our
continuous-integration system; the steps to do this are deliberately not
documented here.

To return to the recommended version of `pressure-vessel`, simply delete
the `steamapps/common/SteamLinuxRuntime_sniper/pressure-vessel/`
directory and use Steam's [Verify integrity][] feature to re-download it.

[soldier][] works in the same way, but with `soldier` instead of `sniper`.

## Attaching a debugger by using gdbserver

[gdbserver]: #attaching-a-debugger-by-using-gdbserver

The Platform runtime does not contain a full version of the `gdb` debugger,
but it does contain `gdbserver`, a `gdb` "stub" to which a full debugger
can be connected.

To use `gdbserver`, either run it from an interactive shell in the
container environment, or add it to your game's command-line
(perhaps via a wrapper script).
For example, instead of

```
$ ./my-game-executable $game_options
```

you could run

```
$ gdbserver 127.0.0.1:12345 ./my-game-executable $game_options
```

<details><summary>Example for non-Steam games</summary>

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_sniper/run \
    $pressure_vessel_options \
    -- \
    gdbserver 127.0.0.1:12345 ./my-game-executable \
    $game_options
```

</details>

Alternatively, some games' launch scripts have a way to attach an external
debugger given in an environment variable, such as `GAME_DEBUGGER` in
several Valve games, including the `dota.sh` script that launches DOTA 2.
If your game runs via a wrapper script, implementing the same pattern
seen in DOTA 2 is a convenient way to provide debugger integration.
For example:

```
#!/bin/sh
# my-game.sh
set -e

# ... any other setup you want can go here ...

set -- ./bin/my-game "$@"

if [ -n "${GAME_DEBUGGER-}" ]; then
    set -- $GAME_DEBUGGER "$@"
fi

exec "$@"
```

For games that implement this pattern,
export an environment variable similar to
`GAME_DEBUGGER="gdbserver 127.0.0.1:12345"`
to enable the `gdbserver`.

When `gdbserver` is used like this, it will pause until a debugger is
attached.
You can connect a debugger running outside the container to `gdb`
by writing gdb configuration similar to:

```
# This will search /builds/my-game/lib:/builds/my-game/lib64 for
# libraries
set sysroot /nonexistent
set solib-search-path /builds/my-game/lib:/builds/my-game/lib64
target remote 127.0.0.1:12345
```

or

```
# This will transfer executables and libraries through the remote
# debugging TCP channel
set sysroot /proc/54321/root
target remote 127.0.0.1:12345
```

where 54321 is the process ID of any process in the container, and then
running `gdb -x file-containing-configuration`.
In gdb, use the `cont` command to continue execution.

### Remote debugging via TCP

`gdbserver` and `gdb` communicate via TCP, so you can run a game on
one computer (such as a Steam Deck) and debug it on another (such as
your workstation).

Note that **there is no authentication**, so anyone on your local LAN
can use this to remote-control the `gdbserver`. Only do this on fully
trusted networks.

To use remote debugging, tell the `gdbserver` on the gaming device to
listen on `0.0.0.0` instead of `127.0.0.1`,
by using a command prefix like `gdbserver 0.0.0.0:12345`.

<details><summary>Complete example for non-Steam games</summary>

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_sniper/run \
    $pressure_vessel_options \
    -- \
    gdbserver 0.0.0.0:12345 ./my-game-executable \
    $game_options
```

</details>

On the developer workstation, you can configure `gdb` to communicate
with the game,
replacing `192.0.2.42` with the gaming device's local IP address:

```
$ cat > gdb-config <<EOF
set sysroot /nonexistent
set solib-search-path /builds/my-game/lib:/builds/my-game/lib64
target remote 192.0.2.42:12345
EOF
$ gdb -x gdb-config
```

If your network assigns locally-resolvable hostnames to IP addresses,
then you can use those instead of the IP address.

### Remote debugging via ssh

Alternatively, if you have `ssh` access to the remote device, you can use
`ssh` port-forwarding to make the remote device's debugger port available
on your workstation.
On the gaming device, listen on 127.0.0.1, the same as for local debugging.

<details><summary>Complete example for non-Steam games</summary>

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_sniper/run \
    $pressure_vessel_options \
    -- \
    gdbserver 127.0.0.1:12345 ./my-game-executable \
    $game_options
```

</details>

On the developer workstation, configure `gdb` to communicate with it
via a port forwarded through a ssh tunnel, for example:

```
$ ssh -f -N -L 23456:127.0.0.1:12345 user@192.0.2.42
$ cat > gdb-config <<EOF
set sysroot /nonexistent
set solib-search-path /builds/my-game/lib:/builds/my-game/lib64
target remote 127.0.0.1:23456
EOF
$ gdb -x gdb-config
```

## Getting debug symbols

`gdb` can provide better backtraces for crashes and breakpoints if it
is given access to some sources of detached debug symbols.
Because the Steam Linux Runtime container combines libraries from the
container runtime with graphics drivers from the host system, a backtrace
might involve libraries from both of those locations, therefore detached
debug symbols for both of those might be required.

### For the host system

For Linux distributions that provide a [debuginfod][] server, it is usually
the easiest way to obtain detached debug symbols on-demand.
For example, on Debian systems:

```
$ export DEBUGINFOD_URLS="https://debuginfod.debian.net"
$ gdb -x file-containing-configuration
```

or on Arch Linux systems:

```
$ export DEBUGINFOD_URLS="https://debuginfod.archlinux.org"
$ gdb -x file-containing-configuration
```

Ubuntu does not yet provide a `debuginfod` server.
For Ubuntu, you will need to install special `-dbgsym` packages that
contain the detached debug symbols.

### For the container runtime

There is currently no public `debuginfod` instance for the Steam Runtime.
Many of the libraries in soldier and sniper are taken directly from Debian,
so their debug symbols can be obtained from Debian's `debuginfod`:

```
$ export DEBUGINFOD_URLS="https://debuginfod.debian.net"
$ gdb -x file-containing-configuration
```

This can be combined with a `debuginfod` for a non-Debian distribution
such as Fedora by setting `DEBUGINFOD_URLS` to a space-separated list
of URLs.

For more thorough symbol coverage, first identify the version of the
Platform that you are using.
This information can be found in `SteamLinuxRuntime_sniper/VERSIONS.txt`,
in the row starting with `sniper`.
Next, visit the corresponding numbered directory in
<https://repo.steampowered.com/steamrt-images-sniper/snapshots/>
and download the large archive named
`com.valvesoftware.SteamRuntime.Sdk-amd64,i386-sniper-debug.tar.gz`.
Create a directory, for example `/tmp/sniper-dbgsym-0.20211013.0`,
and unpack the archive into that directory.

Then configure gdb with:

```
set debug-file-directory /tmp/sniper-dbgsym-0.20211013.0/files:/usr/lib/debug
```

and it should load the new debug symbols.

[soldier][] works in the same way, but with `soldier` instead of `sniper`.

## Making a game container-friendly

The container runtime is intended to be relatively "transparent" so
that it can run existing games without modification, but there are
some things that game developers can do to make games work better in
the container environment, particularly developers of Linux-native games.

### Working directory

*Windows or Linux-native*

Each game has a subdirectory in `steamapps/common`, such as
`steamapps/common/My Great Game`, referred to in Steamworks as the
[install folder][].

It's simplest and most reliable if the game is designed to be launched
with its working directory equal to the top-level install folder.
In the [launch options][], this means leaving the `Working Dir` box
empty.
The main executable can be in a subdirectory, if you want it to be
(for example, DOTA 2 does this).

* Good: `Working Dir:` *(empty)*
* Might cause issues: `Working Dir: bin/linux64`

If you are choosing the name of the install folder for a new game, it's
simplest for various developer workflows if that subdirectory uses only
letters, digits, dashes and underscores, and doesn't contain punctuation
or Unicode.
Spaces are usually OK, but can be awkward when you are writing shell
scripts.

The container runtime is designed to cope with any directory name, but
it's more likely to have bugs when the directory name contains special
characters.

* Good: `steamapps/common/my-great-game` or `steamapps/common/MyGreatGame`
* Might cause issues: `steamapps/common/My Great Game™... 😹 Edition!`

### Configuration and state

*Windows or Linux-native*

For best results, either use the [Steam Cloud API][], or save configuration
and state in the conventional directories for the platform.

For Windows games running under Proton, paths below `%USERPROFILE%`
should work well.
In Proton, these are redirected into the `steamapps/compatdata` directory.

For Linux-native games, the configuration and data directories from the
[freedesktop.org Base Directory specification][basedirs]
are recommended.

Major game engines and middleware libraries often have built-in support
for these conventional directories.
For example, the Unity engine has [Application.persistentDataPath][]
and the SDL library has [SDL\_GetPrefPath][SDL_GetPrefPath], both of
which are suitable.

### Build environment

*Linux-native only*

For best results, compile Linux-native games in the official
Steam Runtime SDK Docker container using [Docker][], [Podman][]
or [Toolbx][].
The SDK documentation has more information about this.

Linux-native games released on Steam can be compiled for either
[Steam Runtime 1 'scout'][scout SDK] or
[Steam Runtime 3 'sniper'][sniper SDK].
For new Linux-native games,
the recommended runtime environment is Steam Runtime 3, `sniper`.

Steam Runtime 2 'soldier' also has [a similar SDK][soldier SDK],
but releasing games compiled for soldier on Steam is not supported.

### Detecting the container environment

*Linux-native only*

When running in the Steam Linux Runtime environment and using Steam Runtime
libraries, the file `/etc/os-release` will contain a line `ID=steamrt`,
`ID="steamrt"` or `ID='steamrt'`.
Please see [os-release(5)][] for more details of the format and contents
of this file.

When running under the `pressure-vessel` container manager used by the
Steam Linux Runtime, the file `/run/host/container-manager` will contain
`pressure-vessel` followed by a newline.
The same file can be used to detect Flatpak ≥ 1.10.x, which
are identified as `flatpak` followed by a newline.
To support Flatpak 1.8.x or older, check whether the file `/.flatpak-info`
exists.

### Input devices

*Linux-native only*

For best results, either use the [Steam Input][] APIs, or use a
middleware library with container support (such as SDL 2) to access
input devices more directly.
This ensures that your game will automatically detect new hotplugged
controllers, even across a container boundary.

If lower-level access is required, please note that `libudev` does not
provide hotplug support in the Steam Linux Runtime container, and cannot
guarantee to provide device enumeration either.  This is because the
protocol between `libudev` and `udevd` was not designed for use with
containers and is considered private to a particular version of udev.

In engines that implement their own input device handling, the suggested
approach is currently what SDL and Proton do: if one of the files
`/run/host/container-manager` or `/.flatpak-info` exists, then
enumerate input devices by reading `/dev` and `/sys`, with
change-notification by monitoring `/dev` using [inotify][].
Please see the [Linux joystick implementation in SDL][], specifically
the `ENUMERATION_FALLBACK` code paths, for sample code.

### Shared libraries

[shared libraries]: #shared-libraries

*Linux-native only*

Try to avoid bundling libraries with your game if they are also available
in the Steam Runtime.
This can cause compatibility problems.
In particular, the Steam Runtime contains an up-to-date release of SDL 2,
so it should not be necessary to build your own version of SDL.

If you load a library dynamically, make sure to use its versioned SONAME,
such as `libvulkan.so.1` or `libgtk-3.so.0`, as the name to search for.
Avoid using the development symlink such as `libvulkan.so` or `libgtk-3.so`,
which will not be available in the Steam Linux Runtime container,
and does not guarantee ABI compatibility even if it works.
Also avoid using the fully-versioned name such as `libvulkan.so.1.2.189`
or `libgtk-3.so.0.2404.26`,
because that name will no longer work if the library is upgraded to a
newer compatible version.

Use the versions of libraries that are included in the Steam Runtime,
if possible.

If you need to include a library in your game, consider using static
linking if the library's licensing permits this.
If you link statically, linking with the `-Wl,-Bsymbolic` compiler option
might avoid compatibility issues.

### Environment variables

*Linux-native only*

Avoid overwriting the `LD_LIBRARY_PATH` environment variable: that will
break some of the Steam Runtime's compatibility mechanisms.
If your game needs to use local (bundled, vendored) [shared libraries][],
it's better to append or prepend your library directory, depending on
whether your library directory should be treated as higher or lower
priority than system and container libraries.

Similarly, avoid overwriting the `LD_PRELOAD` environment variable:
that will break the Steam Overlay.
If your game needs to load a module via `LD_PRELOAD`, it's better to
append or prepend your module.

### Scripts

*Linux-native only*

Shell scripts can start with `#!/bin/sh` to use a small POSIX shell,
or `#!/bin/bash` to use GNU `bash`.
Similar to Debian and Ubuntu, the `/bin/sh` in the container is not
`bash`, so `bash` features cannot be used in `#!/bin/sh` scripts.
[Debian Policy][] has some useful advice on writing robust shell scripts,
and the [shellcheck][] lint tool usually gives good recommendations.

Basic shell utilities are available in the container runtime, but more
advanced utilities will often not be present.
The legacy [`LD_LIBRARY_PATH`-based scout runtime][ldlp-runtime]
cannot guarantee the presence of anything beyond basic shell utilities
either.

Titles that are configured to run in Steam Linux Runtime 3.0 (sniper)
can include Python scripts,
starting with `#!/usr/bin/env python3` or `#!/usr/bin/python3`,
as an alternative to shell scripts.
sniper includes Python 3.9 and most of the standard library
(except for a few larger modules like `tk`),
but does not include external modules like `pip`.

<!-- References: -->

[Application.persistentDataPath]: https://docs.unity3d.com/ScriptReference/Application-persistentDataPath.html
[Debian Policy]: https://www.debian.org/doc/debian-policy/ch-files.html#scripts
[Debian's /etc/bash.bashrc]: https://sources.debian.org/src/bash/5.1-2/debian/etc.bash.bashrc/
[Docker]: https://www.docker.com/
[Linux joystick implementation in SDL]: https://github.com/libsdl-org/SDL/blob/main/src/joystick/linux/SDL_sysjoystick.c
[Paths shared between host system and container runtime]: shared-paths.md
[Podman]: https://podman.io/
[Proton documentation]: https://github.com/ValveSoftware/Proton/
[SDL_GetPrefPath]: https://wiki.libsdl.org/SDL_GetPrefPath
[Steam Cloud API]: https://partner.steamgames.com/doc/features/cloud
[Steam Input]: https://partner.steamgames.com/doc/features/steam_controller
[Steam client 2024-11-05]: https://store.steampowered.com/news/collection/steam/?emclan=103582791457287600&emgid=4472730495692571024
[Steam support documentation]: https://help.steampowered.com/
[Toolbx]: https://containertoolbx.org/
[Verify integrity]: https://help.steampowered.com/en/faqs/view/0C48-FCBD-DA71-93EB
[add a Steam Library folder]: https://help.steampowered.com/en/faqs/view/4BD4-4528-6B2E-8327
[basedirs]: https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
[compatibility tool interface]: steam-compat-tool-interface.md
[debuginfod]: https://sourceware.org/elfutils/Debuginfod.html
[inotify]: https://man7.org/linux/man-pages/man7/inotify.7.html
[install folder]: https://partner.steamgames.com/doc/store/application/depots
[launch options]: https://partner.steamgames.com/doc/sdk/uploading
[ldlp-runtime]: ld-library-path-runtime.md
[os-release(5)]: https://www.freedesktop.org/software/systemd/man/os-release.html
[scout SDK]: https://gitlab.steamos.cloud/steamrt/scout/sdk/-/blob/steamrt/scout/README.md
[scout-on-soldier]: container-runtime.md#scout-on-soldier
[set launch options]: https://help.steampowered.com/en/faqs/view/7D01-D2DD-D75E-2955
[shared-paths]: shared-paths.md
[shellcheck]: https://www.shellcheck.net/
[sniper SDK]: https://gitlab.steamos.cloud/steamrt/sniper/sdk/-/blob/steamrt/sniper/README.md
[sniper]: https://gitlab.steamos.cloud/steamrt/steamrt/-/blob/steamrt/sniper/README.md
[soldier SDK]: https://gitlab.steamos.cloud/steamrt/soldier/sdk/-/blob/steamrt/soldier/README.md
[soldier]: https://gitlab.steamos.cloud/steamrt/steamrt/-/blob/steamrt/soldier/README.md
[steam-runtime-launch-client]: https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/-/blob/main/bin/launch-client.md?ref_type=heads
[switching a game to a beta branch]: https://help.steampowered.com/en/faqs/view/5A86-0DF4-C59E-8C4A
