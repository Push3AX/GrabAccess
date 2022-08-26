/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2000,2001,2002,2003,2004,2005,2007,2008,2009,2010  Free Software Foundation, Inc.
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

#include <grub/i386/pc/int.h>
#include <grub/machine/console.h>
#include <grub/cpu/io.h>
#include <grub/mm.h>
#include <grub/video.h>
#include <grub/video_fb.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/vga.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define VGA_WIDTH	640
#define VGA_MEM		((grub_uint8_t *) 0xa0000)
#define PAGE_OFFSET(x)	((x) * (VGA_WIDTH * vga_height / 8))

static unsigned char text_mode;
static unsigned char saved_map_mask;
static int vga_height;

static struct
{
  struct grub_video_mode_info mode_info;
  struct grub_video_render_target *render_target;
  grub_uint8_t *temporary_buffer;
  int front_page;
  int back_page;
} framebuffer;

static unsigned char 
grub_vga_set_mode (unsigned char mode)
{
  struct grub_bios_int_registers regs;
  unsigned char ret;
  /* get current mode */
  regs.eax = 0x0f00;
  regs.ebx = 0;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x10, &regs);

  ret = regs.eax & 0xff;
  regs.eax = mode;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x10, &regs);

  return ret;
}

static inline void
wait_vretrace (void)
{
  /* Wait until there is a vertical retrace.  */
  while (! (grub_inb (GRUB_VGA_IO_INPUT_STATUS1_REGISTER)
	    & GRUB_VGA_IO_INPUT_STATUS1_VERTR_BIT));
}

/* Get Map Mask Register.  */
static unsigned char
get_map_mask (void)
{
  return grub_vga_sr_read (GRUB_VGA_SR_MAP_MASK_REGISTER);
}

/* Set Map Mask Register.  */
static void
set_map_mask (unsigned char mask)
{
  grub_vga_sr_write (mask, GRUB_VGA_SR_MAP_MASK_REGISTER);
}

#if 0
/* Set Read Map Register.  */
static void
set_read_map (unsigned char map)
{
  grub_vga_gr_write (map, GRUB_VGA_GR_READ_MAP_REGISTER);
}
#endif

/* Set start address.  */
static void
set_start_address (unsigned int start)
{
  grub_vga_cr_write (start & 0xFF, GRUB_VGA_CR_START_ADDR_LOW_REGISTER);
  grub_vga_cr_write (start >> 8, GRUB_VGA_CR_START_ADDR_HIGH_REGISTER);
}

static int setup = 0;
static int is_target = 0;

static grub_err_t
grub_video_vga_init (void)
{
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_vga_setup (unsigned int width, unsigned int height,
                      grub_video_mode_type_t mode_type,
		      grub_video_mode_type_t mode_mask)
{
  grub_err_t err;

  if ((width && width != VGA_WIDTH) || (height && height != 350 && height != 480))
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "no matching mode found");

  vga_height = height ? : 480;

  framebuffer.temporary_buffer = grub_calloc (vga_height, VGA_WIDTH);
  framebuffer.front_page = 0;
  framebuffer.back_page = 0;
  if (!framebuffer.temporary_buffer)
    return grub_errno;

  saved_map_mask = get_map_mask ();

  text_mode = grub_vga_set_mode (vga_height == 480 ? 0x12 : 0x10);
  setup = 1;
  set_map_mask (0x0f);
  set_start_address (PAGE_OFFSET (framebuffer.front_page));

  framebuffer.mode_info.width = VGA_WIDTH;
  framebuffer.mode_info.height = vga_height;

  framebuffer.mode_info.mode_type = GRUB_VIDEO_MODE_TYPE_INDEX_COLOR;

  if (grub_video_check_mode_flag (mode_type, mode_mask,
				  GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED,
				  (VGA_WIDTH * vga_height <= (1 << 18))))
    {
      framebuffer.back_page = 1;
      framebuffer.mode_info.mode_type |= GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED
	| GRUB_VIDEO_MODE_TYPE_UPDATING_SWAP;
    }

  framebuffer.mode_info.bpp = 8;
  framebuffer.mode_info.bytes_per_pixel = 1;
  framebuffer.mode_info.pitch = VGA_WIDTH;
  framebuffer.mode_info.number_of_colors = 16;
  framebuffer.mode_info.red_mask_size = 0;
  framebuffer.mode_info.red_field_pos = 0;
  framebuffer.mode_info.green_mask_size = 0;
  framebuffer.mode_info.green_field_pos = 0;
  framebuffer.mode_info.blue_mask_size = 0;
  framebuffer.mode_info.blue_field_pos = 0;
  framebuffer.mode_info.reserved_mask_size = 0;
  framebuffer.mode_info.reserved_field_pos = 0;

  framebuffer.mode_info.blit_format
    = grub_video_get_blit_format (&framebuffer.mode_info);

  err = grub_video_fb_create_render_target_from_pointer (&framebuffer.render_target,
							 &framebuffer.mode_info,
							 framebuffer.temporary_buffer);

  if (err)
    {
      grub_dprintf ("video", "Couldn't create FB target\n");
      return err;
    }

  is_target = 1;
  err = grub_video_fb_set_active_render_target (framebuffer.render_target);
 
  if (err)
    return err;
 
  err = grub_video_fb_set_palette (0, GRUB_VIDEO_FBSTD_NUMCOLORS,
				   grub_video_fbstd_colors);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_vga_fini (void)
{
  if (setup)
    {
      set_map_mask (saved_map_mask);
      grub_vga_set_mode (text_mode);
    }
  setup = 0;
  grub_free (framebuffer.temporary_buffer);
  framebuffer.temporary_buffer = 0;
  return GRUB_ERR_NONE;
}

static inline void
update_target (void)
{
  int plane;

  if (!is_target)
    return;

  for (plane = 0x01; plane <= 0x08; plane <<= 1)
    {
      grub_uint8_t *ptr;
      volatile grub_uint8_t *ptr2;
      unsigned cbyte = 0;
      int shift = 7;
      set_map_mask (plane);
      for (ptr = framebuffer.temporary_buffer,
	     ptr2 = VGA_MEM + PAGE_OFFSET (framebuffer.back_page);
	   ptr < framebuffer.temporary_buffer + VGA_WIDTH * vga_height; ptr++)
	{
	  cbyte |= (!!(plane & *ptr)) << shift;
	  shift--;
	  if (shift == -1)
	    {
	      *ptr2++ = cbyte;
	      shift = 7;
	      cbyte = 0;
	    }
	}
    }
}

static grub_err_t
grub_video_vga_blit_bitmap (struct grub_video_bitmap *bitmap,
			    enum grub_video_blit_operators oper, int x, int y,
			    int offset_x, int offset_y,
			    unsigned int width, unsigned int height)
{
  grub_err_t ret;
  ret = grub_video_fb_blit_bitmap (bitmap, oper, x, y, offset_x, offset_y,
				   width, height);
  if (!(framebuffer.mode_info.mode_type & GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED))
    update_target ();
  return ret;
}

static grub_err_t
grub_video_vga_blit_render_target (struct grub_video_fbrender_target *source,
                                   enum grub_video_blit_operators oper,
                                   int x, int y, int offset_x, int offset_y,
                                   unsigned int width, unsigned int height)
{
  grub_err_t ret;

  ret = grub_video_fb_blit_render_target (source, oper, x, y,
					  offset_x, offset_y, width, height);
  if (!(framebuffer.mode_info.mode_type & GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED))
    update_target ();

  return ret;
}

static grub_err_t
grub_video_vga_set_active_render_target (struct grub_video_render_target *target)
{
  if (target == GRUB_VIDEO_RENDER_TARGET_DISPLAY)
    {
      is_target = 1;
      target = framebuffer.render_target;
    }
  else
    is_target = 0;

  return grub_video_fb_set_active_render_target (target);
}

static grub_err_t
grub_video_vga_get_active_render_target (struct grub_video_render_target **target)
{
  grub_err_t err;
  err = grub_video_fb_get_active_render_target (target);
  if (err)
    return err;

  if (*target == framebuffer.render_target)
    *target = GRUB_VIDEO_RENDER_TARGET_DISPLAY;

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_vga_swap_buffers (void)
{
  if (!(framebuffer.mode_info.mode_type & GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED))
    return GRUB_ERR_NONE;

  update_target ();

  if ((VGA_WIDTH * vga_height <= (1 << 18)))
    {
      /* Activate the other page.  */
      framebuffer.front_page = !framebuffer.front_page;
      framebuffer.back_page = !framebuffer.back_page;
      wait_vretrace ();
      set_start_address (PAGE_OFFSET (framebuffer.front_page));
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_vga_set_palette (unsigned int start __attribute__ ((unused)),
			    unsigned int count __attribute__ ((unused)),
                            struct grub_video_palette_data *palette_data __attribute__ ((unused)))
{
  return grub_error (GRUB_ERR_IO, "can't change palette");
}

static grub_err_t
grub_video_vga_get_info_and_fini (struct grub_video_mode_info *mode_info,
				  void **framebuf)
{
  set_map_mask (0xf);

  grub_memcpy (mode_info, &(framebuffer.mode_info), sizeof (*mode_info));
  mode_info->bpp = 1;
  mode_info->bytes_per_pixel = 0;
  mode_info->pitch = VGA_WIDTH / 8;
  mode_info->number_of_colors = 1;

  mode_info->bg_red = 0;
  mode_info->bg_green = 0;
  mode_info->bg_blue = 0;
  mode_info->bg_alpha = 255;

  mode_info->fg_red = 255;
  mode_info->fg_green = 255;
  mode_info->fg_blue = 255;
  mode_info->fg_alpha = 255;

  *framebuf = VGA_MEM + PAGE_OFFSET (framebuffer.front_page);

  grub_video_fb_fini ();
  grub_free (framebuffer.temporary_buffer);
  framebuffer.temporary_buffer = 0;
  setup = 0;

  return GRUB_ERR_NONE;
}


static struct grub_video_adapter grub_video_vga_adapter =
  {
    .name = "VGA Video Driver",
    .id = GRUB_VIDEO_DRIVER_VGA,

    .prio = GRUB_VIDEO_ADAPTER_PRIO_FALLBACK,

    .init = grub_video_vga_init,
    .fini = grub_video_vga_fini,
    .setup = grub_video_vga_setup,
    .get_info = grub_video_fb_get_info,
    .get_info_and_fini = grub_video_vga_get_info_and_fini,
    .set_palette = grub_video_vga_set_palette,
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
    .blit_bitmap = grub_video_vga_blit_bitmap,
    .blit_render_target = grub_video_vga_blit_render_target,
    .scroll = grub_video_fb_scroll,
    .swap_buffers = grub_video_vga_swap_buffers,
    .create_render_target = grub_video_fb_create_render_target,
    .delete_render_target = grub_video_fb_delete_render_target,
    .set_active_render_target = grub_video_vga_set_active_render_target,
    .get_active_render_target = grub_video_vga_get_active_render_target,

    .next = 0
  };

GRUB_MOD_INIT(vga)
{
  grub_video_register (&grub_video_vga_adapter);
}

GRUB_MOD_FINI(vga)
{
  grub_video_unregister (&grub_video_vga_adapter);
}
