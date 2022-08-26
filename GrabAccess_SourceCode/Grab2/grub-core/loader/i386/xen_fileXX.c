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

#include <grub/xen_file.h>
#include <grub/misc.h>
#include <xen/elfnote.h>

static grub_err_t
parse_xen_guest (grub_elf_t elf, struct grub_xen_file_info *xi,
		 grub_off_t off, grub_size_t sz)
{
  char *buf;
  const char *ptr;
  int has_paddr = 0;

  grub_errno = GRUB_ERR_NONE;
  if (grub_file_seek (elf->file, off) == (grub_off_t) -1)
    return grub_errno;
  buf = grub_malloc (sz);
  if (!buf)
    return grub_errno;

  if (grub_file_read (elf->file, buf, sz) != (grub_ssize_t) sz)
    {
      if (grub_errno)
	goto out;
      grub_free (buf);
      return grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			 elf->file->name);
    }
  xi->has_xen_guest = 1;
  for (ptr = buf; ptr && ptr - buf < (grub_ssize_t) sz;
       ptr = grub_strchr (ptr, ','), (ptr ? ptr++ : 0))
    {
      if (grub_strncmp (ptr, "PAE=no,", sizeof ("PAE=no,") - 1) == 0)
	{
	  if (xi->arch != GRUB_XEN_FILE_I386
	      && xi->arch != GRUB_XEN_FILE_I386_PAE
	      && xi->arch != GRUB_XEN_FILE_I386_PAE_BIMODE)
	    continue;
	  xi->arch = GRUB_XEN_FILE_I386;
	  continue;
	}

      if (grub_strncmp (ptr, "PAE=yes,", sizeof ("PAE=yes,") - 1) == 0)
	{
	  if (xi->arch != GRUB_XEN_FILE_I386
	      && xi->arch != GRUB_XEN_FILE_I386_PAE
	      && xi->arch != GRUB_XEN_FILE_I386_PAE_BIMODE)
	    continue;
	  xi->arch = GRUB_XEN_FILE_I386_PAE;
	  continue;
	}

      if (grub_strncmp (ptr, "PAE=yes[extended-cr3],",
			sizeof ("PAE=yes[extended-cr3],") - 1) == 0)
	{
	  if (xi->arch != GRUB_XEN_FILE_I386
	      && xi->arch != GRUB_XEN_FILE_I386_PAE
	      && xi->arch != GRUB_XEN_FILE_I386_PAE_BIMODE)
	    continue;
	  xi->arch = GRUB_XEN_FILE_I386_PAE;
	  xi->extended_cr3 = 1;
	  continue;
	}

      if (grub_strncmp (ptr, "PAE=bimodal,", sizeof ("PAE=bimodal,") - 1) == 0)
	{
	  if (xi->arch != GRUB_XEN_FILE_I386
	      && xi->arch != GRUB_XEN_FILE_I386_PAE
	      && xi->arch != GRUB_XEN_FILE_I386_PAE_BIMODE)
	    continue;
	  xi->arch = GRUB_XEN_FILE_I386_PAE_BIMODE;
	  continue;
	}

      if (grub_strncmp (ptr, "PAE=bimodal[extended-cr3],",
			sizeof ("PAE=bimodal[extended-cr3],") - 1) == 0)
	{
	  if (xi->arch != GRUB_XEN_FILE_I386
	      && xi->arch != GRUB_XEN_FILE_I386_PAE
	      && xi->arch != GRUB_XEN_FILE_I386_PAE_BIMODE)
	    continue;
	  xi->arch = GRUB_XEN_FILE_I386_PAE_BIMODE;
	  xi->extended_cr3 = 1;
	  continue;
	}

      if (grub_strncmp (ptr, "PAE=yes,bimodal,", sizeof ("PAE=yes,bimodal,") - 1) == 0)
	{
	  if (xi->arch != GRUB_XEN_FILE_I386
	      && xi->arch != GRUB_XEN_FILE_I386_PAE
	      && xi->arch != GRUB_XEN_FILE_I386_PAE_BIMODE)
	    continue;
	  xi->arch = GRUB_XEN_FILE_I386_PAE_BIMODE;
	  continue;
	}

      if (grub_strncmp (ptr, "PAE=yes[extended-cr3],bimodal,",
			sizeof ("PAE=yes[extended-cr3],bimodal,") - 1) == 0)
	{
	  if (xi->arch != GRUB_XEN_FILE_I386
	      && xi->arch != GRUB_XEN_FILE_I386_PAE
	      && xi->arch != GRUB_XEN_FILE_I386_PAE_BIMODE)
	    continue;
	  xi->arch = GRUB_XEN_FILE_I386_PAE_BIMODE;
	  xi->extended_cr3 = 1;
	  continue;
	}

      if (grub_strncmp (ptr, "VIRT_BASE=", sizeof ("VIRT_BASE=") - 1) == 0)
	{
	  xi->virt_base = grub_strtoull (ptr + sizeof ("VIRT_BASE=") - 1, &ptr, 16);
	  if (grub_errno)
	    goto out;
	  continue;
	}
      if (grub_strncmp (ptr, "VIRT_ENTRY=", sizeof ("VIRT_ENTRY=") - 1) == 0)
	{
	  xi->entry_point = grub_strtoull (ptr + sizeof ("VIRT_ENTRY=") - 1, &ptr, 16);
	  if (grub_errno)
	    goto out;
	  continue;
	}
      if (grub_strncmp (ptr, "HYPERCALL_PAGE=", sizeof ("HYPERCALL_PAGE=") - 1) == 0)
	{
	  xi->hypercall_page = grub_strtoull (ptr + sizeof ("HYPERCALL_PAGE=") - 1, &ptr, 16);
	  xi->has_hypercall_page = 1;
	  if (grub_errno)
	    goto out;
	  continue;
	}
      if (grub_strncmp (ptr, "ELF_PADDR_OFFSET=", sizeof ("ELF_PADDR_OFFSET=") - 1) == 0)
	{
	  xi->paddr_offset = grub_strtoull (ptr + sizeof ("ELF_PADDR_OFFSET=") - 1, &ptr, 16);
	  has_paddr = 1;
	  if (grub_errno)
	    goto out;
	  continue;
	}
    }
  if (xi->has_hypercall_page)
    xi->hypercall_page = (xi->hypercall_page << 12) + xi->virt_base;
  if (!has_paddr)
    xi->paddr_offset = xi->virt_base;

out:
  grub_free (buf);

  return grub_errno;
}

#pragma GCC diagnostic ignored "-Wcast-align"

static grub_err_t
parse_note (grub_elf_t elf, struct grub_xen_file_info *xi,
	    grub_off_t off, grub_size_t sz)
{
  grub_uint32_t *buf;
  grub_uint32_t *ptr;
  if (grub_file_seek (elf->file, off) == (grub_off_t) -1)
    return grub_errno;
  buf = grub_malloc (sz);
  if (!buf)
    return grub_errno;

  if (grub_file_read (elf->file, buf, sz) != (grub_ssize_t) sz)
    {
      if (grub_errno)
	return grub_errno;
      return grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			 elf->file->name);
    }
  for (ptr = buf; ptr - buf < (grub_ssize_t) (sz / sizeof (grub_uint32_t));)
    {
      Elf_Nhdr *nh = (Elf_Nhdr *) ptr;
      char *name;
      grub_uint32_t *desc;
      grub_uint32_t namesz, descsz;
      ptr += sizeof (*nh) / sizeof (grub_uint32_t);
      name = (char *) ptr;
      namesz = grub_le_to_cpu32 (nh->n_namesz);
      descsz = grub_le_to_cpu32 (nh->n_descsz);
      ptr += (namesz + 3) / 4;
      desc = ptr;
      ptr += (grub_le_to_cpu32 (nh->n_descsz) + 3) / 4;
      if ((namesz < 3) || grub_memcmp (name, "Xen", namesz == 3 ? 3 : 4) != 0)
	continue;
      xi->has_note = 1;
      switch (nh->n_type)
	{
	case XEN_ELFNOTE_ENTRY:
	  xi->entry_point = grub_le_to_cpu_addr (*(Elf_Addr *) desc);
	  break;
	case XEN_ELFNOTE_HYPERCALL_PAGE:
	  xi->hypercall_page = grub_le_to_cpu_addr (*(Elf_Addr *) desc);
	  xi->has_hypercall_page = 1;
	  break;
	case XEN_ELFNOTE_VIRT_BASE:
	  xi->virt_base = grub_le_to_cpu_addr (*(Elf_Addr *) desc);
	  break;
	case XEN_ELFNOTE_PADDR_OFFSET:
	  xi->paddr_offset = grub_le_to_cpu_addr (*(Elf_Addr *) desc);
	  break;
	case XEN_ELFNOTE_XEN_VERSION:
	  grub_dprintf ("xen", "xenversion = `%s'\n", (char *) desc);
	  break;
	case XEN_ELFNOTE_GUEST_OS:
	  grub_dprintf ("xen", "name = `%s'\n", (char *) desc);
	  break;
	case XEN_ELFNOTE_GUEST_VERSION:
	  grub_dprintf ("xen", "version = `%s'\n", (char *) desc);
	  break;
	case XEN_ELFNOTE_LOADER:
	  if (descsz < 7
	      || grub_memcmp (desc, "generic", descsz == 7 ? 7 : 8) != 0)
	    return grub_error (GRUB_ERR_BAD_OS, "invalid loader");
	  break;
	  /* PAE */
	case XEN_ELFNOTE_PAE_MODE:
	  grub_dprintf ("xen", "pae = `%s', %d, %d\n", (char *) desc,
			xi->arch, descsz);
	  if (xi->arch != GRUB_XEN_FILE_I386
	      && xi->arch != GRUB_XEN_FILE_I386_PAE
	      && xi->arch != GRUB_XEN_FILE_I386_PAE_BIMODE)
	    break;
	  if (descsz >= 3 && grub_memcmp (desc, "yes",
					  descsz == 3 ? 3 : 4) == 0)
	    {
	      xi->extended_cr3 = 1;
	      xi->arch = GRUB_XEN_FILE_I386_PAE;
	    }
	  if (descsz >= 7 && grub_memcmp (desc, "bimodal",
					  descsz == 7 ? 7 : 8) == 0)
	    {
	      xi->extended_cr3 = 1;
	      xi->arch = GRUB_XEN_FILE_I386_PAE_BIMODE;
	    }
	  if (descsz >= 11 && grub_memcmp (desc, "yes,bimodal",
					   descsz == 11 ? 11 : 12) == 0)
	    {
	      xi->extended_cr3 = 1;
	      xi->arch = GRUB_XEN_FILE_I386_PAE_BIMODE;
	    }
	  if (descsz >= 2 && grub_memcmp (desc, "no",
					  descsz == 2 ? 2 : 3) == 0)
	    xi->arch = GRUB_XEN_FILE_I386;
	  break;
	case XEN_ELFNOTE_INIT_P2M:
	  xi->p2m_base = grub_le_to_cpu_addr (*(Elf_Addr *) desc);
	  xi->has_p2m_base = 1;
	  break;
	case XEN_ELFNOTE_MOD_START_PFN:
	  xi->unmapped_initrd = !!grub_le_to_cpu32(*(grub_uint32_t *) desc);
	  break;
	default:
	  grub_dprintf ("xen", "unknown note type %d\n", nh->n_type);
	  break;
	}
    }
  return GRUB_ERR_NONE;
}

grub_err_t
grub_xen_get_infoXX (grub_elf_t elf, struct grub_xen_file_info *xi)
{
  Elf_Shdr *s, *s0;
  grub_size_t shnum = elf->ehdr.ehdrXX.e_shnum;
  grub_size_t shentsize = elf->ehdr.ehdrXX.e_shentsize;
  grub_size_t shsize = shnum * shentsize;
  grub_off_t stroff;
  grub_err_t err;
  Elf_Phdr *phdr;

  xi->kern_end = 0;
  xi->kern_start = ~0;
  xi->entry_point = elf->ehdr.ehdrXX.e_entry;

  /* FIXME: check note.  */
  FOR_ELF_PHDRS (elf, phdr)
    {
      Elf_Addr paddr;

      if (phdr->p_type == PT_NOTE)
	{
	  err = parse_note (elf, xi, phdr->p_offset, phdr->p_filesz);
	  if (err)
	    return err;
	}

      if (phdr->p_type != PT_LOAD)
	continue;

      paddr = phdr->p_paddr;

      if (paddr < xi->kern_start)
	xi->kern_start = paddr;

      if (paddr + phdr->p_memsz > xi->kern_end)
	xi->kern_end = paddr + phdr->p_memsz;
    }

  if (xi->has_note)
    return GRUB_ERR_NONE;

  if (!shnum || !shentsize)
    return grub_error (GRUB_ERR_BAD_OS, "no XEN note");

  s0 = grub_malloc (shsize);
  if (!s0)
    return grub_errno;

  if (grub_file_seek (elf->file, elf->ehdr.ehdrXX.e_shoff) == (grub_off_t) -1)
    {
      err = grub_errno;
      goto cleanup;
    }

  if (grub_file_read (elf->file, s0, shsize) != (grub_ssize_t) shsize)
    {
      if (grub_errno)
	err = grub_errno;
      else
	err = grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			 elf->file->name);
      goto cleanup;
    }

  s = (Elf_Shdr *) ((char *) s0 + elf->ehdr.ehdrXX.e_shstrndx * shentsize);
  stroff = s->sh_offset;

  for (s = s0; s < (Elf_Shdr *) ((char *) s0 + shnum * shentsize);
       s = (Elf_Shdr *) ((char *) s + shentsize))
    {
      if (s->sh_type == SHT_NOTE)
	{
	  err = parse_note (elf, xi, s->sh_offset, s->sh_size);
	  if (err)
	    goto cleanup;
	}
    }

  if (xi->has_note)
    {
      err = GRUB_ERR_NONE;
      goto cleanup;
    }

  for (s = s0; s < (Elf_Shdr *) ((char *) s0 + shnum * shentsize);
       s = (Elf_Shdr *) ((char *) s + shentsize))
    {
      char name[sizeof("__xen_guest")];
      grub_memset (name, 0, sizeof (name));
      if (grub_file_seek (elf->file, stroff + s->sh_name) == (grub_off_t) -1)
	{
	  err = grub_errno;
	  goto cleanup;
	}

      if (grub_file_read (elf->file, name, sizeof (name)) != (grub_ssize_t) sizeof (name))
	{
	  if (grub_errno)
	    {
	      err = grub_errno;
	      goto cleanup;
	    }
	  continue;
	}
      if (grub_memcmp (name, "__xen_guest",
		       sizeof("__xen_guest")) != 0)
	continue;
      err = parse_xen_guest (elf, xi, s->sh_offset, s->sh_size);
      goto cleanup;
    }
  err = grub_error (GRUB_ERR_BAD_OS, "no XEN note found");

cleanup:
  grub_free (s0);
  return err;
}
