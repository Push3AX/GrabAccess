/* dl_helper.c - relocation helper functions for modules and grub-mkimage */
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

#include <grub/dl.h>
#include <grub/elf.h>
#include <grub/misc.h>
#include <grub/err.h>
#include <grub/mm.h>
#include <grub/i18n.h>
#include <grub/arm64/reloc.h>

/*
 * grub_arm64_reloc_xxxx26():
 *
 * JUMP26/CALL26 relocations for B and BL instructions.
 */

int
grub_arm_64_check_xxxx26_offset (grub_int64_t offset)
{
  const grub_ssize_t offset_low = -(1 << 27), offset_high = (1 << 27) - 1;

  if ((offset < offset_low) || (offset > offset_high))
    return 0;
  return 1;
}

void
grub_arm64_set_xxxx26_offset (grub_uint32_t *place, grub_int64_t offset)
{
  const grub_uint32_t insmask = grub_cpu_to_le32_compile_time (0xfc000000);

  grub_dprintf ("dl", "  reloc_xxxx64 %p %c= 0x%llx\n",
		place, offset > 0 ? '+' : '-',
		offset < 0 ? (long long) -(unsigned long long) offset : offset);

  *place &= insmask;
  *place |= grub_cpu_to_le32 (offset >> 2) & ~insmask;
}

int
grub_arm64_check_hi21_signed (grub_int64_t offset)
{
  if (offset != (grub_int64_t)(grub_int32_t)offset)
    return 0;
  return 1;
}

void
grub_arm64_set_hi21 (grub_uint32_t *place, grub_int64_t offset)
{
  const grub_uint32_t insmask = grub_cpu_to_le32_compile_time (0x9f00001f);
  grub_uint32_t val;

  offset >>= 12;
  
  val = ((offset & 3) << 29) | (((offset >> 2) & 0x7ffff) << 5);
  
  *place &= insmask;
  *place |= grub_cpu_to_le32 (val) & ~insmask;
}

void
grub_arm64_set_abs_lo12 (grub_uint32_t *place, grub_int64_t target)
{
  const grub_uint32_t insmask = grub_cpu_to_le32_compile_time (0xffc003ff);

  *place &= insmask;
  *place |= grub_cpu_to_le32 (target << 10) & ~insmask;
}

void
grub_arm64_set_abs_lo12_ldst64 (grub_uint32_t *place, grub_int64_t target)
{
  const grub_uint32_t insmask = grub_cpu_to_le32_compile_time (0xfff803ff);

  *place &= insmask;
  *place |= grub_cpu_to_le32 (target << 7) & ~insmask;
}

#pragma GCC diagnostic ignored "-Wcast-align"

grub_err_t
grub_arm64_dl_get_tramp_got_size (const void *ehdr, grub_size_t *tramp,
				  grub_size_t *got)
{
  const Elf64_Ehdr *e = ehdr;
  const Elf64_Shdr *s;
  unsigned i;

  *tramp = 0;
  *got = 0;

  for (i = 0, s = (Elf64_Shdr *) ((char *) e + grub_le_to_cpu64 (e->e_shoff));
       i < grub_le_to_cpu16 (e->e_shnum);
       i++, s = (Elf64_Shdr *) ((char *) s + grub_le_to_cpu16 (e->e_shentsize)))
    if (s->sh_type == grub_cpu_to_le32_compile_time (SHT_REL)
	|| s->sh_type == grub_cpu_to_le32_compile_time (SHT_RELA))
      {
	const Elf64_Rela *rel, *max;

	for (rel = (Elf64_Rela *) ((char *) e + grub_le_to_cpu64 (s->sh_offset)),
	       max = (const Elf64_Rela *) ((char *) rel + grub_le_to_cpu64 (s->sh_size));
	     rel < max; rel = (const Elf64_Rela *) ((char *) rel + grub_le_to_cpu64 (s->sh_entsize)))
	  switch (ELF64_R_TYPE (rel->r_info))
	    {
	    case R_AARCH64_CALL26:
	    case R_AARCH64_JUMP26:
	      *tramp += sizeof (struct grub_arm64_trampoline);
	      break;
	    case R_AARCH64_ADR_GOT_PAGE:
	      *got += 8;
	      break;
	    }
      }

  return GRUB_ERR_NONE;
}
