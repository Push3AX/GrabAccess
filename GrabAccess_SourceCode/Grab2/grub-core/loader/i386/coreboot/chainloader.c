/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2011  Free Software Foundation, Inc.
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

#include <grub/loader.h>
#include <grub/memory.h>
#include <grub/i386/memory.h>
#include <grub/file.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/elfload.h>
#include <grub/video.h>
#include <grub/relocator.h>
#include <grub/i386/relocator.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/cbfs_core.h>
#include <grub/lib/LzmaDec.h>
#include <grub/efi/pe32.h>
#include <grub/i386/cpuid.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_addr_t entry;
static struct grub_relocator *relocator = NULL;

static grub_err_t
grub_chain_boot (void)
{
  struct grub_relocator32_state state;

  grub_video_set_mode ("text", 0, 0);

  state.eip = entry;
  return grub_relocator32_boot (relocator, state, 0);
}

static grub_err_t
grub_chain_unload (void)
{
  grub_relocator_unload (relocator);
  relocator = NULL;

  return GRUB_ERR_NONE;
}

static grub_err_t
load_elf (grub_file_t file, const char *filename)
{
  grub_elf_t elf;
  Elf32_Phdr *phdr;
  grub_err_t err;

  elf = grub_elf_file (file, filename);
  if (!elf)
    return grub_errno;

  if (!grub_elf_is_elf32 (elf))
    return grub_error (GRUB_ERR_BAD_OS, "only ELF32 can be coreboot payload");

  entry = elf->ehdr.ehdr32.e_entry;

  FOR_ELF32_PHDRS(elf, phdr)
    {
      grub_uint8_t *load_addr;
      grub_relocator_chunk_t ch;

      if (phdr->p_type != PT_LOAD)
	continue;

      err = grub_relocator_alloc_chunk_addr (relocator, &ch,
					     phdr->p_paddr, phdr->p_memsz);
      if (err)
	{
	  elf->file = 0;
	  grub_elf_close (elf);
	  return err;
	}

      load_addr = get_virtual_current_address (ch);

      if (grub_file_seek (elf->file, phdr->p_offset) == (grub_off_t) -1)
	{
	  elf->file = 0;
	  grub_elf_close (elf);
	  return grub_errno;
	}

      if (phdr->p_filesz)
	{
	  grub_ssize_t read;
	  read = grub_file_read (elf->file, load_addr, phdr->p_filesz);
	  if (read != (grub_ssize_t) phdr->p_filesz)
	    {
	      if (!grub_errno)
		grub_error (GRUB_ERR_FILE_READ_ERROR,
			    N_("premature end of file %s"),
			    filename);
	      elf->file = 0;
	      grub_elf_close (elf);
	      return grub_errno;
	    }
	}

      if (phdr->p_filesz < phdr->p_memsz)
	grub_memset ((load_addr + phdr->p_filesz),
		     0, phdr->p_memsz - phdr->p_filesz);
    }

  elf->file = 0;
  grub_elf_close (elf);
  return GRUB_ERR_NONE;
}

static void *SzAlloc(void *p __attribute__ ((unused)), size_t size) { return grub_malloc (size); }
static void SzFree(void *p __attribute__ ((unused)), void *address) { grub_free (address); }
static ISzAlloc g_Alloc = { SzAlloc, SzFree };


static grub_err_t
load_segment (grub_file_t file, const char *filename,
	      void *load_addr, grub_uint32_t comp,
	      grub_size_t *size, grub_size_t max_size)
{
  switch (comp)
    {
    case grub_cpu_to_be32_compile_time (CBFS_COMPRESS_NONE):
      if (grub_file_read (file, load_addr, *size)
	  != (grub_ssize_t) *size)
	{
	  if (!grub_errno)
	    grub_error (GRUB_ERR_FILE_READ_ERROR,
			N_("premature end of file %s"),
			filename);
	      return grub_errno;
	}
      return GRUB_ERR_NONE;
    case grub_cpu_to_be32_compile_time (CBFS_COMPRESS_LZMA):
      {
	grub_uint8_t *buf;
	grub_size_t outsize, insize;
	SRes res;
	SizeT src_len, dst_len;
	ELzmaStatus status;
	if (*size < 13)
	  return grub_error (GRUB_ERR_BAD_OS, "invalid compressed chunk");
	buf = grub_malloc (*size);
	if (!buf)
	  return grub_errno;
	if (grub_file_read (file, buf, *size)
	    != (grub_ssize_t) *size)
	  {
	    if (!grub_errno)
	      grub_error (GRUB_ERR_FILE_READ_ERROR,
			  N_("premature end of file %s"),
			  filename);
	    grub_free (buf);
	    return grub_errno;
	  }
	outsize = grub_get_unaligned64 (buf + 5);
	if (outsize > max_size)
	  {
	    grub_free (buf);
	    return grub_error (GRUB_ERR_BAD_OS, "invalid compressed chunk");
	  }
	insize = *size - 13;

	src_len = insize;
	dst_len = outsize;
	res = LzmaDecode (load_addr, &dst_len, buf + 13, &src_len,
			  buf, 5, LZMA_FINISH_END, &status, &g_Alloc);
	/* ELzmaFinishMode finishMode,
	   ELzmaStatus *status, ISzAlloc *alloc)*/
	grub_free (buf);
	grub_dprintf ("chain", "%x, %x, %x, %x\n",
		      insize, src_len, outsize, dst_len);
	if (res != SZ_OK
	    || src_len != insize || dst_len != outsize)
	  return grub_error (GRUB_ERR_BAD_OS, "decompression failure %d", res);
	*size = outsize;
      }
      return GRUB_ERR_NONE;
    default:
      return grub_error (GRUB_ERR_BAD_OS, "unsupported compression %d",
			 grub_be_to_cpu32 (comp));
    }
}

static grub_err_t
load_tianocore (grub_file_t file)
{
  grub_uint16_t header_length;
  grub_uint32_t section_head;
  grub_uint8_t mz[2], pe[4];
  struct grub_pe32_coff_header coff_head;
  struct file_header
  {
    grub_uint8_t unused[18];
    grub_uint8_t type;
    grub_uint8_t unused2;
    grub_uint8_t size[3];
    grub_uint8_t unused3;
  } file_head;
  grub_relocator_chunk_t ch;

  if (grub_file_seek (file, 48) == (grub_off_t) -1
      || grub_file_read (file, &header_length, sizeof (header_length))
      != sizeof (header_length)
      || grub_file_seek (file, header_length) == (grub_off_t) -1)
    goto fail;

  while (1)
    {
      grub_off_t off;
      if (grub_file_read (file, &file_head, sizeof (file_head))
	  != sizeof (file_head))
	goto fail;
      if (file_head.type != 0xf0)
	break;
      off = grub_get_unaligned32 (file_head.size) & 0xffffff;
      if (off < sizeof (file_head))
	goto fail;
      if (grub_file_seek (file, grub_file_tell (file) + off
			  - sizeof (file_head)) == (grub_off_t) -1)
	goto fail;
    }

  if (file_head.type != 0x03)
    goto fail;

  while (1)
    {
      if (grub_file_read (file, &section_head, sizeof (section_head))
	  != sizeof (section_head))
	goto fail;
      if ((section_head >> 24) != 0x19)
	break;

      if ((section_head & 0xffffff) < sizeof (section_head))
	goto fail;

      if (grub_file_seek (file, grub_file_tell (file)
			  + (section_head & 0xffffff)
			  - sizeof (section_head)) == (grub_off_t) -1)
	goto fail;
    }

  if ((section_head >> 24) != 0x10)
    goto fail;

  grub_off_t exe_start = grub_file_tell (file);

  if (grub_file_read (file, &mz, sizeof (mz)) != sizeof (mz))
    goto fail;
  if (mz[0] != 'M' || mz[1] != 'Z')
    goto fail;

  if (grub_file_seek (file, grub_file_tell (file) + 0x3a) == (grub_off_t) -1)
    goto fail;

  if (grub_file_read (file, &section_head, sizeof (section_head))
      != sizeof (section_head))
    goto fail;
  if (section_head < 0x40)
    goto fail;

  if (grub_file_seek (file, grub_file_tell (file)
		      + section_head - 0x40) == (grub_off_t) -1)
    goto fail;

  if (grub_file_read (file, &pe, sizeof (pe))
      != sizeof (pe))
    goto fail;

  if (pe[0] != 'P' || pe[1] != 'E' || pe[2] != '\0' || pe[3] != '\0')
    goto fail;

  if (grub_file_read (file, &coff_head, sizeof (coff_head))
      != sizeof (coff_head))
    goto fail;

  grub_uint32_t loadaddr;

  switch (coff_head.machine)
    {
    case GRUB_PE32_MACHINE_I386:
      {
	struct grub_pe32_optional_header oh;
	if (grub_file_read (file, &oh, sizeof (oh))
	    != sizeof (oh))
	  goto fail;
	if (oh.magic != GRUB_PE32_PE32_MAGIC)
	  goto fail;
	loadaddr = oh.image_base - exe_start;
	entry = oh.image_base + oh.entry_addr;
	break;
      }
    case GRUB_PE32_MACHINE_X86_64:
      {
	struct grub_pe64_optional_header oh;
	if (! grub_cpuid_has_longmode)
	  {
	    grub_error (GRUB_ERR_BAD_OS, "your CPU does not implement AMD64 architecture");
	    goto fail;
	  }

	if (grub_file_read (file, &oh, sizeof (oh))
	    != sizeof (oh))
	  goto fail;
	if (oh.magic != GRUB_PE32_PE64_MAGIC)
	  goto fail;
	loadaddr = oh.image_base - exe_start;
	entry = oh.image_base + oh.entry_addr;
	break;
      }
    default:
      goto fail;
    }
  if (grub_file_seek (file, 0) == (grub_off_t) -1)
    goto fail;

  grub_size_t fz = grub_file_size (file);

  if (grub_relocator_alloc_chunk_addr (relocator, &ch,
				       loadaddr, fz))
    goto fail;

  if (grub_file_read (file, get_virtual_current_address (ch), fz)
      != (grub_ssize_t) fz)
    goto fail;

  return GRUB_ERR_NONE;

 fail:
  if (!grub_errno)
    grub_error (GRUB_ERR_BAD_OS, "fv volume is invalid");
  return grub_errno;
}

static grub_err_t
load_chewed (grub_file_t file, const char *filename)
{
  grub_size_t i;
  for (i = 0;; i++)
    {
      struct cbfs_payload_segment segment;
      grub_err_t err;

      if (grub_file_seek (file, sizeof (segment) * i) == (grub_off_t) -1
	  || grub_file_read (file, &segment, sizeof (segment))
	  != sizeof (segment))
	{
	  if (!grub_errno)
	    return grub_error (GRUB_ERR_BAD_OS,
			       "payload is too short");
	  return grub_errno;
	}

      switch (segment.type)
	{
	case PAYLOAD_SEGMENT_PARAMS:
	  break;

	case PAYLOAD_SEGMENT_ENTRY:
	  entry = grub_be_to_cpu64 (segment.load_addr);
	  return GRUB_ERR_NONE;

	case PAYLOAD_SEGMENT_BSS:
	  segment.len = 0;
	  segment.offset = 0;
	  segment.len = 0;
	  /* Fallthrough.  */
	case PAYLOAD_SEGMENT_CODE:
	case PAYLOAD_SEGMENT_DATA:
	  {
	    grub_uint32_t target = grub_be_to_cpu64 (segment.load_addr);
	    grub_uint32_t memsize = grub_be_to_cpu32 (segment.mem_len);
	    grub_uint32_t filesize = grub_be_to_cpu32 (segment.len);
	    grub_uint8_t *load_addr;
	    grub_relocator_chunk_t ch;

	    if (memsize < filesize)
	      memsize = filesize;

	    grub_dprintf ("chain", "%x+%x\n", target, memsize);

	    err = grub_relocator_alloc_chunk_addr (relocator, &ch,
						   target, memsize);
	    if (err)
	      return err;

	    load_addr = get_virtual_current_address (ch);

	    if (filesize)
	      {
		if (grub_file_seek (file, grub_be_to_cpu32 (segment.offset))
		    == (grub_off_t) -1)
		  return grub_errno;

		err = load_segment (file, filename, load_addr,
				    segment.compression, &filesize, memsize);
		if (err)
		  return err;
	      }

	    if (filesize < memsize)
	      grub_memset ((load_addr + filesize),
			   0, memsize - filesize);
	  }
	}
    }
}

static grub_err_t
grub_cmd_chain (grub_command_t cmd __attribute__ ((unused)),
		int argc, char *argv[])
{
  grub_err_t err;
  grub_file_t file;
  grub_uint32_t head;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  grub_loader_unset ();

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_COREBOOT_CHAINLOADER);
  if (!file)
    return grub_errno;

  relocator = grub_relocator_new ();
  if (!relocator)
    {
      grub_file_close (file);
      return grub_errno;
    }

  if (grub_file_read (file, &head, sizeof (head)) != sizeof (head)
      || grub_file_seek (file, 0) == (grub_off_t) -1)
    {
      grub_file_close (file);
      grub_relocator_unload (relocator);
      relocator = 0;
      if (!grub_errno)
	return grub_error (GRUB_ERR_BAD_OS,
			   "payload is too short");
      return grub_errno;
    }
      
  switch (head)
    {
    case ELFMAG0 | (ELFMAG1 << 8) | (ELFMAG2 << 16) | (ELFMAG3 << 24):
      err = load_elf (file, argv[0]);
      break;
    case PAYLOAD_SEGMENT_CODE:
    case PAYLOAD_SEGMENT_DATA:
    case PAYLOAD_SEGMENT_PARAMS:
    case PAYLOAD_SEGMENT_BSS:
    case PAYLOAD_SEGMENT_ENTRY:
      err = load_chewed (file, argv[0]);
      break;

    default:
      if (grub_file_seek (file, 40) == (grub_off_t) -1
	  || grub_file_read (file, &head, sizeof (head)) != sizeof (head)
	  || grub_file_seek (file, 0) == (grub_off_t) -1
	  || head != 0x4856465f)
	err = grub_error (GRUB_ERR_BAD_OS, "unrecognised payload type");
      else
	err = load_tianocore (file);
      break;
    }
  grub_file_close (file);
  if (err)
    {
      grub_relocator_unload (relocator);
      relocator = 0;
      return err;
    }

  grub_loader_set (grub_chain_boot, grub_chain_unload, 0);
  return GRUB_ERR_NONE;
}

static grub_command_t cmd_chain;

GRUB_MOD_INIT (chain)
{
  cmd_chain = grub_register_command ("chainloader", grub_cmd_chain,
				     N_("FILE"),
				     /* TRANSLATORS: "payload" is a term used
					by coreboot and must be translated in
					sync with coreboot. If unsure,
					let it untranslated.  */
				     N_("Load another coreboot payload"));
}

GRUB_MOD_FINI (chain)
{
  grub_unregister_command (cmd_chain);
  grub_chain_unload ();
}
