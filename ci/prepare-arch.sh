#!/bin/bash
# Copyright 2022 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

# Enable multilib repository
echo -e "\n[multilib]\nInclude = /etc/pacman.d/mirrorlist" >> /etc/pacman.conf

pacman -Syu --needed --noconfirm --noprogressbar \
base-devel \
git \
lib32-glibc \
lib32-mesa \
libxrandr \
python \
python-chardet \
python-debian \
python-six \
python-tappy \
sudo \
"$@"
