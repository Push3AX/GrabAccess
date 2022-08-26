/* dl.c - arch-dependent part of loadable module support */
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
#include <grub/cpu/reloc.h>

#define LDR 0x58000050
#define BR 0xd61f0200


/*
 * Check if EHDR is a valid ELF header.
 */
grub_err_t
grub_arch_dl_check_header (void *ehdr)
{
  Elf_Ehdr *e = ehdr;

  /* Check the magic numbers.  */
  if (e->e_ident[EI_CLASS] != ELFCLASS64
      || e->e_ident[EI_DATA] != ELFDATA2LSB || e->e_machine != EM_AARCH64)
    return grub_error (GRUB_ERR_BAD_OS,
		       N_("invalid arch-dependent ELF magic"));

  return GRUB_ERR_NONE;
}

#pragma GCC diagnostic ignored "-Wcast-align"

/*
 * Unified function for both REL and RELA 
 */
grub_err_t
grub_arch_dl_relocate_symbols (grub_dl_t mod, void *ehdr,
			       Elf_Shdr *s, grub_dl_segment_t seg)
{
  Elf_Rel *rel, *max;
  unsigned unmatched_adr_got_page = 0;

  for (rel = (Elf_Rel *) ((char *) ehdr + s->sh_offset),
	 max = (Elf_Rel *) ((char *) rel + s->sh_size);
       rel < max;
       rel = (Elf_Rel *) ((char *) rel + s->sh_entsize))
    {
      Elf_Sym *sym;
      void *place;
      grub_uint64_t sym_addr;

      if (rel->r_offset >= seg->size)
	return grub_error (GRUB_ERR_BAD_MODULE,
			   "reloc offset is out of the segment");

      sym = (Elf_Sym *) ((char *) mod->symtab
			 + mod->symsize * ELF_R_SYM (rel->r_info));

      sym_addr = sym->st_value;
      if (s->sh_type == SHT_RELA)
	sym_addr += ((Elf_Rela *) rel)->r_addend;

      place = (void *) ((grub_addr_t) seg->addr + rel->r_offset);

      switch (ELF_R_TYPE (rel->r_info))
	{
	case R_AARCH64_ABS64:
	  {
	    grub_uint64_t *abs_place = place;

	    grub_dprintf ("dl", "  reloc_abs64 %p => 0x%016llx\n",
			  place, (unsigned long long) sym_addr);

	    *abs_place = (grub_uint64_t) sym_addr;
	  }
	  break;
	case R_AARCH64_ADD_ABS_LO12_NC:
	  grub_arm64_set_abs_lo12 (place, sym_addr);
	  break;
	case R_AARCH64_LDST64_ABS_LO12_NC:
	  grub_arm64_set_abs_lo12_ldst64 (place, sym_addr);
	  break;
	case R_AARCH64_CALL26:
	case R_AARCH64_JUMP26:
	  {
	    grub_int64_t offset = sym_addr - (grub_uint64_t) place;

	    if (!grub_arm_64_check_xxxx26_offset (offset))
	      {
		struct grub_arm64_trampoline *tp = mod->trampptr;
		mod->trampptr = tp + 1;
		tp->ldr = LDR;
		tp->br = BR;
		tp->addr = sym_addr;
		offset = (grub_uint8_t *) tp - (grub_uint8_t *) place;
	      }

	    if (!grub_arm_64_check_xxxx26_offset (offset))
		return grub_error (GRUB_ERR_BAD_MODULE,
				   "trampoline out of range");

	    grub_arm64_set_xxxx26_offset (place, offset);
	  }
	  break;
	case R_AARCH64_PREL32:
	  {
	    grub_int64_t value;
	    Elf64_Word *addr32 = place;
	    value = ((grub_int32_t) *addr32) + sym_addr -
	      (Elf64_Xword) (grub_addr_t) seg->addr - rel->r_offset;
	    if (value != (grub_int32_t) value)
	      return grub_error (GRUB_ERR_BAD_MODULE, "relocation out of range");
	    grub_dprintf("dl", "  reloc_prel32 %p => 0x%016llx\n",
			  place, (unsigned long long) sym_addr);
	    *addr32 = value;
	  }
	  break;
	case R_AARCH64_ADR_GOT_PAGE:
	  {
	    grub_uint64_t *gp = mod->gotptr;
	    Elf_Rela *rel2;
	    grub_int64_t gpoffset = ((grub_uint64_t) gp & ~0xfffULL) - (((grub_uint64_t) place) & ~0xfffULL);
	    *gp = (grub_uint64_t) sym_addr;
	    mod->gotptr = gp + 1;
	    unmatched_adr_got_page++;
	    grub_dprintf("dl", "  reloc_got %p => 0x%016llx (0x%016llx)\n",
			 place, (unsigned long long) sym_addr, (unsigned long long) gp);
	    if (!grub_arm64_check_hi21_signed (gpoffset))
		return grub_error (GRUB_ERR_BAD_MODULE,
				   "HI21 out of range");
	    grub_arm64_set_hi21(place, gpoffset);
	    for (rel2 = (Elf_Rela *) ((char *) rel + s->sh_entsize);
		 rel2 < (Elf_Rela *) max;
		 rel2 = (Elf_Rela *) ((char *) rel2 + s->sh_entsize))
	      if (ELF_R_SYM (rel2->r_info)
		  == ELF_R_SYM (rel->r_info)
		  && ((Elf_Rela *) rel)->r_addend == rel2->r_addend
		  && ELF_R_TYPE (rel2->r_info) == R_AARCH64_LD64_GOT_LO12_NC)
		{
		  grub_arm64_set_abs_lo12_ldst64 ((void *) ((grub_addr_t) seg->addr + rel2->r_offset),
						  (grub_uint64_t)gp);
		  break;
		}
	    if (rel2 >= (Elf_Rela *) max)
	      return grub_error (GRUB_ERR_BAD_MODULE,
				 "ADR_GOT_PAGE without matching LD64_GOT_LO12_NC");
	  }
	  break;
	case R_AARCH64_LD64_GOT_LO12_NC:
	  if (unmatched_adr_got_page == 0)
	    return grub_error (GRUB_ERR_BAD_MODULE,
			       "LD64_GOT_LO12_NC without matching ADR_GOT_PAGE");
	  unmatched_adr_got_page--;
	  break;
	case R_AARCH64_ADR_PREL_PG_HI21:
	  {
	    grub_int64_t offset = (sym_addr & ~0xfffULL) - (((grub_uint64_t) place) & ~0xfffULL);

	    if (!grub_arm64_check_hi21_signed (offset))
		return grub_error (GRUB_ERR_BAD_MODULE,
				   "HI21 out of range");

	    grub_arm64_set_hi21 (place, offset);
	  }
	  break;

	default:
	  {
	    char rel_info[17]; /* log16(2^64) = 16, plus NUL. */

	    grub_snprintf (rel_info, sizeof (rel_info) - 1, "%" PRIxGRUB_UINT64_T,
			   ELF_R_TYPE (rel->r_info));
	    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
			       N_("relocation 0x%s is not implemented yet"), rel_info);
	  }
	}
    }

  return GRUB_ERR_NONE;
}
