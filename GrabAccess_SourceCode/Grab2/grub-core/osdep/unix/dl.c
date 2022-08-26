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
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>

void *
grub_osdep_dl_memalign (grub_size_t align, grub_size_t size)
{
  void *ret;
  if (align < 8192 * 16)
    align = 8192 * 16;
  size = ALIGN_UP (size, 8192 * 16);

#if defined(HAVE_POSIX_MEMALIGN)
  if (posix_memalign (&ret, align, size) != 0)
    ret = 0;
#elif defined(HAVE_MEMALIGN)
  ret = memalign (align, size);
#else
#error "Complete this"
#endif

  if (!ret)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
      return NULL;
    }

  mprotect (ret, size, PROT_READ | PROT_WRITE | PROT_EXEC);
  return ret;
}

void
grub_dl_osdep_dl_free (void *ptr)
{
  if (ptr)
    free (ptr);
}
