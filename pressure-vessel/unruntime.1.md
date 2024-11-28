---
title: pressure-vessel-unruntime
section: 1
...

<!-- This document:
Copyright © 2020 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

pressure-vessel-unruntime - run Steam games in a bubblewrap container

# SYNOPSIS

**pressure-vessel-unruntime**
[*OPTIONS*]
[**--**]
*COMMAND* [*ARGUMENTS...*]

# DESCRIPTION

**pressure-vessel-unruntime** escapes from the Steam Runtime to run
**pressure-vessel-wrap** with its own bundled libraries.

# OPTIONS

All options are passed directly to **pressure-vessel-wrap**(1).
One option is special:

<dl>
<dt>

`--batch`

</dt><dd>

If present, the test-UI will not be run.

</dd>
</dl>

# ENVIRONMENT

<dl>
<dt>

`LD_LIBRARY_PATH`

</dt><dd>

Unset, and passed to **pressure-vessel-wrap** via the
`--env-if-host` option so that it will be reinstated if the game
is run with the host `/usr`.

</dd>
<dt>

`LD_PRELOAD`

</dt><dd>

Unset, and passed to **pressure-vessel-wrap** via the
`--env-if-host` option so that it will be reinstated if the game
is run with the host `/usr`. Individual items are also passed to
**pressure-vessel-wrap** via the **--host-ld-preload** option,
so that `LD_PRELOAD` modules can be made available read-only
in the container.

</dd>
<dt>

`PATH`

</dt><dd>

Reset to a sensible default, and passed to **pressure-vessel-wrap**
via the `--env-if-host` option so that it will be reinstated if the
game is run with the host `/usr`.

</dd>
<dt>

`PRESSURE_VESSEL_BATCH`

</dt><dd>

If set to 1, it is equivalent to `--batch`.

</dd>
<dt>

`STEAM_RUNTIME`

</dt><dd>

Unset.

</dd>
<dt>

`SYSTEM_LD_LIBRARY_PATH`

</dt><dd>

Used to reset `LD_LIBRARY_PATH` to the value it had on entry to Steam.

</dd>
<dt>

`SYSTEM_PATH`

</dt><dd>

Used to reset `PATH` to the value it had on entry to Steam.

</dd>
</dl>

# SIGNALS

The **pressure-vessel-unruntime** process replaces itself with
**pressure-vessel-wrap**, so signal
handling is the same as for **pressure-vessel-wrap**.

# EXIT STATUS

The same as **pressure-vessel-wrap**, or nonzero for an internal error
in **pressure-vessel-unruntime**.

# EXAMPLE

    $ steam steam://install/1070560     # Steam Linux Runtime 1.0 (scout)
    $ steam steam://install/302380      # Floating Point, a small free game
    $ rm -fr ~/tmp/scout
    $ mkdir -p ~/tmp/scout
    $ tar \
        -C ~/tmp/scout \
        -xzvf ~/.steam/steamapps/common/SteamLinuxRuntime/com.valvesoftware.SteamRuntime.Platform-amd64,i386-scout-runtime.tar.gz

In Steam, disable all Steam compatibility tools for Floating Point, and
set its launch options to:

    /path/to/pressure-vessel/bin/pressure-vessel-unruntime \
        --runtime ~/tmp/scout/files \
        --shell=instead \
        -- \
        %command%

Launching it will launch an `xterm` instead.
In the resulting `xterm`(1), you can explore the container interactively,
then type `"$@"` to run the game itself.

For more joined-up integration with Steam, install
Steam Linux Runtime 1.0 (scout) (`steam://install/1070560`),
configure a native Linux game in Steam to be run with the
`Steam Linux Runtime 1.0 (scout)` "compatibility tool",
and reset the launch options to be empty.

<!-- vim:set sw=4 sts=4 et: -->
