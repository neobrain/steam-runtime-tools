#!/bin/sh
# Copyright 2022-2025 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

# This assumes a Steam Runtime SDK where the deb-build-snapshot package
# is available. If we want to run this step on a generic container image
# (like Debian) we will have to install deb-build-snapshot from git instead.
apt-get install -y --no-install-recommends \
    build-essential \
    deb-build-snapshot \
    debhelper \
    devscripts \
    dpkg-dev \
    rsync \
    ${NULL+}

set --

if [ -n "${CI_COMMIT_TAG-}" ]; then
    set -- "$@" --release
fi

mkdir -p "debian/tmp/artifacts/source"
set -- "$@" --download "$(pwd)/debian/tmp/artifacts/source"

deb-build-snapshot "$@" --source-only localhost
