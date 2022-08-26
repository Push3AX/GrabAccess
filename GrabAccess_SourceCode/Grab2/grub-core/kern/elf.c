/* elf.c - load ELF files */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2004,2005,2006,2007,2008,2009  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/err.h>
#include <grub/elf.h>
#include <grub/elfload.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/dl.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

#pragma GCC diagnostic ignored "-Wcast-align"

#if defined(__powerpc__) && defined(GRUB_MACHINE_IEEE1275)
#define GRUB_ELF_ENABLE_BI_ENDIAN 1
#else
#define GRUB_ELF_ENABLE_BI_ENDIAN 0
#endif

#if defined(GRUB_CPU_WORDS_BIGENDIAN)
#define GRUB_ELF_NATIVE_ENDIANNESS ELFDATA2MSB
#define GRUB_ELF_OPPOSITE_ENDIANNESS ELFDATA2LSB
#else
#define GRUB_ELF_NATIVE_ENDIANNESS ELFDATA2LSB
#define GRUB_ELF_OPPOSITE_ENDIANNESS ELFDATA2MSB
#endif

static int grub_elf32_check_endianess_and_bswap_ehdr (grub_elf_t elf);
static int grub_elf64_check_endianess_and_bswap_ehdr (grub_elf_t elf);

/* Check if EHDR is a valid ELF header.  */
static grub_err_t
grub_elf_check_header (grub_elf_t elf)
{
  Elf32_Ehdr *e = &elf->ehdr.ehdr32;

  if (e->e_ident[EI_MAG0] != ELFMAG0
      || e->e_ident[EI_MAG1] != ELFMAG1
      || e->e_ident[EI_MAG2] != ELFMAG2
      || e->e_ident[EI_MAG3] != ELFMAG3
      || e->e_ident[EI_VERSION] != EV_CURRENT)
    return grub_error (GRUB_ERR_BAD_OS, N_("invalid arch-independent ELF magic"));

  if (grub_elf_is_elf32 (elf))
    {
      if (!grub_elf32_check_endianess_and_bswap_ehdr (elf)) {
	return grub_error (GRUB_ERR_BAD_OS, "invalid ELF endianness magic");
      }
    }
  else if (grub_elf_is_elf64 (elf))
    {
      if (!grub_elf64_check_endianess_and_bswap_ehdr (elf)) {
	return grub_error (GRUB_ERR_BAD_OS, "invalid ELF endianness magic");
      }
    }
  else
    return grub_error (GRUB_ERR_BAD_OS, "unknown ELF class");

  if (e->e_version != EV_CURRENT)
    return grub_error (GRUB_ERR_BAD_OS, N_("invalid arch-independent ELF magic"));

  return GRUB_ERR_NONE;
}

grub_err_t
grub_elf_close (grub_elf_t elf)
{
  grub_file_t file = elf->file;

  grub_free (elf->phdrs);
  grub_free (elf->filename);
  grub_free (elf);

  if (file)
    grub_file_close (file);

  return grub_errno;
}

grub_elf_t
grub_elf_file (grub_file_t file, const char *filename)
{
  grub_elf_t elf;

  elf = grub_zalloc (sizeof (*elf));
  if (! elf)
    return 0;

  elf->file = file;

  if (grub_file_seek (elf->file, 0) == (grub_off_t) -1)
    goto fail;

  if (grub_file_read (elf->file, &elf->ehdr, sizeof (elf->ehdr))
      != sizeof (elf->ehdr))
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_FILE_READ_ERROR, N_("premature end of file %s"),
		    filename);
      goto fail;
    }

  if (grub_elf_check_header (elf))
    goto fail;

  elf->filename = grub_strdup (filename);
  if (!elf->filename)
    goto fail;

  return elf;

fail:
  grub_free (elf->filename);
  grub_free (elf->phdrs);
  grub_free (elf);
  return 0;
}

grub_elf_t
grub_elf_open (const char *name, enum grub_file_type type)
{
  grub_file_t file;
  grub_elf_t elf;

  file = grub_file_open (name, type);
  if (! file)
    return 0;

  elf = grub_elf_file (file, name);
  if (! elf)
    grub_file_close (file);

  return elf;
}


#define grub_swap_bytes_halfXX grub_swap_bytes16
#define grub_swap_bytes_wordXX grub_swap_bytes32

/* 32-bit */
#define ehdrXX ehdr32
#define ELFCLASSXX ELFCLASS32
#define ElfXX_Addr Elf32_Addr
#define grub_elfXX_size grub_elf32_size
#define grub_elfXX_load grub_elf32_load
#define FOR_ELFXX_PHDRS FOR_ELF32_PHDRS
#define grub_elf_is_elfXX grub_elf_is_elf32
#define grub_elfXX_load_phdrs grub_elf32_load_phdrs
#define ElfXX_Phdr Elf32_Phdr
#define ElfXX_Ehdr Elf32_Ehdr
#define grub_uintXX_t grub_uint32_t
#define grub_swap_bytes_addrXX grub_swap_bytes32
#define grub_swap_bytes_offXX grub_swap_bytes32
#define grub_swap_bytes_XwordXX grub_swap_bytes32
#define grub_elfXX_check_endianess_and_bswap_ehdr grub_elf32_check_endianess_and_bswap_ehdr

#include "elfXX.c"

#undef ehdrXX
#undef ELFCLASSXX
#undef ElfXX_Addr
#undef grub_elfXX_size
#undef grub_elfXX_load
#undef FOR_ELFXX_PHDRS
#undef grub_elf_is_elfXX
#undef grub_elfXX_load_phdrs
#undef ElfXX_Phdr
#undef ElfXX_Ehdr
#undef grub_uintXX_t
#undef grub_swap_bytes_addrXX
#undef grub_swap_bytes_offXX
#undef grub_swap_bytes_XwordXX
#undef grub_elfXX_check_endianess_and_bswap_ehdr


/* 64-bit */
#define ehdrXX ehdr64
#define ELFCLASSXX ELFCLASS64
#define ElfXX_Addr Elf64_Addr
#define grub_elfXX_size grub_elf64_size
#define grub_elfXX_load grub_elf64_load
#define FOR_ELFXX_PHDRS FOR_ELF64_PHDRS
#define grub_elf_is_elfXX grub_elf_is_elf64
#define grub_elfXX_load_phdrs grub_elf64_load_phdrs
#define ElfXX_Phdr Elf64_Phdr
#define ElfXX_Ehdr Elf64_Ehdr
#define grub_uintXX_t grub_uint64_t
#define grub_swap_bytes_addrXX grub_swap_bytes64
#define grub_swap_bytes_offXX grub_swap_bytes64
#define grub_swap_bytes_XwordXX grub_swap_bytes64
#define grub_elfXX_check_endianess_and_bswap_ehdr grub_elf64_check_endianess_and_bswap_ehdr

#include "elfXX.c"
