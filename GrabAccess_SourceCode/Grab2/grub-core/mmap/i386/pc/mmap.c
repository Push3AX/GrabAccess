/* Mmap management. */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
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

#include <grub/machine/memory.h>
#include <grub/memory.h>
#include <grub/misc.h>
#include <grub/term.h>
#include <grub/loader.h>

#define min(a,b) (((a) < (b)) ? (a) : (b))

static void *hooktarget = 0;

extern grub_uint8_t grub_machine_mmaphook_start;
extern grub_uint8_t grub_machine_mmaphook_end;
extern grub_uint8_t grub_machine_mmaphook_int12;
extern grub_uint8_t grub_machine_mmaphook_int15;

static grub_uint16_t grub_machine_mmaphook_int12offset = 0;
static grub_uint16_t grub_machine_mmaphook_int12segment = 0;
extern grub_uint16_t grub_machine_mmaphook_int15offset;
extern grub_uint16_t grub_machine_mmaphook_int15segment;

extern grub_uint16_t grub_machine_mmaphook_mmap_num;
extern grub_uint16_t grub_machine_mmaphook_kblow;
extern grub_uint16_t grub_machine_mmaphook_kbin16mb;
extern grub_uint16_t grub_machine_mmaphook_64kbin4gb;

/* Helper for preboot.  */
static int fill_hook (grub_uint64_t addr, grub_uint64_t size,
		      grub_memory_type_t type, void *data)
{
  struct grub_e820_mmap_entry **hookmmapcur = data;
  grub_dprintf ("mmap", "mmap chunk %llx-%llx:%x\n", addr, addr + size, type);
  (*hookmmapcur)->addr = addr;
  (*hookmmapcur)->len = size;
  (*hookmmapcur)->type = type;
  (*hookmmapcur)++;
  return 0;
}

static grub_err_t
preboot (int noreturn __attribute__ ((unused)))
{
  struct grub_e820_mmap_entry *hookmmap, *hookmmapcur;

  if (! hooktarget)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY,
		       "no space is allocated for memory hook");

  grub_dprintf ("mmap", "installing preboot handlers\n");

  hookmmapcur = hookmmap = (struct grub_e820_mmap_entry *)
    ((grub_uint8_t *) hooktarget + (&grub_machine_mmaphook_end
				    - &grub_machine_mmaphook_start));

  grub_mmap_iterate (fill_hook, &hookmmapcur);
  grub_machine_mmaphook_mmap_num = hookmmapcur - hookmmap;

  grub_machine_mmaphook_kblow = grub_mmap_get_lower () >> 10;
  grub_machine_mmaphook_kbin16mb
    = min (grub_mmap_get_upper (),0x3f00000ULL) >> 10;
  grub_machine_mmaphook_64kbin4gb
    = min (grub_mmap_get_post64 (), 0xfc000000ULL) >> 16;

  /* Correct BDA. */
  *((grub_uint16_t *) 0x413) = grub_mmap_get_lower () >> 10;

  /* Save old interrupt handlers. */
  grub_machine_mmaphook_int12offset = *((grub_uint16_t *) 0x48);
  grub_machine_mmaphook_int12segment = *((grub_uint16_t *) 0x4a);
  grub_machine_mmaphook_int15offset = *((grub_uint16_t *) 0x54);
  grub_machine_mmaphook_int15segment = *((grub_uint16_t *) 0x56);

  grub_dprintf ("mmap", "hooktarget = %p\n", hooktarget);

  /* Install the interrupt handlers. */
  grub_memcpy (hooktarget, &grub_machine_mmaphook_start,
	       &grub_machine_mmaphook_end - &grub_machine_mmaphook_start);

  *((grub_uint16_t *) 0x4a) = ((grub_addr_t) hooktarget) >> 4;
  *((grub_uint16_t *) 0x56) = ((grub_addr_t) hooktarget) >> 4;
  *((grub_uint16_t *) 0x48) = &grub_machine_mmaphook_int12
    - &grub_machine_mmaphook_start;
  *((grub_uint16_t *) 0x54) = &grub_machine_mmaphook_int15
    - &grub_machine_mmaphook_start;

  return GRUB_ERR_NONE;
}

static grub_err_t
preboot_rest (void)
{
  /* Restore old interrupt handlers. */
  *((grub_uint16_t *) 0x48) = grub_machine_mmaphook_int12offset;
  *((grub_uint16_t *) 0x4a) = grub_machine_mmaphook_int12segment;
  *((grub_uint16_t *) 0x54) = grub_machine_mmaphook_int15offset;
  *((grub_uint16_t *) 0x56) = grub_machine_mmaphook_int15segment;

  return GRUB_ERR_NONE;
}

/* Helper for malloc_hook.  */
static int
count_hook (grub_uint64_t addr __attribute__ ((unused)),
	    grub_uint64_t size __attribute__ ((unused)),
	    grub_memory_type_t type __attribute__ ((unused)), void *data)
{
  int *regcount = data;
  (*regcount)++;
  return 0;
}

static grub_err_t
malloc_hook (void)
{
  static int reentry = 0;
  static int mmapregion = 0;
  static int slots_available = 0;
  int hooksize;
  int regcount = 0;

  if (reentry)
    return GRUB_ERR_NONE;

  grub_dprintf ("mmap", "registering\n");

  grub_mmap_iterate (count_hook, &regcount);

  /* Mapping hook itself may introduce up to 2 additional regions. */
  regcount += 2;

  if (regcount <= slots_available)
    return GRUB_ERR_NONE;

  if (mmapregion)
    {
      grub_mmap_free_and_unregister (mmapregion);
      mmapregion = 0;
      hooktarget = 0;
    }

  hooksize = &grub_machine_mmaphook_end - &grub_machine_mmaphook_start
    + regcount * sizeof (struct grub_e820_mmap_entry);
  /* Allocate an integer number of KiB. */
  hooksize = ((hooksize - 1) | 0x3ff) + 1;
  slots_available = (hooksize - (&grub_machine_mmaphook_end
				 - &grub_machine_mmaphook_start))
    / sizeof (struct grub_e820_mmap_entry);

  reentry = 1;
  hooktarget
    = grub_mmap_malign_and_register (16, ALIGN_UP (hooksize, 16), &mmapregion,
				     GRUB_MEMORY_RESERVED,
				     GRUB_MMAP_MALLOC_LOW);
  reentry = 0;

  if (! hooktarget)
    {
      slots_available = 0;
      return grub_error (GRUB_ERR_OUT_OF_MEMORY, "no space for mmap hook");
    }
  return GRUB_ERR_NONE;
}

grub_err_t
grub_machine_mmap_register (grub_uint64_t start __attribute__ ((unused)),
			    grub_uint64_t size __attribute__ ((unused)),
			    int type __attribute__ ((unused)),
			    int handle  __attribute__ ((unused)))
{
  grub_err_t err;
  static struct grub_preboot *preb_handle = 0;

  err = malloc_hook ();
  if (err)
    return err;

  if (! preb_handle)
    {
      grub_dprintf ("mmap", "adding preboot\n");
      preb_handle
	= grub_loader_register_preboot_hook (preboot, preboot_rest,
					     GRUB_LOADER_PREBOOT_HOOK_PRIO_MEMORY);
      if (! preb_handle)
	return grub_errno;
    }
  return GRUB_ERR_NONE;
}

grub_err_t
grub_machine_mmap_unregister (int handle __attribute__ ((unused)))
{
  return GRUB_ERR_NONE;
}
