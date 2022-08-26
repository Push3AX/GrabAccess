/* init.c - generic U-Boot initialization and finalization */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2016  Free Software Foundation, Inc.
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

#include <grub/uboot/uboot.h>
#include <grub/arm/startup.h>
#include <grub/uboot/api_public.h>

extern int (*grub_uboot_syscall_ptr) (int, int *, ...);

grub_uint32_t
grub_uboot_get_machine_type (void)
{
  return grub_arm_saved_registers.r[1];
}

grub_addr_t
grub_uboot_get_boot_data (void)
{
  return grub_arm_saved_registers.r[2];
}

int
grub_uboot_api_init (void)
{
  struct api_signature *start, *end;
  struct api_signature *p;
  grub_addr_t grub_uboot_search_hint = grub_arm_saved_registers.sp;
  if (grub_uboot_search_hint)
    {
      /* Extended search range to work around Trim Slice U-Boot issue */
      start = (struct api_signature *) ((grub_uboot_search_hint & ~0x000fffff)
					- 0x00500000);
      end =
	(struct api_signature *) ((grub_addr_t) start + UBOOT_API_SEARCH_LEN -
				  API_SIG_MAGLEN + 0x00500000);
    }
  else
    {
      start = 0;
      end = (struct api_signature *) (256 * 1024 * 1024);
    }

  /* Structure alignment is (at least) 8 bytes */
  for (p = start; p < end; p = (void *) ((grub_addr_t) p + 8))
    {
      if (grub_memcmp (&(p->magic), API_SIG_MAGIC, API_SIG_MAGLEN) == 0)
	{
	  grub_uboot_syscall_ptr = p->syscall;
	  return p->version;
	}
    }

  return 0;
}
