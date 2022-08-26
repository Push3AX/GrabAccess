/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2006,2007,2008,2009,2010,2011  Free Software Foundation, Inc.
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

#define GRUB_RADEON_FULOONG2E_TOTAL_MEMORY_SPACE 1048576

static struct
{
  struct grub_video_mode_info mode_info;
  struct grub_video_render_target *render_target;

  grub_uint8_t *ptr;
  int mapped;
  grub_uint32_t base;
  grub_pci_device_t dev;
} framebuffer;

static grub_err_t
grub_video_radeon_fuloong2e_video_init (void)
{
  /* Reset frame buffer.  */
  grub_memset (&framebuffer, 0, sizeof(framebuffer));

  return grub_video_fb_init ();
}

static grub_err_t
grub_video_radeon_fuloong2e_video_fini (void)
{
  if (framebuffer.mapped)
    grub_pci_device_unmap_range (framebuffer.dev, framebuffer.ptr,
				 GRUB_RADEON_FULOONG2E_TOTAL_MEMORY_SPACE);

  return grub_video_fb_fini ();
}

#ifndef TEST
/* Helper for grub_video_radeon_fuloong2e_setup.  */
static int
find_card (grub_pci_device_t dev, grub_pci_id_t pciid, void *data)
{
  int *found = data;
  grub_pci_address_t addr;
  grub_uint32_t class;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_CLASS);
  class = grub_pci_read (addr);

  if (((class >> 16) & 0xffff) != GRUB_PCI_CLASS_SUBCLASS_VGA
      || pciid != 0x515a1002)
    return 0;
  
  *found = 1;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_ADDRESS_REG0);
  framebuffer.base = grub_pci_read (addr);
  framebuffer.dev = dev;

  return 1;
}
#endif

static grub_err_t
grub_video_radeon_fuloong2e_setup (unsigned int width, unsigned int height,
			unsigned int mode_type, unsigned int mode_mask __attribute__ ((unused)))
{
  int depth;
  grub_err_t err;
  int found = 0;

#ifndef TEST
  /* Decode depth from mode_type.  If it is zero, then autodetect.  */
  depth = (mode_type & GRUB_VIDEO_MODE_TYPE_DEPTH_MASK)
          >> GRUB_VIDEO_MODE_TYPE_DEPTH_POS;

  if ((width != 640 && width != 0) || (height != 480 && height != 0)
      || (depth != 16 && depth != 0))
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		       "Only 640x480x16 is supported");

  grub_pci_iterate (find_card, &found);
  if (!found)
    return grub_error (GRUB_ERR_IO, "Couldn't find graphics card");
#endif
  /* Fill mode info details.  */
  framebuffer.mode_info.width = 640;
  framebuffer.mode_info.height = 480;
  framebuffer.mode_info.mode_type = GRUB_VIDEO_MODE_TYPE_RGB;
  framebuffer.mode_info.bpp = 16;
  framebuffer.mode_info.bytes_per_pixel = 2;
  framebuffer.mode_info.pitch = 640 * 2;
  framebuffer.mode_info.number_of_colors = 256;
  framebuffer.mode_info.red_mask_size = 5;
  framebuffer.mode_info.red_field_pos = 11;
  framebuffer.mode_info.green_mask_size = 6;
  framebuffer.mode_info.green_field_pos = 5;
  framebuffer.mode_info.blue_mask_size = 5;
  framebuffer.mode_info.blue_field_pos = 0;
  framebuffer.mode_info.reserved_mask_size = 0;
  framebuffer.mode_info.reserved_field_pos = 0;
#ifndef TEST
  framebuffer.mode_info.blit_format
    = grub_video_get_blit_format (&framebuffer.mode_info);
#endif

  /* We can safely discard volatile attribute.  */
#ifndef TEST
  framebuffer.ptr
    = (void *) grub_pci_device_map_range (framebuffer.dev,
					  framebuffer.base,
					  GRUB_RADEON_FULOONG2E_TOTAL_MEMORY_SPACE);
#endif
  framebuffer.mapped = 1;

  /* Prevent garbage from appearing on the screen.  */
  grub_memset (framebuffer.ptr, 0x55, 
	       framebuffer.mode_info.height * framebuffer.mode_info.pitch);

#ifndef TEST
  err = grub_video_fb_create_render_target_from_pointer (&framebuffer
							 .render_target,
							 &framebuffer.mode_info,
							 framebuffer.ptr);

  if (err)
    return err;

  err = grub_video_fb_set_active_render_target (framebuffer.render_target);
  
  if (err)
    return err;

  /* Copy default palette to initialize emulated palette.  */
  err = grub_video_fb_set_palette (0, GRUB_VIDEO_FBSTD_NUMCOLORS,
				   grub_video_fbstd_colors);
#endif
  return err;
}

static grub_err_t
grub_video_radeon_fuloong2e_swap_buffers (void)
{
  /* TODO: Implement buffer swapping.  */
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_radeon_fuloong2e_set_active_render_target (struct grub_video_render_target *target)
{
  if (target == GRUB_VIDEO_RENDER_TARGET_DISPLAY)
    target = framebuffer.render_target;

  return grub_video_fb_set_active_render_target (target);
}

static grub_err_t
grub_video_radeon_fuloong2e_get_info_and_fini (struct grub_video_mode_info *mode_info,
				    void **framebuf)
{
  grub_memcpy (mode_info, &(framebuffer.mode_info), sizeof (*mode_info));
  *framebuf = (char *) framebuffer.ptr;

  grub_video_fb_fini ();

  return GRUB_ERR_NONE;
}

static struct grub_video_adapter grub_video_radeon_fuloong2e_adapter =
  {
    .name = "RADEON RV100 QZ (Fuloong2E) Video Driver",
    .id = GRUB_VIDEO_DRIVER_RADEON_FULOONG2E,

    .prio = GRUB_VIDEO_ADAPTER_PRIO_NATIVE,

    .init = grub_video_radeon_fuloong2e_video_init,
    .fini = grub_video_radeon_fuloong2e_video_fini,
    .setup = grub_video_radeon_fuloong2e_setup,
    .get_info = grub_video_fb_get_info,
    .get_info_and_fini = grub_video_radeon_fuloong2e_get_info_and_fini,
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
    .swap_buffers = grub_video_radeon_fuloong2e_swap_buffers,
    .create_render_target = grub_video_fb_create_render_target,
    .delete_render_target = grub_video_fb_delete_render_target,
    .set_active_render_target = grub_video_radeon_fuloong2e_set_active_render_target,
    .get_active_render_target = grub_video_fb_get_active_render_target,

    .next = 0
  };

GRUB_MOD_INIT(video_radeon_fuloong2e)
{
  grub_video_register (&grub_video_radeon_fuloong2e_adapter);
}

GRUB_MOD_FINI(video_radeon_fuloong2e)
{
  grub_video_unregister (&grub_video_radeon_fuloong2e_adapter);
}
