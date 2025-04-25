#!/usr/bin/env python3

# Copyright © 2019-2022 Collabora Ltd.
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
Build the steam-container-runtime (aka SteamLinuxRuntime) depot, either
from just-built files or by downloading a previous build.

The oldest distribution we are currently testing with the CI is Ubuntu
18.04, that is shipping with Python 3.6.5.
In order to keep the compatibility with Ubuntu 18.04, this Python script
should not require a Python version newer than the 3.6.
"""

import argparse
import errno
import gzip
import hashlib
import json
import logging
import os
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
import urllib.parse
import urllib.request
from contextlib import suppress
from pathlib import Path
from typing import (
    Any,
    Dict,
    List,
    Optional,
    Sequence,
    Set,
    TextIO,
    Tuple,
)

from debian.deb822 import (
    Sources,
)


HERE = Path(__file__).resolve().parent


logger = logging.getLogger('populate-depot')


DEFAULT_PRESSURE_VESSEL_URI = (
    'https://repo.steampowered.com/pressure-vessel/snapshots'
)

DEFAULT_IMAGES_URI = (
    'https://repo.steampowered.com/steamrt-images-SUITE/snapshots'
)

SUITES = {
    'scout': 1,
    'soldier': 2,
    'sniper': 3,
    'medic': 4,
}


class InvocationError(Exception):
    pass


class PressureVesselRelease:
    def __init__(
        self,
        *,
        cache: str = '.cache',
        ssh_host: str = '',
        ssh_path: str = '',
        uri: str = DEFAULT_PRESSURE_VESSEL_URI,
        version: str = ''
    ) -> None:
        self.cache = cache
        self.pinned_version: Optional[str] = None
        self.ssh_host = ssh_host
        self.ssh_path = ssh_path
        self.uri = uri
        self.version = version

    def get_uri(
        self,
        filename: str,
        version: Optional[str] = None,
    ) -> str:
        uri = self.uri
        v = version or self.pinned_version or self.version or 'latest'
        return f'{uri}/{v}/{filename}'

    def get_ssh_path(
        self,
        filename: str,
        version: Optional[str] = None,
    ) -> str:
        ssh_host = self.ssh_host
        ssh_path = self.ssh_path
        v = version or self.pinned_version or self.version or 'latest'

        if not ssh_host or not ssh_path:
            raise RuntimeError('ssh host/path not configured')

        return f'{ssh_path}/{v}/{filename}'

    def fetch(
        self,
        filename: str,
        opener: urllib.request.OpenerDirector,
        version: Optional[str] = None,
    ) -> str:
        dest = os.path.join(self.cache, filename)

        if self.ssh_host and self.ssh_path:
            path = self.get_ssh_path(filename)
            logger.info('Downloading %r...', path)
            subprocess.run([
                'rsync',
                '--archive',
                '--partial',
                '--progress',
                self.ssh_host + ':' + path,
                dest,
            ], check=True)
        else:
            uri = self.get_uri(filename)
            logger.info('Downloading %r...', uri)

            with opener.open(uri) as response:
                with open(dest + '.new', 'wb') as writer:
                    shutil.copyfileobj(response, writer)

                os.rename(dest + '.new', dest)

        return dest

    def pin_version(
        self,
        opener: urllib.request.OpenerDirector,
    ) -> str:
        pinned = self.pinned_version

        if pinned is None:
            if self.ssh_host and self.ssh_path:
                path = self.get_ssh_path(filename='VERSION.txt')
                logger.info('Determining version number from %r...', path)
                pinned = subprocess.run([
                    'ssh', self.ssh_host,
                    'cat {}'.format(shlex.quote(path)),
                ], stdout=subprocess.PIPE).stdout.decode('utf-8').strip()
            else:
                uri = self.get_uri(filename='VERSION.txt')
                logger.info('Determining version number from %r...', uri)
                with opener.open(uri) as response:
                    pinned = response.read().decode('utf-8').strip()

            self.pinned_version = pinned

        return pinned


class Runtime:
    def __init__(
        self,
        name,
        *,
        suite: str,

        architecture: str = 'amd64,i386',
        cache: str = '.cache',
        images_uri: str = DEFAULT_IMAGES_URI,
        official: bool = False,
        path: Optional[str] = None,
        ssh_host: str = '',
        ssh_path: str = '',
        version: str = '',
    ) -> None:
        self.architecture = architecture
        self.cache = cache
        self.images_uri = images_uri
        self.name = name
        self.official = official
        self.path = path
        self.suite = suite
        self.ssh_host = ssh_host
        self.ssh_path = ssh_path
        self.version = version
        self.pinned_version: Optional[str] = None
        self.sha256: Dict[str, str] = {}

        os.makedirs(self.cache, exist_ok=True)

        self.prefix = 'com.valvesoftware.SteamRuntime'
        self.platform = self.prefix + '.Platform'
        self.sdk = self.prefix + '.Sdk'
        self.tarball = '{}-{}-{}-runtime.tar.gz'.format(
            self.platform,
            self.architecture,
            self.suite,
        )
        self.dockerfile = '{}-{}-{}-sysroot.Dockerfile'.format(
            self.sdk,
            self.architecture,
            self.suite,
        )
        self.sdk_tarball = '{}-{}-{}-runtime.tar.gz'.format(
            self.sdk,
            self.architecture,
            self.suite,
        )
        self.debug_tarball = '{}-{}-{}-debug.tar.gz'.format(
            self.sdk,
            self.architecture,
            self.suite,
        )
        self.sysroot_tarball = '{}-{}-{}-sysroot.tar.gz'.format(
            self.sdk,
            self.architecture,
            self.suite,
        )
        self.build_id_file = '{}-{}-{}-buildid.txt'.format(
            self.platform,
            self.architecture,
            self.suite,
        )
        self.sdk_build_id_file = '{}-{}-{}-buildid.txt'.format(
            self.sdk,
            self.architecture,
            self.suite,
        )
        self.sources = '{}-{}-{}-sources.deb822.gz'.format(
            self.sdk,
            self.architecture,
            self.suite,
        )

    def get_archives(
        self,
        include_sdk_sysroot=False,
    ):
        archives = [self.tarball]

        if include_sdk_sysroot:
            archives.append(self.dockerfile)
            archives.append(self.sysroot_tarball)

        return archives

    def __str__(self) -> str:
        return self.name

    @classmethod
    def from_details(
        cls,
        name: str,
        details: Dict[str, Any],
        cache: str = '.cache',
        default_architecture: str = 'amd64,i386',
        default_suite: str = '',
        default_version: str = '',
        images_uri: str = DEFAULT_IMAGES_URI,
        ssh_host: str = '',
        ssh_path: str = '',
    ):
        return cls(
            name,
            architecture=details.get(
                'architecture', default_architecture,
            ),
            cache=cache,
            images_uri=images_uri,
            official=details.get('official', False),
            path=details.get('path', None),
            ssh_host=ssh_host,
            ssh_path=ssh_path,
            suite=details.get('suite', default_suite or name),
            version=details.get('version', default_version),
        )

    def get_uri(
        self,
        filename: str,
        version: Optional[str] = None,
    ) -> str:
        suite = self.suite
        uri = self.images_uri.replace('SUITE', suite)
        v = version or self.pinned_version or self.version or 'latest'
        return f'{uri}/{v}/{filename}'

    def get_ssh_path(
        self,
        filename: str,
        version: Optional[str] = None,
    ) -> str:
        ssh_host = self.ssh_host
        suite = self.suite
        ssh_path = self.ssh_path.replace('SUITE', suite)
        v = version or self.pinned_version or self.version or 'latest'

        if not ssh_host or not ssh_path:
            raise RuntimeError('ssh host/path not configured')

        return f'{ssh_path}/{v}/{filename}'

    def fetch(
        self,
        filename: str,
        opener: urllib.request.OpenerDirector,
        version: Optional[str] = None,
    ) -> str:
        dest = os.path.join(self.cache, filename)

        if filename in self.sha256:
            try:
                with open(dest, 'rb') as reader:
                    hasher = hashlib.sha256()

                    while True:
                        blob = reader.read(4096)

                        if not blob:
                            break

                        hasher.update(blob)

                    digest = hasher.hexdigest()
            except OSError:
                pass
            else:
                if digest == self.sha256[filename]:
                    logger.info('Using cached %r', dest)
                    return dest

        if self.ssh_host and self.ssh_path:
            path = self.get_ssh_path(filename)
            logger.info('Downloading %r...', path)
            subprocess.run([
                'rsync',
                '--archive',
                '--partial',
                '--progress',
                self.ssh_host + ':' + path,
                dest,
            ], check=True)
        else:
            uri = self.get_uri(filename)
            logger.info('Downloading %r...', uri)

            with opener.open(uri) as response:
                with open(dest + '.new', 'wb') as writer:
                    shutil.copyfileobj(response, writer)

                os.rename(dest + '.new', dest)

        return dest

    def pin_version(
        self,
        opener: urllib.request.OpenerDirector,
    ) -> str:
        pinned = self.pinned_version
        sha256: Dict[str, str] = {}

        if pinned is None:
            if self.ssh_host and self.ssh_path:
                path = self.get_ssh_path(filename='VERSION.txt')
                logger.info('Determining version number from %r...', path)
                pinned = subprocess.run([
                    'ssh', self.ssh_host,
                    'cat {}'.format(shlex.quote(path)),
                ], stdout=subprocess.PIPE).stdout.decode('utf-8').strip()

                path = self.get_ssh_path(filename='SHA256SUMS')

                sha256sums = subprocess.run([
                    'ssh', self.ssh_host,
                    'cat {}'.format(shlex.quote(path)),
                ], stdout=subprocess.PIPE).stdout
                assert sha256sums is not None

            else:
                uri = self.get_uri(filename='VERSION.txt')
                logger.info('Determining version number from %r...', uri)
                with opener.open(uri) as response:
                    pinned = response.read().decode('utf-8').strip()

                uri = self.get_uri(filename='SHA256SUMS')

                with opener.open(uri) as response:
                    sha256sums = response.read()

            for line in sha256sums.splitlines():
                sha256_bytes, name_bytes = line.split(maxsplit=1)
                name = name_bytes.decode('utf-8')

                if name.startswith('*'):
                    name = name[1:]

                sha256[name] = sha256_bytes.decode('ascii')

            self.sha256 = sha256
            self.pinned_version = pinned

        return pinned


RUN_IN_DIR_SOURCE = '''\
#!/bin/sh
# {source_for_generated_file}

set -eu

me="$(readlink -f "$0")"
here="${{me%/*}}"
me="${{me##*/}}"

dir={escaped_dir}
pressure_vessel="${{PRESSURE_VESSEL_PREFIX:-"${{here}}/pressure-vessel"}}"

export PRESSURE_VESSEL_COPY_RUNTIME=1
export PRESSURE_VESSEL_RUNTIME="${{dir}}"
unset PRESSURE_VESSEL_RUNTIME_ARCHIVE
export PRESSURE_VESSEL_RUNTIME_BASE="${{here}}"

if [ -z "${{PRESSURE_VESSEL_VARIABLE_DIR-}}" ]; then
    export PRESSURE_VESSEL_VARIABLE_DIR="${{here}}/var"
fi

exec "${{pressure_vessel}}/bin/pressure-vessel-unruntime" "$@"
'''


class ComponentVersion:
    def __init__(
        self,
        name: str = '',
        sort_weight: int = 0,
    ) -> None:
        self.name = name
        self.version = ''
        self.runtime = ''
        self.runtime_version = ''
        self.sort_weight = sort_weight
        self.comment = ''

    def __str__(self) -> str:
        ret = '{} version {!r}'.format(self.name, self.version)

        if self.runtime or self.runtime_version:
            ret = ret + ' (from {} version {})'.format(
                self.runtime or '(unknown runtime)',
                self.runtime_version or '(unknown)',
            )

        return ret

    def to_sort_key(self) -> Tuple[int, str]:
        return (self.sort_weight, self.to_tsv())

    def to_tsv(self) -> str:
        if self.comment:
            comment = '# ' + self.comment
        else:
            comment = ''

        return '\t'.join((
            self.name, self.version,
            self.runtime, self.runtime_version,
            comment,
        )) + '\n'


class Main:
    def __init__(
        self,
        architecture: str = 'amd64,i386',
        cache: str = '.cache',
        credential_envs: Sequence[str] = (),
        credential_hosts: Sequence[str] = (),
        depot: str = 'depot',
        depot_archive: str = '',
        depot_version: str = '',
        fast: bool = False,
        images_uri: str = DEFAULT_IMAGES_URI,
        include_sdk_sysroot: bool = False,
        layered: bool = False,
        mtree: bool = True,
        pressure_vessel_archive: str = '',
        pressure_vessel_from_runtime: str = '',
        pressure_vessel_from_runtime_json: str = '',
        pressure_vessel_guess: str = '',
        pressure_vessel_official: bool = False,
        pressure_vessel_ssh_host: str = '',
        pressure_vessel_ssh_path: str = '',
        pressure_vessel_uri: str = DEFAULT_PRESSURE_VESSEL_URI,
        pressure_vessel_version: str = '',
        runtime: str = 'scout',
        scripts_version: str = '',
        source_dir: str = str(HERE),
        ssh_host: str = '',
        ssh_path: str = '',
        steam_app_id: str = '',
        steam_depot_id: str = '',
        suite: str = '',
        toolmanifest: bool = False,
        unpack_ld_library_path: str = '',
        unpack_sources: Sequence[str] = (),
        unpack_sources_into: str = '.',
        version: str = '',
        versioned_directories: bool = False,
        **kwargs: Dict[str, Any],
    ) -> None:
        openers: List[urllib.request.BaseHandler] = []

        if not credential_hosts:
            credential_hosts = []
            host = urllib.parse.urlparse(images_uri).hostname

            if host is not None:
                credential_hosts.append(host)

        if credential_envs:
            password_manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()

            for cred in credential_envs:
                if ':' in cred:
                    username_env, password_env = cred.split(':', 1)
                    logger.info(
                        'Using username from $%s and password from $%s',
                        username_env, password_env)
                    username = os.environ[username_env]
                    password = os.environ[password_env]
                else:
                    logger.info(
                        'Using username and password from $%s', cred)
                    username, password = os.environ[cred].split(':', 1)

                for host in credential_hosts:
                    password_manager.add_password(
                        None,       # type: ignore
                        host,
                        username,
                        password,
                    )

            openers.append(
                urllib.request.HTTPBasicAuthHandler(password_manager)
            )

        self.opener = urllib.request.build_opener(*openers)

        self.cache = cache
        self.default_architecture = architecture
        self.default_suite = suite
        self.default_version = version
        self.depot = os.path.abspath(depot)
        self.depot_archive = depot_archive
        self.depot_version = depot_version
        self.fast = fast
        self.images_uri = images_uri
        self.include_sdk_sysroot = include_sdk_sysroot
        self.layered = layered
        self.mtree = mtree
        self.pressure_vessel_ssh_host = pressure_vessel_ssh_host or ssh_host
        self.pressure_vessel_ssh_path = pressure_vessel_ssh_path
        self.pressure_vessel_uri = pressure_vessel_uri
        self.scripts_version = scripts_version
        self.source_dir = source_dir
        self.ssh_host = ssh_host
        self.ssh_path = ssh_path
        self.steam_app_id = steam_app_id
        self.steam_depot_id = steam_depot_id
        self.toolmanifest = toolmanifest
        self.unpack_ld_library_path = unpack_ld_library_path
        self.unpack_sources = unpack_sources
        self.unpack_sources_into = unpack_sources_into
        self.versioned_directories = versioned_directories

        if (
            depot_archive
            and not depot_archive.endswith(('.tar.gz', '.tar.xz'))
        ):
            raise InvocationError(f'Unknown archive format: {depot_archive}')

        n_sources = 0

        for source in (
            pressure_vessel_archive,
            pressure_vessel_from_runtime,
            pressure_vessel_from_runtime_json,
            pressure_vessel_guess,
            pressure_vessel_version,
        ):
            if source:
                n_sources += 1

        if n_sources == 0:
            pressure_vessel_version = 'latest'
        elif n_sources > 1:
            raise RuntimeError(
                'Cannot combine more than one of '
                '--pressure-vessel, '
                '--pressure-vessel-archive, '
                '--pressure-vessel-from-runtime, '
                '--pressure-vessel-from-runtime-json and '
                '--pressure-vessel-version'
            )

        os.makedirs(self.cache, exist_ok=True)

        if '=' in runtime:
            name, rhs = runtime.split('=', 1)

            if rhs.startswith('{'):
                details = json.loads(rhs)
            else:
                with open(rhs, 'rb') as reader:
                    details = json.load(reader)
        else:
            name = runtime
            details = {}

        self.runtime = self.new_runtime(name, details)

        self.versions: List[ComponentVersion] = []

        if pressure_vessel_guess:
            if self.runtime.name == pressure_vessel_guess:
                pressure_vessel_from_runtime = pressure_vessel_guess
            elif pressure_vessel_guess.startswith('{'):
                pressure_vessel_from_runtime_json = pressure_vessel_guess
            elif os.path.isdir(pressure_vessel_guess):
                pressure_vessel_archive = os.path.join(
                    pressure_vessel_guess, 'pressure-vessel-bin.tar.gz',
                )
            elif (
                os.path.isfile(pressure_vessel_guess)
                and pressure_vessel_guess.endswith('.tar.gz')
            ):
                pressure_vessel_archive = pressure_vessel_guess
            else:
                pressure_vessel_from_runtime = pressure_vessel_guess

        self.pressure_vessel_runtime: Optional[Runtime] = None
        self.pressure_vessel_version = ''

        if pressure_vessel_version:
            self.pressure_vessel_version = pressure_vessel_version
        elif pressure_vessel_archive:
            self.pressure_vessel_runtime = self.new_runtime(
                'scout',
                {
                    'official': pressure_vessel_official,
                    'path': pressure_vessel_archive,
                },
                default_suite='scout',
            )
        elif self.runtime.name == pressure_vessel_from_runtime:
            self.pressure_vessel_runtime = self.runtime
        elif pressure_vessel_from_runtime:
            self.pressure_vessel_runtime = self.new_runtime(
                pressure_vessel_from_runtime, {},
                default_suite=pressure_vessel_from_runtime,
            )
        elif pressure_vessel_from_runtime_json:
            self.pressure_vessel_runtime = self.new_runtime(
                'scout',
                json.loads(pressure_vessel_from_runtime_json),
                default_suite='scout',
            )

        if 'SOURCE_DATE_EPOCH' in os.environ:
            self.reference_timestamp = int(os.environ['SOURCE_DATE_EPOCH'])
        else:
            self.reference_timestamp = int(time.time())

    def new_runtime(
        self,
        name: str,
        details: Dict[str, Any],
        default_suite: str = '',
    ) -> Runtime:
        return Runtime.from_details(
            name,
            details,
            cache=self.cache,
            default_architecture=self.default_architecture,
            default_suite=default_suite or self.default_suite,
            default_version=self.default_version,
            images_uri=self.images_uri,
            ssh_host=self.ssh_host,
            ssh_path=self.ssh_path,
        )

    def merge_dir_into_depot(
        self,
        source_root: str,
    ):
        for (dirpath, dirnames, filenames) in os.walk(source_root):
            relative_path = os.path.relpath(dirpath, source_root)

            for member in dirnames:
                os.makedirs(
                    os.path.join(self.depot, relative_path, member),
                    exist_ok=True,
                )

            for member in filenames:
                source = os.path.join(dirpath, member)
                merged = os.path.join(self.depot, relative_path, member)

                with suppress(FileNotFoundError):
                    os.unlink(merged)

                os.makedirs(os.path.dirname(merged), exist_ok=True)
                shutil.copy(source, merged)

    def run(self) -> None:
        if self.layered:
            self.do_layered_runtime()
        else:
            self.do_container_runtime()

        if self.mtree:
            self.write_top_level_mtree()

        if self.steam_app_id and self.steam_depot_id:
            self.write_steampipe_config()

        if self.depot_archive:
            self.do_depot_archive(self.depot_archive)

    def do_layered_runtime(self) -> None:
        if self.runtime.name != 'scout':
            raise InvocationError('Can only layer scout onto soldier')

        if self.unpack_ld_library_path:
            raise InvocationError(
                'Cannot use --unpack-ld-library-path with --layered'
            )

        if self.include_sdk_sysroot:
            raise InvocationError(
                'Cannot use --include-sdk-* with --layered'
            )

        if self.unpack_sources:
            raise InvocationError(
                'Cannot use --unpack-source with --layered'
            )

        self.merge_dir_into_depot(
            os.path.join(self.source_dir, 'runtimes', 'scout-on-soldier')
        )

        if self.runtime.version:
            self.unpack_ld_library_path = self.depot
            self.download_scout_tarball(self.runtime)
            local_version = ComponentVersion('LD_LIBRARY_PATH')
            version = self.runtime.pinned_version
            assert version is not None
            local_version.version = version
            local_version.runtime = 'scout'
            local_version.runtime_version = version
            local_version.comment = 'steam-runtime/'
            self.versions.append(local_version)
        else:
            unspecified_version = ComponentVersion('LD_LIBRARY_PATH')
            unspecified_version.version = '-'
            unspecified_version.runtime = 'scout'
            unspecified_version.runtime_version = '-'
            unspecified_version.comment = (
                'see ~/.steam/root/ubuntu12_32/steam-runtime/version.txt'
            )
            self.versions.append(unspecified_version)

        self.write_component_versions()

    def prune_runtime(self, directory: Path) -> None:
        """
        Remove files that are considered to be unnecessary
        """

        usr_share = directory / 'files' / 'share'
        doc = usr_share / 'doc'

        # This is a fairly generic list of files that are safe to be removed.
        # Please keep it in sync with prune_files() of steam-runtime.git's
        # build-runtime.py
        paths: list[Path] = [
            # Nvidia cg toolkit manuals, tutorials and documentation
            doc / 'nvidia-cg-toolkit' / 'html',
            *doc.glob('nvidia-cg-toolkit/*.pdf.gz'),
            # Sample code
            *doc.glob('**/examples'),
            # Debian bug reporting scripts
            usr_share / 'bug',
            # Debian documentation metadata
            usr_share / 'doc-base',
            # Debian QA metadata
            usr_share / 'lintian',
            # Programs and utilities manuals
            usr_share / 'man',
            # Remove the localized messages that are likely never going to be
            # seen. Keep only "en", because that's the default language we are
            # using.
            *[x for x in usr_share.glob('locale/*') if x.name != 'en'],
        ]

        if self.runtime.suite not in ('scout', 'soldier', 'sniper'):
            paths.extend(directory.glob('files/lib/*/dri'))

        for path in paths:
            if path.is_dir():
                shutil.rmtree(path)
            else:
                with suppress(FileNotFoundError):
                    path.unlink()

    def do_container_runtime(self) -> None:
        pv_version = ComponentVersion('pressure-vessel')

        self.merge_dir_into_depot(os.path.join(self.source_dir, 'common'))

        root = os.path.join(self.source_dir, 'runtimes', self.runtime.name)

        if os.path.exists(root):
            self.merge_dir_into_depot(root)

        pressure_vessel_runtime = self.pressure_vessel_runtime

        if self.pressure_vessel_version:
            logger.info(
                'Downloading standalone pressure-vessel release'
            )
            pv_version.version = self.download_pressure_vessel_standalone(
                self.pressure_vessel_version
            )
        else:
            assert pressure_vessel_runtime is not None

            if pressure_vessel_runtime.path:
                self.use_local_pressure_vessel(pressure_vessel_runtime.path)

                if pressure_vessel_runtime.official:
                    pv_version.comment = 'pressure-vessel-bin.tar.gz'
                else:
                    pv_version.comment = 'from local file'
            else:
                pv_version.comment = (
                    self.download_pressure_vessel_from_runtime(
                        pressure_vessel_runtime
                    )
                )

        for path in ('metadata/VERSION.txt', 'sources/VERSION.txt'):
            full = os.path.join(self.depot, 'pressure-vessel', path)
            if os.path.exists(full):
                with open(full) as text_reader:
                    v = text_reader.read().rstrip('\n')
                    if pv_version.version:
                        if pv_version.version != v:
                            raise RuntimeError(
                                'Inconsistent version! '
                                '{} says {}, but expected {}'.format(
                                    path, v, pv_version.version,
                                )
                            )
                    else:
                        pv_version.version = v

                break

        if pressure_vessel_runtime is not None:
            pv_version.runtime = pressure_vessel_runtime.suite or ''
            pv_version.runtime_version = (
                pressure_vessel_runtime.pinned_version or ''
            )

        self.versions.append(pv_version)

        if self.unpack_ld_library_path and pressure_vessel_runtime is None:
            if self.runtime.name == 'scout':
                scout = self.runtime
            else:
                scout = self.new_runtime(
                    'scout',
                    dict(version='latest'),
                    default_suite='scout',
                )
            logger.info(
                'Downloading LD_LIBRARY_PATH Steam Runtime from scout into %r',
                self.unpack_ld_library_path)
            self.download_scout_tarball(scout)
        elif self.unpack_ld_library_path:
            assert pressure_vessel_runtime is not None
            logger.info(
                'Downloading LD_LIBRARY_PATH Steam Runtime from same place '
                'as pressure-vessel into %r',
                self.unpack_ld_library_path)
            self.download_scout_tarball(pressure_vessel_runtime)

        if self.unpack_sources:
            logger.info(
                'Will download %s source code into %r',
                ', '.join(self.unpack_sources), self.unpack_sources_into)
            os.makedirs(self.unpack_sources_into, exist_ok=True)

            os.makedirs(
                os.path.join(self.unpack_sources_into, self.runtime.name),
                exist_ok=True,
            )

        runtime = self.runtime

        if runtime.path:
            logger.info(
                'Using runtime from local directory %r',
                runtime.path)
            self.use_local_runtime(runtime)
        else:
            logger.info(
                'Downloading runtime from %s',
                runtime)
            self.download_runtime(runtime)

        component_version = ComponentVersion(runtime.name)

        if runtime.path:
            with open(
                os.path.join(runtime.path, runtime.build_id_file), 'r',
            ) as text_reader:
                version = text_reader.read().strip()
        else:
            version = runtime.pinned_version or ''
            assert version

        runtime_files = set()

        if self.versioned_directories:
            subdir = '{}_platform_{}'.format(runtime.name, version)
        else:
            subdir = runtime.name

        dest = os.path.join(self.depot, subdir)
        runtime_files.add(subdir + '/')

        with suppress(FileNotFoundError):
            shutil.rmtree(dest)

        os.makedirs(dest, exist_ok=True)
        argv = [
            'tar',
            '-C', dest,
            '-xf',
            os.path.join(self.cache, runtime.tarball),
        ]
        logger.info('%r', argv)
        subprocess.run(argv, check=True)
        self.minimize_runtime(dest)

        if self.include_sdk_sysroot:
            if self.versioned_directories:
                sysroot_subdir = '{}_sysroot_{}'.format(
                    runtime.name, version,
                )
            else:
                sysroot_subdir = '{}_sysroot'.format(runtime.name)

            sysroot = os.path.join(self.depot, sysroot_subdir)
            runtime_files.add(sysroot_subdir + '/')

            with suppress(FileNotFoundError):
                shutil.rmtree(sysroot)

            os.makedirs(os.path.join(sysroot, 'files'), exist_ok=True)
            argv = [
                'tar',
                '-C', os.path.join(sysroot, 'files'),
                '--exclude', 'dev/*',
                '-xf',
                os.path.join(self.cache, runtime.sysroot_tarball),
            ]
            logger.info('%r', argv)
            subprocess.run(argv, check=True)

            os.makedirs(
                os.path.join(
                    sysroot, 'files', 'usr', 'lib', 'debug',
                ),
                exist_ok=True,
            )

        with open(os.path.join(self.depot, 'run'), 'w') as writer:
            writer.write(
                RUN_IN_DIR_SOURCE.format(
                    escaped_dir=shlex.quote(subdir),
                    source_for_generated_file=(
                        'Generated file, do not edit'
                    ),
                )
            )

        os.chmod(os.path.join(self.depot, 'run'), 0o755)

        if runtime.name in ('scout', 'soldier', 'sniper'):
            shutil.copy2(
                os.path.join(self.depot, 'run'),
                os.path.join(self.depot, 'run-in-' + runtime.name),
            )
            os.chmod(os.path.join(self.depot, 'run-in-' + runtime.name), 0o755)

        comment = ', '.join(sorted(runtime_files))

        if runtime.path and not runtime.official:
            comment += ' (from local build)'

        component_version.version = version
        component_version.runtime = runtime.suite
        component_version.runtime_version = version
        component_version.comment = comment
        self.versions.append(component_version)

        if self.toolmanifest:

            with open(
                os.path.join(self.depot, 'toolmanifest.vdf'), 'w'
            ) as writer:
                import vdf      # noqa

                writer.write('// Generated file, do not edit\n')
                words = [
                    '/_v2-entry-point',
                    '--verb=%verb%',
                    '--',
                ]

                # Each ABI level needs a unique "priority" so that we will
                # not suggest SLR 2.0 as a runtime for SLR 3.0 games, etc.;
                # and they all need to be less than 10, to avoid colliding
                # with SLR 1.0 (scout), which needs to be highest-priority
                # to make it the default for legacy games.
                filter_exclusive_priority = '9'

                if runtime.suite.startswith('steamrt'):
                    # steamrt5, steamrt6, ... get priority 5, 6, ...
                    # If we get to steamrt9 with this limitation still
                    # present, we'll have to bump the priority of SLR 1.0
                    # or teach the Steam Client a different mechanism.
                    major_version = runtime.suite[len('steamrt'):]
                    assert int(major_version) >= 5, major_version
                    assert int(major_version) <= 9, major_version
                    filter_exclusive_priority = major_version
                elif runtime.suite == 'scout':
                    # Hypothetically we might have a super-strict scout
                    # runtime for QA purposes (steamrt/tasks#59) and if
                    # we do, it must match SLR 1.0
                    filter_exclusive_priority = '10'
                else:
                    # For older suites with whimsical codenames, we have a
                    # mapping from codename to major version.
                    # Set the priority equal to the major version.
                    filter_exclusive_priority = str(
                        SUITES.get(runtime.suite, 9)
                    )

                content: Dict[str, Any] = dict(
                    manifest=dict(
                        commandline=' '.join(words),
                        filter_exclusive_priority=filter_exclusive_priority,
                        version='2',
                        use_tool_subprocess_reaper='1',
                    )
                )

                content['manifest']['compatmanager_layer_name'] = (
                    'container-runtime'
                )

                vdf.dump(content, writer, pretty=True, escaped=True)

        self.write_component_versions()

    def write_component_versions(self) -> None:
        try:
            with subprocess.Popen(
                [
                    'git', 'describe',
                    '--always',
                    '--dirty',
                    '--long',
                ],
                cwd=os.path.dirname(__file__),
                stdout=subprocess.PIPE,
                universal_newlines=True,
            ) as describe:
                stdout = describe.stdout
                assert stdout is not None
                version = stdout.read().strip()
                # Deliberately ignoring exit status:
                # if git is missing or old we'll use 'unknown'
        except (OSError, subprocess.SubprocessError):
            version = ''

        try:
            with open(HERE / '.tarball-version', 'r') as reader:
                version = reader.read().strip()
        except OSError:
            pass

        if self.scripts_version:
            version = self.scripts_version

        if self.depot_version:
            component_version = ComponentVersion('depot', sort_weight=-1)
            component_version.version = self.depot_version
            component_version.comment = 'Overall version number'
            self.versions.append(component_version)

        component_version = ComponentVersion('scripts')
        component_version.version = version or 'unknown'
        component_version.comment = 'from steam-runtime-tools'
        self.versions.append(component_version)

        with open(os.path.join(self.depot, 'VERSIONS.txt'), 'w') as writer:
            writer.write(
                '#Name\tVersion\t\tRuntime\tRuntime_Version\tComment\n'
            )

            for entry in sorted(self.versions, key=lambda v: v.to_sort_key()):
                logger.info('Component version: %s', entry)
                writer.write(entry.to_tsv())

    def use_local_pressure_vessel(self, path: str = '.') -> None:
        pv_dir = os.path.join(self.depot, 'pressure-vessel')
        os.makedirs(pv_dir, exist_ok=True)
        argv = ['tar', '-C', pv_dir, '--strip-components=1', '-xf']

        if os.path.isfile(path):
            argv.append(path)
        else:
            argv.append(os.path.join(path, 'pressure-vessel-bin.tar.gz'))

        logger.info('%r', argv)
        subprocess.run(argv, check=True)

    def download_pressure_vessel_standalone(
        self,
        version: str,
    ) -> str:
        pv = PressureVesselRelease(
            cache=self.cache,
            ssh_host=self.pressure_vessel_ssh_host,
            ssh_path=self.pressure_vessel_ssh_path,
            uri=self.pressure_vessel_uri,
            version=version,
        )
        pinned = pv.pin_version(self.opener)
        self.use_local_pressure_vessel(
            pv.fetch('pressure-vessel-bin.tar.gz', self.opener, pinned)
        )
        return pinned

    def download_pressure_vessel_from_runtime(self, runtime: Runtime) -> str:
        filename = 'pressure-vessel-bin.tar.gz'
        runtime.pin_version(self.opener)

        downloaded = runtime.fetch(
            filename,
            self.opener,
        )

        os.makedirs(self.depot, exist_ok=True)
        subprocess.run(
            [
                'tar', '-C', self.depot, '-xf', downloaded,
            ],
            check=True,
        )

        return filename

    def use_local_runtime(self, runtime: Runtime) -> None:
        assert runtime.path

        for basename in runtime.get_archives(
            include_sdk_sysroot=self.include_sdk_sysroot,
        ):
            src = os.path.join(runtime.path, basename)
            dest = os.path.join(self.cache, basename)
            logger.info('Hard-linking local runtime %r to %r', src, dest)

            with suppress(FileNotFoundError):
                os.unlink(dest)

            os.link(src, dest)

        if self.unpack_sources:
            with open(
                os.path.join(runtime.path, runtime.sources), 'rb',
            ) as reader:
                for stanza in Sources.iter_paragraphs(
                    sequence=reader,
                    use_apt_pkg=True,
                ):
                    if stanza['package'] in self.unpack_sources:
                        for f in stanza['files']:
                            name = f['name']

                            if name.endswith('.dsc'):
                                dest = os.path.join(
                                    self.unpack_sources_into,
                                    runtime.name,
                                    stanza['package'],
                                )

                                with suppress(FileNotFoundError):
                                    logger.info('Removing %r', dest)
                                    shutil.rmtree(dest)

                                subprocess.run(
                                    [
                                        'dpkg-source',
                                        '-x',
                                        dest,
                                        os.path.join(
                                            runtime.path,
                                            'sources',
                                            f['name'],
                                        ),
                                    ],
                                    check=True,
                                )

    def download_runtime(self, runtime: Runtime) -> None:
        """
        Download a pre-prepared Platform from a previous container
        runtime build.
        """

        runtime.pin_version(self.opener)

        for basename in runtime.get_archives(
            include_sdk_sysroot=self.include_sdk_sysroot,
        ):
            downloaded = runtime.fetch(basename, self.opener)

        if self.unpack_sources:
            with tempfile.TemporaryDirectory(prefix='populate-depot.') as tmp:
                want = set(self.unpack_sources)
                downloaded = runtime.fetch(
                    runtime.sources,
                    self.opener,
                )
                with open(downloaded, 'rb') as reader:
                    for stanza in Sources.iter_paragraphs(
                        sequence=reader,
                        use_apt_pkg=True,
                    ):
                        if stanza['package'] in self.unpack_sources:
                            logger.info(
                                'Found %s in %s',
                                stanza['package'], runtime.name,
                            )
                            want.discard(stanza['package'])
                            os.makedirs(
                                os.path.join(self.cache or tmp, 'sources'),
                                exist_ok=True,
                            )

                            file_path: Dict[str, str] = {}

                            for f in stanza['files']:
                                name = f['name']
                                file_path[name] = runtime.fetch(
                                    os.path.join('sources', name),
                                    self.opener,
                                )

                            for f in stanza['files']:
                                name = f['name']

                                if name.endswith('.dsc'):
                                    dest = os.path.join(
                                        self.unpack_sources_into,
                                        runtime.name,
                                        stanza['package'],
                                    )

                                    with suppress(FileNotFoundError):
                                        logger.info('Removing %r', dest)
                                        shutil.rmtree(dest)

                                    subprocess.run(
                                        [
                                            'dpkg-source',
                                            '-x',
                                            file_path[name],
                                            dest,
                                        ],
                                        check=True,
                                    )

                if want:
                    logger.warning(
                        'Did not find source package(s) %s in %s',
                        ', '.join(want), runtime.name,
                    )

    def download_scout_tarball(self, runtime: Runtime) -> None:
        """
        Download a pre-prepared LD_LIBRARY_PATH Steam Runtime from a
        previous scout build.
        """
        filename = 'steam-runtime.tar.xz'

        pinned = runtime.pin_version(self.opener)
        logger.info('Downloading steam-runtime build %s', pinned)
        os.makedirs(self.unpack_ld_library_path, exist_ok=True)

        downloaded = runtime.fetch(
            filename,
            self.opener,
        )
        subprocess.run(
            [
                'tar', '-C', self.unpack_ld_library_path, '-xf',
                downloaded,
            ],
            check=True,
        )

    def octal_escape_char(self, match: 're.Match') -> str:
        ret: List[str] = []

        for byte in match.group(0).encode('utf-8', 'surrogateescape'):
            ret.append('\\%03o' % byte)

        return ''.join(ret)

    _NEEDS_OCTAL_ESCAPE = re.compile(r'[^-A-Za-z0-9+,./:@_]')

    def octal_escape(self, s: str) -> str:
        return self._NEEDS_OCTAL_ESCAPE.sub(self.octal_escape_char, s)

    def filename_is_friendly(self, s: str) -> bool:
        '''
        Return true if the filename is non-problematic for Windows
        filesystems, Steampipe, Unix shells and so on.
        '''

        # Some relevant restrictions:
        #
        # * Windows and Steampipe don't allow <>:"\|?*
        # * Windows doesn't allow surrogate escapes U+DC80 to U+DCFF
        # * #$&'()[]{};`~ are special to Unix shells in general
        # * !^ are special to interactive Unix shells
        # * % is special to Windows shells
        # * , is special to the Steam bootstrapper
        # * whitespace is awkward and not necessarily handled consistently
        # * ASCII control characters are not necessarily handled consistently
        # * non-ASCII is not necessarily handled consistently

        for c in s:
            if c >= 'A' and c <= 'Z':
                continue
            elif c >= 'a' and c <= 'z':
                continue
            elif c >= '0' and c <= '9':
                continue
            elif c not in '+-./=@_':
                return False

        # Unix command-line tools can get confused by basenames starting
        # with a dash
        if s.startswith('-') or '/-' in s:
            return False

        # Also avoid filenames like __pycache__/*.pyc, which might otherwise
        # be deleted by "helpful" file cleaning tools
        if (
            '/.cache/' in s
            or '/__pycache__/' in s
            or '/tmp/' in s
            or s.endswith((
                '.pyc',
                '.pyo',
                'CACHEDIR.TAG',
            ))
        ):
            return False

        return True

    def write_mtree(
        self,
        top: Path,
        writer: TextIO,
        *,
        minimize: bool = False,
        preserve_mode: bool = True,
        preserve_time: bool = True,
        skip_runtime_files: bool = False
    ) -> Dict[str, str]:
        lc_names: Dict[str, str] = {}
        # { truncated hash: number of distinct files with this hash }
        # Note that even if the content is identical, there can be
        # more than one unique set of permissions for the same content.
        hashed_names_used: Dict[str, int] = {}
        # { new name: old name }
        rename: Dict[str, str] = {}
        unlink_later: Set[str] = set()
        differ_only_by_case: Set[str] = set()
        unfriendly_filenames: Set[str] = set()
        # { [device, inode]: hex sha256 }
        sha256: Dict[Tuple[int, int], str] = {}
        # { [device, inode]: hashed name }
        hashed_names: Dict[Tuple[int, int], str] = {}

        writer.write('#mtree\n')
        writer.write('. type=dir\n')

        for dirpath, dirnames, filenames in os.walk(top):
            for base in sorted(dirnames + filenames):
                member = Path(dirpath) / base
                name = str(member.relative_to(top))

                if name == 'steampipe' and base in dirnames:
                    dirnames.remove(base)
                    continue

                if (
                    skip_runtime_files
                    and base == 'files'
                    and base in dirnames
                    and (member.parent / 'usr-mtree.txt.gz').exists()
                ):
                    escaped = self.octal_escape(name)
                    writer.write(f'./{escaped} type=dir ignore\n')
                    dirnames.remove(base)
                    continue

                if not self.filename_is_friendly(name):
                    unfriendly_filenames.add(name)

                if name.lower() in lc_names:
                    differ_only_by_case.add(lc_names[name.lower()])
                    differ_only_by_case.add(name)
                else:
                    lc_names[name.lower()] = name

                fields = ['./' + self.octal_escape(name)]

                stat_info = os.lstat(member)

                if stat.S_ISREG(stat_info.st_mode):
                    fields.append('type=file')

                    if preserve_mode:
                        fields.append('mode=%o' % (stat_info.st_mode & 0o777))
                    elif stat_info.st_mode & 0o111:
                        fields.append('mode=755')

                    if preserve_time:
                        # With sub-second precision, note that some versions
                        # of mtree use the part after the dot as integer
                        # nanoseconds, so "1.234" is actually 1 sec + 234 ns,
                        # or what normal people would write as 1.000000234.
                        # To be compatible with both, we always show the time
                        # with 9 digits after the decimal point, unless it's
                        # exactly an integer.
                        if stat_info.st_mtime == int(stat_info.st_mtime):
                            fields.append(f'time={stat_info.st_mtime:.1f}')
                        else:
                            fields.append(f'time={stat_info.st_mtime:.9f}')

                    fields.append(f'size={stat_info.st_size}')
                    file_id = (stat_info.st_dev, stat_info.st_ino)
                    if stat_info.st_size > 0:
                        if file_id in sha256:
                            digest = sha256[file_id]
                        else:
                            hasher = hashlib.sha256()
                            with open(member, 'rb') as f:
                                while True:
                                    blob = f.read(4096)

                                    if not blob:
                                        break

                                    hasher.update(blob)

                            digest = hasher.hexdigest()
                            sha256[file_id] = digest

                        short_hash = digest[:2] + '/' + digest[2:8]
                        fields.append(f'sha256={digest}')

                        if minimize and (
                            stat_info.st_nlink > 1
                            or name in differ_only_by_case
                            or name in unfriendly_filenames
                        ):
                            # Represent hard-linked files or problematic
                            # filenames by a semi-content-addressed name.
                            if file_id in hashed_names:
                                hashed_name = hashed_names[file_id]
                            else:
                                index = hashed_names_used.get(short_hash, 0)
                                index += 1
                                hashed_names_used[short_hash] = index
                                hashed_name = f'{short_hash}-{index}.bin'
                                hashed_names[file_id] = hashed_name

                            fields.append(f'contents=./{hashed_name}')
                            rename[hashed_name] = name
                            unlink_later.add(name)
                        # else represent ./foo/bar as ./foo/bar
                    elif minimize:
                        unlink_later.add(name)

                elif stat.S_ISLNK(stat_info.st_mode):
                    fields.append('type=link')
                    fields.append(
                        f'link={self.octal_escape(os.readlink(member))}')

                    if minimize:
                        unlink_later.add(name)

                elif stat.S_ISDIR(stat_info.st_mode):
                    fields.append('type=dir')
                else:
                    writer.write(
                        '# unknown file type: {}\n'.format(
                            self.octal_escape(name),
                        ),
                    )
                    continue

                writer.write(' '.join(fields) + '\n')

            if differ_only_by_case and not minimize:
                writer.write('\n')
                writer.write('# Files whose names differ only by case:\n')

                for name in sorted(differ_only_by_case):
                    writer.write('# {}\n'.format(self.octal_escape(name)))

            if unfriendly_filenames and not minimize:
                writer.write('\n')
                writer.write('# Files with unfriendly names:\n')

                for name in sorted(unfriendly_filenames):
                    writer.write('# {}\n'.format(self.octal_escape(name)))

        if minimize:
            for name, original in rename.items():
                (top / name).parent.mkdir(parents=True, exist_ok=True)
                (top / original).replace(top / name)

            for name in unlink_later:
                with suppress(FileNotFoundError):
                    (top / name).unlink()
        else:
            assert not rename, rename
            assert not unlink_later, unlink_later

        return lc_names

    def write_top_level_mtree(self) -> None:
        with tempfile.TemporaryDirectory(prefix='slr-mtree-') as temp:
            writer = gzip.open(os.path.join(temp, 'mtree.txt.gz'), 'wt')

            lc_names = self.write_mtree(
                Path(self.depot),
                writer,
                minimize=False,
                preserve_mode=False,
                preserve_time=False,
                skip_runtime_files=True,
            )

            if '.ref' not in lc_names:
                writer.write('./.ref type=file size=0 optional\n')

            if (
                self.steam_app_id
                and self.steam_depot_id
                and 'steampipe' not in lc_names
            ):
                writer.write('./steampipe type=dir ignore optional\n')

            if 'var' not in lc_names:
                writer.write('./var type=dir ignore optional\n')

            writer.write('./mtree.txt.gz type=file\n')
            writer.close()
            shutil.copy2(writer.name, self.depot)

    def minimize_runtime(self, root: str) -> None:
        '''
        Convert $root from an ordinary runtime into a minimized runtime
        described by a mtree manifest usr-mtree.txt.gz, which
        pressure-vessel can reconstitute back into the original runtime.
        '''

        # Remove unnecessary files
        self.prune_runtime(Path(root))

        # Generate the manifest
        with tempfile.TemporaryDirectory(prefix='slr-mtree-') as temp:
            writer = gzip.open(os.path.join(temp, 'usr-mtree.txt.gz'), 'wt')

            lc_names = self.write_mtree(
                Path(root) / 'files',
                writer,
                minimize=True,
            )

            if '.ref' not in lc_names:
                writer.write('./.ref type=file size=0 mode=644\n')

            # We need to close the gzip before copying it, otherwise we
            # will end up with a corrupted file
            writer.close()
            shutil.copy2(writer.name, root)

        # Remove files that can be restored from the manifest
        for (dirpath, dirnames, filenames) in os.walk(
            os.path.join(root, 'files'),
            topdown=False,
        ):
            for f in filenames + dirnames:
                path = os.path.join(dirpath, f)

                try:
                    statinfo = os.lstat(path)
                except FileNotFoundError:
                    continue

                if stat.S_ISLNK(statinfo.st_mode) or statinfo.st_size == 0:
                    os.remove(path)
            try:
                os.rmdir(dirpath)
            except OSError as e:
                if e.errno != errno.ENOTEMPTY:
                    raise

        # Create $path/files/.ref as an empty regular file.
        #
        # This is useful because pressure-vessel would create this file
        # during processing. If it gets committed to the depot, then Steampipe
        # will remove it when superseded.

        ref = os.path.join(root, 'files', '.ref')

        try:
            statinfo = os.stat(ref, follow_symlinks=False)
        except FileNotFoundError:
            with open(ref, 'x'):
                pass
        else:
            if statinfo.st_size > 0 or not stat.S_ISREG(statinfo.st_mode):
                raise RuntimeError(
                    'Expected {} to be an empty regular file'.format(root)
                )

    def write_steampipe_config(self) -> None:
        import vdf                          # noqa
        from vdf.vdict import VDFDict       # noqa

        assert self.steam_app_id
        assert self.steam_depot_id

        depot = Path(self.depot)
        steampipe = depot / 'steampipe'
        steampipe.mkdir(exist_ok=True)
        app_vdf = f'app_build_{self.steam_app_id}.vdf'
        depot_vdf = f'depot_build_{self.steam_depot_id}.vdf'

        content = dict(
            appbuild=dict(
                appid=self.steam_app_id,
                buildoutput='output',
                depots={self.steam_depot_id: depot_vdf},
            )
        )

        with open(steampipe / app_vdf, 'w') as writer:
            vdf.dump(content, writer, pretty=True, escaped=True)

        file_mappings: List[Tuple[str, Any]] = []

        for child in sorted(depot.iterdir()):
            if child.name in ('steampipe', 'var'):
                continue
            elif child.is_dir():
                file_mappings.append(
                    (
                        'FileMapping', dict(
                            LocalPath=f'../{child.name}/*',
                            DepotPath=f'{child.name}/',
                            recursive='1',
                        ),
                    )
                )
            else:
                file_mappings.append(
                    (
                        'FileMapping', dict(
                            LocalPath='../' + str(child.relative_to(depot)),
                            DepotPath='.',
                        ),
                    )
                )

        content = dict(
            DepotBuildConfig=VDFDict(
                [('DepotID', self.steam_depot_id)] + file_mappings,
            )
        )

        with open(steampipe / depot_vdf, 'w') as writer:
            vdf.dump(content, writer, pretty=True, escaped=True)

    def do_depot_archive(self, name: str) -> None:
        if name.endswith('.tar.gz'):
            compress_command = ['pigz', '--fast', '-c', '-n', '--rsyncable']
            artifact_prefix = name[:-len('.tar.gz')]
        elif name.endswith('.tar.xz'):
            if self.fast:
                compress_command = ['xz', '-0']
            else:
                compress_command = ['xz']

            artifact_prefix = name[:-len('.tar.xz')]
        else:
            raise InvocationError(f'Unknown archive format: {name}')

        stem = Path(artifact_prefix).name

        with open(
            name, 'wb'
        ) as archive_writer, subprocess.Popen(
            compress_command,
            stdin=subprocess.PIPE,
            stdout=archive_writer,
        ) as compressor, tarfile.open(
            name,
            mode='w|',
            format=tarfile.GNU_FORMAT,
            fileobj=compressor.stdin,
        ) as archiver:
            members = []
            depot = Path(self.depot)

            for dir_path, dirs, files in os.walk(
                depot,
                topdown=True,
                followlinks=False,
            ):
                rel_dir = Path(dir_path).relative_to(depot)

                if rel_dir == Path('.'):
                    for exclude in ('var',):
                        try:
                            dirs.remove(exclude)
                        except ValueError:
                            pass

                for item in dirs + files:
                    members.append(rel_dir / item)

            root = tarfile.TarInfo(stem)
            root.size = 0
            root.type = tarfile.DIRTYPE
            root = self.normalize_tar_entry(root)
            archiver.addfile(root)

            for member in sorted(members):
                archiver.add(
                    str(depot / member),
                    arcname=f'{stem}/{member}',
                    recursive=False,
                    filter=self.normalize_tar_entry,
                )

        if not self.layered:
            with open(
                HERE / 'SteamLinuxRuntime_whatever.sh.in'
            ) as reader, open(
                artifact_prefix + '.sh', 'w'
            ) as writer:
                for line in reader:
                    writer.write(line.replace('@RUNTIME@', stem))

            shutil.copy(
                depot / 'VERSIONS.txt',
                artifact_prefix + '.VERSIONS.txt',
            )
            os.chmod(artifact_prefix + '.VERSIONS.txt', 0o644)
            os.chmod(artifact_prefix + '.sh', 0o755)

    def normalize_tar_entry(self, entry: tarfile.TarInfo) -> tarfile.TarInfo:
        entry.uid = 65534
        entry.gid = 65534

        if entry.mtime > self.reference_timestamp:
            entry.mtime = self.reference_timestamp

        if (entry.mode & 0o111) != 0 or entry.isdir():
            entry.mode = 0o755
        else:
            entry.mode = 0o644

        entry.uname = 'nobody'
        entry.gname = 'nogroup'

        return entry


class SelfTest(unittest.TestCase):
    def test_filename_is_friendly(self) -> None:
        m = Main()

        for good in (
            'bin/bash',
            'lib/x86_64-linux-gnu/libglib-2.0.so.0',
        ):
            self.assertTrue(
                m.filename_is_friendly(good),
                f'{good!r} should be treated as friendly'
            )

        for bad in r'<>:"\|?*':
            self.assertFalse(
                m.filename_is_friendly(bad),
                f'{bad!r} is not Windows-compatible'
            )
            self.assertFalse(
                m.filename_is_friendly(f'my{bad}file'),
                f'{bad!r} is not Windows-compatible'
            )

        for bad in r"#$&'()[]{};`~":
            self.assertFalse(
                m.filename_is_friendly(bad),
                f'{bad!r} is not Unix-shell-friendly',
            )

        for bad, why in (
            ('bin/[', 'Special to Unix shells'),
            ('%TEMP%', 'Special to Windows shells'),
            ('Program Files', 'Has whitespace'),
            ('carriage\rreturn', 'Has ASCII control character'),
            ('interrobang\u203D', 'Has non-ASCII'),
            ('delete\x7F', 'Has ASCII DEL'),
            ('foo/tmp/bar', 'Could be deleted by cleanup tools'),
            ('foo/__pycache__/bar', 'Could be deleted by cleanup tools'),
            ('foo/.cache/bar', 'Could be deleted by cleanup tools'),
            ('foo/CACHEDIR.TAG', 'Could be deleted by cleanup tools'),
            ('foo/bar.pyc', 'Could be deleted by cleanup tools'),
            ('libexec/--inadvisable', 'Special to Unix CLI tools'),
            ('--inadvisable', 'Special to Unix CLI tools'),
            ('share/i18n/charmaps/ISO_8859-1,GL.gz',
             "Steam bootstrapper doesn't like commas"),
        ):
            self.assertFalse(
                m.filename_is_friendly(bad),
                f'{bad!r} should be unfriendly: {why}'
            )

def main() -> None:
    logging.basicConfig()
    logging.getLogger().setLevel(logging.DEBUG)

    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument(
        '--architecture', default='amd64,i386',
        help=(
            'Default dpkg architecture or comma-separated list of '
            'architectures'
        )
    )
    parser.add_argument(
        '--suite', default='',
        help=(
            'Default suite to use if none is specified'
        )
    )
    parser.add_argument(
        '--version', default='',
        help=(
            'Default version to use if none is specified'
        )
    )

    parser.add_argument(
        '--cache', default='.cache',
        help=(
            'Cache downloaded files that are not in --depot here'
        ),
    )

    parser.add_argument(
        '--credential-env',
        action='append',
        default=[],
        dest='credential_envs',
        help=(
            'Environment variable to be evaluated for login:password, '
            'or a pair of environment variables VAR1:VAR2 to be evaluated '
            'for login and password respectively'
        ),
    )
    parser.add_argument(
        '--credential-host',
        action='append',
        default=[],
        dest='credential_hosts',
        metavar='HOST',
        help=(
            'Use --credential-env when downloading from the given HOST'
            '(default: hostname of --images-uri)'
        ),
    )
    parser.add_argument(
        '--images-uri',
        default=DEFAULT_IMAGES_URI,
        metavar='URI',
        help=(
            'Download files from the given URI. '
            '"SUITE" will be replaced with the suite name.'
        ),
    )

    parser.add_argument(
        '--ssh-host', default='', metavar='HOST',
        help='Use ssh and rsync to download files from HOST',
    )
    parser.add_argument(
        '--ssh-path', default='', metavar='PATH',
        help=(
            'Use ssh and rsync to download files from PATH on HOST. '
            '"SUITE" will be replaced with the suite name.'
        ),
    )

    parser.add_argument(
        '--depot', default='depot',
        help=(
            'Download runtime into this existing directory'
        )
    )
    parser.add_argument(
        '--depot-version', default='',
        help=(
            'Set an overall version number for the depot contents'
        )
    )
    parser.add_argument(
        '--depot-archive', default='',
        help=(
            'Export the depot as an archive'
        )
    )
    parser.add_argument(
        '--fast', default=False, action='store_true',
        help=(
            'Speed up compression at the expense of compression ratio'
        )
    )
    parser.add_argument(
        '--scripts-version', default='',
        help=(
            'Set a version number for the scripts from steam-runtime-tools'
        )
    )

    parser.add_argument(
        '--pressure-vessel-uri',
        default=DEFAULT_PRESSURE_VESSEL_URI,
        metavar='URI',
        help=(
            'Download pressure-vessel from a versioned subdirectory of URI'
        ),
    )
    parser.add_argument(
        '--pressure-vessel-ssh-host', default='', metavar='HOST',
        help=(
            'Use ssh and rsync to download pressure-vessel from HOST '
            '[default: same as --ssh-host]'
        ),
    )
    parser.add_argument(
        '--pressure-vessel-ssh-path', default='', metavar='PATH',
        help=(
            'Use ssh and rsync to download pressure-vessel from a versioned '
            'subdirectory of PATH on HOST'
        ),
    )
    parser.add_argument(
        '--pressure-vessel-version', default='', metavar='0.x.y|latest',
        help=(
            'Use this version of pressure-vessel from --pressure-vessel-uri '
            'or --pressure-vessel-ssh-path'
        )
    )
    parser.add_argument(
        '--pressure-vessel-archive', default='', metavar='PATH',
        help=(
            'Unpack pressure-vessel from the named archive'
        ),
    )
    parser.add_argument(
        '--pressure-vessel-from-runtime', default='', metavar='NAME',
        help=(
            'Get pressure-vessel from the named runtime (default "scout")'
        ),
    )
    parser.add_argument(
        '--pressure-vessel-from-runtime-json', default='', metavar='NAME',
        help=(
            'Get pressure-vessel from a separate runtime version given as a '
            'JSON object'
        ),
    )
    parser.add_argument(
        '--pressure-vessel', default='', metavar='NAME|PATH|DETAILS',
        dest='pressure_vessel_guess',
        help=(
            '--pressure-vessel-archive=ARCHIVE, '
            '--pressure-vessel-archive=DIRECTORY/pressure-vessel-bin.tar.gz, '
            '--pressure-vessel-from-runtime=NAME or '
            '--pressure-vessel-from-runtime-json=DETAILS, '
            'based on form of argument given '
            '(disambiguate with ./ if necessary)'
        ),
    )
    parser.add_argument(
        '--pressure-vessel-official', default=False, action='store_true',
        help=(
            'Assume that a copy of pressure-vessel provided as a local '
            'file/directory is an official one'
        ),
    )
    parser.add_argument(
        '--no-include-archives', dest='_ignored', action='store_true',
        help='Ignored for backwards compatibility',
    )
    parser.add_argument(
        '--include-sdk-sysroot', default=False, action='store_true',
        help='Include a corresponding SDK',
    )
    parser.add_argument(
        '--layered', default=False, action='store_true',
        help='Produce a layered runtime that runs scout on soldier',
    )
    parser.add_argument(
        '--minimize', dest='_ignored', action='store_true',
        help='Ignored for backwards compatibility',
    )
    parser.add_argument(
        '--no-mtrees', dest='mtree', action='store_false', default=True,
        help='Skip generation of non-essential mtree manifests',
    )
    parser.add_argument(
        '--source-dir', default=str(HERE),
        help=(
            'Source directory for files to include in the depot'
        )
    )
    parser.add_argument(
        '--steam-app-id', default='',
        help='Set Steam app ID for the depot',
    )
    parser.add_argument(
        '--steam-depot-id', default='',
        help='Set Steam depot ID',
    )
    parser.add_argument(
        '--toolmanifest', default=False, action='store_true',
        help='Generate toolmanifest.vdf',
    )
    parser.add_argument(
        '--unpack-ld-library-path', metavar='PATH', default='',
        help=(
            'Get the steam-runtime.tar.xz from the same place as '
            'pressure-vessel and unpack it into the given PATH, '
            'for use in regression testing.'
        )
    )
    parser.add_argument(
        '--unpack-runtime', '--unpack-runtimes', dest='_ignored',
        action='store_true', default=True,
        help='Ignored for backwards compatibility',
    )
    parser.add_argument(
        '--unpack-source', metavar='PACKAGE', action='append', default=[],
        dest='unpack_sources',
        help=(
            'Download and unpack the given source package from each runtime '
            'if it exists, for use in regression testing. May be repeated.'
        )
    )
    parser.add_argument(
        '--unpack-sources-into', metavar='PATH', default='.',
        help=(
            'Unpack any source packages specified by --unpack-source '
            'into PATH/RUNTIME/SOURCE (default: ./RUNTIME/SOURCE).'
        )
    )
    parser.add_argument(
        '--versioned-directories', action='store_true', default=True,
        help=(
            'Include version number in unpacked runtime directories '
            '[default]'
        )
    )
    parser.add_argument(
        '--no-versioned-directories', action='store_false',
        dest='versioned_directories',
        help=(
            'Do not include version number in unpacked runtime directories'
        )
    )
    parser.add_argument(
        'runtime',
        default='',
        metavar='NAME[="DETAILS"]',
        help=(
            'Runtime to download, in the form NAME or NAME="DETAILS". '
            'DETAILS is a JSON object containing something like '
            '{"path": "../prebuilt", "suite: "scout", "version": "latest", '
            '"architecture": "amd64,i386"}, or the '
            'path to a file with the same JSON object in. All JSON fields '
            'are optional.'
        ),
    )

    if sys.argv[1:2] == ['--self-test']:
        del sys.argv[1:2]
        unittest.main()
        return

    try:
        args = parser.parse_args()
        Main(**vars(args)).run()
    except InvocationError as e:
        parser.error(str(e))


if __name__ == '__main__':
    main()
