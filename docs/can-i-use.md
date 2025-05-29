# Can I use?

<!-- This file:
Copyright 2025 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

A cheat-sheet for nice things that we can or cannot have in various
branches of the Steam Runtime.

## apt and dpkg features

We have:

* scout: dpkg 1.17.5 (backport), apt 1.0.9.8.6 (backport)
* soldier: dpkg 1.19.8, apt 1.8.2.3 (from Debian 10)
* sniper/steamrt3c: dpkg 1.20.13, apt 2.2.4 (from Debian 11)
* steamrt4: dpkg 1.22.x, apt 3.0.x (from Debian 13)

therefore we can use:

* Versioned `Provides` (dpkg ≥ 1.17.11)
    * NO: scout
    * YES: Debian 10 and up, therefore soldier and up

* `sources.list.d/*.list` one-line format
    * YES: all branches

* `sources.list.d/*.list` `Signed-By` filename (apt ≥ 1.1)
    * NO: scout
    * YES: soldier and up

* `sources.list.d/*.sources` deb822 format (apt ≥ 1.1)
    * NO: scout
    * YES: soldier and up

* `sources.list.d/*.sources` embeds ASCII-armored PGP keys (apt ≥ 2.3.10)
    * NO: scout
    * NO: soldier
    * NO: sniper/steamrt3c
    * YES: steamrt4 and up

## debhelper features

We have:

* scout: debhelper 9.20131227 (backport)
* soldier: debhelper 12.1.1 (from Debian 10)
* sniper/steamrt3c: debhelper 13.3.4 (from Debian 11)
* steamrt4: debhelper 13.24.2 (from Debian 13)

therefore we can use:

* compat level 10 or older:
    * YES: all branches

* compat levels 11, 12:
    * NO: scout
    * YES: soldier and up

* compat level 13:
    * `${variable}` expansion in `d/*.install`, etc.
    * NO: scout
    * NO: soldier
    * YES: sniper and up

* `dh-sequence-*` (≥ 12.5 for full functionality):
    * NO: scout
    * NO: soldier
    * YES: sniper and up

* `execute_{before,after}_*` hooks (≥ 12.8):
    * NO: scout
    * NO: soldier
    * YES: sniper and up

* `dh-sequence-single-binary` (≥ 13.5):
    * NO: scout
    * NO: soldier
    * NO: sniper/steamrt3c
    * YES: steamrt4 and up

## glibc

We have:

* scout: eglibc 2.15 (from Ubuntu 12.04) in the SDK, 2.31 in SLR 1.0,
  OS's version in legacy runtime
* soldier: 2.28 (from Debian 10) in the SDK, 2.31 (from Debian 11) at runtime
* sniper/steamrt3c: 2.31 (from Debian 11)
* steamrt4: 2.41 (from Debian 13)

## GLib

We have:

* scout: 2.58.3 (backport from Debian 10)
* soldier: 2.58.3 (from Debian 10)
* sniper/steamrt3c: 2.66.8 (from Debian 11)
* steamrt4: 2.84.x (from Debian 13)

plus backports via `libglnx` in selected projects.

## CMake

We have:

* scout: 3.18.4 (backport from Debian 11)
* soldier: 3.18.4 (backport from Debian 11)
* sniper/steamrt3c: 3.25.1 (backport from Debian 12)
* steamrt4: 3.31.x (from Debian 13)

## Meson

We have:

* scout: 0.56.2 (backport from Debian 11)
* soldier: 1.0.1 (backport from Debian 12)
* sniper/steamrt3c: 1.0.1 (backport from Debian 12)
* steamrt4: 1.7.x (from Debian 13)

## Python

We have:

* scout: 2.7, 3.2 (default), 3.5 (backport)
* soldier: 2.7, 3.7 (from Debian 10)
* sniper/steamrt3c: 3.9 (from Debian 11)
* steamrt4: 3.13 (from Debian 13)

therefore we can use:

* f-strings:
    * NO: scout
    * YES: soldier and up

* `pathlib`:
    * NO: scout `#!/usr/bin/python3`
    * YES: scout if you explicitly use `python3.5`
    * YES: soldier and up, but be careful with newer features

* `subprocess.run`:
    * NO: scout `#!/usr/bin/python3`
    * YES: scout if you explicitly use `python3.5`,
      but no `encoding=`, `errors=`, `text=`
    * YES: soldier and up

* `typing`:
    * NO: scout `#!/usr/bin/python3`
    * YES: scout if you explicitly use `python3.5`
    * YES: soldier and up

* `var: type = value` (PEP 526):
    * NO: scout
    * YES: soldier and up

* `list[str]`, etc. (PEP 585):
    * NO: scout
    * NO: soldier
    * YES: sniper and up
