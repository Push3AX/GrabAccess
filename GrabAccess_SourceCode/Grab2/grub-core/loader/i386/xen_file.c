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
#include <grub/i386/linux.h>
#include <grub/misc.h>

#define XZ_MAGIC "\3757zXZ\0"

grub_elf_t
grub_xen_file (grub_file_t file)
{
  grub_elf_t elf;
  struct linux_i386_kernel_header lh;
  grub_file_t off_file;
  grub_uint32_t payload_offset, payload_length;
  grub_uint8_t magic[6];

  elf = grub_elf_file (file, file->name);
  if (elf)
    return elf;
  grub_errno = GRUB_ERR_NONE;

  if (grub_file_seek (file, 0) == (grub_off_t) -1)
    goto fail;

  if (grub_file_read (file, &lh, sizeof (lh)) != sizeof (lh))
    goto fail;

  if (lh.boot_flag != grub_cpu_to_le16_compile_time (0xaa55)
      || lh.header != grub_cpu_to_le32_compile_time (GRUB_LINUX_I386_MAGIC_SIGNATURE)
      || grub_le_to_cpu16 (lh.version) < 0x0208)
    {
      grub_error (GRUB_ERR_BAD_OS, "version too old for xen boot");
      return NULL;
    }

  payload_length = lh.payload_length;
  payload_offset = (lh.setup_sects + 1) * 512
    + lh.payload_offset;

  if (payload_length < sizeof (magic))
    {
      grub_error (GRUB_ERR_BAD_OS, "payload too short");
      return NULL;
    }

  grub_dprintf ("xen", "found bzimage payload 0x%llx-0x%llx\n",
		(unsigned long long) payload_offset,
		(unsigned long long) lh.payload_length);

  grub_file_seek (file, payload_offset);

  if (grub_file_read (file, &magic, sizeof (magic)) != sizeof (magic))
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		    file->name);
      goto fail;
    }

  /* Kernel suffixes xz payload with their uncompressed size.
     Trim it.  */
  if (grub_memcmp (magic, XZ_MAGIC, sizeof (XZ_MAGIC) - 1) == 0)
    payload_length -= 4;
  off_file = grub_file_offset_open (file, GRUB_FILE_TYPE_LINUX_KERNEL, payload_offset,
				    payload_length);
  if (!off_file)
    goto fail;

  elf = grub_elf_file (off_file, file->name);
  if (elf)
    return elf;
  grub_file_offset_close (off_file);

fail:
  grub_error (GRUB_ERR_BAD_OS, "not xen image");
  return NULL;
}

grub_err_t
grub_xen_get_info (grub_elf_t elf, struct grub_xen_file_info * xi)
{
  grub_memset (xi, 0, sizeof (*xi));

  if (grub_elf_is_elf64 (elf)
      && elf->ehdr.ehdr64.e_machine
      == grub_cpu_to_le16_compile_time (EM_X86_64)
      && elf->ehdr.ehdr64.e_ident[EI_DATA] == ELFDATA2LSB)
    {
      xi->arch = GRUB_XEN_FILE_X86_64;
      return grub_xen_get_info64 (elf, xi);
    }
  if (grub_elf_is_elf32 (elf)
      && elf->ehdr.ehdr32.e_machine == grub_cpu_to_le16_compile_time (EM_386)
      && elf->ehdr.ehdr32.e_ident[EI_DATA] == ELFDATA2LSB)
    {
      xi->arch = GRUB_XEN_FILE_I386;
      return grub_xen_get_info32 (elf, xi);
    }
  return grub_error (GRUB_ERR_BAD_OS, "unknown ELF type");
}
