// Copyright © 2017 Collabora Ltd
// SPDX-License-Identifier: LGPL-2.1-or-later

// This file is part of libcapsule.

// libcapsule is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.

// libcapsule is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with libcapsule.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <stdarg.h>
#include <syslog.h>

enum
{
    DEBUG_NONE       = 0,
    DEBUG_PATH       = 0x1,
    DEBUG_SEARCH     = 0x1 << 1,
    DEBUG_LDCACHE    = 0x1 << 2,
    DEBUG_CAPSULE    = 0x1 << 3,
    DEBUG_MPROTECT   = 0x1 << 4,
    DEBUG_WRAPPERS   = 0x1 << 5,
    DEBUG_RELOCS     = 0x1 << 6,
    DEBUG_ELF        = 0x1 << 7,
    DEBUG_DLFUNC     = 0x1 << 8,
    DEBUG_TOOL       = 0x1 << 9,
    DEBUG_ALL        = 0xffff,
};

#define LDLIB_DEBUG(ldl, flags, fmt, args...)  \
    if( ldl->debug && (ldl->debug & (flags)) ) \
        capsule_log( LOG_DEBUG, "%s:" fmt, __PRETTY_FUNCTION__, ##args )

#define DEBUG(flags, fmt, args...)               \
    if( debug_flags && (debug_flags & (flags)) ) \
        capsule_log( LOG_DEBUG, "%s:" fmt, __PRETTY_FUNCTION__, ##args )

extern unsigned long debug_flags;
void  set_debug_flags (const char *control);

extern int capsule_level_prefix;
void capsule_log (int log_level, const char *fmt, ...) __attribute__((__format__(__printf__, 2, 3)));
void capsule_logv (int log_level, const char *fmt, va_list ap) __attribute__((__format__(__printf__, 2, 0)));

#define capsule_err(status, fmt, args...) \
  do { \
      capsule_log( LOG_ERR, fmt, ##args ); \
      exit (status); \
  } while (0)

#define capsule_warn(fmt, args...) \
  capsule_log( LOG_WARNING, fmt, ##args )
