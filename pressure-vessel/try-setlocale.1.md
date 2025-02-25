---
title: pv-try-setlocale
section: 1
...

<!-- This document:
Copyright © 2019 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

pv-try-setlocale - check whether a locale works

# SYNOPSIS

**pv-try-setlocale** [*LOCALE*]

# DESCRIPTION

This tool checks whether a locale works. If no locale is specified,
the standard locale environment variables such as **LANG**, **LC\_ALL**
and **LC\_MESSAGES** are used.

# OPTIONS

There are no options.

# POSITIONAL ARGUMENTS

<dl>
<dt>

*LOCALE*

</dt><dd>

A POSIX locale name, such as **en_US.UTF-8** or **it_IT@euro**,
or an empty string to use the standard locale environment variables
(which is the default).

</dd>
</dl>

# OUTPUT

On failure, an unstructured diagnostic message is printed on standard
error.

# EXIT STATUS

0
:   The given locale is available.

1
:   The given locale is not available.

2
:   Invalid arguments were given.

# EXAMPLES

The output quoted here is from a system with no Canadian French locale
available, correctly configured for some other locale.

    $ pv-try-setlocale; echo "exit status $?"
    exit status 0
    $ pv-try-setlocale fr_CA; echo "exit status $?"
    setlocale "fr_CA": No such file or directory
    exit status 1
    $ pv-try-setlocale POSIX; echo "exit status $?"
    exit status 0

<!-- vim:set sw=4 sts=4 et: -->
