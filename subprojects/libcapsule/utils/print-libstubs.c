// Copyright © 2017-2019 Collabora Ltd
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

#include <dlfcn.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>

#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include <link.h>

#include "tools.h"
#include "utils.h"
#include "ld-cache.h"
#include "ld-libs.h"

static void usage (int code) __attribute__((noreturn));
static void usage (int code)
{
  FILE *fh;

  if( code == 0 )
  {
      fh = stdout;
  }
  else
  {
      fh = stderr;
  }

  fprintf( fh, "Usage: %s SONAME [SYSROOT]\n",
           program_invocation_short_name );
  fprintf( fh, "SONAME is the machine-readable name of a shared library,\n"
               "for example 'libz.so.1'.\n" );
  fprintf( fh, "SYSROOT is the root directory where we look for SONAME.\n" );
  exit( code );
}

typedef struct
{
    size_t plen;
    const char *prefix;
    const char *target;
} dso_search_t;

// these macros are secretly the same for elf32 & elf64:
#define ELFW_ST_TYPE(a)       ELF32_ST_TYPE(a)
#define ELFW_ST_BIND(a)       ELF32_ST_BIND(a)
#define ELFW_ST_VISIBILITY(a) ELF32_ST_VISIBILITY(a)

// given a symbol in DT_SYMTAB at index 𝒊, jump to its
// entry in DT_VERSYM (also at offset 𝒊 for an array of ElfW(Versym))
// and extract its value 𝒗𝒔 (a number). The corresponding DT_VERDEF entry
// (ElfW(Verdef)) is the one whose vd_ndx member == 𝒗𝒔 & 0x7fff
//
// NOTE: if 𝒗𝒔 & 0x8000 then the version is the default or base version
// of the symbol, which should be used if the requestor has not specified
// a version for this symbol
//
// NOTE: in practice the vd_ndx member is the 1-based array position in
// the DT_VERDEF array, but the linker/elfutils code does not rely on
// this, so neither do we.
//
// next we check that the vd_flags member in the DT_VERDEF entry does not
// contain VER_FLG_BASE, as that is the DT_VERDEF entry for the entire DSO
// and must not be used as a symbol version (this should never happen:
// the spec does not allow it, but it's not physically impossible).
//
// if we have a valid DT_VERDEF entry the ElfW(Verdaux) array entry at offset
// vd_aux (from the address of the DT_VERDEF entry itself) will give
// the address of an ElfW(Verdaux) struct whose vda_name entry points
// to (𝑓𝑖𝑛𝑎𝑙𝑙𝑦) an offset into the DT_STRTAB which gives the version name.
//
// And that's how symbol version lookup works, as near as I can tell.
static void
symbol_version ( ElfW(Sym) *symbol,
                 int i,
                 const char *strtab,
                 const ElfW(Versym) *versym,
                 const void *verdef,
                 const int verdefnum,
                 int *default_version,
                 const char **version,
                 ElfW(Versym) *vs)
{
    *default_version = 0;
    *version = NULL;

    if( versym == NULL )
        return;

    switch( symbol->st_shndx )
    {
      case SHN_UNDEF:
      case SHN_ABS:
      case SHN_COMMON:
      case SHN_BEFORE:
      case SHN_AFTER:
      case SHN_XINDEX:
        // none of these are handled (and we're very unlikely to need to)
        break;

      default:
        if( symbol->st_shndx < SHN_LORESERVE )
        {
            const char  *vd = verdef;
            *vs = *(versym + i);

            for( int x = 0; x < verdefnum; x++ )
            {
                ElfW(Verdef) *entry = (ElfW(Verdef) *) vd;

                if( entry->vd_ndx == (*vs & 0x7fff) )
                {
                    const char *au;

                    if( entry->vd_flags & VER_FLG_BASE )
                        break;

                    au = vd + entry->vd_aux;
                    ElfW(Verdaux) *aux = (ElfW(Verdaux) *) au;
                    *version = strtab + aux->vda_name;
                    *default_version = (*vs & 0x8000) ? 0 : 1;
                }

                vd = vd + entry->vd_next;
            }
        }
    }
}


static int
symbol_excluded (const char *name)
{
    if( !strcmp(name, "_init") ||
        !strcmp(name, "_fini") )
        return 1;

    return 0;
}

static void
parse_symtab (const void *start,
              const char *strtab,
              const ElfW(Versym) *versym,
              const void *verdef,
              const int verdefnum)
{
    int x = 0;
    ElfW(Sym) *entry;

    for( entry = (ElfW(Sym) *)start;
         ( (ELFW_ST_TYPE(entry->st_info) < STT_NUM) &&
           (ELFW_ST_BIND(entry->st_info) < STB_NUM) );
         entry++, x++ )
    {
        int defversym = 0;
        const char *version = NULL;
        ElfW(Versym) vs = 0;

        switch( ELFW_ST_TYPE(entry->st_info) )
        {
          case STT_FUNC:
          case STT_OBJECT:
            symbol_version( entry, x, strtab, versym, verdef, verdefnum,
                            &defversym, &version, &vs );

            if( !vs )
                continue;

            if( symbol_excluded(strtab + entry->st_name) )
                continue;

            fprintf( stdout, "%s %s%s%s\n",
                     strtab + entry->st_name  ,
                     version   ? "@"     : "" ,
                     defversym ? "@"     : "" ,
                     version   ? version : "" );
            break;

          default:
            // Assumed to be uninteresting?
            break;
        }
    }
}

static void
parse_dynamic (ElfW(Addr) base, ElfW(Dyn) *dyn)
{

    int verdefnum  = -1;
    void *start    = NULL;
    const void *symtab = NULL;
    const void *versym = NULL;
    const void *verdef = NULL;
    const char *strtab = NULL;
    ElfW(Dyn) *entry   = NULL;

    start  = (void *) ((ElfW(Addr)) dyn - base);
    strtab = dynamic_section_find_strtab( dyn, (const void *) base, NULL );

    for( entry = dyn; entry->d_tag != DT_NULL; entry++ )
    {
        switch( entry->d_tag )
        {
          case DT_SYMTAB:
            if( versym == NULL )
                versym = (const void *) find_ptr( base, start, DT_VERSYM );
            if( verdef == NULL )
                verdef = (const void *) find_ptr( base, start, DT_VERDEF );
            if( verdefnum == -1 )
                verdefnum = find_value( base, start, DT_VERDEFNUM );

            symtab = (const void *) entry->d_un.d_ptr;
            parse_symtab( symtab, strtab, versym, verdef, verdefnum );
            break;

          case DT_VERDEFNUM:
            verdefnum = entry->d_un.d_val;
            break;

          case DT_VERDEF:
            if( verdefnum == -1 )
                verdefnum = find_value( base, start, DT_VERDEFNUM );
            verdef = (const void *) entry->d_un.d_ptr;
            // parse_verdef( verdef, strtab, verdefnum );
            break;

          case DT_VERSYM:
            if( versym == NULL )
                versym = (const void *) entry->d_un.d_ptr;
            break;

          default:
            break;
        }
    }

    return;
}

static int
dso_name_matches (const char *target, const char *maybe)
{
    const char *dir = strrchr( maybe, '/' );
    const size_t len = strlen( target );

    // maybe contains a full path:
    if( !dir )
        return 0;

    // maybe's filename is at least long enough to match target
    if( strlen(++dir) < len )
        return 0;

    // /maybefilename matches /target
    if( memcmp(dir, target, len) )
        return 0;

    // the last major/minor/etc number didn't actually match
    // eg libfoo.so.1 vs libfoo.so.10
    if ( *(dir + len) != '.' && *(dir + len) != '\0' )
        return 0;

    return 1;
}

static void
dump_symbols (void *handle, const char *libname)
{
    int dlcode = 0;
    struct link_map *map;

    if( (dlcode = dlinfo( handle, RTLD_DI_LINKMAP, &map )) )
    {
        fprintf( stderr, "cannot access symbols for %s via handle %p [%d]\n",
                 libname, handle, dlcode );
        exit( dlcode );
    }

    // find start of link map chain:
    while( map->l_prev )
        map = map->l_prev;

    for( struct link_map *m = map; m; m = m->l_next )
        if( dso_name_matches( libname, m->l_name ) )
            parse_dynamic( m->l_addr, m->l_ld );

}

enum
{
  OPTION_HELP,
  OPTION_VERSION,
};

static struct option long_options[] =
{
    { "help", no_argument, NULL, OPTION_HELP },
    { "version", no_argument, NULL, OPTION_VERSION },
    { NULL }
};

int main (int argc, char **argv)
{
    const char *libname;
    const char *prefix = NULL;
    _capsule_autofree char *message = NULL;
    _capsule_cleanup(ld_libs_clear_pointer) ld_libs *ldlibs = new0( ld_libs, 1 );
    int error = 0;
    Lmid_t ns = LM_ID_BASE;
    void *handle;

    while( 1 )
    {
        int opt = getopt_long( argc, argv, "", long_options, NULL );

        if( opt == -1 )
        {
            break;
        }

        switch( opt )
        {
            case '?':
            default:
                usage( 2 );
                break;  // not reached

            case OPTION_HELP:
                usage( 0 );
                break;  // not reached

            case OPTION_VERSION:
                _capsule_tools_print_version( "capsule-symbols" );
                return 0;
        }
    }

    if( argc < optind + 1 || argc > optind + 2)
        usage( 1 );

    if( argc > optind + 1 )
        prefix = argv[optind + 1];

    set_debug_flags( getenv("CAPSULE_DEBUG") );

    if( !ld_libs_init( ldlibs, NULL, prefix, 0, &error, &message ) )
    {
        fprintf( stderr, "%s: failed to initialize for prefix %s (%d: %s)\n",
                 program_invocation_short_name, prefix, error, message );
        exit( error ? error : ENOENT );
    }

    if( !ld_libs_set_target( ldlibs, argv[optind], &error, &message ) ||
        !ld_libs_find_dependencies( ldlibs, &error, &message ) )
    {
        fprintf( stderr, "%s: failed to open [%s]%s (%d: %s)\n",
                 program_invocation_short_name, prefix, argv[optind], error, message );
        exit( error ? error : ENOENT );
    }

    if( ( handle = ld_libs_load( ldlibs, &ns, 0, &error, &message ) ) )
    {
        if( (libname = strrchr( argv[optind], '/' )) )
            libname = libname + 1;
        else
            libname = argv[optind];

        // dl_iterate_phdr won't work with private dlmopen namespaces:
        dump_symbols( handle, libname );
    }
    else
    {
        fprintf( stderr, "%s: failed to open [%s]%s (%d: %s)\n",
                 program_invocation_short_name, prefix, argv[optind], error, message );
        exit(error ? error : ENOENT);
    }

    exit(0);
}
