Third-party modules
===================

<!-- This document:
Copyright © 2018-2025 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

pressure-vessel includes the capsule-capture-libs utility from libcapsule,
which is licensed under [LGPL-2.1-or-later][]. Complete source code can
be obtained from the same place as pressure-vessel, or from
<https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/>.

pressure-vessel includes bubblewrap (bwrap), which is licensed under
[LGPL-2.0-or-later][]. Complete source code can
be obtained from the same place as pressure-vessel, or from
<https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/>.

pressure-vessel incorporates code from GNOME libglnx, variously licensed
under [LGPL-2.0-or-later][] and [LGPL-2.1-or-later][].
Complete source code can be obtained from the same place as
pressure-vessel, or from
<https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/>.

pressure-vessel incorporates code from Flatpak, licensed under
[LGPL-2.1-or-later][]. Complete source code can be obtained from the
same place as pressure-vessel, or from
<https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/>.

pressure-vessel includes the steam-runtime-tools library and utilities,
which contain code licensed under the [Apache-2.0][], [BSD-2-Clause][],
[LGPL-2.1-or-later][], [MIT][] and [Zlib][] licenses. See
steam-runtime-tools.txt. Complete source code can be obtained from the
same place as pressure-vessel, or from
<https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/>.

Binary releases of pressure-vessel include the following third-party
programs and libraries:

- libffi.so.* from libffi: [MIT][]. See `libffi_*.txt`.

- libgio-2.0.so.0, libglib-2.0.so.0, libgobject-2.0.so.0,
  libgmodule-2.0.so.0 from GLib: [LGPL-2.1-or-later][].
  See `glib2.0_*.txt`.

- libcap.so.2 from libcap: dual-licensed under [GPL-2.0-only][] and
  a [BSD-3-Clause][] variant. See `libcap2_*.txt`.

- libelf.so.1 from Red Hat elfutils: [GPL-2.0-or-later][] with linking
  exception. See `elfutils_*.txt`.

- libXau.so.6 from Xorg: [MIT][]. See `libxau_*.txt`.

- libz.so.1 from zlib: [Zlib][] license. See `zlib_*.txt`.

- libjson-glib-1.0.so.0 from json-glib: [LGPL-2.0-or-later][]. See
  `json-glib_*.txt`.

- libwaffle-1.so.0 from waffle: [BSD-2-Clause][]. See `waffle_*.txt`.

Depending on build configuration, some builds also include:

- libblkid.so.1, libmount.so.1 from util-linux: [LGPL-2.1-or-later][].
  See `libblkid1_*.txt`, `libmount1_*.txt`.

- libpcre.so.3 or libpcre2-8.so.0 from PCRE: [BSD-3-Clause][].
  See `pcre3_*.txt` or `pcre2_*.txt`.

- libselinux.so.1 from SELinux: public-domain with [GPL-2.0-only][] parts.
  See `libselinux_*.txt`.

- libuuid.so.1 from util-linux: [BSD-3-Clause][]. See `util-linux_*.txt`.

Complete source code can be obtained from
<https://repo.steampowered.com/pressure-vessel/>.

[Apache-2.0]: https://spdx.org/licenses/Apache-2.0.html
[BSD-2-Clause]: https://spdx.org/licenses/BSD-2-Clause.html
[BSD-3-Clause]: https://spdx.org/licenses/BSD-3-Clause.html
[GPL-2.0-only]: https://spdx.org/licenses/GPL-2.0-only.html
[GPL-2.0-or-later]: https://spdx.org/licenses/GPL-2.0-or-later.html
[LGPL-2.0-or-later]: https://spdx.org/licenses/LGPL-2.0-or-later.html
[LGPL-2.1-or-later]: https://spdx.org/licenses/LGPL-2.1-or-later.html
[MIT]: https://spdx.org/licenses/MIT.html
[Zlib]: https://spdx.org/licenses/Zlib.html
