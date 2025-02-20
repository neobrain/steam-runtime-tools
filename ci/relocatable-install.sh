#!/bin/sh
# Copyright 2022-2025 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

primary_arch="$(dpkg --print-architecture)"
multiarch="$(dpkg-architecture -qDEB_HOST_MULTIARCH)"

case "$primary_arch" in
    (amd64)
        dir="_build"
        secondary_archs=i386
        ;;

    (*)
        dir="_build/$primary_arch"
        secondary_archs=
        ;;
esac

# We need up-to-date packages for the relocatable install to
# be able to get its source code
apt-get -y dist-upgrade

# shellcheck source=/dev/null
suite="$(. /usr/lib/os-release; echo "${VERSION_CODENAME-${VERSION}}")"

case "$suite" in
    (scout)
        # The default g++ 4.6 is too old (see also debian/rules)
        export CC=gcc-4.8
        export CXX=g++-4.8
        PYTHON=python3.5
        ;;

    (*)
        PYTHON=python3
        ;;
esac

mkdir -p _build/cache
dcmd cp -al debian/tmp/artifacts/source/*.dsc _build/cache

set -- meson

for arch in "$primary_arch" $secondary_archs; do
    set -- "$@" "libglib2.0-dev:$arch"
    set -- "$@" "libxau-dev:$arch"
done

apt-get -y --no-install-recommends install "$@"

set -- debian/tmp/artifacts/build/*_"$primary_arch".*deb
for arch in $secondary_archs; do
    set -- \
        debian/tmp/artifacts/build/libsteam-runtime-tools-0-0-dbgsym_*_"$arch".*deb \
        debian/tmp/artifacts/build/libsteam-runtime-tools-0-0_*_"$arch".deb \
        debian/tmp/artifacts/build/libsteam-runtime-tools-0-dev_*_"$arch".deb \
        debian/tmp/artifacts/build/libsteam-runtime-tools-0-helpers-dbgsym_*_"$arch".*deb \
        debian/tmp/artifacts/build/libsteam-runtime-tools-0-helpers_*_"$arch".deb \
        debian/tmp/artifacts/build/libsteam-runtime-tools-0-relocatable-libs_*_"$arch".deb \
        debian/tmp/artifacts/build/pressure-vessel-libs-"$arch"_*_"$arch".deb \
        "$@"

    case "$suite" in
        (scout)
            set -- \
                debian/tmp/artifacts/build/libsteam-runtime-shim-libcurl4_*_"$arch".deb \
                "$@"
            ;;
    esac
done

dpkg -i "$@"

rm -fr "$dir"/pressure-vessel-*.tar.gz
rm -fr "$dir/relocatable-install"
mkdir -p "$dir"

set --

case "$primary_arch" in
    (amd64)
        ;;

    (*)
        set -- "$@" --architecture-name "$primary_arch"
        set -- "$@" --architecture-multiarch "$multiarch"
        ;;
esac

"$PYTHON" pressure-vessel/build-relocatable-install.py \
    --cache _build/cache \
    --output "$dir/relocatable-install" \
    --archive "$(pwd)/$dir" \
    --no-archive-versions \
    ${CI_ALLOW_MISSING_SOURCES:+--allow-missing-sources} \
    "$@"

set --

case "$primary_arch" in
    (amd64)
        ;;

    (*)
        set -- "$@" --multiarch-tuple="$multiarch"
        ;;
esac

PYTHONPATH="$(pwd)/tests" "$PYTHON" \
    ./tests/pressure-vessel/relocatable-install.py \
    "$@" \
    "$(pwd)/$dir/relocatable-install"
