/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2006,2007,2008,2009,2010  Free Software Foundation, Inc.
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

#define grub_video_render_target grub_video_fbrender_target

#include <grub/err.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/video.h>
#include <grub/video_fb.h>
#include <grub/pci.h>
#include <grub/vga.h>
#include <grub/cache.h>

#define GRUB_SIS315PRO_PCIID 0x03251039
#define GRUB_SIS315PRO_TOTAL_MEMORY_SPACE  0x800000
#define GRUB_SIS315PRO_MMIO_SPACE 0x1000

static struct
{
  struct grub_video_mode_info mode_info;

  grub_uint8_t *ptr;
  volatile grub_uint8_t *direct_ptr;
  int mapped;
  grub_uint32_t base;
  grub_uint32_t mmiobase;
  volatile grub_uint32_t *mmioptr;
  grub_pci_device_t dev;
  grub_port_t io;
} framebuffer;

static grub_uint8_t
read_sis_cmd (grub_uint8_t addr)
{
  grub_outb (addr, framebuffer.io + 0x44);
  return grub_inb (framebuffer.io + 0x45);
}

static void
write_sis_cmd (grub_uint8_t val, grub_uint8_t addr)
{
  grub_outb (addr, framebuffer.io + 0x44);
  grub_outb (val, framebuffer.io + 0x45);
}

#ifndef TEST
static grub_err_t
grub_video_sis315pro_video_init (void)
{
  /* Reset frame buffer.  */
  grub_memset (&framebuffer, 0, sizeof(framebuffer));

  return grub_video_fb_init ();
}

static grub_err_t
grub_video_sis315pro_video_fini (void)
{
  if (framebuffer.mapped)
    {
      grub_pci_device_unmap_range (framebuffer.dev, framebuffer.ptr,
				   GRUB_SIS315PRO_TOTAL_MEMORY_SPACE);
      grub_pci_device_unmap_range (framebuffer.dev, framebuffer.direct_ptr,
				   GRUB_SIS315PRO_TOTAL_MEMORY_SPACE);
    }

  return grub_video_fb_fini ();
}
#endif

#include "sis315_init.c"

#ifndef TEST
/* Helper for grub_video_sis315pro_setup.  */
static int
find_card (grub_pci_device_t dev, grub_pci_id_t pciid, void *data)
{
  int *found = data;
  grub_pci_address_t addr;
  grub_uint32_t class;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_CLASS);
  class = grub_pci_read (addr);

  if (((class >> 16) & 0xffff) != GRUB_PCI_CLASS_SUBCLASS_VGA
      || pciid != GRUB_SIS315PRO_PCIID)
    return 0;
  
  *found = 1;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_ADDRESS_REG0);
  framebuffer.base = grub_pci_read (addr) & GRUB_PCI_ADDR_MEM_MASK;
  addr = grub_pci_make_address (dev, GRUB_PCI_REG_ADDRESS_REG1);
  framebuffer.mmiobase = grub_pci_read (addr) & GRUB_PCI_ADDR_MEM_MASK;
  addr = grub_pci_make_address (dev, GRUB_PCI_REG_ADDRESS_REG2);
  framebuffer.io = (grub_pci_read (addr) & GRUB_PCI_ADDR_IO_MASK)
    + GRUB_MACHINE_PCI_IO_BASE;
  framebuffer.dev = dev;

  return 1;
}
#endif

static grub_err_t
grub_video_sis315pro_setup (unsigned int width, unsigned int height,
			    unsigned int mode_type,
			    unsigned int mode_mask __attribute__ ((unused)))
{
  int depth;
  grub_err_t err;
  int found = 0;
  unsigned i;

#ifndef TEST
  /* Decode depth from mode_type.  If it is zero, then autodetect.  */
  depth = (mode_type & GRUB_VIDEO_MODE_TYPE_DEPTH_MASK)
          >> GRUB_VIDEO_MODE_TYPE_DEPTH_POS;

  if ((width != 640 && width != 0) || (height != 480 && height != 0)
      || (depth != 8 && depth != 0))
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		       "Only 640x480x8 is supported");

  grub_pci_iterate (find_card, &found);
  if (!found)
    return grub_error (GRUB_ERR_IO, "Couldn't find graphics card");
#endif
  /* Fill mode info details.  */
  framebuffer.mode_info.width = 640;
  framebuffer.mode_info.height = 480;
  framebuffer.mode_info.mode_type = (GRUB_VIDEO_MODE_TYPE_INDEX_COLOR
				     | GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED
				     | GRUB_VIDEO_MODE_TYPE_UPDATING_SWAP);
  framebuffer.mode_info.bpp = 8;
  framebuffer.mode_info.bytes_per_pixel = 1;
  framebuffer.mode_info.pitch = 640 * 1;
  framebuffer.mode_info.number_of_colors = 16;
  framebuffer.mode_info.red_mask_size = 0;
  framebuffer.mode_info.red_field_pos = 0;
  framebuffer.mode_info.green_mask_size = 0;
  framebuffer.mode_info.green_field_pos = 0;
  framebuffer.mode_info.blue_mask_size = 0;
  framebuffer.mode_info.blue_field_pos = 0;
  framebuffer.mode_info.reserved_mask_size = 0;
  framebuffer.mode_info.reserved_field_pos = 0;
#ifndef TEST
  framebuffer.mode_info.blit_format
    = grub_video_get_blit_format (&framebuffer.mode_info);
#endif

#ifndef TEST
  if (found && (framebuffer.base == 0 || framebuffer.mmiobase == 0))
    {
      grub_pci_address_t addr;
      /* FIXME: choose address dynamically if needed.   */
      framebuffer.base =     0x40000000;
      framebuffer.mmiobase = 0x04000000;
      framebuffer.io = 0xb300;

      addr = grub_pci_make_address (framebuffer.dev, GRUB_PCI_REG_ADDRESS_REG0);
      grub_pci_write (addr, framebuffer.base | GRUB_PCI_ADDR_MEM_PREFETCH);

      addr = grub_pci_make_address (framebuffer.dev, GRUB_PCI_REG_ADDRESS_REG1);
      grub_pci_write (addr, framebuffer.mmiobase);

      addr = grub_pci_make_address (framebuffer.dev, GRUB_PCI_REG_ADDRESS_REG2);
      grub_pci_write (addr, framebuffer.io | GRUB_PCI_ADDR_SPACE_IO);

      /* Set latency.  */
      addr = grub_pci_make_address (framebuffer.dev, GRUB_PCI_REG_CACHELINE);
      grub_pci_write (addr, 0x80004700);

      /* Enable address spaces.  */
      addr = grub_pci_make_address (framebuffer.dev, GRUB_PCI_REG_COMMAND);
      grub_pci_write (addr, 0x7);

      addr = grub_pci_make_address (framebuffer.dev, 0x30);
      grub_pci_write (addr, 0x04060001);

      framebuffer.io += GRUB_MACHINE_PCI_IO_BASE;
    }
#endif


  /* We can safely discard volatile attribute.  */
#ifndef TEST
  framebuffer.ptr
    = grub_pci_device_map_range_cached (framebuffer.dev,
					framebuffer.base,
					GRUB_SIS315PRO_TOTAL_MEMORY_SPACE);
  framebuffer.direct_ptr
    = grub_pci_device_map_range (framebuffer.dev,
				 framebuffer.base,
				 GRUB_SIS315PRO_TOTAL_MEMORY_SPACE);
  framebuffer.mmioptr = grub_pci_device_map_range (framebuffer.dev,
						   framebuffer.mmiobase,
						   GRUB_SIS315PRO_MMIO_SPACE);
#endif
  framebuffer.mapped = 1;

#ifndef TEST
  /* Prevent garbage from appearing on the screen.  */
  grub_memset (framebuffer.ptr, 0, 
	       framebuffer.mode_info.height * framebuffer.mode_info.pitch);
  grub_arch_sync_dma_caches (framebuffer.ptr,
			     framebuffer.mode_info.height
			     * framebuffer.mode_info.pitch);
#endif

  grub_outb (GRUB_VGA_IO_MISC_NEGATIVE_VERT_POLARITY
	     | GRUB_VGA_IO_MISC_NEGATIVE_HORIZ_POLARITY
	     | GRUB_VGA_IO_MISC_UPPER_64K
	     | GRUB_VGA_IO_MISC_EXTERNAL_CLOCK_0
	     | GRUB_VGA_IO_MISC_28MHZ
	     | GRUB_VGA_IO_MISC_ENABLE_VRAM_ACCESS
	     | GRUB_VGA_IO_MISC_COLOR, 
	     GRUB_VGA_IO_MISC_WRITE + GRUB_MACHINE_PCI_IO_BASE);

  grub_vga_sr_write (0x86, 5);
  for (i = 6; i <= 0x27; i++)
    grub_vga_sr_write (0, i);

  for (i = 0x31; i <= 0x3d; i++)
    grub_vga_sr_write (0, i);

  for (i = 0; i < ARRAY_SIZE (sr_dump); i++)
    grub_vga_sr_write (sr_dump[i].val, sr_dump[i].reg);

  for (i = 0x30; i < 0x40; i++)
    grub_vga_cr_write (0, i);

  grub_vga_cr_write (0x77, 0x40);
  grub_vga_cr_write (0x77, 0x41);
  grub_vga_cr_write (0x00, 0x42);
  grub_vga_cr_write (0x5b, 0x43);
  grub_vga_cr_write (0x00, 0x44);
  grub_vga_cr_write (0x23, 0x48);
  grub_vga_cr_write (0xaa, 0x49);
  grub_vga_cr_write (0x02, 0x37);
  grub_vga_cr_write (0x20, 0x5b);
  grub_vga_cr_write (0x00, 0x83);
  grub_vga_cr_write (0x80, 0x63);

  grub_vga_cr_write (0x0c, GRUB_VGA_CR_VSYNC_END);
  grub_vga_cr_write (0x5f, GRUB_VGA_CR_HTOTAL);
  grub_vga_cr_write (0x4f, GRUB_VGA_CR_HORIZ_END);
  grub_vga_cr_write (0x50, GRUB_VGA_CR_HBLANK_START);
  grub_vga_cr_write (0x82, GRUB_VGA_CR_HBLANK_END);
  grub_vga_cr_write (0x54, GRUB_VGA_CR_HORIZ_SYNC_PULSE_START);
  grub_vga_cr_write (0x80, GRUB_VGA_CR_HORIZ_SYNC_PULSE_END);
  grub_vga_cr_write (0x0b, GRUB_VGA_CR_VERT_TOTAL);
  grub_vga_cr_write (0x3e, GRUB_VGA_CR_OVERFLOW);
  grub_vga_cr_write (0x00, GRUB_VGA_CR_BYTE_PANNING);
  grub_vga_cr_write (0x40, GRUB_VGA_CR_CELL_HEIGHT);
  grub_vga_cr_write (0x00, GRUB_VGA_CR_CURSOR_START);
  grub_vga_cr_write (0x00, GRUB_VGA_CR_CURSOR_END);
  grub_vga_cr_write (0x00, GRUB_VGA_CR_START_ADDR_HIGH_REGISTER);
  grub_vga_cr_write (0x00, GRUB_VGA_CR_START_ADDR_LOW_REGISTER);
  grub_vga_cr_write (0x00, GRUB_VGA_CR_CURSOR_ADDR_HIGH);
  grub_vga_cr_write (0x00, GRUB_VGA_CR_CURSOR_ADDR_LOW);
  grub_vga_cr_write (0xea, GRUB_VGA_CR_VSYNC_START);
  grub_vga_cr_write (0x8c, GRUB_VGA_CR_VSYNC_END);
  grub_vga_cr_write (0xdf, GRUB_VGA_CR_VDISPLAY_END);
  grub_vga_cr_write (0x28, GRUB_VGA_CR_PITCH);
  grub_vga_cr_write (0x40, GRUB_VGA_CR_UNDERLINE_LOCATION);
  grub_vga_cr_write (0xe7, GRUB_VGA_CR_VERTICAL_BLANK_START);
  grub_vga_cr_write (0x04, GRUB_VGA_CR_VERTICAL_BLANK_END);
  grub_vga_cr_write (0xa3, GRUB_VGA_CR_MODE);
  grub_vga_cr_write (0xff, GRUB_VGA_CR_LINE_COMPARE);

  grub_vga_cr_write (0x0c, GRUB_VGA_CR_VSYNC_END);
  grub_vga_cr_write (0x5f, GRUB_VGA_CR_HTOTAL);
  grub_vga_cr_write (0x4f, GRUB_VGA_CR_HORIZ_END);
  grub_vga_cr_write (0x50, GRUB_VGA_CR_HBLANK_START);
  grub_vga_cr_write (0x82, GRUB_VGA_CR_HBLANK_END);
  grub_vga_cr_write (0x55, GRUB_VGA_CR_HORIZ_SYNC_PULSE_START);
  grub_vga_cr_write (0x81, GRUB_VGA_CR_HORIZ_SYNC_PULSE_END);
  grub_vga_cr_write (0x0b, GRUB_VGA_CR_VERT_TOTAL);
  grub_vga_cr_write (0x3e, GRUB_VGA_CR_OVERFLOW);
  grub_vga_cr_write (0xe9, GRUB_VGA_CR_VSYNC_START);
  grub_vga_cr_write (0x8b, GRUB_VGA_CR_VSYNC_END);
  grub_vga_cr_write (0xdf, GRUB_VGA_CR_VDISPLAY_END);
  grub_vga_cr_write (0xe7, GRUB_VGA_CR_VERTICAL_BLANK_START);
  grub_vga_cr_write (0x04, GRUB_VGA_CR_VERTICAL_BLANK_END);
  grub_vga_cr_write (0x40, GRUB_VGA_CR_CELL_HEIGHT);
  grub_vga_cr_write (0x50, GRUB_VGA_CR_PITCH);

  grub_vga_cr_write (0x00, 0x19);
  grub_vga_cr_write (0x00, 0x1a);
  grub_vga_cr_write (0x6c, 0x52);
  grub_vga_cr_write (0x2e, 0x34);
  grub_vga_cr_write (0x00, 0x31);


  grub_vga_cr_write (0, GRUB_VGA_CR_START_ADDR_HIGH_REGISTER);
  grub_vga_cr_write (0, GRUB_VGA_CR_START_ADDR_LOW_REGISTER);

  for (i = 0; i < 16; i++)
    grub_vga_write_arx (i, i);
  grub_vga_write_arx (1, GRUB_VGA_ARX_MODE);
  grub_vga_write_arx (0, GRUB_VGA_ARX_OVERSCAN);
  grub_vga_write_arx (0, GRUB_VGA_ARX_COLOR_PLANE_ENABLE);
  grub_vga_write_arx (0, GRUB_VGA_ARX_HORIZONTAL_PANNING);
  grub_vga_write_arx (0, GRUB_VGA_ARX_COLOR_SELECT);

  grub_outb (0xff, GRUB_VGA_IO_PIXEL_MASK + GRUB_MACHINE_PCI_IO_BASE);

  for (i = 0; i < ARRAY_SIZE (gr); i++)
    grub_vga_gr_write (gr[i], i);

  for (i = 0; i < GRUB_VIDEO_FBSTD_NUMCOLORS; i++)
    grub_vga_palette_write (i, grub_video_fbstd_colors[i].r,
			    grub_video_fbstd_colors[i].g,
			    grub_video_fbstd_colors[i].b);

#if 1
  {
    if (read_sis_cmd (0x5) != 0xa1)
      write_sis_cmd (0x86, 0x5);
    
    write_sis_cmd (read_sis_cmd (0x20) | 0xa1, 0x20);
    write_sis_cmd (read_sis_cmd (0x1e) | 0xda, 0x1e);

#define IND_SIS_CMDQUEUE_SET            0x26
#define IND_SIS_CMDQUEUE_THRESHOLD      0x27

#define COMMAND_QUEUE_THRESHOLD 0x1F
#define SIS_CMD_QUEUE_RESET             0x01

#define SIS_AGP_CMDQUEUE_ENABLE         0x80  /* 315/330/340 series SR26 */
#define SIS_VRAM_CMDQUEUE_ENABLE        0x40
#define SIS_MMIO_CMD_ENABLE             0x20
#define SIS_CMD_QUEUE_SIZE_512k         0x00
#define SIS_CMD_QUEUE_SIZE_1M           0x04
#define SIS_CMD_QUEUE_SIZE_2M           0x08
#define SIS_CMD_QUEUE_SIZE_4M           0x0C
#define SIS_CMD_QUEUE_RESET             0x01
#define SIS_CMD_AUTO_CORR               0x02


    write_sis_cmd (COMMAND_QUEUE_THRESHOLD, IND_SIS_CMDQUEUE_THRESHOLD);
    write_sis_cmd (SIS_CMD_QUEUE_RESET, IND_SIS_CMDQUEUE_SET);
    framebuffer.mmioptr[0x85C4 / 4] = framebuffer.mmioptr[0x85C8 / 4];
    write_sis_cmd (SIS_MMIO_CMD_ENABLE | SIS_CMD_AUTO_CORR, IND_SIS_CMDQUEUE_SET);
    framebuffer.mmioptr[0x85C0 / 4] = (0x1000000 - (512 * 1024));
  }
#endif

#ifndef TEST
  err = grub_video_fb_setup (mode_type, mode_mask,
			     &framebuffer.mode_info,
			     framebuffer.ptr, NULL, NULL);
  if (err)
    return err;

  /* Copy default palette to initialize emulated palette.  */
  err = grub_video_fb_set_palette (0, GRUB_VIDEO_FBSTD_EXT_NUMCOLORS,
				   grub_video_fbstd_colors);
#endif
  return err;
}

#ifndef TEST

static grub_err_t
grub_video_sis315pro_swap_buffers (void)
{
  grub_size_t s;
  s = (framebuffer.mode_info.height
       * framebuffer.mode_info.pitch
       * framebuffer.mode_info.bytes_per_pixel);
  grub_video_fb_swap_buffers ();
  grub_arch_sync_dma_caches (framebuffer.ptr, s);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_sis315pro_get_info_and_fini (struct grub_video_mode_info *mode_info,
					void **framebuf)
{
  grub_memcpy (mode_info, &(framebuffer.mode_info), sizeof (*mode_info));
  *framebuf = (void *) framebuffer.direct_ptr;

  grub_video_fb_fini ();

  return GRUB_ERR_NONE;
}

static struct grub_video_adapter grub_video_sis315pro_adapter =
  {
    .name = "SIS315PRO Video Driver",
    .id = GRUB_VIDEO_DRIVER_SIS315PRO,

    .prio = GRUB_VIDEO_ADAPTER_PRIO_NATIVE,

    .init = grub_video_sis315pro_video_init,
    .fini = grub_video_sis315pro_video_fini,
    .setup = grub_video_sis315pro_setup,
    .get_info = grub_video_fb_get_info,
    .get_info_and_fini = grub_video_sis315pro_get_info_and_fini,
    .set_palette = grub_video_fb_set_palette,
    .get_palette = grub_video_fb_get_palette,
    .set_viewport = grub_video_fb_set_viewport,
    .get_viewport = grub_video_fb_get_viewport,
    .set_region = grub_video_fb_set_region,
    .get_region = grub_video_fb_get_region,
    .set_area_status = grub_video_fb_set_area_status,
    .get_area_status = grub_video_fb_get_area_status,
    .map_color = grub_video_fb_map_color,
    .map_rgb = grub_video_fb_map_rgb,
    .map_rgba = grub_video_fb_map_rgba,
    .unmap_color = grub_video_fb_unmap_color,
    .fill_rect = grub_video_fb_fill_rect,
    .blit_bitmap = grub_video_fb_blit_bitmap,
    .blit_render_target = grub_video_fb_blit_render_target,
    .scroll = grub_video_fb_scroll,
    .swap_buffers = grub_video_sis315pro_swap_buffers,
    .create_render_target = grub_video_fb_create_render_target,
    .delete_render_target = grub_video_fb_delete_render_target,
    .set_active_render_target = grub_video_fb_set_active_render_target,
    .get_active_render_target = grub_video_fb_get_active_render_target,

    .next = 0
  };

GRUB_MOD_INIT(video_sis315pro)
{
  grub_video_register (&grub_video_sis315pro_adapter);
}

GRUB_MOD_FINI(video_sis315pro)
{
  grub_video_unregister (&grub_video_sis315pro_adapter);
}
#else
int
main ()
{
  grub_video_sis315pro_setup (640, 400, 0, 0);
}
#endif
