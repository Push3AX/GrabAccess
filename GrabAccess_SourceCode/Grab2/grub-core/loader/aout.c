/*
 *  Copyright (C) 2008 Free Software Foundation, Inc.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/file.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/aout.h>

GRUB_MOD_LICENSE ("GPLv3+");

int
grub_aout_get_type (union grub_aout_header *header)
{
  int magic;

  magic = AOUT_GETMAGIC (header->aout32);
  if ((magic == AOUT32_OMAGIC) || (magic == AOUT32_NMAGIC) ||
      (magic == AOUT32_ZMAGIC) || (magic == AOUT32_QMAGIC))
    return AOUT_TYPE_AOUT32;
  else if ((magic == AOUT64_OMAGIC) || (magic == AOUT64_NMAGIC) ||
	   (magic == AOUT64_ZMAGIC))
    return AOUT_TYPE_AOUT64;
  else
    return AOUT_TYPE_NONE;
}

grub_err_t
grub_aout_load (grub_file_t file, int offset,
                void *load_addr,
		int load_size, grub_size_t bss_size)
{
  if ((grub_file_seek (file, offset)) == (grub_off_t) - 1)
    return grub_errno;

  if (!load_size)
    load_size = file->size - offset;

  grub_file_read (file, load_addr, load_size);

  if (grub_errno)
    return grub_errno;

  if (bss_size)
    grub_memset ((char *) load_addr + load_size, 0, bss_size);

  return GRUB_ERR_NONE;
}
