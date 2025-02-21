#!/bin/sh
# Copyright 2022-2025 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

# We need access to source code, but Debian's semi-official Docker
# images don't have deb-src configured in order to save some space.
if ! grep deb-src /etc/apt/sources.list \
    || grep deb-src /etc/apt/sources.list.d/*.list \
    || grep deb-src /etc/apt/sources.list.d/*.sources \
; then
    # This is not 100% right because in principle the types could be
    # on an indented continuation line, but it's good enough in practice
    for f in /etc/apt/sources.list.d/*.sources; do
        if [ -e "$f" ]; then
            sed -i -e 's/^Types: *deb$/& deb-src/' "$f"
        fi
    done

    # Similarly this is not a perfect parser but good enough in practice
    for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.list; do
        if [ -e "$f" ]; then
            sed -i -e '/^deb / { p; s/^deb /deb-src /; }' "$f"
        fi
    done
fi

apt-get -y update
