/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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

#include <grub/fileid.h>
#include <grub/elfload.h>
#include <grub/misc.h>

#pragma GCC diagnostic ignored "-Wcast-align"

int
grub_file_check_netbsdXX (grub_elf_t elf)
{
  Elf_Shdr *s, *s0;

  grub_size_t shnum = elf->ehdr.ehdrXX.e_shnum;
  grub_size_t shentsize = elf->ehdr.ehdrXX.e_shentsize;
  grub_size_t shsize = shnum * shentsize;
  grub_off_t stroff;

  if (!shnum || !shentsize)
    return 0;

  s0 = grub_malloc (shsize);
  if (!s0)
    return 0;

  if (grub_file_seek (elf->file, elf->ehdr.ehdrXX.e_shoff) == (grub_off_t) -1)
    goto fail;

  if (grub_file_read (elf->file, s0, shsize) != (grub_ssize_t) shsize)
    goto fail;

  s = (Elf_Shdr *) ((char *) s0 + elf->ehdr.ehdrXX.e_shstrndx * shentsize);
  stroff = s->sh_offset;

  for (s = s0; s < (Elf_Shdr *) ((char *) s0 + shnum * shentsize);
       s = (Elf_Shdr *) ((char *) s + shentsize))
    {
      char name[sizeof(".note.netbsd.ident")];
      grub_memset (name, 0, sizeof (name));
      if (grub_file_seek (elf->file, stroff + s->sh_name) == (grub_off_t) -1)
	goto fail;

      if (grub_file_read (elf->file, name, sizeof (name)) != (grub_ssize_t) sizeof (name))
	{
	  if (grub_errno)
	    goto fail;
	  continue;
	}
      if (grub_memcmp (name, ".note.netbsd.ident",
		       sizeof(".note.netbsd.ident")) != 0)
	continue;
      grub_free (s0);
      return 1;
    }
 fail:
  grub_free (s0);
  return 0;
}
