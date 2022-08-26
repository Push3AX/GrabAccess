/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2003,2004,2005,2006,2007,2008,2009,2013  Free Software Foundation, Inc.
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

#include <grub/kernel.h>
#include <grub/mm.h>
#include <grub/machine/time.h>
#include <grub/machine/memory.h>
#include <grub/machine/console.h>
#include <grub/machine/kernel.h>
#include <grub/offsets.h>
#include <grub/types.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/loader.h>
#include <grub/env.h>
#include <grub/cache.h>
#include <grub/time.h>
#include <grub/symbol.h>
#include <grub/cpu/io.h>
#include <grub/cpu/floppy.h>
#include <grub/cpu/tsc.h>
#include <grub/video.h>
#include <grub/acpi.h>
#include <multiboot.h>
#include <multiboot2.h>

extern grub_uint8_t _start[];
extern grub_uint8_t _end[];
extern grub_uint8_t _edata[];

grub_uint32_t grub_boot_device = 0;

/* MBI2 data */
static char mbi2_cmdline[1024];
static char mbi2_bootloader[64];
multiboot_memory_map_t mbi2_mmap[256];
static struct multiboot2_color mbi2_palette[256];
static struct mbi2_extra_info mbi2;
/* The MBI has to be copied to our BSS so that it won't be
   overwritten.  This is its final location.  */
static struct multiboot_info mbi;
void *kern_multiboot_info = 0;
grub_uint32_t kern_multiboot_magic = 0;

struct multiboot_info *grub_multiboot_info = 0;
struct mbi2_extra_info *grub_multiboot2_info = 0;

void  __attribute__ ((noreturn))
grub_exit (int rc __attribute__((unused)))
{
  /* We can't use grub_fatal() in this function.  This would create an infinite
     loop, since grub_fatal() calls grub_abort() which in turn calls grub_exit().  */
  while (1)
    grub_cpu_idle ();
}

grub_addr_t grub_modbase = GRUB_KERNEL_I386_COREBOOT_MODULES_ADDR;
static grub_uint64_t modend;
static int have_memory = 0;

/* Helper for grub_machine_init.  */
static int
heap_init (grub_uint64_t addr, grub_uint64_t size, grub_memory_type_t type,
           void *data __attribute__ ((unused)))
{
  grub_uint64_t begin = addr, end = addr + size;

#if GRUB_CPU_SIZEOF_VOID_P == 4
  /* Restrict ourselves to 32-bit memory space.  */
  if (begin > GRUB_ULONG_MAX)
    return 0;
  if (end > GRUB_ULONG_MAX)
    end = GRUB_ULONG_MAX;
#endif

  if (type != GRUB_MEMORY_AVAILABLE)
    return 0;

  /* Avoid the lower memory.  */
  if (begin < GRUB_MEMORY_MACHINE_LOWER_SIZE)
    begin = GRUB_MEMORY_MACHINE_LOWER_SIZE;

  if (modend && begin < modend)
    begin = modend;

  if (end <= begin)
    return 0;

  grub_mm_init_region ((void *) (grub_addr_t) begin, (grub_size_t) (end - begin));

  have_memory = 1;

  return 0;
}

/* Move MBI to a safe place. */
static void
fill_mb_info (void)
{
  grub_uint32_t len = sizeof (struct multiboot_info);
  if (!kern_multiboot_info)
    grub_fatal ("Unable to find Multiboot Information");
  if (kern_multiboot_magic == MULTIBOOT2_BOOTLOADER_MAGIC)
  {
    struct multiboot2_tag *tag;
    grub_multiboot2_info = &mbi2;
    grub_memset (&mbi2, 0, sizeof (mbi2));
    grub_memset (&mbi, 0, len);
    for (tag = (struct multiboot2_tag *) ((grub_uint8_t *) kern_multiboot_info + 8);
         tag->type != MULTIBOOT2_TAG_TYPE_END;
         tag = (struct multiboot2_tag *)
               ((multiboot_uint8_t *) tag + ((tag->size + 7) & ~7)))
    {
      switch (tag->type)
      {
        case MULTIBOOT2_TAG_TYPE_CMDLINE:
          mbi.flags |= MULTIBOOT_INFO_CMDLINE;
          grub_snprintf (mbi2_cmdline, 1024, "%s",
                  ((struct multiboot2_tag_string *) tag)->string);
          mbi.cmdline = (grub_addr_t) mbi2_cmdline;
          break;
        case MULTIBOOT2_TAG_TYPE_BOOT_LOADER_NAME:
          mbi.flags |= MULTIBOOT_INFO_BOOT_LOADER_NAME;
          grub_snprintf (mbi2_bootloader, 64, "%s",
                  ((struct multiboot2_tag_string *) tag)->string);
          mbi.boot_loader_name = (grub_addr_t) mbi2_bootloader;
          break;
        case MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO:
          mbi.flags |= MULTIBOOT_INFO_MEMORY;
          mbi.mem_lower = ((struct multiboot2_tag_basic_meminfo *) tag)->mem_lower;
          mbi.mem_upper = ((struct multiboot2_tag_basic_meminfo *) tag)->mem_upper;
          break;
        case MULTIBOOT2_TAG_TYPE_BOOTDEV:
          mbi.flags |= MULTIBOOT_INFO_BOOTDEV;
          mbi.boot_device = ((struct multiboot2_tag_bootdev *) tag)->biosdev;
          break;
        case MULTIBOOT2_TAG_TYPE_MMAP:
          {
            grub_uint32_t i;
            multiboot2_memory_map_t *mmap =
                    ((struct multiboot2_tag_mmap *) tag)->entries;
            for (i = 0; (i < 256) &&
                 ((grub_uint8_t *) mmap < (grub_uint8_t *) tag + tag->size);
                 i++, mmap = (multiboot2_memory_map_t *) ((grub_addr_t) mmap
                    + ((struct multiboot2_tag_mmap *) tag)->entry_size))
            {
              mbi2_mmap[i].size = 20;
              mbi2_mmap[i].addr = mmap->addr;
              mbi2_mmap[i].len = mmap->len;
              mbi2_mmap[i].type = mmap->type;
            }
            mbi.flags |= MULTIBOOT_INFO_MEM_MAP;
            mbi.mmap_addr = (grub_addr_t) mbi2_mmap;
            mbi.mmap_length = i * sizeof (multiboot_memory_map_t);
          }
          break;
        case MULTIBOOT2_TAG_TYPE_VBE:
          mbi.flags |= MULTIBOOT_INFO_VBE_INFO;
          break;
        case MULTIBOOT2_TAG_TYPE_FRAMEBUFFER:
          {
            struct multiboot2_tag_framebuffer *fb = (void *) tag;
            mbi.flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;
            mbi.framebuffer_addr = fb->common.framebuffer_addr;
            mbi.framebuffer_pitch = fb->common.framebuffer_pitch;
            mbi.framebuffer_width = fb->common.framebuffer_width;
            mbi.framebuffer_height = fb->common.framebuffer_height;
            mbi.framebuffer_bpp = fb->common.framebuffer_bpp;
            mbi.framebuffer_type = fb->common.framebuffer_type;
            switch (fb->common.framebuffer_type)
            {
              case MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED:
                {
                  grub_uint32_t num = fb->framebuffer_palette_num_colors;
                  if (num > 256)
                    num = 256;
                  mbi.framebuffer_palette_num_colors = num;
                  grub_memmove (mbi2_palette, fb->framebuffer_palette,
                                num * sizeof (struct multiboot2_color));
                  mbi.framebuffer_palette_addr = (grub_addr_t) mbi2_palette;
                }
                break;
              case MULTIBOOT_FRAMEBUFFER_TYPE_RGB:
                mbi.framebuffer_red_field_position =
                        fb->framebuffer_red_field_position;
                mbi.framebuffer_green_field_position =
                        fb->framebuffer_green_field_position;
                mbi.framebuffer_blue_field_position =
                        fb->framebuffer_blue_field_position;
                mbi.framebuffer_red_mask_size =
                        fb->framebuffer_red_mask_size;
                mbi.framebuffer_green_mask_size =
                        fb->framebuffer_green_mask_size;
                mbi.framebuffer_blue_mask_size =
                        fb->framebuffer_blue_mask_size;
                break;
              default:
                break;
            }
          }
          break;
        case MULTIBOOT2_TAG_TYPE_EFI32:
          mbi2.systab32 = ((struct multiboot2_tag_efi32 *)tag)->pointer;
          break;
        case MULTIBOOT2_TAG_TYPE_EFI64:
          mbi2.systab64 = ((struct multiboot2_tag_efi64 *)tag)->pointer;
          break;
        case MULTIBOOT2_TAG_TYPE_EFI32_IH:
          mbi2.ih32 = ((struct multiboot2_tag_efi32_ih *)tag)->pointer;
          break;
        case MULTIBOOT2_TAG_TYPE_EFI64_IH:
          mbi2.ih64 = ((struct multiboot2_tag_efi64_ih *)tag)->pointer;
          break;
        case MULTIBOOT2_TAG_TYPE_EFI_BS:
          mbi2.efibs = 1;
          break;
        case MULTIBOOT2_TAG_TYPE_ACPI_OLD:
          grub_memmove (&mbi2.acpi1, ((struct multiboot2_tag_old_acpi *)tag)->rsdp,
                        sizeof (struct grub_acpi_rsdp_v10));
          break;
        case MULTIBOOT2_TAG_TYPE_ACPI_NEW:
          grub_memmove (&mbi2.acpi2, ((struct multiboot2_tag_new_acpi *)tag)->rsdp,
                        sizeof (struct grub_acpi_rsdp_v20));
          break;
        case MULTIBOOT2_TAG_TYPE_SMBIOS:
          if (((struct multiboot2_tag_smbios *)tag)->major == 3)
            grub_memmove (&mbi2.eps3, ((struct multiboot2_tag_smbios *)tag)->tables,
                        sizeof (struct grub_smbios_eps3));
          else if (((struct multiboot2_tag_smbios *)tag)->major < 3)
            grub_memmove (&mbi2.eps, ((struct multiboot2_tag_smbios *)tag)->tables,
                        sizeof (struct grub_smbios_eps));
          break;
        default:
          break;
      }
    }
  }
  else if (kern_multiboot_magic == MULTIBOOT_BOOTLOADER_MAGIC)
  {
    grub_memmove (&mbi, kern_multiboot_info, len);
    if ((mbi.flags & MULTIBOOT_INFO_MEM_MAP) == 0)
      grub_fatal ("Missing Multiboot memory information");
    /* Move the memory map to a safe place.  */
    grub_uint32_t i;
    grub_uint8_t *mmap = (void *)(grub_addr_t) mbi.mmap_addr;
    for (i = 0; i < 256 && (grub_addr_t) mmap < mbi.mmap_addr + mbi.mmap_length;
         i++, mmap += ((multiboot_memory_map_t *)mmap)->size + 4)
    {
      mbi2_mmap[i].size = 20;
      mbi2_mmap[i].addr = ((multiboot_memory_map_t *)mmap)->addr;
      mbi2_mmap[i].len = ((multiboot_memory_map_t *)mmap)->len;
      mbi2_mmap[i].type = ((multiboot_memory_map_t *)mmap)->type;
    }
    mbi.mmap_addr = (grub_addr_t) mbi2_mmap;
    mbi.mmap_length = i * sizeof (multiboot_memory_map_t);
  }
  else
    grub_fatal ("Bad Multiboot magic");
  grub_multiboot_info = &mbi;
}

extern grub_uint16_t grub_bios_via_workaround1, grub_bios_via_workaround2;

/* Via needs additional wbinvd.  */
static void
grub_via_workaround_init (void)
{
  grub_uint32_t manufacturer[3], max_cpuid;
  if (! grub_cpu_is_cpuid_supported ())
    return;

  grub_cpuid (0, max_cpuid, manufacturer[0], manufacturer[2], manufacturer[1]);

  if (grub_memcmp (manufacturer, "CentaurHauls", 12) != 0)
    return;

  grub_bios_via_workaround1 = 0x090f;
  grub_bios_via_workaround2 = 0x090f;
  asm volatile ("wbinvd");
}

void
grub_machine_init (void)
{
  modend = grub_modules_get_end ();

  /* This has to happen before any BIOS calls. */
  grub_via_workaround_init ();

  grub_console_pcbios_init ();
  grub_vga_text_init ();

  fill_mb_info ();

  grub_machine_mmap_init ();
  grub_machine_mmap_iterate (heap_init, NULL);

  grub_video_multiboot_fb_init ();

  grub_font_init ();

  grub_gfxterm_init ();

  grub_tsc_init ();

  if (grub_mb_check_bios_int (0x13) &&
      (grub_multiboot_info->flags & MULTIBOOT_INFO_BOOTDEV))
    grub_boot_device = grub_multiboot_info->boot_device;
}

void
grub_machine_get_bootlocation (char **device,
                               char **path __attribute__ ((unused)))
{
  char *ptr;
  grub_uint8_t boot_drive, dos_part, bsd_part;

  if (!grub_mb_check_bios_int (0x13))
    return;

  boot_drive = (grub_boot_device >> 24);
  dos_part = (grub_boot_device >> 16);
  bsd_part = (grub_boot_device >> 8);

  /* XXX: This should be enough.  */
#define DEV_SIZE 100
  *device = grub_malloc (DEV_SIZE);
  ptr = *device;
  grub_snprintf (*device, DEV_SIZE, "%cd%u",
                 (boot_drive & 0x80) ? 'h' : 'f', boot_drive & 0x7f);
  ptr += grub_strlen (ptr);

  if (dos_part != 0xff)
    grub_snprintf (ptr, DEV_SIZE - (ptr - *device), ",%u", dos_part + 1);
  ptr += grub_strlen (ptr);

  if (bsd_part != 0xff)
    grub_snprintf (ptr, DEV_SIZE - (ptr - *device), ",%u", bsd_part + 1);
  ptr += grub_strlen (ptr);
  *ptr = 0;
}

void
grub_machine_fini (int flags)
{
  if (flags & GRUB_LOADER_FLAG_NORETURN)
    grub_vga_text_fini ();
  grub_video_multiboot_fb_fini ();
  grub_stop_floppy ();
  grub_console_pcbios_fini ();
}
