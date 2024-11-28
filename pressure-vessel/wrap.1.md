---
title: pressure-vessel-wrap
section: 1
...

<!-- This document:
Copyright © 2020-2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

pressure-vessel-wrap - run programs in a bubblewrap container

# SYNOPSIS

**pressure-vessel-wrap**
[*OPTIONS*]
[**--**]
*COMMAND* [*ARGUMENTS...*]

**pressure-vessel-wrap --test**

**pressure-vessel-wrap --version**

# DESCRIPTION

**pressure-vessel-wrap** runs *COMMAND* in a container, using **bwrap**(1).

# OPTIONS

<dl>
<dt>

**--batch**

</dt><dd>

Disable all interactivity and redirection: ignore `--shell`,
all `--shell-` options, `--terminal`, `--tty` and `--xterm`.

</dd>
<dt>

**--copy-runtime**, **--no-copy-runtime**

</dt><dd>

If a `--runtime` is active, copy it into a subdirectory of the
`--variable-dir`, edit the copy in-place, and mount the copy read-only
in the container, instead of setting up elaborate bind-mount structures.
This option requires the `--variable-dir` option to be used.

`--no-copy-runtime` disables this behaviour and is currently
the default.

</dd>
<dt>

**--copy-runtime-into** *DIR*

</dt><dd>

If *DIR* is an empty string, equivalent to `--no-copy-runtime`.
Otherwise, equivalent to `--copy-runtime --variable-dir=DIR`.

</dd>
<dt>

**--deterministic**

</dt><dd>

Process directories in a deterministic order.
This debugging option will make **pressure-vessel-wrap** slightly slower,
but makes it easier to compare log files.

</dd>
<dt>

**--devel**

</dt><dd>

Run in a mode that enables experimental or developer-oriented features.
Please see
[Steam Linux Runtime - guide for game developers](https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/-/blob/main/docs/slr-for-game-developers.md#developer-mode)
for more details of the precise meaning of this option.

</dd>
<dt>

**--env-if-host** *VAR=VAL*

</dt><dd>

If *COMMAND* is run with `/usr` from the host system, set
environment variable *VAR* to *VAL*. If not, leave *VAR* unchanged.

</dd>
<dt>

**--filesystem** *PATH*

</dt><dd>

Share *PATH* from the host system with the container.

</dd>
<dt>

**--freedesktop-app-id** *ID*

</dt><dd>

If using `--unshare-home`, use *~/.var/app/ID* as the home
directory. This is the same home directory that a Flatpak app for
freedesktop.org app *ID* would use.

</dd>
<dt>

**--gc-legacy-runtimes**, **--no-gc-legacy-runtimes**

</dt><dd>

Garbage-collect old temporary runtimes in the `--runtime-base` that
appear to be left over from older versions of the **SteamLinuxRuntime**
launcher scripts. `--no-gc-legacy-runtimes` disables this behaviour,
and is the default.

</dd>
<dt>

**--gc-runtimes**, **--no-gc-runtimes**

</dt><dd>

If using `--variable-dir`, garbage-collect old temporary
runtimes that are left over from a previous **pressure-vessel-wrap**.
This is the default. `--no-gc-runtimes` disables this behaviour.

</dd>
<dt>

</dd>
<dt>

**--generate-locales**, **--no-generate-locales**

</dt><dd>

Passed to **pressure-vessel-adverb**(1).
The default is `--generate-locales`, overriding the default
behaviour of **pressure-vessel-adverb**(1).

</dd>
<dt>

**--graphics-provider** *DIR*

</dt><dd>

If using a `--runtime`, use *DIR* to provide graphics drivers.

If *DIR* is the empty string, take the graphics drivers from the
runtime. This will often lead to software rendering, poor performance,
incompatibility with recent GPUs or a crash, and is only intended to
be done for development or debugging.

Otherwise, *DIR* must be the absolute path to a directory that is
the root of an operating system installation (a "sysroot"). The
default is `/`.

</dd>
<dt>

**--home** *DIR*

</dt><dd>

Use *DIR* as the home directory. This implies `--unshare-home`.

</dd>
<dt>

**--host-ld-preload** *MODULE*

</dt><dd>

Deprecated equivalent of `--ld-preload`. Despite its name, the
*MODULE* has always been interpreted as relative to the current
execution environment, even if **pressure-vessel-wrap** is running
in a container.

</dd>
<dt>

**--import-vulkan-layers**, **--no-import-vulkan-layers**

</dt><dd>

If `--no-import-vulkan-layers` is specified, the Vulkan layers will
not be imported from the host system.
The default is `--import-vulkan-layers`.

</dd>
<dt>

**--keep-game-overlay**, **--remove-game-overlay**

</dt><dd>

If `--remove-game-overlay` is specified, remove the Steam Overlay
from the `LD_PRELOAD`. The default is `--keep-game-overlay`.

</dd>
<dt>

**--launcher**

</dt><dd>

Instead of specifying a command with its arguments to execute, all the
elements after `--` will be used as arguments for
`steam-runtime-launcher-service`. This option implies `--batch`.

</dd>
<dt>

**--ld-audit** *MODULE*

</dt><dd>

Add *MODULE* from the current execution environment to `LD_AUDIT`
when executing *COMMAND*. If *COMMAND* is run in a container, or if
**pressure-vessel-wrap** is run in a Flatpak sandbox and *COMMAND*
will be run in a different container or on the host system, then the
path of the *MODULE* will be adjusted as necessary.

</dd>
<dt>

**--ld-audits** *MODULE*[**:**_MODULE_...]

</dt><dd>

Same as `--ld-audit`, but the argument is a colon-delimited string
(the same as `$LD_AUDIT` itself).

</dd>
<dt>

**--ld-preload** *MODULE*

</dt><dd>

Add *MODULE* from the current execution environment to `LD_PRELOAD`
when executing *COMMAND*. If *COMMAND* is run in a container, or if
**pressure-vessel-wrap** is run in a Flatpak sandbox and *COMMAND*
will be run in a different container or on the host system, then the
path of the *MODULE* will be adjusted as necessary.

</dd>
<dt>

**--ld-preloads** *MODULE*[**:**_MODULE_...]

</dt><dd>

Same as `--ld-preload`, but the argument is a string delimited by
colons and/or spaces (the same as `$LD_PRELOAD` itself).

</dd>
<dt>

**--only-prepare**

</dt><dd>

Prepare the runtime, but do not actually run *COMMAND*.
With `--copy-runtime`, the prepared runtime will appear in
a subdirectory of the `--variable-dir`.

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

**--runtime=**

</dt><dd>

Use the current execution environment's /usr to provide /usr in
the container.

</dd>
<dt>

**--runtime** *PATH*

</dt><dd>

Use *PATH* to provide /usr in the container.
If *PATH*/files/ is a directory, *PATH*/files/ is used as the source
of runtime files: this is appropriate for Flatpak-style runtimes that
contain *PATH*/files/ and *PATH*/metadata. Otherwise, *PATH* is used
directly.

If *PATH*/files/usr or *PATH*/usr exists, the source of runtime files
is assumed to be a complete sysroot
(containing bin/sh, usr/bin/env and many other OS files).
Otherwise, it is assumed to be a merged-/usr environment
(containing bin/sh, bin/env and many other OS files).
For example, a Flatpak runtime is a suitable value for *PATH*.

</dd>
<dt>

**--runtime-base** *PATH*

</dt><dd>

If `--runtime` is specified as a relative path,
look for it relative to *PATH*.

</dd>
<dt>

**--share-home**, **--unshare-home**

</dt><dd>

If `--unshare-home` is specified, use the home directory given
by `--home`, `--freedesktop-app-id`, `--steam-app-id` or their
corresponding environment variables, at least one of which must be
provided.

If `--share-home` is specified, use the home directory from the
current execution environment.

The default is `--share-home`, unless `--home` or its
corresponding environment variable is used.

</dd>
<dt>

**--share-pid**, **--unshare-pid**

</dt><dd>

If `--unshare-pid` is specified, create a new process ID namespace.
Note that this is known to interfere with IPC between Steam and its
games. The default is `--share-pid`.

</dd>
<dt>

**--shell=none**

</dt><dd>

Don't run an interactive shell. This is the default.

</dd>
<dt>

**--shell=after**, **--shell-after**

</dt><dd>

Run an interactive shell in the container after *COMMAND* exits.
In that shell, running `"$@"` will re-run *COMMAND*.

</dd>
<dt>

**--shell=fail**, **--shell-fail**

</dt><dd>

The same as `--shell=after`, but do not run the shell if *COMMAND*
exits with status 0.

</dd>
<dt>

**--shell=instead**, **--shell-instead**

</dt><dd>

The same as `--shell=after`, but do not run *COMMAND* at all.

</dd>
<dt>

**--single-thread**

</dt><dd>

Avoid multi-threaded code paths, for debugging.

</dd>
<dt>

**--steam-app-id** *N*

</dt><dd>

Assume that we are running Steam app ID *N*, specified as an integer.
If using `--unshare-home`, use *~/.var/app/com.steampowered.AppN*
as the home directory.

</dd>
<dt>

**--systemd-scope**, **--no-systemd-scope**

</dt><dd>

If `--systemd-scope` is specified, attempt to put the game into a
`systemd --user` scope. Its name will be similar to
`app-steam-app975370-12345.scope`, or `app-steam-unknown-12345.scope`
if the Steam app ID is not known (for example when running the
`Help -> System Information` diagnostic tools). The scope can be viewed
with `systemctl --user` or `systemd-cgls`, and can be terminated with
a command like `systemctl --user kill app-steam-app975370-12345.scope`.

These options have no effect if pressure-vessel cannot contact
systemd's D-Bus interface, and in particular they do not prevent
pressure-vessel from working on machines that do not have systemd.

These options have no effect when running under Flatpak. Flatpak will
always try to put the game in a scope with a name like
`app-flatpak-com.valvesoftware.Steam-12345.scope`, separate from the
scope used to run Steam itself.

The default is `--no-systemd-scope`.

</dd>
<dt>

**--terminal=none**

</dt><dd>

Disable features that would ordinarily use a terminal.

</dd>
<dt>

**--terminal=auto**

</dt><dd>

Equivalent to `--terminal=xterm` if a `--shell` option other
than `none` is used, or `--terminal=none` otherwise.

</dd>
<dt>

**--terminal=tty**, **--tty**

</dt><dd>

Use the current execution environment's controlling tty for
the `--shell` options.

</dd>
<dt>

**--terminal=xterm**, **--xterm**

</dt><dd>

Start an **xterm**(1) inside the container.

</dd>
<dt>

**--terminate-timeout** *SECONDS*, **--terminate-idle-timeout** *SECONDS*

</dt><dd>

Passed to **pressure-vessel-adverb**(1).

</dd>
<dt>

**--test**

</dt><dd>

Perform a smoke-test to determine whether **pressure-vessel-wrap**
can work, and exit. Exit with status 0 if it can or 1 if it cannot.

</dd>
<dt>

**--variable-dir** *PATH*

</dt><dd>

Use *PATH* as a cache directory for files that are temporarily
unpacked or copied. It will be created automatically if necessary.

</dd>
<dt>

**--verbose**

</dt><dd>

Be more verbose.

</dd>
<dt>

**--version**

</dt><dd>

Print the version number and exit.

</dd>
<dt>

**--with-host-graphics**, **--without-host-graphics**

</dt><dd>

Deprecated form of `--graphics-provider`.
`--with-host-graphics` is equivalent to either
`--graphics-provider=/run/host` if it looks suitable, or
`--graphics-provider=/` if not.
`--without-host-graphics` is equivalent to `--graphics-provider=""`.

</dd>
</dl>

# ENVIRONMENT

The following environment variables (among others) are read by
**pressure-vessel-wrap**(1).

<dl>
<dt>

`__EGL_VENDOR_LIBRARY_DIRS`, `__EGL_VENDOR_LIBRARY_FILENAMES`

</dt><dd>

Used to locate EGL ICDs to be made available in the container
if `--runtime` and `--graphics-provider` are active.

</dd>
<dt>

`BWRAP` (path)

</dt><dd>

Absolute path to **bwrap**(1).
The default is to try several likely locations.

</dd>
<dt>

`DBUS_SESSION_BUS_ADDRESS`, `DBUS_SYSTEM_BUS_ADDRESS`

</dt><dd>

Used to locate the well-known D-Bus session and system buses
so that they can be made available in the container.

</dd>
<dt>

`DISPLAY`

</dt><dd>

Used to locate the X11 display to make available in the container.

</dd>
<dt>

`FLATPAK_ID`

</dt><dd>

Used to locate the app-specific data directory when running in a
Flatpak environment.

</dd>
<dt>

`LIBGL_DRIVERS_PATH`

</dt><dd>

Used to locate Mesa DRI drivers to be made available in the container
if `--runtime` and `--graphics-provider` are active.

</dd>
<dt>

`LIBVA_DRIVERS_PATH`

</dt><dd>

Used to locate VA-API drivers to be made available in the container
if `--runtime` and `--graphics-provider` are active.

</dd>
<dt>

`PRESSURE_VESSEL_BATCH` (boolean)

</dt><dd>

If set to `1`, equivalent to `--batch`.
If set to `0`, no effect.

</dd>
<dt>

`PRESSURE_VESSEL_COPY_RUNTIME` (boolean)

</dt><dd>

If set to `1`, equivalent to `--copy-runtime`.
If set to `0`, equivalent to `--no-copy-runtime`.

</dd>
<dt>

`PRESSURE_VESSEL_COPY_RUNTIME_INTO` (path or empty string)

</dt><dd>

If the string is empty, it is a deprecated equivalent of
`--no-copy-runtime`. Otherwise, it is a deprecated equivalent of
`--copy-runtime --variable-dir="$PRESSURE_VESSEL_COPY_RUNTIME_INTO"`.

</dd>
<dt>

`PRESSURE_VESSEL_DETERMINISTIC` (boolean)

</dt><dd>

If set to `1`, equivalent to `--deterministic`.
If set to `0`, no effect.

</dd>
<dt>

`PRESSURE_VESSEL_DEVEL` (boolean)

</dt><dd>

If set to `1`, equivalent to `--devel`.
If set to `0`, no effect.

</dd>
<dt>

`PRESSURE_VESSEL_FILESYSTEMS_RO` (`:`-separated list of paths)

</dt><dd>

Make these paths available read-only inside the container if they
exist, similar to `--filesystem` but read-only.
For example, MangoHUD and vkBasalt users might use
`PRESSURE_VESSEL_FILESYSTEMS_RO="$MANGOHUD_CONFIGFILE:$VKBASALT_CONFIG_FILE"`
if the configuration files are outside the home directory.

</dd>
<dt>

`PRESSURE_VESSEL_FILESYSTEMS_RW` (`:`-separated list of paths)

</dt><dd>

Make these paths available read/write inside the container if they
exist, similar to `--filesystem`.

</dd>
<dt>

`PRESSURE_VESSEL_FDO_APP_ID` (string)

</dt><dd>

Equivalent to
`--freedesktop-app-id="$PRESSURE_VESSEL_FDO_APP_ID"`.

</dd>
<dt>

`PRESSURE_VESSEL_GC_RUNTIMES` (boolean)

</dt><dd>

If set to `1`, equivalent to `--gc-runtimes`.
If set to `0`, equivalent to `--no-gc-runtimes`.

</dd>
<dt>

`PRESSURE_VESSEL_GENERATE_LOCALES` (boolean)

</dt><dd>

If set to `1`, equivalent to `--generate-locales`.
If set to `0`, equivalent to `--no-generate-locales`.

</dd>
<dt>

`PRESSURE_VESSEL_GRAPHICS_PROVIDER` (absolute path or empty string)

</dt><dd>

Equivalent to `--graphics-provider="$PRESSURE_VESSEL_GRAPHICS_PROVIDER"`.

</dd>
<dt>

`PRESSURE_VESSEL_HOME` (path)

</dt><dd>

Equivalent to `--home="$PRESSURE_VESSEL_HOME"`.

</dd>
<dt>

`PRESSURE_VESSEL_HOST_GRAPHICS` (boolean)

</dt><dd>

Deprecated form of `$PRESSURE_VESSEL_GRAPHICS_PROVIDER`.
If set to `1`, equivalent to either
`--graphics-provider=/run/host` if it looks suitable, or
`--graphics-provider=/` if not.
If set to `0`, equivalent to `--graphics-provider=""`.

</dd>
<dt>

`PRESSURE_VESSEL_IMPORT_VULKAN_LAYERS` (boolean)

</dt><dd>

If set to `1`, equivalent to `--import-vulkan-layers`.
If set to `0`, equivalent to `--no-import-vulkan-layers`.

</dd>
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

`PRESSURE_VESSEL_PREFIX` (path)

</dt><dd>

pressure-vessel itself does not use this,
but the Steam Linux Runtime container runtime framework uses it to
locate a custom version of pressure-vessel.
If used, it must be set to a relocatable build of pressure-vessel,
so that `$PRESSURE_VESSEL_PREFIX/bin/pressure-vessel-unruntime` exists.

</dd>
<dt>

`PRESSURE_VESSEL_REMOVE_GAME_OVERLAY` (boolean)

</dt><dd>

If set to `1`, equivalent to `--remove-game-overlay`.
If set to `0`, equivalent to `--keep-game-overlay`.

</dd>
<dt>

`PRESSURE_VESSEL_RUNTIME` (path, filename or empty string)

</dt><dd>

Equivalent to `--runtime="$PRESSURE_VESSEL_RUNTIME"`.

</dd>
<dt>

`PRESSURE_VESSEL_RUNTIME_BASE` (path, filename or empty string)

</dt><dd>

Equivalent to `--runtime-base="$PRESSURE_VESSEL_RUNTIME_BASE"`.

</dd>
<dt>

`PRESSURE_VESSEL_SHARE_HOME` (boolean)

</dt><dd>

If set to `1`, equivalent to `--share-home`.
If set to `0`, equivalent to `--unshare-home`.

</dd>
<dt>

`PRESSURE_VESSEL_SHARE_PID` (boolean)

</dt><dd>

If set to `1`, equivalent to `--share-pid`.
If set to `0`, equivalent to `--unshare-pid`.

</dd>
<dt>

`PRESSURE_VESSEL_SHELL` (`none`, `after`, `fail` or `instead`)

</dt><dd>

Equivalent to `--shell="$PRESSURE_VESSEL_SHELL"`.

</dd>
<dt>

`PRESSURE_VESSEL_SYSTEMD_SCOPE` (boolean)

</dt><dd>

If set to `1`, equivalent to `--systemd-scope`.
If set to `0`, equivalent to `--no-systemd-scope`.

</dd>
<dt>

`PRESSURE_VESSEL_TERMINAL` (`none`, `auto`, `tty` or `xterm`)

</dt><dd>

Equivalent to `--terminal="$PRESSURE_VESSEL_TERMINAL"`.

</dd>
<dt>

`PRESSURE_VESSEL_VARIABLE_DIR` (path)

</dt><dd>

Equivalent to `--variable-dir="$PRESSURE_VESSEL_VARIABLE_DIR"`.

</dd>
<dt>

`PRESSURE_VESSEL_VERBOSE` (boolean)

</dt><dd>

If set to `1`, equivalent to `--verbose`.

</dd>
<dt>

`PRESSURE_VESSEL_WORKAROUNDS` (tokens separated by space, tab and/or comma)

</dt><dd>

Tokens of the form `foo` or `+foo` enable various workarounds.
Tokens of the form `-foo` or `!foo` disable the corresponding workaround.
See pressure-vessel source code for details.
The default is to auto-detect which workarounds are needed.

</dd>
<dt>

`PULSE_CLIENTCONFIG`

</dt><dd>

Used to locate PulseAudio client configuration.

</dd>
<dt>

`PULSE_SERVER`

</dt><dd>

Used to locate a PulseAudio server.

</dd>
<dt>

`PWD`

</dt><dd>

Used to choose between logically equivalent names for the current
working directory (see **get_current_dir_name**(3)).

</dd>
<dt>

`SRT_LAUNCHER_SERVICE` (path)

</dt><dd>

pressure-vessel itself does not use this,
but in the Steam Linux Runtime container runtime framework,
it sets an implementation of **steam-runtime-launcher-service**(1)
to use instead of selecting one automatically.

</dd>
<dt>

`SRT_LAUNCHER_SERVICE_STOP_ON_EXIT` (boolean)

</dt><dd>

pressure-vessel itself does not use this,
but in the Steam Linux Runtime container runtime framework,
setting it to `0` prevents the service started by
**STEAM_COMPAT_LAUNCHER_SERVICE** from exiting until it is specifically
terminated by `steam-runtime-launch-client --terminate` or a signal.

</dd>
<dt>

`SRT_LOG`

</dt><dd>

A sequence of tokens separated by colons, spaces or commas
affecting how output is recorded. See source code for details.

</dd>
<dt>

`SRT_LOG_ROTATION` (boolean)

</dt><dd>

pressure-vessel itself does not use this,
but in the Steam Linux Runtime container runtime framework,
setting it to `0` prevents cleanup of old log files.

</dd>
<dt>

`STEAM_COMPAT_APP_ID` (integer)

</dt><dd>

Equivalent to `--steam-app-id="$STEAM_COMPAT_APP_ID"`.

</dd>
<dt>

`STEAM_COMPAT_APP_LIBRARY_PATH` (path)

</dt><dd>

Deprecated equivalent of `STEAM_COMPAT_MOUNTS`, except that it is
a single path instead of being colon-delimited.

</dd>
<dt>

`STEAM_COMPAT_APP_LIBRARY_PATHS` (`:`-separated list of paths)

</dt><dd>

Deprecated equivalent of `STEAM_COMPAT_MOUNTS`.

</dd>
<dt>

`STEAM_COMPAT_CLIENT_INSTALL_PATH` (path)

</dt><dd>

When used as a Steam compatibility tool, the absolute path to the
Steam client installation directory.
This is made available read/write in the container.

</dd>
<dt>

`STEAM_COMPAT_DATA_PATH` (path)

</dt><dd>

When used as a Steam compatibility tool, the absolute path to the
variable data directory used by Proton, if any.
This is made available read/write in the container.

</dd>
<dt>

`STEAM_COMPAT_FLAGS` (comma-separated list of tokens)

</dt><dd>

`runtime-sdl2` is equivalent to `STEAM_COMPAT_RUNTIME_SDL2=1`.
`runtime-sdl3` is equivalent to `STEAM_COMPAT_RUNTIME_SDL3=1`.
Other tokens are ignored by pressure-vessel-wrap, but might
have an effect on other components.

</dd>
<dt>

`STEAM_COMPAT_INSTALL_PATH` (path)

</dt><dd>

Top-level directory containing the game itself, even if the current
working directory is actually a subdirectory of this.
This is made available read/write in the container.

</dd>
<dt>

`STEAM_COMPAT_LAUNCHER_SERVICE` (token)

</dt><dd>

pressure-vessel itself does not use this,
but in the Steam Linux Runtime container runtime framework,
setting it to **container-runtime** provides a D-Bus service
which can be used to send debugging commands into the container
environment.
In Steam Linux Runtime 1.0,
setting it to **scout-in-container** provides a similar D-Bus service
where the execution environment is compatible with Steam Runtime 1 'scout'.
In Proton, setting it to **proton** provides a similar D-Bus service
with an execution environment that is preconfigured to run Proton's
version of Wine.
See **steam-runtime-launch-client**(1) for examples of how to use this.

</dd>
<dt>

`STEAM_COMPAT_LIBRARY_PATHS` (`:`-separated list of paths)

</dt><dd>

Colon-delimited list of paths to Steam Library directories containing
the game, the compatibility tools if any, and any other resources
that the game will need, such as DirectX installers.
Each is currently made available read/write in the container.

</dd>
<dt>

`STEAM_COMPAT_MOUNT_PATHS` (`:`-separated list of paths)

</dt><dd>

Deprecated equivalent of `STEAM_COMPAT_MOUNTS`.

</dd>
<dt>

`STEAM_COMPAT_MOUNTS` (`:`-separated list of paths)

</dt><dd>

Colon-delimited list of paths to additional directories that are to
be made available read/write in the container.

</dd>
<dt>

`STEAM_COMPAT_RUNTIME_SDL2` (boolean)

</dt><dd>

If set to `1`, set `SDL_DYNAMIC_API` so that the runtime's version
of SDL 2 will be used in preference to any copy that might be bundled
or statically linked in an app or game (as long as it is version ≥ 2.0.2
and the `dynapi` feature has not been disabled).
The advantage of this variable over setting `SDL_DYNAMIC_API` directly
is that pressure-vessel-wrap sets up appropriate distribution-specific
paths so that both 32- and 64-bit SDL 2 will work.
Typically this would be enabled by setting a game's Steam launch
options to `STEAM_COMPAT_RUNTIME_SDL2=1 %command%`.

</dd>
<dt>

`STEAM_COMPAT_RUNTIME_SDL3` (boolean)

</dt><dd>

Same as `STEAM_COMPAT_RUNTIME_SDL2`, but setting `SDL3_DYNAMIC_API`
for SDL 3 apps or games.

</dd>
<dt>

`STEAM_COMPAT_SESSION_ID` (integer)

</dt><dd>

(Not used yet, but should be.)

</dd>
<dt>

`STEAM_COMPAT_SHADER_PATH` (path)

</dt><dd>

When used as a Steam compatibility tool, the absolute path to the
variable data directory used for cached shaders, if any.
This is made available read/write in the container.

</dd>
<dt>

`STEAM_COMPAT_TOOL_PATH` (path)

</dt><dd>

Deprecated equivalent of `STEAM_COMPAT_TOOL_PATHS`, except that it is
a single path instead of being colon-delimited.

</dd>
<dt>

`STEAM_COMPAT_TOOL_PATHS` (`:`-separated list of paths)

</dt><dd>

Colon-delimited list of paths to Steam compatibility tools in use,
such as Proton and the Steam Linux Runtime.
They are currently made available read/write in the container.

</dd>
<dt>

`STEAM_COMPAT_TRACING` (boolean)

</dt><dd>

If set to `1`, allow system tracing if it is enabled at system level.
This currently results in `/sys/kernel/tracing` being read/write,
which has no practical effect unless the OS has granted access to that
directory to the user running Steam.
Steam automatically sets this when system tracing is enabled on a
Steam Deck in developer mode.

</dd>
<dt>

`STEAM_LINUX_RUNTIME_LOG` (boolean)

</dt><dd>

pressure-vessel itself does not use this,
but in the Steam Linux Runtime container runtime framework,
setting it to `1` redirects standard output and standard error to
`$STEAM_LINUX_RUNTIME_LOG_DIR/slr-*.log` or
`$PRESSURE_VESSEL_VARIABLE_DIR/slr-*.log`,
while also enabling info-level logging similar to
`PRESSURE_VESSEL_LOG_INFO=1`.

</dd>
<dt>

`STEAM_LINUX_RUNTIME_KEEP_LOGS` (boolean)

</dt><dd>

pressure-vessel itself does not use this,
but in the Steam Linux Runtime container runtime framework,
setting it to `1` prevents cleanup of old log files.

</dd>
<dt>

`STEAM_LINUX_RUNTIME_VERBOSE` (boolean)

</dt><dd>

pressure-vessel itself does not use this,
but in the Steam Linux Runtime container runtime framework,
setting it to `1` enables debug-level messages similar to
`PRESSURE_VESSEL_VERBOSE=1`.

</dd>
<dt>

`STEAM_RUNTIME` (path)

</dt><dd>

**pressure-vessel-wrap** refuses to run if this environment variable
    is set. Use **pressure-vessel-unruntime**(1) instead.

</dd>
<dt>

`SteamAppId` (integer)

</dt><dd>

Equivalent to `--steam-app-id="$SteamAppId"`.
Must only be set for the main processes that are running a game, not
for any setup/installation steps that happen first.
`STEAM_COMPAT_APP_ID` is used with a higher priority.

</dd>
<dt>

`VDPAU_DRIVER_PATH`

</dt><dd>

Used to locate VDPAU drivers to be made available in the container
if `--runtime` and `--graphics-provider` are active.

</dd>
<dt>

`VK_ADD_DRIVER_FILES`, `VK_DRIVER_FILES`, `VK_ICD_FILENAMES`

</dt><dd>

Used to locate Vulkan ICDs to be made available in the container
if `--runtime` and `--graphics-provider` are active.

</dd>
<dt>

`VK_ADD_LAYER_PATH`, `VK_LAYER_PATH`

</dt><dd>

Used to locate Vulkan explicit layers
if `--runtime` and `--graphics-provider` are active.

</dd>
<dt>

`WAYLAND_DISPLAY`

</dt><dd>

Used to locate the Wayland display to make available in the container.

</dd>
<dt>

`XDG_DATA_DIRS`

</dt><dd>

Used to locate Vulkan ICDs and layers
if `--runtime` and `--graphics-provider` are active.

</dd>
</dl>

The following environment variables are set by **pressure-vessel-wrap**(1).

<dl>
<dt>

`__EGL_VENDOR_LIBRARY_DIRS`

</dt><dd>

Unset if `--runtime` and `--graphics-provider` are active,
to make sure `__EGL_VENDOR_LIBRARY_FILENAMES` will be used instead.

</dd>
<dt>

`__EGL_VENDOR_LIBRARY_FILENAMES`

</dt><dd>

Set to a search path for EGL ICDs
if `--runtime` and `--graphics-provider` are active.

</dd>
<dt>

`DBUS_SESSION_BUS_ADDRESS`, `DBUS_SYSTEM_BUS_ADDRESS`

</dt><dd>

Set to paths in the container's private `/run` where the well-known
D-Bus session and system buses are made available.

</dd>
<dt>

`DISPLAY`

</dt><dd>

Set to a value corresponding to the socket in the container's
`/tmp/.X11-unix`.

</dd>
<dt>

`LD_AUDIT`

</dt><dd>

Set according to `--ld-audit`.

</dd>
<dt>

`LD_LIBRARY_PATH`

</dt><dd>

Set to a search path for shared libraries if `--runtime` is active.

</dd>
<dt>

`LD_PRELOAD`

</dt><dd>

Set according to `--ld-preload`, `--keep-game-overlay`,
`--remove-game-overlay`.

</dd>
<dt>

`LIBGL_DRIVERS_PATH`

</dt><dd>

Set to a search path for Mesa DRI drivers
if `--runtime` and `--graphics-provider` are active.

</dd>
<dt>

`LIBVA_DRIVERS_PATH`

</dt><dd>

Set to a search path for VA-API drivers
if `--runtime` and `--graphics-provider` are active.

</dd>
<dt>

`PATH`

</dt><dd>

Reset to a reasonable value if `--runtime` is active.

</dd>
<dt>

`PULSE_CLIENTCONFIG`

</dt><dd>

Set to the address of a PulseAudio client configuration file.

</dd>
<dt>

`PULSE_SERVER`

</dt><dd>

Set to the address of a PulseAudio server socket.

</dd>
<dt>

`PWD`

</dt><dd>

Set to the current working directory inside the container.

</dd>
<dt>

`STEAM_RUNTIME`

</dt><dd>

Set to `/` if using the Steam Runtime 1 'scout' runtime.

</dd>
<dt>

`TERMINFO_DIRS`

</dt><dd>

Set to the required search path for **terminfo**(5) files if
the `--runtime` appears to be Debian-based.

</dd>
<dt>

`VDPAU_DRIVER_PATH`

</dt><dd>

Set to a search path for VDPAU drivers
if `--runtime` and `--graphics-provider` are active.

</dd>
<dt>

`VK_DRIVER_FILES`, `VK_ICD_FILENAMES`

</dt><dd>

Set to a search path for Vulkan ICDs
if `--runtime` and `--graphics-provider` are active.

</dd>
<dt>

`VK_ADD_DRIVER_FILES`, `VK_ADD_LAYER_PATH`, `VK_LAYER_PATH`

</dt><dd>

Unset if `--runtime` and `--graphics-provider` are active.

</dd>
<dt>

`XAUTHORITY`

</dt><dd>

Set to a value corresponding to a file in the container's
private `/run`.

</dd>
<dt>

`XDG_CACHE_HOME`

</dt><dd>

Set to `$HOME/.cache` (in the private home directory)
if `--unshare-home` is active.

</dd>
<dt>

`XDG_CONFIG_HOME`

</dt><dd>

Set to `$HOME/.config` (in the private home directory)
if `--unshare-home` is active.

</dd>
<dt>

`XDG_DATA_HOME`

</dt><dd>

Set to `$HOME/.local/share` (in the private home directory)
if `--unshare-home` is active.

</dd>
<dt>

`XDG_DATA_DIRS`

</dt><dd>

Set to include a search path for Vulkan layers
if `--runtime` and `--graphics-provider` are active.

</dd>
<dt>

</dd>
<dt>

`XDG_RUNTIME_DIR`

</dt><dd>

Set to a new directory in the container's private `/run`
if `--runtime` is active.

</dd>
</dl>

# OUTPUT

The standard output from *COMMAND* is printed on standard output.

The standard error from *COMMAND* is printed on standard error.
Diagnostic messages from **pressure-vessel-wrap** and
**pressure-vessel-wrap** may also be printed on standard error.

# SIGNALS

The **pressure-vessel-wrap** process replaces itself with a **bwrap**(1)
process. Fatal signals to the resulting **bwrap**(1) process will result
in `SIGTERM` being received by the **pressure-vessel-wrap** process
that runs *COMMAND* inside the container.

# EXIT STATUS

Nonzero (failure) exit statuses are subject to change, and might be
changed to be more like **env**(1) in future.

0
:   The *COMMAND* exited successfully with status 0

Assorted nonzero statuses
:   An error occurred while setting up the execution environment or
    starting the *COMMAND*

Any value 1-255
:   The *COMMAND* exited unsuccessfully with the status indicated

128 + *n*
:   The *COMMAND* was killed by signal *n*
    (this is the same encoding used by **bash**(1), **bwrap**(1) and
    **env**(1))

255
:   The *COMMAND* terminated in an unknown way (neither a normal exit
    nor terminated by a signal).

# EXAMPLE

In this example we install and run a small free game that does not
require communication or integration with Steam, without going via
Steam to launch it. This will only work for simple games without DRM
or significant Steam integration.

    $ steam steam://install/1628350     # Steam Linux Runtime 3.0 'sniper'
    $ steam steam://install/599390      # Battle for Wesnoth
    $ rm -fr ~/tmp/pressure-vessel-var
    $ mkdir -p ~/tmp/pressure-vessel-var
    $ cd ~/.steam/steam/steamapps/common/wesnoth
    $ /path/to/pressure-vessel/bin/pressure-vessel-wrap \
        --runtime ~/.steam/steam/steamapps/common/SteamLinuxRuntime_sniper/sniper_platform_*/
        --variable-dir ~/tmp/pressure-vessel-var \
        --shell=instead \
        -- \
        ./start.sh

In the resulting **xterm**(1), you can explore the container interactively,
then type `"$@"` (including the double quotes) to run the game itself.

# SEE ALSO

* <https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/-/blob/main/docs/container-runtime.md>
* <https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/-/blob/main/docs/slr-for-game-developers.md>

<!-- vim:set sw=4 sts=4 et: -->
