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

#include <config.h>
#include <config-util.h>

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

void *
grub_osdep_dl_memalign (grub_size_t align, grub_size_t size)
{
  void *ret;
  if (align > 4096)
    {
      grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET, "too large alignment");
      return NULL;
    }

  size = ALIGN_UP (size, 4096);

  ret = VirtualAlloc (NULL, size, MEM_COMMIT | MEM_RESERVE,
		      PAGE_EXECUTE_READWRITE);

  if (!ret)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
      return NULL;
    }

  return ret;
}

void
grub_dl_osdep_dl_free (void *ptr)
{
  if (!ptr)
    return;
  VirtualFree (ptr, 0,  MEM_RELEASE);
}
