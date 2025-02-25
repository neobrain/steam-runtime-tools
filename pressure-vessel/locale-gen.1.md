---
title: pv-locale-gen
section: 1
...

<!-- This document:
Copyright © 2019-2020 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

pv-locale-gen - generate additional locales

# SYNOPSIS

**pv-locale-gen**
[**--output-directory** *DIR*]
[*LOCALE*…]

# DESCRIPTION

This tool generates locale files for the locales that might be necessary
for Steam games. It should be invoked with the **--output-directory**
(for which the default is the current working directory) set to an
empty directory.

If the output directory is non-empty, the behaviour is undefined.
Existing subdirectories corresponding to locales might be overwritten, or
might be kept. (The current implementation is that they are kept, even if
they are outdated or invalid.)

# OPTIONS

<dl>
<dt>

**--output-directory** *DIR*, **--output-dir** *DIR*, **-o** *DIR*

</dt><dd>

Output to *DIR* instead of the current working directory.

</dd>
<dt>

**--verbose**

</dt><dd>

Be more verbose.

</dd>
</dl>

# POSITIONAL ARGUMENTS

<dl>
<dt>

*LOCALE*

</dt><dd>

One or more additional POSIX locale names, such as **en_US.UTF-8** or
**it_IT@euro**. By default, **pv-locale-gen**
generates all the locales required by the standard locale environment
variables such as **LC_ALL**, plus the value of *$HOST_LC_ALL* if
set, plus the American English locale **en_US.UTF-8** (which is
frequently assumed to be present, even though many operating systems
do not guarantee it).

</dd>
</dl>

# OUTPUT

The diagnostic output on standard error is not machine-readable.

Locale files are generated in the **--output-directory**, or the current
working directory if not specified. On exit,
if the output directory is non-empty (exit status 72 or 73),
its path should be added to the **LOCPATH** environment variable. If
the output directory is empty (exit status 0),
**LOCPATH** should not be modified.

# EXIT STATUS

0
:   All necessary locales were already available without altering
    the **LOCPATH** environment variable.

64 (`EX_USAGE` from `sysexits.h`)
:   Invalid arguments were given.

72 (`EX_OSFILE` from `sysexits.h`)
:   Not all of the necessary locales were already available, but all
    were generated in the output directory.

73 (`EX_CANTCREAT` from `sysexits.h`)
:   At least one locale was not generated successfully. Some other
    locales might have been generated in the output directory as usual.

78 (`EX_CONFIG` from `sysexits.h`)
:   One of the locales given was invalid or could lead to path traversal.
    Other locales might have been generated as usual.

Anything else
:   There was an internal error.

# EXAMPLE

    $ mkdir locales
    $ pv-locale-gen --output-directory=locales
    $ if [ $? = 0 ]; then \
      ./bin/some-game; \
    else \
      LOCPATH="$(pwd)/locales" ./bin/some-game; \
    fi

<!-- vim:set sw=4 sts=4 et: -->
