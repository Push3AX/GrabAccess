/* chainloader.c - boot another boot loader */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2004,2006,2007,2008  Free Software Foundation, Inc.
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

/* TODO: support load options.  */

#include <grub/loader.h>
#include <grub/file.h>
#include <grub/err.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/misc.h>
#include <grub/charset.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/disk.h>
#include <grub/efi/pe32.h>
#include <grub/efi/linux.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/net.h>
#include <grub/term.h>
#if defined (__i386__) || defined (__x86_64__)
#include <grub/macho.h>
#include <grub/i386/macho.h>
#endif

GRUB_MOD_LICENSE ("GPLv3+");

static grub_dl_t my_mod;

static const struct grub_arg_option options_chain[] = {
  {"alt", 'a', 0, N_("Use alternative secure boot loader."), 0, 0},
  {"text", 't', 0, N_("Set terminal to text."), 0, 0},
  {"boot", 'b', 0, N_("Start Image (for command line)."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

enum options_chain
{
  CHAIN_ALT,
  CHAIN_TEXT,
  CHAIN_BOOT
};

static grub_efi_physical_address_t address;
static grub_efi_uintn_t pages;
static grub_ssize_t fsize;
static grub_efi_device_path_t *file_path;
static grub_efi_handle_t image_handle;
static grub_efi_char16_t *cmdline;
static grub_ssize_t cmdline_len;
static grub_efi_handle_t dev_handle;

static grub_efi_status_t (*entry_point) (grub_efi_handle_t image_handle, grub_efi_system_table_t *system_table);

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
#endif

static grub_err_t
grub_chainloader_unload (void)
{
  grub_efi_boot_services_t *b;

  b = grub_efi_system_table->boot_services;
  efi_call_1 (b->unload_image, image_handle);
  grub_efi_free_pages (address, pages);

  grub_free (file_path);
  grub_free (cmdline);
  cmdline = 0;
  file_path = 0;
  dev_handle = 0;

  grub_dl_unref (my_mod);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_chainloader_boot (void)
{
  grub_efi_boot_services_t *b;
  grub_efi_status_t status;
  grub_efi_uintn_t exit_data_size;
  grub_efi_char16_t *exit_data = NULL;

  b = grub_efi_system_table->boot_services;
  status = efi_call_3 (b->start_image, image_handle, &exit_data_size, &exit_data);
  if (status != GRUB_EFI_SUCCESS)
    {
      if (exit_data)
	{
	  char *buf;

	  buf = grub_malloc (exit_data_size * 4 + 1);
	  if (buf)
	    {
	      *grub_utf16_to_utf8 ((grub_uint8_t *) buf,
				   exit_data, exit_data_size) = 0;

	      grub_dprintf ("chain", "%s\n", buf);
	      grub_free (buf);
	    }
	}
      else
	grub_dprintf ("chain", "Exit status code: 0x%08lx\n", (long unsigned int) status);
    }

  if (exit_data)
    grub_efi_free_pool (exit_data);

  grub_loader_unset ();

  return grub_errno;
}

typedef union
{
  struct grub_pe32_header_32 pe32;
  struct grub_pe32_header_64 pe32plus;
} grub_pe_header_t;

struct pe_coff_loader_image_context
{
  grub_efi_uint64_t image_address;
  grub_efi_uint64_t image_size;
  grub_efi_uint64_t entry_point;
  grub_efi_uintn_t size_of_headers;
  grub_efi_uint16_t image_type;
  grub_efi_uint16_t number_of_sections;
  grub_efi_uint32_t section_alignment;
  struct grub_pe32_section_table *first_section;
  struct grub_pe32_data_directory *reloc_dir;
  struct grub_pe32_data_directory *sec_dir;
  grub_efi_uint64_t number_of_rva_and_sizes;
  grub_pe_header_t *pe_hdr;
};
typedef struct pe_coff_loader_image_context pe_coff_loader_image_context_t;

struct grub_efi_shim_lock
{
  grub_efi_status_t (*verify)(void *buffer,
                              grub_efi_uint32_t size);
  grub_efi_status_t (*hash)(void *data,
                            grub_efi_int32_t datasize,
                            pe_coff_loader_image_context_t *context,
                            grub_efi_uint8_t *sha256hash,
                            grub_efi_uint8_t *sha1hash);
  grub_efi_status_t (*context)(void *data,
                               grub_efi_uint32_t size,
                               pe_coff_loader_image_context_t *context);
};
typedef struct grub_efi_shim_lock grub_efi_shim_lock_t;

static int
image_is_64_bit (grub_pe_header_t *pe_hdr)
{
  /* .Magic is the same offset in all cases */
  if (pe_hdr->pe32plus.optional_header.magic == GRUB_PE32_PE64_MAGIC)
    return 1;
  return 0;
}

static grub_efi_boolean_t
read_header (void *data, grub_efi_uint32_t size,
	     pe_coff_loader_image_context_t *context)
{
#if defined(__i386__) || defined(__x86_64__)
  char *msdos = (char *) data;
  grub_pe_header_t *pe_hdr = (grub_pe_header_t *)data;

  if (size < sizeof (*pe_hdr))
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid image");
      return 0;
    }

  if (grub_memcmp (msdos, "MZ", 2) == 0)
    {
      grub_uint32_t off = *((grub_uint32_t *) (msdos + 0x3c));
      pe_hdr = (grub_pe_header_t *) ((char *)data + off);
    }

  if (grub_memcmp (pe_hdr->pe32plus.signature, "PE\0\0", 4) != 0 ||
#if defined(__x86_64__)
      ! image_is_64_bit (pe_hdr) ||
      pe_hdr->pe32plus.coff_header.machine != GRUB_PE32_MACHINE_X86_64)
#else
      image_is_64_bit (pe_hdr) ||
      pe_hdr->pe32.coff_header.machine != GRUB_PE32_MACHINE_I386)
#endif
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "Not supported image");
      return 0;
    }

#if defined(__x86_64__)
  context->number_of_rva_and_sizes = pe_hdr->pe32plus.optional_header.num_data_directories;
  context->size_of_headers = pe_hdr->pe32plus.optional_header.header_size;
  context->image_size = pe_hdr->pe32plus.optional_header.image_size;
  context->image_address = pe_hdr->pe32plus.optional_header.image_base;
  context->entry_point = pe_hdr->pe32plus.optional_header.entry_addr;
  context->reloc_dir = &pe_hdr->pe32plus.optional_header.base_relocation_table;
  context->sec_dir = &pe_hdr->pe32plus.optional_header.certificate_table;
  context->number_of_sections = pe_hdr->pe32plus.coff_header.num_sections;
  context->pe_hdr = pe_hdr;
  context->first_section = (struct grub_pe32_section_table *)((char *)(&pe_hdr->pe32plus.optional_header) + pe_hdr->pe32plus.coff_header.optional_header_size);
#else
  context->number_of_rva_and_sizes = pe_hdr->pe32.optional_header.num_data_directories;
  context->size_of_headers = pe_hdr->pe32.optional_header.header_size;
  context->image_size = pe_hdr->pe32.optional_header.image_size;
  context->image_address = pe_hdr->pe32.optional_header.image_base;
  context->entry_point = pe_hdr->pe32.optional_header.entry_addr;
  context->reloc_dir = &pe_hdr->pe32.optional_header.base_relocation_table;
  context->sec_dir = &pe_hdr->pe32.optional_header.certificate_table;
  context->number_of_sections = pe_hdr->pe32.coff_header.num_sections;
  context->pe_hdr = pe_hdr;
  context->first_section = (struct grub_pe32_section_table *)((char *)(&pe_hdr->pe32.optional_header) + pe_hdr->pe32.coff_header.optional_header_size);
#endif
  return 1;
#else
  grub_efi_guid_t guid = GRUB_EFI_SHIM_LOCK_GUID;
  grub_efi_shim_lock_t *shim_lock;
  grub_efi_status_t status;

  shim_lock = grub_efi_locate_protocol (&guid, NULL);

  if (!shim_lock)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "no shim lock protocol");
      return 0;
    }

  status = shim_lock->context (data, size, context);

  if (status == GRUB_EFI_SUCCESS)
    {
      grub_dprintf ("chain", "chain: context success\n");
      return 1;
    }

  switch (status)
    {
      case GRUB_EFI_UNSUPPORTED:
      grub_error (GRUB_ERR_BAD_ARGUMENT, "context error unsupported");
      break;
      case GRUB_EFI_INVALID_PARAMETER:
      grub_error (GRUB_ERR_BAD_ARGUMENT, "context error invalid parameter");
      break;
      default:
      grub_error (GRUB_ERR_BAD_ARGUMENT, "context error code");
      break;
    }

  return 0;
#endif
}

static void*
image_address (void *image, grub_efi_uint64_t sz, grub_efi_uint64_t adr)
{
  if (adr > sz)
    return NULL;

  return ((grub_uint8_t*)image + adr);
}

static const grub_uint16_t machine_type __attribute__((__unused__)) =
#if defined(__x86_64__)
  GRUB_PE32_MACHINE_X86_64;
#elif defined(__aarch64__)
  GRUB_PE32_MACHINE_ARM64;
#elif defined(__arm__)
  GRUB_PE32_MACHINE_ARMTHUMB_MIXED;
#elif defined(__i386__) || defined(__i486__) || defined(__i686__)
  GRUB_PE32_MACHINE_I386;
#elif defined(__ia64__)
  GRUB_PE32_MACHINE_IA64;
#else
#error this architecture is not supported by grub2
#endif

static grub_efi_status_t
relocate_coff (pe_coff_loader_image_context_t *context,
	       struct grub_pe32_section_table *section,
	       void *orig, void *data)
{
  struct grub_pe32_data_directory *reloc_base, *reloc_base_end;
  grub_efi_uint64_t adjust;
  struct grub_pe32_fixup_block *reloc, *reloc_end;
  char *fixup, *fixup_base, *fixup_data = NULL;
  grub_efi_uint16_t *fixup_16;
  grub_efi_uint32_t *fixup_32;
  grub_efi_uint64_t *fixup_64;
  grub_efi_uint64_t size = context->image_size;
  void *image_end = (char *)orig + size;
  int n = 0;

  if (image_is_64_bit (context->pe_hdr))
    context->pe_hdr->pe32plus.optional_header.image_base =
      (grub_uint64_t)(unsigned long)data;
  else
    context->pe_hdr->pe32.optional_header.image_base =
      (grub_uint32_t)(unsigned long)data;

  /* Alright, so here's how this works:
   *
   * context->reloc_dir gives us two things:
   * - the VA the table of base relocation blocks are (maybe) to be
   *   mapped at (reloc_dir->rva)
   * - the virtual size (reloc_dir->size)
   *
   * The .reloc section (section here) gives us some other things:
   * - the name! kind of. (section->name)
   * - the virtual size (section->virtual_size), which should be the same
   *   as RelocDir->Size
   * - the virtual address (section->virtual_address)
   * - the file section size (section->raw_data_size), which is
   *   a multiple of optional_header->file_alignment.  Only useful for image
   *   validation, not really useful for iteration bounds.
   * - the file address (section->raw_data_offset)
   * - a bunch of stuff we don't use that's 0 in our binaries usually
   * - Flags (section->characteristics)
   *
   * and then the thing that's actually at the file address is an array
   * of struct grub_pe32_fixup_block structs with some values packed behind
   * them.  The block_size field of this structure includes the
   * structure itself, and adding it to that structure's address will
   * yield the next entry in the array.
   */

  reloc_base = image_address (orig, size, section->raw_data_offset);
  reloc_base_end = image_address (orig, size, section->raw_data_offset
				  + section->virtual_size);

  grub_dprintf ("chain", "chain: relocate_coff(): reloc_base %p reloc_base_end %p\n",
		reloc_base, reloc_base_end);

  if (!reloc_base && !reloc_base_end)
    return GRUB_EFI_SUCCESS;

  if (!reloc_base || !reloc_base_end)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "Reloc table overflows binary");
      return GRUB_EFI_UNSUPPORTED;
    }

  adjust = (grub_uint64_t)(grub_efi_uintn_t)data - context->image_address;
  if (adjust == 0)
    return GRUB_EFI_SUCCESS;

  while (reloc_base < reloc_base_end)
    {
      grub_uint16_t *entry;
      reloc = (struct grub_pe32_fixup_block *)((char*)reloc_base);

      if ((reloc_base->size == 0) ||
	  (reloc_base->size > context->reloc_dir->size))
	{
	  grub_error (GRUB_ERR_BAD_ARGUMENT,
		      "Reloc %d block size %d is invalid\n", n,
		      reloc_base->size);
	  return GRUB_EFI_UNSUPPORTED;
	}

      entry = &reloc->entries[0];
      reloc_end = (struct grub_pe32_fixup_block *)
	((char *)reloc_base + reloc_base->size);

      if ((void *)reloc_end < orig || (void *)reloc_end > image_end)
        {
          grub_error (GRUB_ERR_BAD_ARGUMENT, "Reloc entry %d overflows binary",
		      n);
          return GRUB_EFI_UNSUPPORTED;
        }

      fixup_base = image_address(data, size, reloc_base->rva);

      if (!fixup_base)
        {
          grub_error (GRUB_ERR_BAD_ARGUMENT, "Reloc %d Invalid fixupbase", n);
          return GRUB_EFI_UNSUPPORTED;
        }

      while ((void *)entry < (void *)reloc_end)
        {
          fixup = fixup_base + (*entry & 0xFFF);
          switch ((*entry) >> 12)
            {
              case GRUB_PE32_REL_BASED_ABSOLUTE:
                break;
              case GRUB_PE32_REL_BASED_HIGH:
                fixup_16 = (grub_uint16_t *)fixup;
                *fixup_16 = (grub_uint16_t)
		  (*fixup_16 + ((grub_uint16_t)((grub_uint32_t)adjust >> 16)));
                if (fixup_data != NULL)
                  {
                    *(grub_uint16_t *) fixup_data = *fixup_16;
                    fixup_data = fixup_data + sizeof (grub_uint16_t);
                  }
                break;
              case GRUB_PE32_REL_BASED_LOW:
                fixup_16 = (grub_uint16_t *)fixup;
                *fixup_16 = (grub_uint16_t) (*fixup_16 + (grub_uint16_t)adjust);
                if (fixup_data != NULL)
                  {
                    *(grub_uint16_t *) fixup_data = *fixup_16;
                    fixup_data = fixup_data + sizeof (grub_uint16_t);
                  }
                break;
              case GRUB_PE32_REL_BASED_HIGHLOW:
                fixup_32 = (grub_uint32_t *)fixup;
                *fixup_32 = *fixup_32 + (grub_uint32_t)adjust;
                if (fixup_data != NULL)
                  {
                    fixup_data = (char *)ALIGN_UP ((grub_addr_t)fixup_data, sizeof (grub_uint32_t));
                    *(grub_uint32_t *) fixup_data = *fixup_32;
                    fixup_data += sizeof (grub_uint32_t);
                  }
                break;
              case GRUB_PE32_REL_BASED_DIR64:
                fixup_64 = (grub_uint64_t *)fixup;
                *fixup_64 = *fixup_64 + (grub_uint64_t)adjust;
                if (fixup_data != NULL)
                  {
                    fixup_data = (char *)ALIGN_UP ((grub_addr_t)fixup_data, sizeof (grub_uint64_t));
                    *(grub_uint64_t *) fixup_data = *fixup_64;
                    fixup_data += sizeof (grub_uint64_t);
                  }
                break;
              default:
                grub_error (GRUB_ERR_BAD_ARGUMENT,
			    "Reloc %d unknown relocation type %d",
			    n, (*entry) >> 12);
                return GRUB_EFI_UNSUPPORTED;
            }
          entry += 1;
        }
      reloc_base = (struct grub_pe32_data_directory *)reloc_end;
      n++;
    }

  return GRUB_EFI_SUCCESS;
}

static grub_efi_device_path_t *
grub_efi_get_media_file_path (grub_efi_device_path_t *dp)
{
  while (1)
    {
      grub_efi_uint8_t type = GRUB_EFI_DEVICE_PATH_TYPE (dp);
      grub_efi_uint8_t subtype = GRUB_EFI_DEVICE_PATH_SUBTYPE (dp);

      if (type == GRUB_EFI_END_DEVICE_PATH_TYPE)
        break;
      else if (type == GRUB_EFI_MEDIA_DEVICE_PATH_TYPE
            && subtype == GRUB_EFI_FILE_PATH_DEVICE_PATH_SUBTYPE)
      return dp;

      dp = GRUB_EFI_NEXT_DEVICE_PATH (dp);
    }

    return NULL;
}

static grub_efi_boolean_t
handle_image (void *data, grub_efi_uint32_t datasize)
{
  grub_efi_loaded_image_t *li, li_bak;
  grub_efi_status_t efi_status;
  void *buffer = NULL;
  char *buffer_aligned = NULL;
  grub_efi_uint32_t i;
  struct grub_pe32_section_table *section;
  char *base, *end;
  pe_coff_loader_image_context_t context;
  grub_uint32_t section_alignment;
  grub_uint32_t buffer_size;
  int found_entry_point = 0;

  if (read_header (data, datasize, &context))
    {
      grub_dprintf ("chain", "chain: Succeed to read header\n");
    }
  else
    {
      grub_dprintf ("chain", "chain: Failed to read header\n");
      goto error_exit;
    }

  /*
   * The spec says, uselessly, of SectionAlignment:
   * =====
   * The alignment (in bytes) of sections when they are loaded into
   * memory. It must be greater than or equal to FileAlignment. The
   * default is the page size for the architecture.
   * =====
   * Which doesn't tell you whose responsibility it is to enforce the
   * "default", or when.  It implies that the value in the field must
   * be > FileAlignment (also poorly defined), but it appears visual
   * studio will happily write 512 for FileAlignment (its default) and
   * 0 for SectionAlignment, intending to imply PAGE_SIZE.
   *
   * We only support one page size, so if it's zero, nerf it to 4096.
   */
  section_alignment = context.section_alignment;
  if (section_alignment == 0)
    section_alignment = 4096;

  buffer_size = context.image_size + section_alignment;
  grub_dprintf ("chain", "chain: image size is %08lx, datasize is %08x\n",
	       (long unsigned)context.image_size, datasize);

  efi_status = grub_efi_allocate_pool (GRUB_EFI_LOADER_DATA, buffer_size, 
                                       &buffer);

  if (efi_status != GRUB_EFI_SUCCESS)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
      goto error_exit;
    }

  buffer_aligned = (char *)ALIGN_UP ((grub_addr_t)buffer, section_alignment);

  if (!buffer_aligned)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
      goto error_exit;
    }

  grub_memcpy (buffer_aligned, data, context.size_of_headers);
  
  entry_point = image_address (buffer_aligned, context.image_size,
			       context.entry_point);

  grub_dprintf ("chain", "chain: entry_point: %p\n", entry_point);
  if (!entry_point)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid entry point");
      goto error_exit;
    }

  char *reloc_base, *reloc_base_end;
  
  reloc_base = image_address (buffer_aligned, context.image_size,
			      context.reloc_dir->rva);
  /* RelocBaseEnd here is the address of the last byte of the table */
  reloc_base_end = image_address (buffer_aligned, context.image_size,
				  context.reloc_dir->rva
				  + context.reloc_dir->size - 1);
  grub_dprintf ("chain", "chain: reloc_base: %p reloc_base_end: %p\n",
		reloc_base, reloc_base_end);
  struct grub_pe32_section_table *reloc_section = NULL;

  section = context.first_section;
  for (i = 0; i < context.number_of_sections; i++, section++)
    {
      char name[9];

      base = image_address (buffer_aligned, context.image_size,
			    section->virtual_address);
      end = image_address (buffer_aligned, context.image_size,
			   section->virtual_address + section->virtual_size -1);

      grub_strncpy(name, section->name, 9);
      name[8] = '\0';
      grub_dprintf ("chain", "chain: Section %d \"%s\" at %p..%p\n", i,
		   name, base, end);

      if (end < base)
	{
	  grub_dprintf ("chain", "chain: base is %p but end is %p... bad.\n",
		       base, end);
	  grub_error (GRUB_ERR_BAD_ARGUMENT,
		      "Image has invalid negative size");
	  goto error_exit;
	}

      if (section->virtual_address <= context.entry_point &&
	  (section->virtual_address + section->raw_data_size - 1)
	  > context.entry_point)
	{
	  found_entry_point++;
	  grub_dprintf ("chain", "chain: section contains entry point\n");
	}

      /* We do want to process .reloc, but it's often marked
       * discardable, so we don't want to memcpy it. */
      if (grub_memcmp (section->name, ".reloc\0\0", 8) == 0)
	{
	  if (reloc_section)
	    {
	      grub_error (GRUB_ERR_BAD_ARGUMENT,
			  "Image has multiple relocation sections");
	      goto error_exit;
	    }

	  /* If it has nonzero sizes, and our bounds check
	   * made sense, and the VA and size match RelocDir's
	   * versions, then we believe in this section table. */
	  if (section->raw_data_size && section->virtual_size &&
	      base && end && reloc_base == base && reloc_base_end == end)
	    {
	      grub_dprintf ("chain", "chain: section is relocation section\n");
          reloc_section = section;
	    }
      else
	    {
	      grub_dprintf ("chain", "chain: section is not reloc section?\n");
	      grub_dprintf ("chain", "chain: rds: 0x%08x, vs: %08x\n",
			    section->raw_data_size, section->virtual_size);
	      grub_dprintf ("chain", "chain: base: %p end: %p\n", base, end);
	      grub_dprintf ("chain", "chain: reloc_base: %p reloc_base_end: %p\n",
			    reloc_base, reloc_base_end);
	    }
	}

      grub_dprintf ("chain", "chain: Section characteristics are %08x\n",
		   section->characteristics);
      grub_dprintf ("chain", "chain: Section virtual size: %08x\n",
		   section->virtual_size);
      grub_dprintf ("chain", "chain: Section raw_data size: %08x\n",
		   section->raw_data_size);
      if (section->characteristics & GRUB_PE32_SCN_MEM_DISCARDABLE)
	{
	  grub_dprintf ("chain", "chain: Discarding section\n");
	  continue;
    }

      if (!base || !end)
        {
          grub_dprintf ("chain", "chain: section is invalid\n");
          grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid section size");
          goto error_exit;
        }

      if (section->characteristics & GRUB_PE32_SCN_CNT_UNINITIALIZED_DATA)
	{
	  if (section->raw_data_size != 0)
	    grub_dprintf ("chain", "chain: UNINITIALIZED_DATA section has data?\n");
	}
      else if (section->virtual_address < context.size_of_headers ||
	       section->raw_data_offset < context.size_of_headers)
	{
	  grub_error (GRUB_ERR_BAD_ARGUMENT,
		      "Section %d is inside image headers", i);
	  goto error_exit;
	}

      if (section->raw_data_size > 0)
    {
	  grub_dprintf ("chain", "chain: copying 0x%08x bytes to %p\n",
			section->raw_data_size, base);
	  grub_memcpy (base,
		       (grub_efi_uint8_t*)data + section->raw_data_offset,
		       section->raw_data_size);
	}

      if (section->raw_data_size < section->virtual_size)
	{
	  grub_dprintf ("chain", "chain: padding with 0x%08x bytes at %p\n",
			section->virtual_size - section->raw_data_size,
			base + section->raw_data_size);
	  grub_memset (base + section->raw_data_size, 0,
		       section->virtual_size - section->raw_data_size);
	}

      grub_dprintf ("chain", "chain: finished section %s\n", name);
    }

  /* 5 == EFI_IMAGE_DIRECTORY_ENTRY_BASERELOC */
  if (context.number_of_rva_and_sizes <= 5)
    {
      grub_dprintf ("chain", "chain: image has no relocation entry\n");
      goto error_exit;
    }

  if (context.reloc_dir->size && reloc_section)
    {
      /* run the relocation fixups */
      efi_status = relocate_coff (&context, reloc_section, data,
				  buffer_aligned);

      if (efi_status != GRUB_EFI_SUCCESS)
	{
	  grub_error (GRUB_ERR_BAD_ARGUMENT, "relocation failed");
	  goto error_exit;
	}
    }

  if (!found_entry_point)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "entry point is not within sections");
      goto error_exit;
    }
  if (found_entry_point > 1)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "%d sections contain entry point",
		  found_entry_point);
      goto error_exit;
    }

  li = grub_efi_get_loaded_image (grub_efi_image_handle);
  if (!li)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "no loaded image available");
      goto error_exit;
    }

  grub_memcpy (&li_bak, li, sizeof (grub_efi_loaded_image_t));
  li->image_base = buffer_aligned;
  li->image_size = context.image_size;
  li->load_options = cmdline;
  li->load_options_size = cmdline_len;
  li->file_path = grub_efi_get_media_file_path (file_path);
  li->device_handle = dev_handle;
  if (!li->file_path)
    {
      grub_error (GRUB_ERR_UNKNOWN_DEVICE, "no matching file path found");
      goto error_exit;
    }

  grub_dprintf ("chain", "chain: booting via entry point\n");
  efi_status = efi_call_2 (entry_point, grub_efi_image_handle,
			   grub_efi_system_table);

  grub_dprintf ("chain", "chain: entry_point returned %ld\n", (long int) efi_status);
  grub_memcpy (li, &li_bak, sizeof (grub_efi_loaded_image_t));
  efi_status = grub_efi_free_pool (buffer);

  return 1;

error_exit:
  grub_dprintf ("chain", "chain: error_exit: grub_errno: %d\n", grub_errno);
  if (buffer)
      grub_efi_free_pool (buffer);
  
  if (grub_errno)
    grub_print_error ();

  return 0;
}

static grub_err_t
grub_secureboot_chainloader_unload (void)
{
  grub_efi_free_pages (address, pages);
  grub_free (file_path);
  grub_free (cmdline);
  cmdline = 0;
  file_path = 0;
  dev_handle = 0;

  grub_dl_unref (my_mod);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_secureboot_chainloader_boot (void)
{
  handle_image ((void *)(unsigned long)address, fsize);
  grub_loader_unset ();
  return grub_errno;
}

static grub_efi_boolean_t
grub_chainloader_dp (const char *devname, const char *filename)
{
  grub_device_t dev = 0;
  grub_efi_device_path_t *dp = 0;

  file_path = 0;
  dev_handle = 0;

  dev = grub_device_open (devname);
  if (! dev)
    goto dp_fail;

  if (dev->disk)
    dev_handle = grub_efidisk_get_device_handle (dev->disk);
  else if (dev->net && dev->net->server)
  {
    grub_net_network_level_address_t addr;
    struct grub_net_network_level_interface *inf;
    grub_net_network_level_address_t gateway;
    grub_err_t err;

    err = grub_net_resolve_address (dev->net->server, &addr);
    if (err)
      goto dp_fail;
    err = grub_net_route_address (addr, &gateway, &inf);
    if (err)
      goto dp_fail;

    dev_handle = grub_efinet_get_device_handle (inf->card);
  }

  if (dev_handle)
    dp = grub_efi_get_device_path (dev_handle);

  if (dp)
    file_path = grub_efi_file_device_path (dp, filename);

dp_fail:
  if (dev)
    grub_device_close (dev);
  return file_path? 1: 0;
}

static grub_err_t
grub_cmd_chainloader (grub_extcmd_context_t ctxt,
		      int argc, char *argv[])
{
  struct grub_arg_list *state = ctxt->state;
  grub_file_t file = 0;
  grub_efi_status_t status;
  grub_efi_boot_services_t *b;
  grub_efi_loaded_image_t *loaded_image;
  char *filename;
  void *boot_image = 0;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));
  filename = argv[0];

  grub_dl_ref (my_mod);

  /* Initialize some global variables.  */
  address = 0;
  image_handle = 0;

  b = grub_efi_system_table->boot_services;

  if (argc > 1)
    {
      int i;
      grub_efi_char16_t *p16;

      for (i = 1, cmdline_len = 0; i < argc; i++)
        cmdline_len += grub_strlen (argv[i]) + 1;

      cmdline_len *= sizeof (grub_efi_char16_t);
      cmdline = p16 = grub_malloc (cmdline_len);
      if (! cmdline)
        goto fail;

      for (i = 1; i < argc; i++)
        {
          char *p8;

          p8 = argv[i];
          while (*p8)
            *(p16++) = *(p8++);

          *(p16++) = ' ';
        }
      *(--p16) = 0;
    }

  file = grub_file_open (filename, GRUB_FILE_TYPE_EFI_CHAINLOADED_IMAGE);
  if (! file)
    goto fail;

  /* Get the device path from filename. */
  char *devname = grub_file_get_device_name (filename);
  if (! grub_chainloader_dp (devname, filename))
  {
    grub_printf ("Warning: Can't get device path from file name.\n");
    if (! grub_chainloader_dp (0, filename))
      grub_printf ("Warning: Can't get device path from root device.\n");
  }
  grub_printf ("Booting ");
  grub_efi_print_device_path (file_path);
  grub_printf ("\n");

  if (devname)
    grub_free (devname);

  fsize = grub_file_size (file);
  if (!fsize)
    {
      grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		  filename);
      goto fail;
    }
  pages = (((grub_efi_uintn_t) fsize + ((1 << 12) - 1)) >> 12);

  status = efi_call_4 (b->allocate_pages, GRUB_EFI_ALLOCATE_ANY_PAGES,
			      GRUB_EFI_LOADER_CODE,
			      pages, &address);
  if (status != GRUB_EFI_SUCCESS)
    {
      grub_dprintf ("chain", "Failed to allocate %u pages\n",
		    (unsigned int) pages);
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
      goto fail;
    }

  boot_image = (void *) ((grub_addr_t) address);
  if (grub_file_read (file, boot_image, fsize) != fsize)
    {
      if (grub_errno == GRUB_ERR_NONE)
	grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		    filename);

      goto fail;
    }

#if defined (__i386__) || defined (__x86_64__)
  if (fsize >= (grub_ssize_t) sizeof (struct grub_macho_fat_header))
    {
      struct grub_macho_fat_header *head = boot_image;
      if (head->magic
	  == grub_cpu_to_le32_compile_time (GRUB_MACHO_FAT_EFI_MAGIC))
	{
	  grub_uint32_t i;
	  struct grub_macho_fat_arch *archs
	    = (struct grub_macho_fat_arch *) (head + 1);

	  for (i = 0; i < grub_cpu_to_le32 (head->nfat_arch); i++)
	    {
	      if (GRUB_MACHO_CPUTYPE_IS_HOST_CURRENT (archs[i].cputype))
		break;
	    }
	  if (i == grub_cpu_to_le32 (head->nfat_arch))
	    {
	      grub_error (GRUB_ERR_BAD_OS, "no compatible arch found");
	      goto fail;
	    }
	  if (grub_cpu_to_le32 (archs[i].offset)
	      > ~grub_cpu_to_le32 (archs[i].size)
	      || grub_cpu_to_le32 (archs[i].offset)
	      + grub_cpu_to_le32 (archs[i].size)
	      > (grub_size_t) fsize)
	    {
	      grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			  filename);
	      goto fail;
	    }
	  boot_image = (char *) boot_image + grub_cpu_to_le32 (archs[i].offset);
	  fsize = grub_cpu_to_le32 (archs[i].size);
	}
    }
#endif

  if (state[CHAIN_TEXT].set)
  {
    grub_script_execute_sourcecode ("terminal_output console");
    grub_printf ("Switch to text mode.\n");
    grub_refresh ();
  }

  if (state[CHAIN_ALT].set)
    {
	  grub_file_close (file);
      grub_loader_set (grub_secureboot_chainloader_boot,
      grub_secureboot_chainloader_unload, 0);
      if (state[CHAIN_BOOT].set)
        {
          handle_image ((void *)(unsigned long)address, fsize);
          grub_dprintf ("chain", "Exit alternative chainloader.\n");
          grub_loader_unset (); 
        } 
      return 0;
    }

  status = efi_call_6 (b->load_image, 0, grub_efi_image_handle, file_path,
		       boot_image, fsize, &image_handle);

  if (status == GRUB_EFI_SECURITY_VIOLATION)
    {
      /* If it failed with security violation while not in secure boot mode,
         the firmware might be broken. We try to workaround on that by forcing
         the SB method! (bsc#887793) */
      grub_dprintf ("chain", "LoadImage failed with EFI_SECURITY_VIOLATION.\n");
      grub_dprintf ("chain", "Try alternative chainloader");
      grub_file_close (file);
      grub_loader_set (grub_secureboot_chainloader_boot,
	      grub_secureboot_chainloader_unload, 0);
      if (state[CHAIN_BOOT].set)
        {
          handle_image ((void *)(unsigned long)address, fsize);
          grub_dprintf ("chain", "Exit alternative chainloader.\n");
          grub_loader_unset (); 
        } 
      return 0;
    }

  if (status != GRUB_EFI_SUCCESS)
    {
      if (status == GRUB_EFI_OUT_OF_RESOURCES)
	grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of resources");
      else
	grub_error (GRUB_ERR_BAD_OS, "cannot load image");

      goto fail;
    }

  /* LoadImage does not set a device handler when the image is
     loaded from memory, so it is necessary to set it explicitly here.
     This is a mess.  */
  loaded_image = grub_efi_get_loaded_image (image_handle);
  if (! loaded_image)
    {
      grub_error (GRUB_ERR_BAD_OS, "no loaded image available");
      goto fail;
    }
  loaded_image->device_handle = dev_handle;

  if (cmdline)
    {
      loaded_image->load_options = cmdline;
      loaded_image->load_options_size = cmdline_len;
    }

  grub_file_close (file);

  grub_loader_set (grub_chainloader_boot, grub_chainloader_unload,
                   GRUB_LOADER_FLAG_EFI_KEEP_ALLOCATED_MEMORY);

  if (state[CHAIN_BOOT].set)
    {
      status = efi_call_3 (b->start_image, image_handle, NULL, NULL);
      grub_dprintf ("chain", "Exit status code: 0x%08lx\n", (long unsigned int) status);
	  grub_loader_unset ();
    }
  return 0;

 fail:

  if (file)
    grub_file_close (file);

  grub_free (file_path);

  if (address)
    grub_efi_free_pages (address, pages);

  if (cmdline)
    grub_free (cmdline);

  grub_dl_unref (my_mod);

  return grub_errno;
}

static grub_extcmd_t cmd;

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

GRUB_MOD_INIT(chainloader)
{
  cmd = grub_register_extcmd ("chainloader", grub_cmd_chainloader,
				   GRUB_COMMAND_ACCEPT_DASH | GRUB_COMMAND_OPTIONS_AT_START,
				   N_("[--alt] [--text] FILE CMDLINE"),
				   N_("Load another boot loader."), options_chain);
  my_mod = mod;
}

GRUB_MOD_FINI(chainloader)
{
  grub_unregister_extcmd (cmd);
}
