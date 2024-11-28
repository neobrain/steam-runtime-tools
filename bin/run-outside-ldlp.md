---
title: srt-run-outside-ldlp
section: 1
...

<!-- This document:
Copyright © 2024 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

srt-run-outside-ldlp - run a command outside the `LD_LIBRARY_PATH` runtime

# SYNOPSIS

**srt-run-outside-ldlp**
[*OPTIONS*]
[**--**]
*COMMAND* [*ARGUMENTS...*]

**${COMMAND}**
[*ARGUMENTS...*]

# DESCRIPTION

**srt-run-outside-ldlp** runs the *COMMAND* with the given
*ARGUMENTS* outside of the `LD_LIBRARY_PATH` (LDLP) runtime (such as Steam
Runtime 1 'scout'), with `LD_PRELOAD` cleared.

If the executable is given a different name (typically via symlink), then it
will use its name as the command to run. (It will automatically avoid
running itself recursively if in the current `PATH`.)

# OPTIONS

<dl>
<dt>

**--verbose**, **-v**

</dt><dd>

Be more verbose. If used twice, debug messages are shown.

</dd>
</dl>

# OUTPUT

If an error occurs while starting the command, then a human-readable message is
shown on standard error.

# EXIT STATUS

The exit status is similar to **env**(1):

0
:   The *COMMAND* exited successfully with status 0.

125
:   Invalid arguments were given, or **srt-run-outside-ldlp** failed to run the
    command.

Any value
:   The *COMMAND* exited unsuccessfully with the status indicated.

# EXAMPLES

Run `xdg-open` outside of the runtime:

```
srt-run-outside-ldlp -- xdg-open https://example.com/
```

Via symlink:

```
ln -s $(which srt-run-outside-ldlp) xdg-open
./xdg-open https://example.com/
```
