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
* steamrt4: dpkg 1.22.21, apt 3.0.3 (from Debian 13)

In software that runs on the host system (`steam-launcher`),
we can expect to find:

* Ubuntu 16.04: dpkg 1.18.4, apt 1.2.35
* Ubuntu 18.04: dpkg 1.19.0.5, apt 1.6.17
* Ubuntu 20.04: dpkg 1.19.7, apt 2.0.11
* Ubuntu 22.04: dpkg 1.21.1, apt 2.4.14
* Debian 12: dpkg 1.21.22, apt 2.6.1
* Ubuntu 24.04: dpkg 1.22.6, apt 2.8.3
* Debian 13: dpkg 1.22.21, apt 3.0.3

therefore we can use:

* Versioned `Provides` (dpkg ≥ 1.17.11)
    * NO: scout
    * YES: Debian 10 and up, therefore soldier and up
    * YES: all supportable Ubuntu releases

* `sources.list.d/*.list` one-line format
    * YES: all branches

* `sources.list.d/*.list` `Signed-By` filename (apt ≥ 1.1)
    * NO: scout
    * YES: soldier and up
    * YES: all supportable Ubuntu releases

* `sources.list.d/*.sources` deb822 format (apt ≥ 1.1)
    * NO: scout
    * YES: soldier and up
    * YES: all supportable Ubuntu releases

* `sources.list.d/*.sources` embeds ASCII-armored PGP keys (apt ≥ 2.3.10)
    * NO: scout
    * NO: soldier
    * NO: sniper/steamrt3c
    * NO: Ubuntu 20.04 or older
    * YES: steamrt4 and up
    * YES: Ubuntu 22.04 and up
    * YES: Debian 12 and up

* `dpkg-build-api (= 1)` (dpkg ≥ 1.22.0)
    * NO: sniper/steamrt3c or older
    * NO: Ubuntu 22.04 or older
    * NO: Debian 12 or older
    * YES: steamrt4 and up
    * YES: Ubuntu 24.04 and up
    * YES: Debian 13 and up

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

For software that runs on the host system (such as `steam-launcher`),
see <https://repology.org/project/glibc/versions>.
Some key versions,
focusing on distros with a slower release cycle (LTS or "enterprise"):

* Ubuntu 16.04: 2.23
    (no longer supported by Steam [after 2025-08-15][glibc-minspec])
* Slackware 14.2: 2.23
    (no longer supported by Steam [after 2025-08-15][glibc-minspec])
* Ubuntu 18.04: 2.27
    (no longer supported by Steam [after 2025-08-15][glibc-minspec])
* RHEL/CentOS 8: 2.28
    (no longer supported by Steam [after 2025-08-15][glibc-minspec])
* Ubuntu 20.04: 2.31
* openSUSE Leap 15.4, 15.5: 2.31
* Mageia 8: 2.32
* Slackware 15: 2.33
* RHEL/CentOS 9: 2.34
* Ubuntu 22.04: 2.35
* Debian 12: 2.36
* Mageia 9: 2.36
* openSUSE Leap 15.6: 2.38
* Ubuntu 24.04: 2.39
* RHEL/CentOS 10: 2.39
* Debian 13: 2.41

(Rolling releases like Arch and short-lifetime distros like Fedora do not
need to be listed here:
we can assume that they are never going to be the limiting factor for
how new a glibc version we can require.)

[glibc-minspec]: https://help.steampowered.com/en/faqs/view/107F-BB20-FB5A-1CE4

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

For software that runs on the host system (such as `steam-launcher`),
see <https://repology.org/project/python/versions>.
Some key versions,
focusing on distros with a slower release cycle (LTS or "enterprise"):

* Ubuntu 16.04: 3.5.1
    (no longer supported by Steam [after 2025-08-15][glibc-minspec])
* Ubuntu 18.04: 3.6.7
    (no longer supported by Steam [after 2025-08-15][glibc-minspec])
* RHEL/CentOS 8: 3.6.8
    (no longer supported by Steam [after 2025-08-15][glibc-minspec])
* Ubuntu 20.04: 3.8.2
* RHEL/CentOS 9: 3.9.23
* Ubuntu 22.04: 3.10.6
* Debian 12: 3.11.2
* Ubuntu 24.04: 3.12.3
* RHEL/CentOS 10: 3.12.11
* Debian 13: 3.13.5

(Rolling releases like Arch and short-lifetime distros like Fedora do not
need to be listed here:
we can assume that they are never going to be the limiting factor for
how new a Python version we can require.)

Therefore we can use:

* f-strings (3.6):
    * NO: scout
    * NO: Ubuntu 16.04
    * YES: soldier and up
    * YES: any supportable Debian release
    * YES: Ubuntu 18.04 and up

* `pathlib` (3.5):
    * NO: scout `#!/usr/bin/python3`
    * YES: scout if you explicitly use `python3.5`
    * YES: soldier and up, but be careful with newer features
    * YES: any supportable Debian/Ubuntu release

* `subprocess.run` (3.5):
    * NO: scout `#!/usr/bin/python3`
    * YES: scout if you explicitly use `python3.5`,
      but no `encoding=`, `errors=`, `text=`
    * YES: soldier and up
    * YES: any supportable Debian/Ubuntu release

* `typing` (3.5):
    * NO: scout `#!/usr/bin/python3`
    * YES: scout if you explicitly use `python3.5`
    * YES: soldier and up
    * YES: any supportable Debian/Ubuntu release

* `var: type = value` (PEP 526) (3.6):
    * NO: scout
    * NO: Ubuntu 16.04
    * YES: soldier and up
    * YES: any supportable Debian release
    * YES: Ubuntu 18.04 and up

* `list[str]`, etc. (PEP 585) (3.9):
    * NO: scout
    * NO: soldier
    * NO: Ubuntu 20.04
    * YES: sniper and up
    * YES: any supportable Debian release
    * YES: Ubuntu 22.04 and up
