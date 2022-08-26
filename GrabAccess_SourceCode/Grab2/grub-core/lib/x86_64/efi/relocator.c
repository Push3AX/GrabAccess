/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
 *  Copyright (C) 2016  Oracle and/or its affiliates. All rights reserved.
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

#include <grub/mm.h>
#include <grub/misc.h>

#include <grub/types.h>
#include <grub/err.h>

#include <grub/i386/relocator.h>
#include <grub/relocator_private.h>

extern grub_uint64_t grub_relocator64_rax;
extern grub_uint64_t grub_relocator64_rbx;
extern grub_uint64_t grub_relocator64_rcx;
extern grub_uint64_t grub_relocator64_rdx;
extern grub_uint64_t grub_relocator64_rip;
extern grub_uint64_t grub_relocator64_rsi;

extern grub_uint8_t grub_relocator64_efi_start;
extern grub_uint8_t grub_relocator64_efi_end;

#define RELOCATOR_SIZEOF(x)	(&grub_relocator##x##_end - &grub_relocator##x##_start)

grub_err_t
grub_relocator64_efi_boot (struct grub_relocator *rel,
			   struct grub_relocator64_efi_state state)
{
  grub_err_t err;
  void *relst;
  grub_relocator_chunk_t ch;

  /*
   * 64-bit relocator code may live above 4 GiB quite well.
   * However, I do not want ask for problems. Just in case.
   */
  err = grub_relocator_alloc_chunk_align_safe (rel, &ch, 0, 0x100000000,
					       RELOCATOR_SIZEOF (64_efi), 16,
					       GRUB_RELOCATOR_PREFERENCE_NONE, 1);
  if (err)
    return err;

  /* Do not touch %rsp! It points to EFI created stack. */
  grub_relocator64_rax = state.rax;
  grub_relocator64_rbx = state.rbx;
  grub_relocator64_rcx = state.rcx;
  grub_relocator64_rdx = state.rdx;
  grub_relocator64_rip = state.rip;
  grub_relocator64_rsi = state.rsi;

  grub_memmove (get_virtual_current_address (ch), &grub_relocator64_efi_start,
		RELOCATOR_SIZEOF (64_efi));

  err = grub_relocator_prepare_relocs (rel, get_physical_target_address (ch),
				       &relst, NULL);
  if (err)
    return err;

  ((void (*) (void)) relst) ();

  /* Not reached.  */
  return GRUB_ERR_NONE;
}
