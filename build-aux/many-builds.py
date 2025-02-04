#!/usr/bin/python3

# Copyright © 2017-2021 Collabora Ltd.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

"""
Convenience script to build and test steam-runtime-tools and pressure-vessel
using multiple platforms.

Build directory layout:

_build/
    # meson/ninja build directories
    # (built by default)
    clang/                      Build for host system with clang
    host/                       Build for host system, with pressure-vessel
    scout-i386/                 Build for scout, i386
    scout-x86_64/               Build for scout, x86_64, with pressure-vessel
    soldier-x86_64/             Build for soldier, x86_64, with pressure-vessel
    sniper-x86_64/              Build for sniper, x86_64, with pressure-vessel

    # Non-Meson-managed
    cache/                      Download cache for populate-depot.py
    containers/                 Container images for testing
        scout_sysroot/          scout sysroot for builds
    host-artifacts/             Additional test logs
    scout-DESTDIR/              Staging directory for scout builds
    scout-layered/              Staging directory for scout-on-soldier
    scout-relocatable/          Staging directory for relocatable scout builds

    # Special-purpose meson/ninja build directories
    # (not built by default)
    coverage/                   Build for host system with coverage
    doc/                        Build for host system with gtk-doc and pandoc
    host-no-asan/               No AddressSanitizer, for use with valgrind
    arm64/                      Build for host system for arm64, as an
                                example of a non-x86 platform
    i386/                       Build for host system for i386
"""

import argparse
import grp
import logging
import os
import shutil
import subprocess
import sys
import tempfile

from contextlib import suppress
from pathlib import Path
from typing import List, Union


logger = logging.getLogger('many-builds')

SYSROOT_TAR = (
    'com.valvesoftware.SteamRuntime.Sdk-amd64,i386-{}-sysroot.tar.gz'
)


class Environment:
    def __init__(
        self,
        builddir_parent: Union[str, os.PathLike] = '_build',
        docker: bool = False,
        podman: bool = False,
        srcdir: Union[str, os.PathLike] = '.',
    ) -> None:
        self.builddir_parent = Path(builddir_parent)
        self.srcdir = Path(srcdir)

        self.builddir_parent.mkdir(exist_ok=True)

        self.abs_srcdir = self.srcdir.resolve()
        self.abs_builddir_parent = self.builddir_parent.resolve()

        self.podman = podman

        if docker:
            groups = set(os.getgroups())
            groups.add(os.geteuid())

            try:
                docker_gid = grp.getgrnam('docker').gr_gid
            except KeyError:
                self.docker = ['sudo', 'docker']
            else:
                if docker_gid in groups:
                    self.docker = ['docker']
                else:
                    self.docker = ['sudo', 'docker']
        else:
            self.docker = []

        self.populate_depot = (
            self.abs_srcdir / 'subprojects' / 'container-runtime'
            / 'populate-depot.py'
        )

        self.cache = self.abs_builddir_parent / 'cache'
        self.containers = self.abs_builddir_parent / 'containers'

        real_builddir = self.abs_builddir_parent.resolve()

        oci_run_args = [
            '--rm',
            '-i',
            '--security-opt', 'label=disable',
            '--arch', 'x86_64',
            '-v', '/etc/passwd:/etc/passwd:ro',
            '-v', '/etc/group:/etc/group:ro',
            '-v', '/etc/resolv.conf:/etc/resolv.conf:ro',
            '--tmpfs', '/tmp',
            '--tmpfs', '/var/tmp',
            '--tmpfs', '/home',
            '--tmpfs', '/run',
            '--tmpfs', '/run/host',
            '-v', '{}:{}'.format(self.abs_srcdir, self.abs_srcdir),
            '-v', '{}:{}'.format(real_builddir, real_builddir),
            '-w', str(self.abs_srcdir),
        ]

        if sys.stdout.isatty() and sys.stderr.isatty():
            oci_run_args.append('-t')

        if real_builddir != self.abs_builddir_parent:
            oci_run_args.extend([
                '-v', '{}:{}'.format(
                    real_builddir,
                    self.abs_builddir_parent,
                ),
            ])

        self.oci_images = {
            'scout': 'registry.gitlab.steamos.cloud/steamrt/scout/sdk:beta',
            'soldier': (
                'registry.gitlab.steamos.cloud/steamrt/soldier/sdk:beta'
            ),
            'sniper': 'registry.gitlab.steamos.cloud/steamrt/sniper/sdk:beta',
            'medic': '',
            'steamrt5': '',
        }

        if self.podman:
            self.oci_run_argv = ['podman', 'run'] + oci_run_args
        elif self.docker:
            self.oci_run_argv = self.docker + [
                'run',
                '-e', 'HOME={}'.format(Path.home()),
                '-u', '{}:{}'.format(os.geteuid(), os.getegid()),
            ] + oci_run_args
        else:
            self.oci_run_argv = []

    def populate_depots(self):
        with tempfile.TemporaryDirectory() as empty_depot_template:
            Path(empty_depot_template, 'common').mkdir()

            for suite in self.oci_images:
                if not self.oci_images[suite]:
                    continue

                if suite == 'scout':
                    version = 'latest-steam-client-public-beta'
                else:
                    version = 'latest-container-runtime-public-beta'

                subprocess.run(
                    [
                        self.populate_depot,
                        '--cache', self.cache,
                        '--depot', self.containers,
                        '--include-sdk-sysroot',
                        '--no-mtrees',
                        '--no-versioned-directories',
                        '--source-dir', empty_depot_template,
                        f'--version={version}',
                        suite,
                    ],
                    check=True,
                )

    def run_in_suite(
        self,
        suite: str,
        argv: List[str],
        check: bool = True
    ) -> None:
        if self.oci_run_argv:
            subprocess.run(
                self.oci_run_argv + [self.oci_images[suite]] + argv,
                check=check,
            )
        else:
            sysroot = self.containers / (suite + '_sysroot')
            tarball = self.cache / SYSROOT_TAR.format(suite)
            subprocess.run(
                [
                    str(self.abs_srcdir / 'build-aux' / 'run-in-sysroot.py'),
                    '--srcdir', str(self.srcdir),
                    '--builddir', str(self.builddir_parent),
                    '--sysroot', str(sysroot),
                    '--tarball', str(tarball),
                    '--',
                ] + argv,
                check=check,
            )

    def run_scout_builds(self, verb: str, args: List[str]) -> None:
        self.run_in_suite(
            'scout',
            [
                str(self.abs_srcdir / 'build-aux' / 'scout-builds.py'),
                '--srcdir', str(self.srcdir),
                '--builddir', str(self.builddir_parent),
                verb,
            ] + args,
        )

    def deps(self, args: List[str]) -> None:
        self.populate_depots()

        for suite, image in self.oci_images.items():
            if image:
                if self.podman:
                    subprocess.run(['podman', 'pull', image], check=True)
                elif self.docker:
                    subprocess.run(self.docker + ['pull', image], check=True)

    def setup_one(
        self,
        subdir: str,
        args: List[str],
        *,
        check: bool = True,
        in_suite: str = '',
    ) -> None:
        d = self.abs_builddir_parent / subdir

        if (d / 'meson-private' / 'coredata.dat').exists():
            maybe_wipe = ['--wipe']
        else:
            maybe_wipe = []

        argv = [
            'meson',
            'setup',
            str(d),
        ] + maybe_wipe + args

        if in_suite:
            self.run_in_suite(in_suite, argv, check=check)
        else:
            subprocess.run(argv, check=check)

    def setup(self, args: List[str]) -> None:
        dev_build = [
            '-Dbin=true',
            # libcurl_compat defaults to false, but for developer builds
            # we want it true so we can get more test coverage
            '-Dlibcurl_compat=true',
            '-Doptimization=g',
            '-Dprefix=/usr',
            '-Dpressure_vessel=true',
            '-Dwarning_level=3',
            '-Dwerror=true',
        ]

        asan_dev_build = dev_build + [
            '-Db_lundef=false',
            '-Db_sanitize=address,undefined',
        ]

        self.setup_one(
            'host',
            asan_dev_build + [
                ('-Dtest_containers_dir='
                 + str(self.abs_builddir_parent / 'containers')),
            ] + args,
        )

        self.setup_one(
            'arm64',
            dev_build + [
                '-Dintrospection=disabled',
                '-Dmultiarch_tuple=aarch64-linux-gnu',
                '--cross-file=build-aux/meson/arm64.txt',
            ] + args,
            # Host system doesn't necessarily have an arm64 toolchain
            check=False,
        )

        self.setup_one(
            'i386',
            asan_dev_build + [
                '-Dmultiarch_tuple=i386-linux-gnu',
                '--cross-file=build-aux/meson/i386.txt',
                '--libdir=lib/i386-linux-gnu',
            ] + args,
            # Host system doesn't necessarily have an i386 toolchain
            check=False,
        )

        self.setup_one(
            'host-no-asan',
            dev_build + [
                ('-Dtest_containers_dir='
                 + str(self.abs_builddir_parent / 'containers')),
            ] + args,
        )

        self.setup_one(
            'coverage',
            dev_build + [
                '-Db_coverage=true',
            ] + args,
        )

        self.setup_one(
            'doc',
            [
                '-Dgtk_doc=enabled',
                '-Dman=enabled',
                '-Dpressure_vessel=true',
            ] + args,
        )

        self.setup_one(
            'clang',
            asan_dev_build + [
                '--native-file=build-aux/meson/clang.txt',
                # Workaround for
                # https://github.com/mesonbuild/meson/issues/13211
                '-Dintrospection=disabled',
            ] + args,
        )

        for suite, image in self.oci_images.items():
            if suite == 'scout' or not image:
                continue

            self.setup_one(
                f'{suite}-x86_64',
                dev_build + ['-Dwarning_level=2'] + args,
                in_suite=suite,
            )

        self.run_scout_builds('setup', args)

    def clean(self, args: List[str]) -> None:
        for builddir in ('clang', 'host', 'coverage', 'doc', 'host-no-asan'):
            subprocess.run(
                [
                    'ninja',
                    '-C', str(self.builddir_parent / builddir),
                    'clean',
                ] + args,
                check=True,
            )

        for builddir in ('i386',):
            subprocess.run(
                [
                    'ninja',
                    '-C', str(self.builddir_parent / builddir),
                    'clean',
                ] + args,
                check=False,
            )

        for suite, image in self.oci_images.items():
            if suite == 'scout' or not image:
                continue

            self.run_in_suite(
                suite,
                [
                    'ninja',
                    '-C', str(self.abs_builddir_parent / f'{suite}-x86_64'),
                    'clean',
                ] + args,
            )

        self.run_scout_builds('clean', args)

    def build(self, args: List[str]) -> None:
        for builddir in ('host', 'clang'):
            subprocess.run(
                [
                    'ninja',
                    '-C', str(self.builddir_parent / builddir),
                ] + args,
                check=True,
            )

        for suite, image in self.oci_images.items():
            if suite == 'scout' or not image:
                continue

            self.run_in_suite(
                suite,
                [
                    'ninja',
                    '-C', str(self.abs_builddir_parent / f'{suite}-x86_64'),
                ] + args,
            )

        self.run_scout_builds('build', args)

    def test(self, args: List[str]) -> None:
        subprocess.run(
            [
                'meson', 'test',
                '-C', str(self.builddir_parent / 'clang'),
            ] + args,
            check=True,
        )

        for suite, image in self.oci_images.items():
            if suite == 'scout' or not image:
                continue

            self.run_in_suite(
                suite,
                [
                    'meson', 'test',
                    '-C', str(self.abs_builddir_parent / f'{suite}-x86_64'),
                ] + args,
            )

        self.run_scout_builds('test', args)

        # We need to set up the relocatable installation before we can
        # have full test coverage for the host build
        self.install([])

        artifacts = self.abs_builddir_parent / 'host-artifacts'

        with suppress(FileNotFoundError):
            shutil.rmtree(artifacts)

        subprocess.run(
            [
                'env',
                'AUTOPKGTEST_ARTIFACTS=' + str(artifacts),
                'meson', 'test',
                '-C', str(self.builddir_parent / 'host'),
            ] + args,
            check=True,
        )

    def install(self, args: List[str]) -> None:
        self.run_scout_builds('install', args)

        subprocess.run(
            [
                str(self.abs_srcdir / 'build-aux' / 'scout-layered.sh'),
                str(self.builddir_parent / 'scout-layered'),
            ],
            check=True,
        )

        pv = self.containers / 'pressure-vessel'

        with suppress(FileNotFoundError):
            shutil.rmtree(pv)

        shutil.copytree(self.builddir_parent / 'scout-relocatable', pv)
        print('To upload to a test machine:')
        print(
            'rsync -avzP --delete {}/ '
            'machine:tmp/steam-runtime-tools-tests/'.format(
                self.builddir_parent
                / 'scout-DESTDIR/usr/libexec/installed-tests'
                / 'steam-runtime-tools-0'
            )
        )
        print(
            'rsync -avzP --delete {}/ '
            'machine:.../steamapps/common/'
            'SteamLinuxRuntime_soldier/pressure-vessel/'.format(pv)
        )
        print(
            'rsync -avzP --delete '
            '{}/scout-layered/SteamLinuxRuntime/ '
            'machine:.../steamapps/common/SteamLinuxRuntime/'.format(
                self.builddir_parent,
            )
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--builddir-parent', default='_build')
    parser.add_argument('--docker', action='store_true', default=False)
    parser.add_argument('--podman', action='store_true', default=False)
    parser.add_argument('--srcdir', default='.')
    parser.add_argument(
        'command',
        choices=(
            'deps',
            'setup',
            'clean',
            'build',
            'test',
            'install',
            'all',
        ),
    )
    parser.add_argument('args', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    env = Environment(
        builddir_parent=args.builddir_parent,
        docker=args.docker,
        podman=args.podman,
        srcdir=args.srcdir,
    )

    if args.command == 'deps':
        env.deps(args.args)
    elif args.command == 'setup':
        env.setup(args.args)
    elif args.command == 'clean':
        env.clean(args.args)
    elif args.command == 'build':
        env.build(args.args)
    elif args.command == 'test':
        env.test(args.args)
    elif args.command == 'install':
        env.install(args.args)
    elif args.command == 'all':
        env.test(args.args)
        env.install(args.args)
    else:
        raise AssertionError

    return 0


if __name__ == '__main__':
    sys.exit(main())

# vim:set sw=4 sts=4 et:
