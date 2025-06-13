#!/bin/sh
# Copyright 2022 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

echo "#!/bin/sh" > /usr/sbin/policy-rc.d
echo "exit 101" >> /usr/sbin/policy-rc.d
chmod +x /usr/sbin/policy-rc.d
echo "Acquire::Languages \"none\";" > /etc/apt/apt.conf.d/90nolanguages
echo 'force-unsafe-io' > /etc/dpkg/dpkg.cfg.d/autopkgtest

if [ -e "$STEAMRT_CI_APT_AUTH_CONF" ]; then
    # We can't use /etc/apt/auth.conf.d in scout, only in >= soldier.
    # It can be root:root in apt >= 1.5~beta2 (>= soldier) or in
    # apt << 1.1~exp8 (scout).
    chown root:root "$STEAMRT_CI_APT_AUTH_CONF"
    chmod 0600 "$STEAMRT_CI_APT_AUTH_CONF"

    # shellcheck source=/dev/null
    case "$(. /usr/lib/os-release; echo "${VERSION_CODENAME-${VERSION}}")" in
        (*scout*)
            ln -fns "$STEAMRT_CI_APT_AUTH_CONF" /etc/apt/auth.conf
            ;;
        (*)
            install -d /etc/apt/auth.conf.d
            ln -fns "$STEAMRT_CI_APT_AUTH_CONF" /etc/apt/auth.conf.d/ci.conf
            ;;
    esac
fi

need_apt_keys=
too_old_for_signed_by=

# shellcheck source=/dev/null
case "$(. /usr/lib/os-release; echo "${VERSION_CODENAME-${VERSION}}")" in
    (soldier)
        if [ -n "${SOLDIER_APT_SOURCES_FILE-}" ]; then
            cp "${SOLDIER_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        need_apt_keys=1
        too_old_for_signed_by=1
        ;;

    (sniper)
        if [ -n "${SNIPER_APT_SOURCES_FILE-}" ]; then
            cp "${SNIPER_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        need_apt_keys=1
        too_old_for_signed_by=1
        ;;

    (medic)
        if [ -n "${MEDIC_APT_SOURCES_FILE-}" ]; then
            cp "${MEDIC_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        need_apt_keys=1
        ;;

    (steamrt4)
        if [ -n "${STEAMRT4_APT_SOURCES_FILE-}" ]; then
            cp "${STEAMRT4_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        need_apt_keys=1
        ;;

    (steamrt5)
        if [ -n "${STEAMRT5_APT_SOURCES_FILE-}" ]; then
            cp "${STEAMRT5_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        need_apt_keys=1
        ;;

    (*scout*)
        if [ -n "${SCOUT_APT_SOURCES_FILE-}" ]; then
            cp "${SCOUT_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        need_apt_keys=1
        too_old_for_signed_by=1

        # Go back to Ubuntu 12.04's default gcc/g++/cpp
        update-alternatives --set g++ /usr/bin/g++-4.6
        update-alternatives --set gcc /usr/bin/gcc-4.6
        update-alternatives --set cpp-bin /usr/bin/cpp-4.6
        ;;
esac

if [ -n "$need_apt_keys" ] && [ -n "${STEAMRT_CI_APT_PGP_TRUSTED_FILE}" ]; then
    if [ -n "$too_old_for_signed_by" ]; then
        mkdir -p /etc/apt/trusted.gpg.d/
        gpg --dearmor \
            < "${STEAMRT_CI_APT_PGP_TRUSTED_FILE}" \
            > /etc/apt/trusted.gpg.d/steamrt-ci.gpg
    else
        # STEAMRT4_APT_SOURCES_FILE, etc. are assumed to use
        # [signed-by=/etc/apt/keyrings/steamrt-ci.gpg]
        # where necessary
        mkdir -p /etc/apt/keyrings/
        gpg --dearmor \
            < "${STEAMRT_CI_APT_PGP_TRUSTED_FILE}" \
            > /etc/apt/keyrings/steamrt-ci.gpg
    fi
fi

apt-get -y update

apt-get install -y --no-install-recommends \
    ca-certificates \
    git \
    "$@"
# Optional
apt-get install -y --no-install-recommends eatmydata || :
dbus-uuidgen --ensure || :
