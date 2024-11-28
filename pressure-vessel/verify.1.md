---
title: pv-verify
section: 1
...

<!-- This document:
Copyright © 2023 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

pv-verify - verify integrity of a pressure-vessel-based container

# SYNOPSIS

**pv-verify**
[*OPTIONS*]
[**--**]
[*DIRECTORY*]

**pv-verify --version**

# DESCRIPTION

**pv-verify** checks a pressure-vessel-based container runtime against
a manifest written with a subset of **mtree**(5) syntax.

If no *DIRECTORY* is given, the default is to try to discover the runtime
that contains pressure-vessel.

# OPTIONS

<dl>
<dt>

**--minimized-runtime**

</dt><dd>

Assume the *DIRECTORY* is a Steam Linux Runtime runtime in minimized
form (removing empty files, empty directories, symbolic links and so
on). This is normally the `*_platform_*/files` directory.

</dd>
<dt>

**--mtree**

</dt><dd>

Use a non-default filename for the **mtree**(5) manifest.
It must currently be compressed with **gzip**(1) or compatible.
The default is normally `mtree.txt.gz` in the *DIRECTORY*.
If `--minimized-runtime` is used, the default is `../usr-mtree.txt.gz`
relative to the *DIRECTORY*.

</dd>
<dt>

**--quiet**

</dt><dd>

Don't show informational messages if the runtime is successfully verified,
just exit with status 0.

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
</dl>

# ENVIRONMENT

<dl>
<dt>

`SRT_LOG`

</dt><dd>

A sequence of tokens separated by colons, spaces or commas
affecting how output is recorded. See source code for details.

</dd>
</dl>

# OUTPUT

Unstructured diagnostic messages are output on standard error.

# EXIT STATUS

0
:   The runtime matches the manifest

Anything else
:   The runtime does not match the manifest, or an error occurred

# EXAMPLE

    $ steam steam://install/1070560     # Steam Linux Runtime 1.0 (scout)
    $ cd ~/.steam/steam/steamapps/common/SteamLinuxRuntime_soldier
    $ ./pressure-vessel/bin/pv-verify
    $ ./pressure-vessel/bin/pv-verify ../SteamLinuxRuntime

<!-- vim:set sw=4 sts=4 et: -->
