#include <grub/loader.h>
#include <grub/i386/bsd.h>
#include <grub/mm.h>
#include <grub/elf.h>
#include <grub/misc.h>
#include <grub/i386/relocator.h>
#include <grub/i18n.h>

#define ALIGN_PAGE(a)	ALIGN_UP (a, 4096)

static inline grub_err_t
load (grub_file_t file, const char *filename, void *where, grub_off_t off, grub_size_t size)
{
  if (grub_file_seek (file, off) == (grub_off_t) -1)
    return grub_errno;
  if (grub_file_read (file, where, size) != (grub_ssize_t) size)
    {
      if (grub_errno)
	return grub_errno;
      return grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			 filename);
    }
  return GRUB_ERR_NONE;
}

static inline grub_err_t
read_headers (grub_file_t file, const char *filename, Elf_Ehdr *e, char **shdr)
{
 if (grub_file_seek (file, 0) == (grub_off_t) -1)
    return grub_errno;

  if (grub_file_read (file, (char *) e, sizeof (*e)) != sizeof (*e))
    {
      if (grub_errno)
	return grub_errno;
      return grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			 filename);
    }

  if (e->e_ident[EI_MAG0] != ELFMAG0
      || e->e_ident[EI_MAG1] != ELFMAG1
      || e->e_ident[EI_MAG2] != ELFMAG2
      || e->e_ident[EI_MAG3] != ELFMAG3
      || e->e_ident[EI_VERSION] != EV_CURRENT
      || e->e_version != EV_CURRENT)
    return grub_error (GRUB_ERR_BAD_OS, N_("invalid arch-independent ELF magic"));

  if (e->e_ident[EI_CLASS] != SUFFIX (ELFCLASS))
    return grub_error (GRUB_ERR_BAD_OS, N_("invalid arch-dependent ELF magic"));

  *shdr = grub_calloc (e->e_shnum, e->e_shentsize);
  if (! *shdr)
    return grub_errno;

  if (grub_file_seek (file, e->e_shoff) == (grub_off_t) -1)
    return grub_errno;

  if (grub_file_read (file, *shdr, (grub_uint32_t) e->e_shnum * e->e_shentsize)
      != (grub_ssize_t) ((grub_uint32_t) e->e_shnum * e->e_shentsize))
    {
      if (grub_errno)
	return grub_errno;
      return grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			 filename);
    }

  return GRUB_ERR_NONE;
}

/* On i386 FreeBSD uses "elf module" approarch for 32-bit variant
   and "elf obj module" for 64-bit variant. However it may differ on other
   platforms. So I keep both versions.  */
#if OBJSYM
grub_err_t
SUFFIX (grub_freebsd_load_elfmodule_obj) (struct grub_relocator *relocator,
					  grub_file_t file, int argc,
					  char *argv[], grub_addr_t *kern_end)
{
  Elf_Ehdr e;
  Elf_Shdr *s;
  char *shdr = 0;
  grub_addr_t curload, module;
  grub_err_t err;
  grub_size_t chunk_size = 0;
  void *chunk_src;

  curload = module = ALIGN_PAGE (*kern_end);

  err = read_headers (file, argv[0], &e, &shdr);
  if (err)
    goto out;

  for (s = (Elf_Shdr *) shdr; s < (Elf_Shdr *) ((char *) shdr
						+ e.e_shnum * e.e_shentsize);
       s = (Elf_Shdr *) ((char *) s + e.e_shentsize))
    {
      if (s->sh_size == 0)
	continue;

      if (s->sh_addralign)
	chunk_size = ALIGN_UP (chunk_size + *kern_end, s->sh_addralign)
	  - *kern_end;

      chunk_size += s->sh_size;
    }

  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_addr (relocator, &ch,
					   module, chunk_size);
    if (err)
      goto out;
    chunk_src = get_virtual_current_address (ch);
  }

  for (s = (Elf_Shdr *) shdr; s < (Elf_Shdr *) ((char *) shdr
						+ e.e_shnum * e.e_shentsize);
       s = (Elf_Shdr *) ((char *) s + e.e_shentsize))
    {
      if (s->sh_size == 0)
	continue;

      if (s->sh_addralign)
	curload = ALIGN_UP (curload, s->sh_addralign);
      s->sh_addr = curload;

      grub_dprintf ("bsd", "loading section to %x, size %d, align %d\n",
		    (unsigned) curload, (int) s->sh_size,
		    (int) s->sh_addralign);

      switch (s->sh_type)
	{
	default:
	case SHT_PROGBITS:
	  err = load (file, argv[0], (grub_uint8_t *) chunk_src + curload - *kern_end,
		      s->sh_offset, s->sh_size);
	  if (err)
	    goto out;
	  break;
	case SHT_NOBITS:
	  grub_memset ((grub_uint8_t *) chunk_src + curload - *kern_end, 0,
		       s->sh_size);
	  break;
	}
      curload += s->sh_size;
    }

  *kern_end = ALIGN_PAGE (curload);

  err = grub_freebsd_add_meta_module (argv[0], FREEBSD_MODTYPE_ELF_MODULE_OBJ,
				      argc - 1, argv + 1, module,
				      curload - module);
  if (! err)
    err = grub_bsd_add_meta (FREEBSD_MODINFO_METADATA 
			     | FREEBSD_MODINFOMD_ELFHDR,
			     &e, sizeof (e));
  if (! err)
    err = grub_bsd_add_meta (FREEBSD_MODINFO_METADATA
			     | FREEBSD_MODINFOMD_SHDR,
			     shdr, e.e_shnum * e.e_shentsize);

out:
  grub_free (shdr);
  return err;
}

#else

grub_err_t
SUFFIX (grub_freebsd_load_elfmodule) (struct grub_relocator *relocator,
				      grub_file_t file, int argc, char *argv[],
				      grub_addr_t *kern_end)
{
  Elf_Ehdr e;
  Elf_Shdr *s;
  char *shdr = 0;
  grub_addr_t curload, module;
  grub_err_t err;
  grub_size_t chunk_size = 0;
  void *chunk_src;

  curload = module = ALIGN_PAGE (*kern_end);

  err = read_headers (file, argv[0], &e, &shdr);
  if (err)
    goto out;

  for (s = (Elf_Shdr *) shdr; s < (Elf_Shdr *) ((char *) shdr
						+ e.e_shnum * e.e_shentsize);
       s = (Elf_Shdr *) ((char *) s + e.e_shentsize))
    {
      if (s->sh_size == 0)
	continue;

      if (! (s->sh_flags & SHF_ALLOC))
	continue;
      if (chunk_size < s->sh_addr + s->sh_size)
	chunk_size = s->sh_addr + s->sh_size;
    }

  if (chunk_size < sizeof (e))
    chunk_size = sizeof (e);
  chunk_size += (grub_uint32_t) e.e_phnum * e.e_phentsize;
  chunk_size += (grub_uint32_t) e.e_shnum * e.e_shentsize;

  {
    grub_relocator_chunk_t ch;

    err = grub_relocator_alloc_chunk_addr (relocator, &ch,
					   module, chunk_size);
    if (err)
      goto out;

    chunk_src = get_virtual_current_address (ch);
  }

  for (s = (Elf_Shdr *) shdr; s < (Elf_Shdr *) ((char *) shdr
						+ e.e_shnum * e.e_shentsize);
       s = (Elf_Shdr *) ((char *) s + e.e_shentsize))
    {
      if (s->sh_size == 0)
	continue;

      if (! (s->sh_flags & SHF_ALLOC))
	continue;

      grub_dprintf ("bsd", "loading section to %x, size %d, align %d\n",
		    (unsigned) curload, (int) s->sh_size,
		    (int) s->sh_addralign);

      switch (s->sh_type)
	{
	default:
	case SHT_PROGBITS:
	  err = load (file, argv[0],
		      (grub_uint8_t *) chunk_src + module
		      + s->sh_addr - *kern_end,
		      s->sh_offset, s->sh_size);
	  if (err)
	    goto out;
	  break;
	case SHT_NOBITS:
	  grub_memset ((grub_uint8_t *) chunk_src + module
		      + s->sh_addr - *kern_end, 0, s->sh_size);
	  break;
	}
      if (curload < module + s->sh_addr + s->sh_size)
	curload = module + s->sh_addr + s->sh_size;
    }

  load (file, argv[0], (grub_uint8_t *) chunk_src + module - *kern_end, 0, sizeof (e));
  if (curload < module + sizeof (e))
    curload = module + sizeof (e);

  load (file, argv[0], (grub_uint8_t *) chunk_src + curload - *kern_end, e.e_shoff,
	(grub_uint32_t) e.e_shnum * e.e_shentsize);
  e.e_shoff = curload - module;
  curload +=  (grub_uint32_t) e.e_shnum * e.e_shentsize;

  load (file, argv[0], (grub_uint8_t *) chunk_src + curload - *kern_end, e.e_phoff,
	(grub_uint32_t) e.e_phnum * e.e_phentsize);
  e.e_phoff = curload - module;
  curload +=  (grub_uint32_t) e.e_phnum * e.e_phentsize;

  *kern_end = curload;

  grub_freebsd_add_meta_module (argv[0], FREEBSD_MODTYPE_ELF_MODULE,
				argc - 1, argv + 1, module,
				curload - module);
out:
  grub_free (shdr);
  if (err)
    return err;
  return SUFFIX (grub_freebsd_load_elf_meta) (relocator, file, argv[0], kern_end);
}

#endif

grub_err_t
SUFFIX (grub_freebsd_load_elf_meta) (struct grub_relocator *relocator,
				     grub_file_t file,
				     const char *filename,
				     grub_addr_t *kern_end)
{
  grub_err_t err;
  Elf_Ehdr e;
  Elf_Shdr *s;
  char *shdr = 0;
  unsigned symoff, stroff, symsize, strsize;
  grub_freebsd_addr_t symstart, symend, symentsize, dynamic;
  Elf_Sym *sym;
  void *sym_chunk;
  grub_uint8_t *curload;
  grub_freebsd_addr_t symtarget;
  const char *str;
  unsigned i;
  grub_size_t chunk_size;

  err = read_headers (file, filename, &e, &shdr);
  if (err)
    goto out;

  err = grub_bsd_add_meta (FREEBSD_MODINFO_METADATA |
			   FREEBSD_MODINFOMD_ELFHDR, &e,
			   sizeof (e));
  if (err)
    goto out;

  for (s = (Elf_Shdr *) shdr; s < (Elf_Shdr *) (shdr
						+ e.e_shnum * e.e_shentsize);
       s = (Elf_Shdr *) ((char *) s + e.e_shentsize))
      if (s->sh_type == SHT_SYMTAB)
	break;
  if (s >= (Elf_Shdr *) ((char *) shdr
			+ e.e_shnum * e.e_shentsize))
    {
      err = grub_error (GRUB_ERR_BAD_OS, N_("no symbol table"));
      goto out;
    }
  symoff = s->sh_offset;
  symsize = s->sh_size;
  symentsize = s->sh_entsize;
  s = (Elf_Shdr *) (shdr + e.e_shentsize * s->sh_link);
  stroff = s->sh_offset;
  strsize = s->sh_size;

  chunk_size = ALIGN_UP (symsize + strsize, sizeof (grub_freebsd_addr_t))
    + 2 * sizeof (grub_freebsd_addr_t);

  symtarget = ALIGN_UP (*kern_end, sizeof (grub_freebsd_addr_t));

  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_addr (relocator, &ch,
					   symtarget, chunk_size);
    if (err)
      goto out;
    sym_chunk = get_virtual_current_address (ch);
  }

  symstart = symtarget;
  symend = symstart + chunk_size;

  curload = sym_chunk;
  *((grub_freebsd_addr_t *) curload) = symsize;
  curload += sizeof (grub_freebsd_addr_t);

  if (grub_file_seek (file, symoff) == (grub_off_t) -1)
    {
      err = grub_errno;
      goto out;
    }
  sym = (Elf_Sym *) curload;
  if (grub_file_read (file, curload, symsize) != (grub_ssize_t) symsize)
    {
      if (! grub_errno)
	err = grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			  filename);
      else
	err = grub_errno;
      goto out;
    }
  curload += symsize;

  *((grub_freebsd_addr_t *) curload) = strsize;
  curload += sizeof (grub_freebsd_addr_t);
  if (grub_file_seek (file, stroff) == (grub_off_t) -1)
    {
      err = grub_errno;
      goto out;
    }
  str = (char *) curload;
  if (grub_file_read (file, curload, strsize) != (grub_ssize_t) strsize)
    {
      if (! grub_errno)
	err = grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			  filename);
      else
	err = grub_errno;
      goto out;
    }

  for (i = 0;
       i * symentsize < symsize;
       i++, sym = (Elf_Sym *) ((char *) sym + symentsize))
    {
      const char *name = str + sym->st_name;
      if (grub_strcmp (name, "_DYNAMIC") == 0)
	break;
    }

  if (i * symentsize < symsize)
    {
      dynamic = sym->st_value;
      grub_dprintf ("bsd", "dynamic = %llx\n", (unsigned long long) dynamic);
      err = grub_bsd_add_meta (FREEBSD_MODINFO_METADATA |
			       FREEBSD_MODINFOMD_DYNAMIC, &dynamic,
			       sizeof (dynamic));
      if (err)
	goto out;
    }

  err = grub_bsd_add_meta (FREEBSD_MODINFO_METADATA |
			   FREEBSD_MODINFOMD_SSYM, &symstart,
			   sizeof (symstart));
  if (err)
    goto out;

  err = grub_bsd_add_meta (FREEBSD_MODINFO_METADATA |
			   FREEBSD_MODINFOMD_ESYM, &symend,
			   sizeof (symend));
out:
  grub_free (shdr);
  if (err)
    return err;

  *kern_end = ALIGN_PAGE (symend);

  return GRUB_ERR_NONE;
}

grub_err_t
SUFFIX (grub_netbsd_load_elf_meta) (struct grub_relocator *relocator,
				    grub_file_t file, const char *filename,
				    grub_addr_t *kern_end)
{
  grub_err_t err;
  Elf_Ehdr e;
  Elf_Shdr *s, *symsh, *strsh;
  char *shdr = NULL;
  unsigned symsize, strsize;
  void *sym_chunk;
  grub_uint8_t *curload;
  grub_size_t chunk_size;
  Elf_Ehdr *e2;
  struct grub_netbsd_btinfo_symtab symtab;
  grub_addr_t symtarget;

  err = read_headers (file, filename, &e, &shdr);
  if (err)
    {
      grub_free (shdr);
      return grub_errno;
    }

  for (s = (Elf_Shdr *) shdr; s < (Elf_Shdr *) (shdr
						+ e.e_shnum * e.e_shentsize);
       s = (Elf_Shdr *) ((char *) s + e.e_shentsize))
      if (s->sh_type == SHT_SYMTAB)
	break;
  if (s >= (Elf_Shdr *) ((char *) shdr
			+ e.e_shnum * e.e_shentsize))
    {
      grub_free (shdr);
      return GRUB_ERR_NONE;
    }
  symsize = s->sh_size;
  symsh = s;
  s = (Elf_Shdr *) (shdr + e.e_shentsize * s->sh_link);
  strsize = s->sh_size;
  strsh = s;

  chunk_size = ALIGN_UP (symsize, sizeof (grub_freebsd_addr_t))
    + ALIGN_UP (strsize, sizeof (grub_freebsd_addr_t))
    + sizeof (e) + (grub_uint32_t) e.e_shnum * e.e_shentsize;

  symtarget = ALIGN_UP (*kern_end, sizeof (grub_freebsd_addr_t));
  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_addr (relocator, &ch,
					   symtarget, chunk_size);
    if (err)
      goto out;
    sym_chunk = get_virtual_current_address (ch);
  }

  symtab.nsyms = 1;
  symtab.ssyms = symtarget;
  symtab.esyms = symtarget + chunk_size;

  curload = sym_chunk;
  
  e2 = (Elf_Ehdr *) curload;
  grub_memcpy (curload, &e, sizeof (e));
  e2->e_phoff = 0;
  e2->e_phnum = 0;
  e2->e_phentsize = 0;
  e2->e_shstrndx = 0;
  e2->e_shoff = sizeof (e);

  curload += sizeof (e);

  for (s = (Elf_Shdr *) shdr; s < (Elf_Shdr *) (shdr
						+ e.e_shnum * e.e_shentsize);
       s = (Elf_Shdr *) ((char *) s + e.e_shentsize))
    {
      Elf_Shdr *s2;
      s2 = (Elf_Shdr *) curload;
      grub_memcpy (curload, s, e.e_shentsize);
      if (s == symsh)
	s2->sh_offset = sizeof (e) + (grub_uint32_t) e.e_shnum * e.e_shentsize;
      else if (s == strsh)
	s2->sh_offset = ALIGN_UP (symsize, sizeof (grub_freebsd_addr_t))
	  + sizeof (e) + (grub_uint32_t) e.e_shnum * e.e_shentsize;
      else
	s2->sh_offset = 0;
      s2->sh_addr = s2->sh_offset;
      curload += e.e_shentsize;
    }

  if (grub_file_seek (file, symsh->sh_offset) == (grub_off_t) -1)
    {
      err = grub_errno;
      goto out;
    }
  if (grub_file_read (file, curload, symsize) != (grub_ssize_t) symsize)
    {
      if (! grub_errno)
	err = grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			  filename);
      else
	err = grub_errno;
      goto out;
    }
  curload += ALIGN_UP (symsize, sizeof (grub_freebsd_addr_t));

  if (grub_file_seek (file, strsh->sh_offset) == (grub_off_t) -1)
    {
      err = grub_errno;
      goto out;
    }
  if (grub_file_read (file, curload, strsize) != (grub_ssize_t) strsize)
    {
      if (! grub_errno)
	err = grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			  filename);
      else
	err = grub_errno;
      goto out;
    }

  err = grub_bsd_add_meta (NETBSD_BTINFO_SYMTAB, 
			   &symtab,
			   sizeof (symtab));
out:
  grub_free (shdr);
  if (err)
    return err;

  *kern_end = ALIGN_PAGE (symtarget + chunk_size);

  return GRUB_ERR_NONE;
}

grub_err_t
SUFFIX(grub_openbsd_find_ramdisk) (grub_file_t file,
				   const char *filename,
				   grub_addr_t kern_start,
				   void *kern_chunk_src,
				   struct grub_openbsd_ramdisk_descriptor *desc)
{
  unsigned symoff, stroff, symsize, strsize, symentsize;

  {
    grub_err_t err;
    Elf_Ehdr e;
    Elf_Shdr *s;
    char *shdr = NULL;
    
    err = read_headers (file, filename, &e, &shdr);
    if (err)
      {
	grub_free (shdr);
	return err;
      }

    for (s = (Elf_Shdr *) shdr; s < (Elf_Shdr *) (shdr
						  + e.e_shnum * e.e_shentsize);
	 s = (Elf_Shdr *) ((char *) s + e.e_shentsize))
      if (s->sh_type == SHT_SYMTAB)
	break;
    if (s >= (Elf_Shdr *) ((char *) shdr + e.e_shnum * e.e_shentsize))
      {
	grub_free (shdr);
	return GRUB_ERR_NONE;
      }

    symsize = s->sh_size;
    symentsize = s->sh_entsize;
    symoff = s->sh_offset;
    
    s = (Elf_Shdr *) (shdr + e.e_shentsize * s->sh_link);
    stroff = s->sh_offset;
    strsize = s->sh_size;
    grub_free (shdr);
  }
  {
    Elf_Sym *syms, *sym, *imagesym = NULL, *sizesym = NULL;
    unsigned i;
    char *strs;

    syms = grub_malloc (symsize);
    if (!syms)
      return grub_errno;

    if (grub_file_seek (file, symoff) == (grub_off_t) -1)
      {
	grub_free (syms);
	return grub_errno;
      }
    if (grub_file_read (file, syms, symsize) != (grub_ssize_t) symsize)
      {
	grub_free (syms);
	if (! grub_errno)
	  return grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			     filename);
	return grub_errno;
      }

    strs = grub_malloc (strsize);
    if (!strs)
      {
	grub_free (syms);
	return grub_errno;
      }

    if (grub_file_seek (file, stroff) == (grub_off_t) -1)
      {
	grub_free (syms);
	grub_free (strs);
	return grub_errno;
      }
    if (grub_file_read (file, strs, strsize) != (grub_ssize_t) strsize)
      {
	grub_free (syms);
	grub_free (strs);
	if (! grub_errno)
	  return grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			     filename);
	return grub_errno;
      }

    for (i = 0, sym = syms; i * symentsize < symsize;
       i++, sym = (Elf_Sym *) ((char *) sym + symentsize))
      {
	if (ELF_ST_TYPE (sym->st_info) != STT_OBJECT)
	  continue;
	if (!sym->st_name)
	  continue;
	if (grub_strcmp (strs + sym->st_name, "rd_root_image") == 0)
	  imagesym = sym;
	if (grub_strcmp (strs + sym->st_name, "rd_root_size") == 0)
	  sizesym = sym;
	if (imagesym && sizesym)
	  break;
      }
    if (!imagesym || !sizesym)
      {
	grub_free (syms);
	grub_free (strs);
	return GRUB_ERR_NONE;
      }
    if (sizeof (*desc->size) != sizesym->st_size)
      {
	grub_free (syms);
	grub_free (strs);
	return grub_error (GRUB_ERR_BAD_OS, "unexpected size of rd_root_size");
      }
    desc->max_size = imagesym->st_size;
    desc->target = (imagesym->st_value & 0xFFFFFF) - kern_start
      + (grub_uint8_t *) kern_chunk_src;
    desc->size = (grub_uint32_t *) ((sizesym->st_value & 0xFFFFFF) - kern_start
				    + (grub_uint8_t *) kern_chunk_src);
    grub_free (syms);
    grub_free (strs);

    return GRUB_ERR_NONE;
  }
}
