---
title: steam-runtime-check-requirements
section: 1
...

<!-- This document:
Copyright 2020 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-check-requirements - perform checks to ensure that the Steam client requirements are met

# SYNOPSIS

**steam-runtime-check-requirements**

# DESCRIPTION

# OPTIONS

**--verbose**, **-v**
:   Show more information on standard error.
    If repeated, produce debug output.

**--version**
:   Instead of performing the checks, write in output the version number as
    YAML.

# OUTPUT

If all the Steam client requirements are met the output on standard output
will be empty.

Otherwise if some of the checks fails, the output on standard output
will have a human-readable message explaining what failed.

Unstructured diagnostic messages appear on standard error.

# EXIT STATUS

0
:   **steam-runtime-check-requirements** ran successfully and all the Steam
    client requirements are met.

71 (`EX_OSERR` from `sysexits.h`)
:   At least one of the requirements is not met.

Other Nonzero
:   An error occurred.

<!-- vim:set sw=4 sts=4 et: -->
