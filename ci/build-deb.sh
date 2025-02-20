#!/bin/sh
# Copyright 2022-2025 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

apt-get install -y --no-install-recommends \
    build-essential \
    debhelper \
    devscripts \
    dpkg-dev \
    rsync \
    "$@"

# shellcheck source=/dev/null
case "$(. /usr/lib/os-release; echo "${VERSION_CODENAME-${VERSION}}")" in
    (scout)
        apt-get -y install pkg-create-dbgsym
        ;;
esac

mkdir -p debian/tmp/artifacts/build
cd debian/tmp/artifacts/build
dpkg-source -x ../source/*.dsc
cd ./*/

set --

case "$STEAM_CI_DEB_BUILD" in
    (all)
        set -- -A
        ;;

    (any)
        set -- -B
        ;;

    (full)
        ;;

    (binary)
        set -- -b
        ;;
esac

dpkg-buildpackage -us -uc "$@"
cd ..
rm -fr ./*/
