/* grub-pe2elf.c - tool to convert pe image to elf.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2009 Free Software Foundation, Inc.
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
#include <grub/types.h>
#include <grub/util/misc.h>
#include <grub/elf.h>
#include <grub/efi/pe32.h>
#include <grub/misc.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>


/* Please don't internationalise this file. It's pointless.  */

/*
 *  Section layout
 *
 *    null
 *    .text
 *    .rdata
 *    .data
 *    .bss
 *    .modname
 *    .moddeps
 *    .symtab
 *    .strtab
 *    relocation sections
 */

#if GRUB_TARGET_WORDSIZE == 64
typedef Elf64_Rela elf_reloc_t;
#define GRUB_PE32_MACHINE GRUB_PE32_MACHINE_X86_64
#else
typedef Elf32_Rel elf_reloc_t;
#define GRUB_PE32_MACHINE GRUB_PE32_MACHINE_I386
#endif

#define STRTAB_BLOCK	256

static char *strtab;
static int strtab_max, strtab_len;

static Elf_Ehdr ehdr;
static Elf_Shdr *shdr;
static int num_sections, first_reloc_section, reloc_sections_end, symtab_section, strtab_section;
static grub_uint32_t offset, image_base;

static int
insert_string (const char *name)
{
  int len, result;

  if (*name == '_')
    name++;

  len = strlen (name);
  if (strtab_len + len >= strtab_max)
    {
      strtab_max += STRTAB_BLOCK;
      strtab = xrealloc (strtab, strtab_max);
    }

  strcpy (strtab + strtab_len, name);
  result = strtab_len;
  strtab_len += len + 1;

  return result;
}

static int *
write_section_data (FILE* fp, const char *name, char *image,
                    struct grub_pe32_coff_header *pe_chdr,
                    struct grub_pe32_section_table *pe_shdr)
{
  int *section_map;
  int i;
  grub_uint32_t last_category = 0;
  grub_uint32_t idx, idx_reloc;
  char *pe_strtab = (image + pe_chdr->symtab_offset
		     + pe_chdr->num_symbols * sizeof (struct grub_pe32_symbol));

  section_map = xcalloc (2 * pe_chdr->num_sections + 5, sizeof (int));
  section_map[0] = 0;
  shdr = xcalloc (2 * pe_chdr->num_sections + 5, sizeof (shdr[0]));
  idx = 1;
  idx_reloc = pe_chdr->num_sections + 1;

  for (i = 0; i < pe_chdr->num_sections; i++, pe_shdr++)
    {
      grub_uint32_t category;
      const char *shname = pe_shdr->name;
      grub_size_t secsize;

      if (shname[0] == '/' && grub_isdigit (shname[1]))
      {
        char t[sizeof (pe_shdr->name) + 1];
        memcpy (t, shname, sizeof (pe_shdr->name));
        t[sizeof (pe_shdr->name)] = 0;
        shname = pe_strtab + atoi (t + 1);
      }

      secsize = pe_shdr->raw_data_size;

      shdr[idx].sh_type = SHT_PROGBITS;

      if (! strcmp (shname, ".text"))
        {
          category = 0;
          shdr[idx].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
        }
      else if (! strncmp (shname, ".rdata", 6))
        {
          category = 1;
          shdr[idx].sh_flags = SHF_ALLOC;
        }
      else if (! strcmp (shname, ".data"))
        {
          category = 2;
          shdr[idx].sh_flags = SHF_ALLOC | SHF_WRITE;
        }
      else if (! strcmp (shname, ".bss"))
        {
          category = 3;
	  shdr[idx].sh_type = SHT_NOBITS;
          shdr[idx].sh_flags = SHF_ALLOC | SHF_WRITE;
	  if (secsize < pe_shdr->virtual_size)
	    secsize = pe_shdr->virtual_size;
        }
      else if (strcmp (shname, ".modname") == 0 || strcmp (shname, ".moddeps") == 0
	       || strcmp (shname, ".module_license") == 0)
        category = 4;
      else
        {
          section_map[i + 1] = -1;
          continue;
        }

      if (category < last_category)
	grub_util_error ("out of order sections");

      section_map[i + 1] = idx;

      if (pe_shdr->virtual_size
	  && pe_shdr->virtual_size < secsize)
	secsize = pe_shdr->virtual_size;

      shdr[idx].sh_size = secsize;
      shdr[idx].sh_addralign = 1 << (((pe_shdr->characteristics >>
                                       GRUB_PE32_SCN_ALIGN_SHIFT) &
                                      GRUB_PE32_SCN_ALIGN_MASK) - 1);
      shdr[idx].sh_addr = pe_shdr->virtual_address + image_base;

      if (shdr[idx].sh_type != SHT_NOBITS)
        {
          shdr[idx].sh_offset = offset;
          grub_util_write_image_at (image + pe_shdr->raw_data_offset,
                                    pe_shdr->raw_data_size, offset, fp,
				    shname);

          offset += secsize;
        }

      if (pe_shdr->relocations_offset)
        {
          char relname[5 + strlen (shname)];

          sprintf (relname, ".rel%s", shname);

          shdr[idx_reloc].sh_name = insert_string (relname);
          shdr[idx_reloc].sh_link = i;
          shdr[idx_reloc].sh_info = idx;

          shdr[idx].sh_name = shdr[idx_reloc].sh_name + 4;

          idx_reloc++;
        }
      else
        shdr[idx].sh_name = insert_string (shname);
      idx++;
    }

  idx_reloc -= pe_chdr->num_sections + 1;
  num_sections = idx + idx_reloc + 2;
  first_reloc_section = idx;
  reloc_sections_end = idx + idx_reloc;
  memmove (shdr + idx, shdr + pe_chdr->num_sections + 1,
	   idx_reloc * sizeof (shdr[0]));
  memset (shdr + idx + idx_reloc, 0, 3 * sizeof (shdr[0]));
  memset (shdr, 0, sizeof (shdr[0]));

  symtab_section = idx + idx_reloc;
  strtab_section = idx + idx_reloc + 1;

  return section_map;
}

static void
write_reloc_section (FILE* fp, const char *name, char *image,
                     struct grub_pe32_coff_header *pe_chdr,
                     struct grub_pe32_section_table  *pe_shdr,
                     Elf_Sym *symtab,
                     int *symtab_map)
{
  int i;

  for (i = first_reloc_section; i < reloc_sections_end; i++)
    {
      struct grub_pe32_section_table *pe_sec;
      struct grub_pe32_reloc *pe_rel;
      elf_reloc_t *rel;
      int num_rels, j, modified;

      pe_sec = pe_shdr + shdr[i].sh_link;
      pe_rel = (struct grub_pe32_reloc *) (image + pe_sec->relocations_offset);
      rel = (elf_reloc_t *) xcalloc (pe_sec->num_relocations, sizeof (elf_reloc_t));
      num_rels = 0;
      modified = 0;

      for (j = 0; j < pe_sec->num_relocations; j++, pe_rel++)
        {
          int type;
          grub_uint32_t ofs, *addr;

          if ((pe_rel->symtab_index >= pe_chdr->num_symbols) ||
              (symtab_map[pe_rel->symtab_index] == -1))
            grub_util_error ("invalid symbol");

          ofs = pe_rel->offset - pe_sec->virtual_address;
          addr = (grub_uint32_t *)(image + pe_sec->raw_data_offset + ofs);

          switch (pe_rel->type)
	    {
#if GRUB_TARGET_WORDSIZE == 64
	    case 1:
	      type = R_X86_64_64;
	      rel[num_rels].r_addend = *(grub_int64_t *)addr;
	      *(grub_int64_t *)addr = 0;
	      modified = 1;
	      break;
	    case 4:
	      type = R_X86_64_PC32;
	      rel[num_rels].r_addend = *(grub_int32_t *)addr;
	      *addr = 0;
	      modified = 1;
	      break;
	    case 14:
	      type = R_X86_64_PC64;
	      rel[num_rels].r_addend = *(grub_uint64_t *)addr - 8;
	      *(grub_uint64_t *)addr = 0;
	      modified = 1;
	      break;
#else
	    case GRUB_PE32_REL_I386_DIR32:
	      type = R_386_32;
	      break;
	    case GRUB_PE32_REL_I386_REL32:
	      type = R_386_PC32;
	      break;
#endif
	    default:
	      grub_util_error ("unknown pe relocation type %d", pe_rel->type);
	    }

          if (type ==
#if GRUB_TARGET_WORDSIZE == 64
	      R_386_PC32
#else
	      R_X86_64_PC32
#endif

	      )
            {
              unsigned char code;

              code = image[pe_sec->raw_data_offset + ofs - 1];

#if GRUB_TARGET_WORDSIZE == 32
              if (((code != 0xe8) && (code != 0xe9)) || (*addr))
                grub_util_error ("invalid relocation (%x %x)", code, *addr);
#endif

              if (symtab[symtab_map[pe_rel->symtab_index]].st_shndx
		  && symtab[symtab_map[pe_rel->symtab_index]].st_shndx
		  == shdr[i].sh_info)
                {
		  modified = 1;
                  *addr += (symtab[symtab_map[pe_rel->symtab_index]].st_value
			    - ofs - 4);

                  continue;
                }
              else
		{
#if GRUB_TARGET_WORDSIZE == 64
		  rel[num_rels].r_addend -= 4;
#else
		  modified = 1;
		  *addr = -4;
#endif
		}
            }

          rel[num_rels].r_offset = ofs;
          rel[num_rels].r_info = ELF_R_INFO (symtab_map[pe_rel->symtab_index],
					     type);
          num_rels++;
        }

      if (modified)
        grub_util_write_image_at (image + pe_sec->raw_data_offset,
                                  shdr[shdr[i].sh_info].sh_size,
                                  shdr[shdr[i].sh_info].sh_offset,
                                  fp, name);

#if GRUB_TARGET_WORDSIZE == 64
      shdr[i].sh_type = SHT_RELA;
#else
      shdr[i].sh_type = SHT_REL;
#endif
      shdr[i].sh_offset = offset;
      shdr[i].sh_link = symtab_section;
      shdr[i].sh_addralign = 4;
      shdr[i].sh_entsize = sizeof (elf_reloc_t);
      shdr[i].sh_size = num_rels * sizeof (elf_reloc_t);

      grub_util_write_image_at (rel, shdr[i].sh_size, offset, fp, name);
      offset += shdr[i].sh_size;
      free (rel);
    }
}

static void
write_symbol_table (FILE* fp, const char *name, char *image,
                    struct grub_pe32_coff_header *pe_chdr,
                    struct grub_pe32_section_table *pe_shdr,
                    int *section_map)
{
  struct grub_pe32_symbol *pe_symtab;
  char *pe_strtab;
  Elf_Sym *symtab;
  int *symtab_map, num_syms;
  int i;

  pe_symtab = (struct grub_pe32_symbol *) (image + pe_chdr->symtab_offset);
  pe_strtab = (char *) (pe_symtab + pe_chdr->num_symbols);

  symtab = (Elf_Sym *) xcalloc (pe_chdr->num_symbols + 1, sizeof (Elf_Sym));
  num_syms = 1;

  symtab_map = (int *) xcalloc (pe_chdr->num_symbols, sizeof (int));

  for (i = 0; i < (int) pe_chdr->num_symbols;
       i += pe_symtab->num_aux + 1, pe_symtab += pe_symtab->num_aux + 1)
    {
      int bind, type;

      symtab_map[i] = -1;
      if ((pe_symtab->section > pe_chdr->num_sections) ||
          (section_map[pe_symtab->section] == -1))
        continue;

      if (! pe_symtab->section)
        type = STT_NOTYPE;
      else if (pe_symtab->type == GRUB_PE32_DT_FUNCTION)
        type = STT_FUNC;
      else
        type = STT_OBJECT;

      if (pe_symtab->storage_class == GRUB_PE32_SYM_CLASS_EXTERNAL)
        bind = STB_GLOBAL;
      else
        bind = STB_LOCAL;

      if ((pe_symtab->type != GRUB_PE32_DT_FUNCTION) && (pe_symtab->num_aux))
        {
          if (! pe_symtab->value)
            type = STT_SECTION;

          symtab[num_syms].st_name = shdr[section_map[pe_symtab->section]].sh_name;
        }
      else
        {
	  char short_name[9];
          char *symname;

	  if (pe_symtab->long_name[0])
	    {
	      strncpy (short_name, pe_symtab->short_name, 8);
	      short_name[8] = 0;
	      symname = short_name;
	    }
	  else
	    symname = pe_strtab + pe_symtab->long_name[1];

          if ((strcmp (symname, "_grub_mod_init")) &&
              (strcmp (symname, "_grub_mod_fini")) &&
	      (strcmp (symname, "grub_mod_init")) &&
              (strcmp (symname, "grub_mod_fini")) &&
              (bind == STB_LOCAL))
              continue;

          symtab[num_syms].st_name = insert_string (symname);
        }

      symtab[num_syms].st_shndx = section_map[pe_symtab->section];
      symtab[num_syms].st_value = pe_symtab->value;
      symtab[num_syms].st_info = ELF_ST_INFO (bind, type);

      symtab_map[i] = num_syms;
      num_syms++;
    }

  write_reloc_section (fp, name, image, pe_chdr, pe_shdr,
		       symtab, symtab_map);

  shdr[symtab_section].sh_name = insert_string (".symtab");
  shdr[symtab_section].sh_type = SHT_SYMTAB;
  shdr[symtab_section].sh_offset = offset;
  shdr[symtab_section].sh_size = num_syms * sizeof (Elf_Sym);
  shdr[symtab_section].sh_entsize = sizeof (Elf_Sym);
  shdr[symtab_section].sh_link = strtab_section;
  shdr[symtab_section].sh_addralign = 4;

  grub_util_write_image_at (symtab, shdr[symtab_section].sh_size,
                            offset, fp, name);
  offset += shdr[symtab_section].sh_size;

  free (symtab);
  free (symtab_map);
}

static void
write_string_table (FILE *fp, const char *name)
{
  shdr[strtab_section].sh_name = insert_string (".strtab");
  shdr[strtab_section].sh_type = SHT_STRTAB;
  shdr[strtab_section].sh_offset = offset;
  shdr[strtab_section].sh_size = strtab_len;
  shdr[strtab_section].sh_addralign = 1;
  grub_util_write_image_at (strtab, strtab_len, offset, fp,
			    name);
  offset += strtab_len;

  free (strtab);
}

static void
write_section_header (FILE *fp, const char *name)
{
  ehdr.e_ident[EI_MAG0] = ELFMAG0;
  ehdr.e_ident[EI_MAG1] = ELFMAG1;
  ehdr.e_ident[EI_MAG2] = ELFMAG2;
  ehdr.e_ident[EI_MAG3] = ELFMAG3;
  ehdr.e_ident[EI_VERSION] = EV_CURRENT;
  ehdr.e_version = EV_CURRENT;
  ehdr.e_type = ET_REL;

#if GRUB_TARGET_WORDSIZE == 64
  ehdr.e_ident[EI_CLASS] = ELFCLASS64;
  ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
  ehdr.e_machine = EM_X86_64;
#else
  ehdr.e_ident[EI_CLASS] = ELFCLASS32;
  ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
  ehdr.e_machine = EM_386;
#endif
  ehdr.e_ehsize = sizeof (ehdr);
  ehdr.e_shentsize = sizeof (Elf_Shdr);
  ehdr.e_shstrndx = strtab_section;

  ehdr.e_shoff = offset;
  ehdr.e_shnum = num_sections;
  grub_util_write_image_at (shdr, sizeof (Elf_Shdr) * num_sections,
                            offset, fp, name);

  grub_util_write_image_at (&ehdr, sizeof (Elf_Ehdr), 0, fp, name);
}

static void
convert_pe (FILE* fp, const char *name, char *image)
{
  struct grub_pe32_coff_header *pe_chdr;
  struct grub_pe32_section_table *pe_shdr;
  int *section_map;

  if (image[0] == 'M' && image[1] == 'Z')
    pe_chdr = (struct grub_pe32_coff_header *) (image + (grub_le_to_cpu32 (((grub_uint32_t *)image)[0xf]) + 4));
  else
    pe_chdr = (struct grub_pe32_coff_header *) image;
  if (grub_le_to_cpu16 (pe_chdr->machine) != GRUB_PE32_MACHINE)
    grub_util_error ("invalid coff image (%x != %x)",
		     grub_le_to_cpu16 (pe_chdr->machine), GRUB_PE32_MACHINE);

  strtab = xmalloc (STRTAB_BLOCK);
  strtab_max = STRTAB_BLOCK;
  strtab[0] = 0;
  strtab_len = 1;

  offset = sizeof (ehdr);
  if (pe_chdr->optional_header_size)
    {
#if GRUB_TARGET_WORDSIZE == 64
      struct grub_pe64_optional_header *o;
#else
      struct grub_pe32_optional_header *o;
#endif
      o = (void *) (pe_chdr + 1);
      image_base = o->image_base;
    }
  pe_shdr = (struct grub_pe32_section_table *) ((char *) (pe_chdr + 1) + pe_chdr->optional_header_size);

  section_map = write_section_data (fp, name, image, pe_chdr, pe_shdr);

  write_symbol_table (fp, name, image, pe_chdr, pe_shdr, section_map);
  free (section_map);

  write_string_table (fp, name);

  write_section_header (fp, name);
}

int
main (int argc, char *argv[])
{
  char *image;
  FILE* fp;
  char *in, *out;

  /* Obtain PATH.  */
  if (1 >= argc)
    {
      fprintf (stderr, "Filename not specified.\n");
      return 1;
    }

  in = argv[1];
  if (argc > 2)
    out = argv[2];
  else
    out = in;
  image = grub_util_read_image (in);

  fp = grub_util_fopen (out, "wb");
  if (! fp)
    grub_util_error ("cannot open %s", out);

  convert_pe (fp, out, image);

  fclose (fp);

  return 0;
}
