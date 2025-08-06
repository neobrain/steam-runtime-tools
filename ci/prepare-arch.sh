#!/bin/bash
# Copyright 2022-2025 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

pacman -Syu --needed --noconfirm --noprogressbar \
git \
lib32-mesa \
libxrandr \
python \
python-chardet \
python-debian \
python-six \
python-tappy \
"$@"
