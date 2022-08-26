/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2005,2007,2008,2009  Free Software Foundation, Inc.
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

#include <grub/memory.h>
#ifdef GRUB_MACHINE_PCBIOS
#include <grub/machine/biosnum.h>
#include <grub/machine/apm.h>
#include <grub/machine/memory.h>
#endif
#include <grub/multiboot.h>
#include <grub/cpu/relocator.h>
#include <grub/disk.h>
#include <grub/device.h>
#include <grub/partition.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/env.h>
#include <grub/relocator.h>
#include <grub/video.h>
#include <grub/file.h>
#include <grub/net.h>
#include <grub/i18n.h>
#include <grub/lib/cmdline.h>

#ifdef GRUB_MACHINE_EFI
#include <grub/efi/efi.h>
#endif

/* The bits in the required part of flags field we don't support.  */
#define UNSUPPORTED_FLAGS			0x0000fff8

struct module
{
  struct module *next;
  grub_addr_t start;
  grub_size_t size;
  char *cmdline;
  int cmdline_size;
};

static struct module *modules, *modules_last;
static grub_size_t cmdline_size;
static grub_size_t total_modcmd;
static unsigned modcnt;
static char *cmdline = NULL;
static grub_uint32_t bootdev;
static int bootdev_set;
static grub_size_t elf_sec_num, elf_sec_entsize;
static unsigned elf_sec_shstrndx;
static void *elf_sections;
grub_multiboot_quirks_t grub_multiboot_quirks;
int grub_multiboot_no_exit = 0;

static grub_err_t
load_kernel (grub_file_t file, const char *filename,
	     char *buffer, struct multiboot_header *header)
{
  grub_err_t err;
  mbi_load_data_t mld;

  mld.file = file;
  mld.filename = filename;
  mld.buffer = buffer;
  mld.mbi_ver = 1;
  mld.relocatable = 0;
  mld.avoid_efi_boot_services = 0;

  if (grub_multiboot_quirks & GRUB_MULTIBOOT_QUIRK_BAD_KLUDGE)
    {
      err = grub_multiboot_load_elf (&mld);
      if (err == GRUB_ERR_NONE) {
	return GRUB_ERR_NONE;
      }
      if (err == GRUB_ERR_UNKNOWN_OS && (header->flags & MULTIBOOT_AOUT_KLUDGE))
	grub_errno = err = GRUB_ERR_NONE;
    }
  if (header->flags & MULTIBOOT_AOUT_KLUDGE)
    {
      int offset = ((char *) header - buffer -
		    (header->header_addr - header->load_addr));
      int load_size = ((header->load_end_addr == 0) ? file->size - offset :
		       header->load_end_addr - header->load_addr);
      grub_size_t code_size;
      void *source;
      grub_relocator_chunk_t ch;

      if (header->bss_end_addr)
	code_size = (header->bss_end_addr - header->load_addr);
      else
	code_size = load_size;

      err = grub_relocator_alloc_chunk_addr (grub_multiboot_relocator, 
					     &ch, header->load_addr,
					     code_size);
      if (err)
	{
	  grub_dprintf ("multiboot_loader", "Error loading aout kludge\n");
	  return err;
	}
      source = get_virtual_current_address (ch);

      if ((grub_file_seek (file, offset)) == (grub_off_t) -1)
	{
	  return grub_errno;
	}

      grub_file_read (file, source, load_size);
      if (grub_errno)
	return grub_errno;

      if (header->bss_end_addr)
	grub_memset ((grub_uint8_t *) source + load_size, 0,
		     header->bss_end_addr - header->load_addr - load_size);

      grub_multiboot_payload_eip = header->entry_addr;
      return GRUB_ERR_NONE;
    }

  return grub_multiboot_load_elf (&mld);
}

static struct multiboot_header *
find_header (char *buffer, grub_ssize_t len)
{
  struct multiboot_header *header;

  /* Look for the multiboot header in the buffer.  The header should
     be at least 12 bytes and aligned on a 4-byte boundary.  */
  for (header = (struct multiboot_header *) buffer;
       ((char *) header <= buffer + len - 12);
       header = (struct multiboot_header *) ((char *) header + MULTIBOOT_HEADER_ALIGN))
    {
      if (header->magic == MULTIBOOT_HEADER_MAGIC
	  && !(header->magic + header->flags + header->checksum))
	return header;
    }
  return NULL;
}

grub_err_t
grub_multiboot_load (grub_file_t file, const char *filename)
{
  char *buffer;
  grub_ssize_t len;
  struct multiboot_header *header;
  grub_err_t err;

  buffer = grub_malloc (MULTIBOOT_SEARCH);
  if (!buffer)
    return grub_errno;

  len = grub_file_read (file, buffer, MULTIBOOT_SEARCH);
  if (len < 32)
    {
      grub_free (buffer);
      if (!grub_errno)
	grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		    filename);
      return grub_errno;
    }

  header = find_header (buffer, len);

  if (header == 0)
    {
      grub_free (buffer);
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "no multiboot header found");
    }

  if (header->flags & UNSUPPORTED_FLAGS)
    {
      grub_free (buffer);
      return grub_error (GRUB_ERR_UNKNOWN_OS,
			 "unsupported flag: 0x%x", header->flags);
    }

  err = load_kernel (file, filename, buffer, header);
  if (err)
    {
      grub_free (buffer);
      return err;
    }

  if (header->flags & MULTIBOOT_VIDEO_MODE)
    {
      switch (header->mode_type)
	{
	case 1:
	  err = grub_multiboot_set_console (GRUB_MULTIBOOT_CONSOLE_EGA_TEXT, 
					    GRUB_MULTIBOOT_CONSOLE_EGA_TEXT
					    | GRUB_MULTIBOOT_CONSOLE_FRAMEBUFFER,
					    0, 0, 0, 0);
	  break;
	case 0:
	  err = grub_multiboot_set_console (GRUB_MULTIBOOT_CONSOLE_FRAMEBUFFER,
					    GRUB_MULTIBOOT_CONSOLE_EGA_TEXT
					    | GRUB_MULTIBOOT_CONSOLE_FRAMEBUFFER,
					    header->width, header->height,
					    header->depth, 0);
	  break;
	default:
	  err = grub_error (GRUB_ERR_BAD_OS, 
			    "unsupported graphical mode type %d",
			    header->mode_type);
	  break;
	}
    }
  else
    err = grub_multiboot_set_console (GRUB_MULTIBOOT_CONSOLE_EGA_TEXT, 
				      GRUB_MULTIBOOT_CONSOLE_EGA_TEXT,
				      0, 0, 0, 0);
  return err;
}

#if GRUB_MACHINE_HAS_VBE || GRUB_MACHINE_HAS_VGA_TEXT
#include <grub/i386/pc/vbe.h>
#endif

static grub_size_t
grub_multiboot_get_mbi_size (void)
{
  grub_size_t ret;
  struct grub_net_network_level_interface *net;

  ret = sizeof (struct multiboot_info) + ALIGN_UP (cmdline_size, 4)
    + modcnt * sizeof (struct multiboot_mod_list) + total_modcmd
    + ALIGN_UP (sizeof(PACKAGE_STRING), 4) 
    + grub_multiboot_get_mmap_count () * sizeof (struct multiboot_mmap_entry)
    + elf_sec_entsize * elf_sec_num
    + 256 * sizeof (struct multiboot_color)
#if GRUB_MACHINE_HAS_VBE || GRUB_MACHINE_HAS_VGA_TEXT
    + sizeof (struct grub_vbe_info_block)
    + sizeof (struct grub_vbe_mode_info_block)
#endif
    + ALIGN_UP (sizeof (struct multiboot_apm_info), 4);

  FOR_NET_NETWORK_LEVEL_INTERFACES(net)
    if (net->dhcp_ack)
      {
	ret += net->dhcp_acklen;
	break;
      }

  return ret;
}

/* Helper for grub_fill_multiboot_mmap.  */
static int
grub_fill_multiboot_mmap_iter (grub_uint64_t addr, grub_uint64_t size,
			       grub_memory_type_t type, void *data)
{
  struct multiboot_mmap_entry **mmap_entry = data;

  (*mmap_entry)->addr = addr;
  (*mmap_entry)->len = size;
  (*mmap_entry)->type = type;
  (*mmap_entry)->size = sizeof (struct multiboot_mmap_entry) - sizeof ((*mmap_entry)->size);
  (*mmap_entry)++;

  return 0;
}

/* Fill previously allocated Multiboot mmap.  */
static void
grub_fill_multiboot_mmap (struct multiboot_mmap_entry *first_entry)
{
  struct multiboot_mmap_entry *mmap_entry = (struct multiboot_mmap_entry *) first_entry;

  grub_mmap_iterate (grub_fill_multiboot_mmap_iter, &mmap_entry);
}

#if GRUB_MACHINE_HAS_VBE || GRUB_MACHINE_HAS_VGA_TEXT

static grub_err_t
fill_vbe_info (struct multiboot_info *mbi, grub_uint8_t *ptrorig,
	       grub_uint32_t ptrdest, int fill_generic)
{
  grub_uint32_t vbe_mode;
  struct grub_vbe_mode_info_block *mode_info;
#if GRUB_MACHINE_HAS_VBE
  grub_vbe_status_t status;
  void *scratch = (void *) GRUB_MEMORY_MACHINE_SCRATCH_ADDR;
    
  status = grub_vbe_bios_get_controller_info (scratch);
  if (status != GRUB_VBE_STATUS_OK)
    return grub_error (GRUB_ERR_IO, "Can't get controller info.");
  
  mbi->vbe_control_info = ptrdest;
  grub_memcpy (ptrorig, scratch, sizeof (struct grub_vbe_info_block));
  ptrorig += sizeof (struct grub_vbe_info_block);
  ptrdest += sizeof (struct grub_vbe_info_block);
#else
  mbi->vbe_control_info = 0;
#endif

#if GRUB_MACHINE_HAS_VBE  
  status = grub_vbe_bios_get_mode (scratch);
  vbe_mode = *(grub_uint32_t *) scratch;
  if (status != GRUB_VBE_STATUS_OK)
    return grub_error (GRUB_ERR_IO, "can't get VBE mode");
#else
  vbe_mode = 3;
#endif
  mbi->vbe_mode = vbe_mode;

  mode_info = (struct grub_vbe_mode_info_block *) ptrorig;
  mbi->vbe_mode_info = ptrdest;
  /* get_mode_info isn't available for mode 3.  */
  if (vbe_mode == 3)
    {
      grub_memset (mode_info, 0, sizeof (struct grub_vbe_mode_info_block));
      mode_info->memory_model = GRUB_VBE_MEMORY_MODEL_TEXT;
      mode_info->x_resolution = 80;
      mode_info->y_resolution = 25;
    }
  else
    {
#if GRUB_MACHINE_HAS_VBE  
      status = grub_vbe_bios_get_mode_info (vbe_mode, scratch);
      if (status != GRUB_VBE_STATUS_OK)
	return grub_error (GRUB_ERR_IO, "can't get mode info");
      grub_memcpy (mode_info, scratch,
		   sizeof (struct grub_vbe_mode_info_block));
#endif
    }
  ptrorig += sizeof (struct grub_vbe_mode_info_block);
  ptrdest += sizeof (struct grub_vbe_mode_info_block);

#if GRUB_MACHINE_HAS_VBE        
  grub_vbe_bios_get_pm_interface (&mbi->vbe_interface_seg,
				  &mbi->vbe_interface_off,
				  &mbi->vbe_interface_len);
#endif
  
  mbi->flags |= MULTIBOOT_INFO_VBE_INFO;

  if (fill_generic && mode_info->memory_model == GRUB_VBE_MEMORY_MODEL_TEXT)
    {
      mbi->framebuffer_addr = 0xb8000;

      mbi->framebuffer_pitch = 2 * mode_info->x_resolution;	
      mbi->framebuffer_width = mode_info->x_resolution;
      mbi->framebuffer_height = mode_info->y_resolution;

      mbi->framebuffer_bpp = 16;

      mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT;

      mbi->flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;
    }

  return GRUB_ERR_NONE;
}
#endif

static grub_err_t
retrieve_video_parameters (struct multiboot_info *mbi,
			   grub_uint8_t *ptrorig, grub_uint32_t ptrdest)
{
  grub_err_t err;
  struct grub_video_mode_info mode_info;
  void *framebuffer;
  grub_video_driver_id_t driv_id;
  struct grub_video_palette_data palette[256];

  err = grub_multiboot_set_video_mode ();
  if (err)
    {
      grub_print_error ();
      grub_errno = GRUB_ERR_NONE;
    }

  grub_video_get_palette (0, ARRAY_SIZE (palette), palette);

  driv_id = grub_video_get_driver_id ();
#if GRUB_MACHINE_HAS_VGA_TEXT
  if (driv_id == GRUB_VIDEO_DRIVER_NONE)
    return fill_vbe_info (mbi, ptrorig, ptrdest, 1);
#else
  if (driv_id == GRUB_VIDEO_DRIVER_NONE)
    return GRUB_ERR_NONE;
#endif

  err = grub_video_get_info_and_fini (&mode_info, &framebuffer);
  if (err)
    return err;

  mbi->framebuffer_addr = (grub_addr_t) framebuffer;
  mbi->framebuffer_pitch = mode_info.pitch;

  mbi->framebuffer_width = mode_info.width;
  mbi->framebuffer_height = mode_info.height;

  mbi->framebuffer_bpp = mode_info.bpp;
      
  if (mode_info.mode_type & GRUB_VIDEO_MODE_TYPE_INDEX_COLOR)
    {
      struct multiboot_color *mb_palette;
      unsigned i;
      mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED;
      mbi->framebuffer_palette_addr = ptrdest;
      mbi->framebuffer_palette_num_colors = mode_info.number_of_colors;
      if (mbi->framebuffer_palette_num_colors > ARRAY_SIZE (palette))
	mbi->framebuffer_palette_num_colors = ARRAY_SIZE (palette);
      mb_palette = (struct multiboot_color *) ptrorig;
      for (i = 0; i < mbi->framebuffer_palette_num_colors; i++)
	{
	  mb_palette[i].red = palette[i].r;
	  mb_palette[i].green = palette[i].g;
	  mb_palette[i].blue = palette[i].b;
	}
      ptrorig += mbi->framebuffer_palette_num_colors
	* sizeof (struct multiboot_color);
      ptrdest += mbi->framebuffer_palette_num_colors
	* sizeof (struct multiboot_color);
    }
  else
    {
      mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
      mbi->framebuffer_red_field_position = mode_info.red_field_pos;
      mbi->framebuffer_red_mask_size = mode_info.red_mask_size;
      mbi->framebuffer_green_field_position = mode_info.green_field_pos;
      mbi->framebuffer_green_mask_size = mode_info.green_mask_size;
      mbi->framebuffer_blue_field_position = mode_info.blue_field_pos;
      mbi->framebuffer_blue_mask_size = mode_info.blue_mask_size;
    }

  mbi->flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;

#if GRUB_MACHINE_HAS_VBE
  if (driv_id == GRUB_VIDEO_DRIVER_VBE)
    return fill_vbe_info (mbi, ptrorig, ptrdest, 0);
#endif

  return GRUB_ERR_NONE;
}

grub_err_t
grub_multiboot_make_mbi (grub_uint32_t *target)
{
  struct multiboot_info *mbi;
  struct multiboot_mod_list *modlist;
  unsigned i;
  struct module *cur;
  grub_size_t mmap_size;
  grub_uint8_t *ptrorig; 
  grub_addr_t ptrdest;

  grub_err_t err;
  grub_size_t bufsize;
  grub_relocator_chunk_t ch;

  bufsize = grub_multiboot_get_mbi_size ();

  err = grub_relocator_alloc_chunk_align_safe (grub_multiboot_relocator, &ch,
					       0x10000, 0xa0000, bufsize, 4,
					       GRUB_RELOCATOR_PREFERENCE_NONE, 0);
  if (err)
    return err;
  ptrorig = get_virtual_current_address (ch);
  ptrdest = get_physical_target_address (ch);

  *target = ptrdest;

  mbi = (struct multiboot_info *) ptrorig;
  ptrorig += sizeof (*mbi);
  ptrdest += sizeof (*mbi);
  grub_memset (mbi, 0, sizeof (*mbi));

  grub_memcpy (ptrorig, cmdline, cmdline_size);
  mbi->flags |= MULTIBOOT_INFO_CMDLINE;
  mbi->cmdline = ptrdest;
  ptrorig += ALIGN_UP (cmdline_size, 4);
  ptrdest += ALIGN_UP (cmdline_size, 4);

  grub_memcpy (ptrorig, PACKAGE_STRING, sizeof(PACKAGE_STRING));
  mbi->flags |= MULTIBOOT_INFO_BOOT_LOADER_NAME;
  mbi->boot_loader_name = ptrdest;
  ptrorig += ALIGN_UP (sizeof(PACKAGE_STRING), 4);
  ptrdest += ALIGN_UP (sizeof(PACKAGE_STRING), 4);

#ifdef GRUB_MACHINE_PCBIOS
  {
    struct grub_apm_info info;
    if (grub_apm_get_info (&info))
      {
	struct multiboot_apm_info *mbinfo = (void *) ptrorig;

	mbinfo->cseg = info.cseg;
	mbinfo->offset = info.offset;
	mbinfo->cseg_16 = info.cseg_16;
	mbinfo->dseg = info.dseg;
	mbinfo->flags = info.flags;
	mbinfo->cseg_len = info.cseg_len;
	mbinfo->dseg_len = info.dseg_len;
	mbinfo->cseg_16_len = info.cseg_16_len;
	mbinfo->version = info.version;

	ptrorig += ALIGN_UP (sizeof (struct multiboot_apm_info), 4);
	ptrdest += ALIGN_UP (sizeof (struct multiboot_apm_info), 4);
      }
  }
#endif

  if (modcnt)
    {
      mbi->flags |= MULTIBOOT_INFO_MODS;
      mbi->mods_addr = ptrdest;
      mbi->mods_count = modcnt;
      modlist = (struct multiboot_mod_list *) ptrorig;
      ptrorig += modcnt * sizeof (struct multiboot_mod_list);
      ptrdest += modcnt * sizeof (struct multiboot_mod_list);

      for (i = 0, cur = modules; i < modcnt; i++, cur = cur->next)
	{
	  modlist[i].mod_start = cur->start;
	  modlist[i].mod_end = modlist[i].mod_start + cur->size;
	  modlist[i].cmdline = ptrdest;
	  grub_memcpy (ptrorig, cur->cmdline, cur->cmdline_size);
	  ptrorig += ALIGN_UP (cur->cmdline_size, 4);
	  ptrdest += ALIGN_UP (cur->cmdline_size, 4);
	}
    }
  else
    {
      mbi->mods_addr = 0;
      mbi->mods_count = 0;
    }

  mmap_size = grub_multiboot_get_mmap_count () 
    * sizeof (struct multiboot_mmap_entry);
  grub_fill_multiboot_mmap ((struct multiboot_mmap_entry *) ptrorig);
  mbi->mmap_length = mmap_size;
  mbi->mmap_addr = ptrdest;
  mbi->flags |= MULTIBOOT_INFO_MEM_MAP;
  ptrorig += mmap_size;
  ptrdest += mmap_size;

  /* Convert from bytes to kilobytes.  */
  mbi->mem_lower = grub_mmap_get_lower () / 1024;
  mbi->mem_upper = grub_mmap_get_upper () / 1024;
  mbi->flags |= MULTIBOOT_INFO_MEMORY;

  if (bootdev_set)
    {
      mbi->boot_device = bootdev;
      mbi->flags |= MULTIBOOT_INFO_BOOTDEV;
    }

  {
    struct grub_net_network_level_interface *net;
    FOR_NET_NETWORK_LEVEL_INTERFACES(net)
      if (net->dhcp_ack)
	{
	  grub_memcpy (ptrorig, net->dhcp_ack, net->dhcp_acklen);
	  mbi->drives_addr = ptrdest;
	  mbi->drives_length = net->dhcp_acklen;
	  ptrorig += net->dhcp_acklen;
	  ptrdest += net->dhcp_acklen;
	  break;
	}
  }

  if (elf_sec_num)
    {
      mbi->u.elf_sec.addr = ptrdest;
      grub_memcpy (ptrorig, elf_sections, elf_sec_entsize * elf_sec_num);
      mbi->u.elf_sec.num = elf_sec_num;
      mbi->u.elf_sec.size = elf_sec_entsize;
      mbi->u.elf_sec.shndx = elf_sec_shstrndx;

      mbi->flags |= MULTIBOOT_INFO_ELF_SHDR;

      ptrorig += elf_sec_entsize * elf_sec_num;
      ptrdest += elf_sec_entsize * elf_sec_num;
    }

  err = retrieve_video_parameters (mbi, ptrorig, ptrdest);
  if (err)
    {
      grub_print_error ();
      grub_errno = GRUB_ERR_NONE;
    }

  if ((mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO)
      && mbi->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED)
    {
      ptrorig += mbi->framebuffer_palette_num_colors
	* sizeof (struct multiboot_color);
      ptrdest += mbi->framebuffer_palette_num_colors
	* sizeof (struct multiboot_color);
    }

#if GRUB_MACHINE_HAS_VBE
  ptrorig += sizeof (struct grub_vbe_info_block);
  ptrdest += sizeof (struct grub_vbe_info_block);
  ptrorig += sizeof (struct grub_vbe_mode_info_block);
  ptrdest += sizeof (struct grub_vbe_mode_info_block);
#endif

#ifdef GRUB_MACHINE_EFI
  if (!grub_multiboot_no_exit)
    err = grub_efi_finish_boot_services (NULL, NULL, NULL, NULL, NULL);
  if (err)
    return err;
#endif

  return GRUB_ERR_NONE;
}

void
grub_multiboot_add_elfsyms (grub_size_t num, grub_size_t entsize,
			    unsigned shndx, void *data)
{
  elf_sec_num = num;
  elf_sec_shstrndx = shndx;
  elf_sec_entsize = entsize;
  elf_sections = data;
}

void
grub_multiboot_free_mbi (void)
{
  struct module *cur, *next;

  cmdline_size = 0;
  total_modcmd = 0;
  modcnt = 0;
  grub_free (cmdline);
  cmdline = NULL;
  bootdev_set = 0;

  for (cur = modules; cur; cur = next)
    {
      next = cur->next;
      grub_free (cur->cmdline);
      grub_free (cur);
    }
  modules = NULL;
  modules_last = NULL;

  grub_free (elf_sections);
  elf_sections = NULL;
  elf_sec_entsize = 0;
  elf_sec_num = 0;
}

grub_err_t
grub_multiboot_init_mbi (int argc, char *argv[])
{
  grub_ssize_t len = 0;

  grub_multiboot_free_mbi ();

  len = grub_loader_cmdline_size (argc, argv);

  cmdline = grub_malloc (len);
  if (! cmdline)
    return grub_errno;
  cmdline_size = len;

  return grub_create_loader_cmdline (argc, argv, cmdline,
				     cmdline_size, GRUB_VERIFY_KERNEL_CMDLINE);
}

grub_err_t
grub_multiboot_add_module (grub_addr_t start, grub_size_t size,
			   int argc, char *argv[])
{
  struct module *newmod;
  grub_size_t len = 0;
  grub_err_t err;

  newmod = grub_malloc (sizeof (*newmod));
  if (!newmod)
    return grub_errno;
  newmod->start = start;
  newmod->size = size;
  newmod->next = 0;

  len = grub_loader_cmdline_size (argc, argv);

  newmod->cmdline = grub_malloc (len);
  if (! newmod->cmdline)
    {
      grub_free (newmod);
      return grub_errno;
    }
  newmod->cmdline_size = len;
  total_modcmd += ALIGN_UP (len, 4);

  err = grub_create_loader_cmdline (argc, argv, newmod->cmdline,
				    newmod->cmdline_size, GRUB_VERIFY_MODULE_CMDLINE);
  if (err)
    {
      grub_free (newmod);
      return grub_errno;
    }

  if (modules_last)
    modules_last->next = newmod;
  else
    modules = newmod;
  modules_last = newmod;

  modcnt++;

  return GRUB_ERR_NONE;
}

void
grub_multiboot_set_bootdev (void)
{
  grub_uint32_t biosdev, slice = ~0, part = ~0;
  grub_device_t dev;

#ifdef GRUB_MACHINE_PCBIOS
  biosdev = grub_get_root_biosnumber ();
#else
  biosdev = 0xffffffff;
#endif

  if (biosdev == 0xffffffff)
    return;

  dev = grub_device_open (0);
  if (dev && dev->disk && dev->disk->partition)
    {
      if (dev->disk->partition->parent)
 	{
	  part = dev->disk->partition->number;
	  slice = dev->disk->partition->parent->number;
	}
      else
	slice = dev->disk->partition->number;
    }
  if (dev)
    grub_device_close (dev);

  bootdev = ((biosdev & 0xff) << 24) | ((slice & 0xff) << 16) 
    | ((part & 0xff) << 8) | 0xff;
  bootdev_set = 1;
}
