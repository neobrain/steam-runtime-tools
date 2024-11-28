---
title: steam-runtime-dialog
section: 1
...

<!-- This document:
Copyright 2024 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-dialog - wrapper for various user interface helpers

# SYNOPSIS

**steam-runtime-dialog**
**--check-features=**_FEATURES_

**steam-runtime-dialog**
**--error**|**--warning**|**--info**
**--text=**_TEXT_
[**--title=**_TITLE_]
[**--no-wrap**]

**steam-runtime-dialog**
**--progress**
[**--pulsate**]
[**--text=**_TEXT_]
[**--title=**_TITLE_]
[**--auto-close**]
[**--no-cancel**]
[**--no-wrap**]

# DESCRIPTION

**steam-runtime-dialog** presents a user interface dialog, similar to **zenity**(1).
It works by locating one of several user interface backends:

* **steam-dialog-ui**, possibly implemented by **steam-runtime-dialog-ui**(1)
* **zenity**(1)
* **yad**(1)

and running that program with arguments similar to its own.

It can be invoked from several execution environments:

* the host operating system
* a Flatpak or Snap sandbox
* the `LD_LIBRARY_PATH`-based Steam Runtime 1 'scout' environment
* the container-based Steam Linux Runtime environments
* an execution environment similar to that of Steam itself,
    for example via **steam-runtime-launch-client --alongside-steam**
    or as part of a non-Steam game

and it will attempt to do the right thing in any of those environments.

The intended implementation is for a copy of **steam-runtime-dialog**
to be installed as part of Steam itself, under the name **steam-dialog**.

# MODES

Exactly one mode argument must be provided.

<dl>
<dt>

**--check-features=**_FEATURES_

</dt><dd>

If **steam-runtime-dialog** supports all of the feature flags in the
space-separated string _FEATURES_, and a user interface backend
supporting the same features is found, then exit with status 0.
Otherwise, exit with a non-zero status.

The known feature flags are:

**message**
:   The **--error**, **--warning** and **--info** modes are supported.
    The **--title** and **--text** options are supported.
    The **--width** and **--no-wrap** options are supported to at least
    a basic level (they might be ignored, if they are not applicable
    to a particular user interface).

**progress**
:   The **--progress** mode is supported.
    The **--pulsate** option is supported.
    The **--auto-close** and **--no-cancel** options are supported
    to at least a basic level (they might be ignored or on-by-default).

</dd>
<dt>

**--error**, **--warning**, **--info**

</dt><dd>

Show an error, warning or informational message and wait for it to be
acknowledged.
These modes are part of the **message** feature flag.

</dd>
<dt>

**--progress**

</dt><dd>

Show a progress bar.
This mode reads progress information from standard input.
Each line read is expected to be either:

* an ASCII decimal integer or floating-point number between 0 and 100:
    set the progress bar to the given percentage
* `#` followed by arbitrary UTF-8 text: change the **--text** to the
    given message
* `pulsate:true`: instead of progress, do the equivalent of **--pulsate**
* `pulsate:false`: show progress instead of the equivalent of **--pulsate**

This mode is part of the **progress** feature flag.

</dd>
</dl>

# OPTIONS

**--auto-close**
:   This option is only applicable when the mode is **--progress**.
    Automatically close the progress dialog when **100** is read from
    standard input, or when end-of-file is reached on standard input.

**--height=**_PX_
:   Make the dialog approximately *PX* logical pixels tall.
    This might be ignored by some user interfaces, especially if they
    display the message full-screen.

**--no-cancel**
:   This option is only applicable when the mode is **--progress**.
    Do not allow the progress dialog to be closed before it has finished.

**--no-wrap**
:   Do not automatically line-wrap the text.

**--percentage**
:   This option is only applicable when the mode is **--progress**.
    Start from the given percentage progress instead of the default 0.

**--pulsate**
:   This option is only applicable when the mode is **--progress**.
    Indicate that progress is indeterminate, by showing an oscillating
    or spinning pattern.

**--text=**_TEXT_
:   Use the given UTF-8 text as the body of the message or progress dialog.
    It may contain newlines.
    It may be line-wrapped automatically.
    Unlike **zenity**(1), but like **yad**(1),
    **steam-runtime-dialog** does not normally interpret
    Pango XML markup in this text.

**--title=**_TITLE_
:   Use the given UTF-8 text as the title of the message or progress dialog.

**--verbose**, **-v**
:   Output debug messages.
    This option may be given twice to increase verbosity.

**--width=**_PX_
:   Make the dialog approximately *PX* logical pixels wide.
    This might be ignored by some user interfaces, especially if they
    display the message full-screen.

# USER INTERFACE

If **steam-runtime-dialog** is run inside a Steam Linux Runtime
container, it will attempt to run a program named **steam-dialog-ui**
provided in the **PATH** by the container.
These programs are assumed to target the Steam Linux Runtime execution
environment.
This step is skipped if it is not running inside Steam Linux Runtime.

Next, it will attempt to run a program named **steam-dialog-ui**
provided by the Steam client itself, or a program named **steam-dialog-ui**
provided by the legacy `LD_LIBRARY_PATH` Steam Runtime 1 'scout' runtime.
These programs are assumed to target the scout environment, and therefore
will be run via **run.sh** so that they can load their dependencies.

Next, it will attempt to run a program named **steam-dialog-ui**
provided by the host operating system or a Flatpak or Snap container,
via the usual **PATH** search mechanism.
This program is expected to target the same execution environment as
non-Steam programs, and therefore is run without any special compatibility
layers.

As a last resort, it will attempt to fall back to **zenity**(1),
**yad**(1), or displaying a message in an **xterm**(1).

# ENVIRONMENT

`STEAM_RUNTIME_SCOUT`, `STEAM_RUNTIME`
:   If set to an absolute path to the `LD_LIBRARY_PATH`-based
    Steam Runtime 1.0 (scout), that version will be used in preference
    to the typical `~/.steam/root/ubuntu12_32/steam-runtime`.
    `STEAM_RUNTIME_SCOUT` is preferred if both are set.

`STEAM_RUNTIME_DIALOG_UI`
:   If set to a command, attempt to use it as a higher priority than
    **steam-dialog-ui**. It will be run as if it was a non-Steam program.
    Fallback to other commands will still be attempted if
    `"$STEAM_RUNTIME_DIALOG_UI" --check-features="$FEATURES"` fails.
    `zenity` and `yad` are not suitable values for this, but a wrapper
    script around `zenity` or `yad` would be possible.

`STEAM_RUNTIME_VERBOSE`
:   If non-empty, debug messages will be logged.

`STEAM_ZENITY`
:   If set to a command, attempt to use it instead of **zenity**(1) as a
    fallback UI implementation.
    If set to an empty string, then `zenity`, `yad` and `xterm` will not
    be used as a fallback implementation.

`SYSTEM_LD_LIBRARY_PATH`
:   Used to reset `LD_LIBRARY_PATH` to the value that it had before
    launching Steam, if **steam-runtime-dialog** is invoked from the
    `LD_LIBRARY_PATH`-based Steam Runtime environment.

`SYSTEM_LD_PRELOAD`
:   Used to reset `LD_PRELOAD` to the value that it had before
    launching Steam, if **steam-runtime-dialog** is invoked from the
    `LD_LIBRARY_PATH`-based Steam Runtime environment.

`SYSTEM_PATH`
:   Used to reset `PATH` to the value that it had before
    launching Steam, if **steam-runtime-dialog** is invoked from the
    `LD_LIBRARY_PATH`-based Steam Runtime environment.

`XDG_CURRENT_DESKTOP`
:   If set to `gamescope`, then `zenity`, `yad` and `xterm` will not be used
    as a fallback implementation.

# EXIT STATUS

0
:   The user interface was presented successfully.

Any other value
:   The user interface was not presented successfully, or was presented
    successfully but cancelled by the user.

# EXAMPLES

An error message:

    steam-runtime-dialog --error \
        --title="Light source required" \
        --text="It is dark here. You are likely to be eaten by a grue."

A progress bar:

    (
        sleep 1
        echo "33.3"
        sleep 1
        echo "#Answering questions about the universe..."
        echo 42
        sleep 1
        echo "#Waiting for Godot..."
        sleep 1
        echo 80
        sleep 1
    ) \
    | steam-runtime-dialog --progress \
        --auto-close --no-cancel \
        --title="Please wait" \
        --text="Reticulating splines..."

# SEE ALSO

**steam-runtime-dialog-ui**(1), **zenity**(1), **yad**(1)

<!-- vim:set sw=4 sts=4 et: -->
