/* dl.c - arch-dependent part of loadable module support */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2018  Free Software Foundation, Inc.
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

/*
 * Instructions and instruction encoding are documented in the RISC-V
 * specification. This file is based on version 2.2:
 *
 * https://github.com/riscv/riscv-isa-manual/blob/master/release/riscv-spec-v2.2.pdf
 */
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
  if (e->e_ident[EI_DATA] != ELFDATA2LSB || e->e_machine != EM_RISCV)
    return grub_error (GRUB_ERR_BAD_OS,
		       N_("invalid arch-dependent ELF magic"));

  return GRUB_ERR_NONE;
}

#pragma GCC diagnostic ignored "-Wcast-align"

/* Relocate symbols. */
grub_err_t
grub_arch_dl_relocate_symbols (grub_dl_t mod, void *ehdr,
			       Elf_Shdr *s, grub_dl_segment_t seg)
{
  Elf_Rel *rel, *max;

  for (rel = (Elf_Rel *) ((char *) ehdr + s->sh_offset),
	 max = (Elf_Rel *) ((char *) rel + s->sh_size);
       rel < max;
       rel = (Elf_Rel *) ((char *) rel + s->sh_entsize))
    {
      Elf_Sym *sym;
      void *place;
      grub_size_t sym_addr;

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
	case R_RISCV_32:
	  {
	    grub_uint32_t *abs_place = place;

	    grub_dprintf ("dl", "  reloc_abs32 %p => 0x%016llx\n",
			  place, (unsigned long long) sym_addr);

	    *abs_place = (grub_uint32_t) sym_addr;
	  }
	  break;
	case R_RISCV_64:
	  {
	    grub_size_t *abs_place = place;

	    grub_dprintf ("dl", "  reloc_abs64 %p => 0x%016llx\n",
			  place, (unsigned long long) sym_addr);

	    *abs_place = (grub_size_t) sym_addr;
	  }
	  break;

	case R_RISCV_ADD8:
	  {
	    grub_uint8_t *abs_place = place;

	    *abs_place += (grub_uint8_t) sym_addr;
	  }
	  break;
	case R_RISCV_ADD16:
	  {
	    grub_uint16_t *abs_place = place;

	    *abs_place += (grub_uint16_t) sym_addr;
	  }
	  break;
	case R_RISCV_ADD32:
	  {
	    grub_uint32_t *abs_place = place;

	    *abs_place += (grub_uint32_t) sym_addr;
	  }
	  break;
	case R_RISCV_ADD64:
	  {
	    grub_size_t *abs_place = place;

	    *abs_place += (grub_size_t) sym_addr;
	  }
	  break;

	case R_RISCV_SUB8:
	  {
	    grub_uint8_t *abs_place = place;

	    *abs_place -= (grub_uint8_t) sym_addr;
	  }
	  break;
	case R_RISCV_SUB16:
	  {
	    grub_uint16_t *abs_place = place;

	    *abs_place -= (grub_uint16_t) sym_addr;
	  }
	  break;
	case R_RISCV_SUB32:
	  {
	    grub_uint32_t *abs_place = place;

	    *abs_place -= (grub_uint32_t) sym_addr;
	  }
	  break;
	case R_RISCV_SUB64:
	  {
	    grub_size_t *abs_place = place;

	    *abs_place -= (grub_size_t) sym_addr;
	  }
	  break;

	case R_RISCV_BRANCH:
	  {
	    grub_uint32_t *abs_place = place;
	    grub_ssize_t off = sym_addr - (grub_addr_t) place;
	    grub_uint32_t imm12 = (off & 0x1000) << (31 - 12);
	    grub_uint32_t imm11 = (off & 0x800) >> (11 - 7);
	    grub_uint32_t imm10_5 = (off & 0x7e0) << (30 - 10);
	    grub_uint32_t imm4_1 = (off & 0x1e) << (11 - 4);
	    *abs_place = (*abs_place & 0x1fff07f)
			 | imm12 | imm11 | imm10_5 | imm4_1;
	  }
	  break;

	case R_RISCV_JAL:
	  {
	    grub_uint32_t *abs_place = place;
	    grub_ssize_t off = sym_addr - (grub_addr_t) place;
	    grub_uint32_t imm20 = (off & 0x100000) << (31 - 20);
	    grub_uint32_t imm19_12 = (off & 0xff000);
	    grub_uint32_t imm11 = (off & 0x800) << (20 - 11);
	    grub_uint32_t imm10_1 = (off & 0x7fe) << (30 - 10);
	    *abs_place = (*abs_place & 0xfff)
			 | imm20 | imm19_12 | imm11 | imm10_1;
	  }
	  break;

	case R_RISCV_CALL:
	  {
	    grub_uint32_t *abs_place = place;
	    grub_ssize_t off = sym_addr - (grub_addr_t) place;
	    grub_uint32_t hi20, lo12;

	    if (off != (grub_int32_t) off)
	      return grub_error (GRUB_ERR_BAD_MODULE, "relocation overflow");

	    hi20 = (off + 0x800) & 0xfffff000;
	    lo12 = (off - hi20) & 0xfff;
	    abs_place[0] = (abs_place[0] & 0xfff) | hi20;
	    abs_place[1] = (abs_place[1] & 0xfffff) | (lo12 << 20);
	  }
	  break;

	case R_RISCV_RVC_BRANCH:
	  {
	    grub_uint16_t *abs_place = place;
	    grub_ssize_t off = sym_addr - (grub_addr_t) place;
	    grub_uint16_t imm8 = (off & 0x100) << (12 - 8);
	    grub_uint16_t imm7_6 = (off & 0xc0) >> (6 - 5);
	    grub_uint16_t imm5 = (off & 0x20) >> (5 - 2);
	    grub_uint16_t imm4_3 = (off & 0x18) << (12 - 5);
	    grub_uint16_t imm2_1 = (off & 0x6) << (12 - 10);
	    *abs_place = (*abs_place & 0xe383)
			 | imm8 | imm7_6 | imm5 | imm4_3 | imm2_1;
	  }
	  break;

	case R_RISCV_RVC_JUMP:
	  {
	    grub_uint16_t *abs_place = place;
	    grub_ssize_t off = sym_addr - (grub_addr_t) place;
	    grub_uint16_t imm11 = (off & 0x800) << (12 - 11);
	    grub_uint16_t imm10 = (off & 0x400) >> (10 - 8);
	    grub_uint16_t imm9_8 = (off & 0x300) << (12 - 11);
	    grub_uint16_t imm7 = (off & 0x80) >> (7 - 6);
	    grub_uint16_t imm6 = (off & 0x40) << (12 - 11);
	    grub_uint16_t imm5 = (off & 0x20) >> (5 - 2);
	    grub_uint16_t imm4 = (off & 0x10) << (12 - 5);
	    grub_uint16_t imm3_1 = (off & 0xe) << (12 - 10);
	    *abs_place = ((*abs_place & 0xe003)
			  | imm11 | imm10 | imm9_8 | imm7 | imm6
			  | imm5 | imm4 | imm3_1);
	  }
	  break;

	case R_RISCV_PCREL_HI20:
	  {
	    grub_uint32_t *abs_place = place;
	    grub_ssize_t off = sym_addr - (grub_addr_t) place;
	    grub_int32_t hi20;

	    if (off != (grub_int32_t)off)
	      return grub_error (GRUB_ERR_BAD_MODULE, "relocation overflow");

	    hi20 = (off + 0x800) & 0xfffff000;
	    *abs_place = (*abs_place & 0xfff) | hi20;
	  }
	break;

	case R_RISCV_PCREL_LO12_I:
	case R_RISCV_PCREL_LO12_S:
	  {
	    grub_uint32_t *t32 = place;
	    Elf_Rela *rel2;
	    /* Search backwards for matching HI20 reloc.  */
	    for (rel2 = (Elf_Rela *) ((char *) rel - s->sh_entsize);
		    (unsigned long)rel2 >= ((unsigned long)ehdr + s->sh_offset);
		    rel2 = (Elf_Rela *) ((char *) rel2 - s->sh_entsize))
	      {
		Elf_Addr rel2_info;
		Elf_Addr rel2_offset;
		Elf_Addr rel2_sym_addr;
		Elf_Addr rel2_loc;
		grub_ssize_t rel2_off;
		grub_ssize_t off;
		Elf_Sym *sym2;

		rel2_offset = rel2->r_offset;
		rel2_info = rel2->r_info;
		rel2_loc = (grub_addr_t) seg->addr + rel2_offset;

		if (ELF_R_TYPE (rel2_info) == R_RISCV_PCREL_HI20
		    && rel2_loc == sym_addr)
		  {
		    sym2 = (Elf_Sym *) ((char *) mod->symtab
				+ mod->symsize * ELF_R_SYM (rel2->r_info));
		    rel2_sym_addr = sym2->st_value;
		    if (s->sh_type == SHT_RELA)
		      rel2_sym_addr += ((Elf_Rela *) rel2)->r_addend;

		    rel2_off = rel2_sym_addr - rel2_loc;
		    off = rel2_off - ((rel2_off + 0x800) & 0xfffff000);

		    if (ELF_R_TYPE (rel->r_info) == R_RISCV_PCREL_LO12_I)
		      *t32 = (*t32 & 0xfffff) | (off & 0xfff) << 20;
		    else
		      {
			grub_uint32_t imm11_5 = (off & 0xfe0) << (31 - 11);
			grub_uint32_t imm4_0 = (off & 0x1f) << (11 - 4);
			*t32 = (*t32 & 0x1fff07f) | imm11_5 | imm4_0;
		      }
		    break;
		  }
	      }
	    if ((unsigned long)rel2 < ((unsigned long)ehdr + s->sh_offset))
	      return grub_error (GRUB_ERR_BAD_MODULE, "cannot find matching HI20 relocation");
	  }
	  break;

	case R_RISCV_HI20:
	  {
	    grub_uint32_t *abs_place = place;
	    *abs_place = (*abs_place & 0xfff) |
			 (((grub_int32_t) sym_addr + 0x800) & 0xfffff000);
	  }
	  break;

	case R_RISCV_LO12_I:
	  {
	    grub_uint32_t *abs_place = place;
	    grub_int32_t lo12 = (grub_int32_t) sym_addr -
				(((grub_int32_t) sym_addr + 0x800) & 0xfffff000);
	    *abs_place = (*abs_place & 0xfffff) | ((lo12 & 0xfff) << 20);
	  }
	  break;

	case R_RISCV_LO12_S:
	  {
	    grub_uint32_t *abs_place = place;
	    grub_int32_t lo12 = (grub_int32_t) sym_addr -
				(((grub_int32_t) sym_addr + 0x800) & 0xfffff000);
	    grub_uint32_t imm11_5 = (lo12 & 0xfe0) << (31 - 11);
	    grub_uint32_t imm4_0 = (lo12 & 0x1f) << (11 - 4);
	    *abs_place = (*abs_place & 0x1fff07f) | imm11_5 | imm4_0;
	  }
	  break;

	case R_RISCV_RELAX:
	  break;
	default:
	  {
	    char rel_info[17]; /* log16(2^64) = 16, plus NUL. */

	    grub_snprintf (rel_info, sizeof (rel_info) - 1, "%" PRIxGRUB_UINT64_T,
			   (grub_uint64_t) ELF_R_TYPE (rel->r_info));
	    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
			       N_("relocation 0x%s is not implemented yet"), rel_info);
	  }
	}
    }

  return GRUB_ERR_NONE;
}
