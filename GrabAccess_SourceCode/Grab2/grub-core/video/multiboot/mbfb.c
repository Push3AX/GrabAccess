/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2006,2007,2008,2009  Free Software Foundation, Inc.
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
#include <grub/machine/console.h>
#include <grub/machine/kernel.h>
#include <multiboot.h>

static struct multiboot_info *mbi = NULL;

static struct
{
  struct grub_video_mode_info mode_info;
  grub_uint8_t *ptr;
} framebuffer;

static grub_err_t
grub_video_mbfb_init (void)
{
  grub_memset (&framebuffer, 0, sizeof(framebuffer));

  return grub_video_fb_init ();
}

static grub_err_t
grub_video_mbfb_fill_mode_info (struct grub_video_mode_info *out)
{
  grub_memset (out, 0, sizeof (*out));

  out->width = mbi->framebuffer_width;
  out->height = mbi->framebuffer_height;
  out->pitch = mbi->framebuffer_pitch;
  out->bpp = mbi->framebuffer_bpp;
  out->bytes_per_pixel = out->bpp >> 3;
  if (mbi->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED)
  {
    out->mode_type = GRUB_VIDEO_MODE_TYPE_INDEX_COLOR;
#if 0
    out->red_field_pos = 0;
    out->red_mask_size = 8;
    out->green_field_pos = 8;
    out->green_mask_size = 8;
    out->blue_field_pos = 16;
    out->blue_mask_size = 8;
#endif
    out->number_of_colors = mbi->framebuffer_palette_num_colors;
  }
  else if (mbi->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB)
  {
    out->mode_type = GRUB_VIDEO_MODE_TYPE_RGB;
    out->red_field_pos = mbi->framebuffer_red_field_position;
    out->red_mask_size = mbi->framebuffer_red_mask_size;
    out->green_field_pos = mbi->framebuffer_green_field_position;
    out->green_mask_size = mbi->framebuffer_green_mask_size;
    out->blue_field_pos = mbi->framebuffer_blue_field_position;
    out->blue_mask_size = mbi->framebuffer_blue_mask_size;
    out->number_of_colors = 256;
  }

  out->blit_format = grub_video_get_blit_format (out);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_mbfb_setup (unsigned int width, unsigned int height,
                       unsigned int mode_type __attribute__ ((unused)),
                       unsigned int mode_mask __attribute__ ((unused)))
{
  unsigned i;
  grub_err_t err;
  struct grub_video_palette_data palette[256];

  if (!mbi)
    return grub_error (GRUB_ERR_IO, "Couldn't find display device.");

  if (!((width == mbi->framebuffer_width && height == mbi->framebuffer_height)
      || (width == 0 && height == 0)))
    return grub_error (GRUB_ERR_IO, "can't set mode %dx%d", width, height);

  err = grub_video_mbfb_fill_mode_info (&framebuffer.mode_info);
  if (err)
  {
    grub_dprintf ("video", "MBFB: couldn't fill mode info\n");
    return err;
  }

  framebuffer.ptr = (void *) (grub_addr_t) mbi->framebuffer_addr;

  grub_dprintf ("video", "MBFB: initialising FB @ %p %dx%dx%d\n",
                framebuffer.ptr, framebuffer.mode_info.width,
                framebuffer.mode_info.height, framebuffer.mode_info.bpp);

  err = grub_video_fb_setup (mode_type, mode_mask,
                             &framebuffer.mode_info,
                             framebuffer.ptr, NULL, NULL);
  if (err)
    return err;

  if (mbi->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED)
  {
    struct multiboot_color *mb_palette =
            (void *)(grub_addr_t) mbi->framebuffer_palette_addr;
    for (i = 0; i < mbi->framebuffer_palette_num_colors; i++)
    {
      palette[i].r = mb_palette[i].red;
      palette[i].g = mb_palette[i].green;
      palette[i].b = mb_palette[i].blue;
      palette[i].a = 255;
    }
    grub_video_fb_set_palette (0, framebuffer.mode_info.number_of_colors,
                               palette);
  }
  else
    grub_video_fb_set_palette (0, GRUB_VIDEO_FBSTD_NUMCOLORS,
                               grub_video_fbstd_colors);
  return err;
}

static grub_err_t
grub_video_mbfb_get_info_and_fini (struct grub_video_mode_info *mode_info,
                                   void **framebuf)
{
  grub_memcpy (mode_info, &(framebuffer.mode_info), sizeof (*mode_info));
  *framebuf = (char *) framebuffer.ptr;

  grub_video_fb_fini ();

  return GRUB_ERR_NONE;
}

static struct grub_video_adapter grub_video_mbfb_adapter =
{
  .name = "Multiboot video driver",

  .prio = GRUB_VIDEO_ADAPTER_PRIO_FIRMWARE_DIRTY,
  .id = GRUB_VIDEO_DRIVER_MULTIBOOT,

  .init = grub_video_mbfb_init,
  .fini = grub_video_fb_fini,
  .setup = grub_video_mbfb_setup,
  .get_info = grub_video_fb_get_info,
  .get_info_and_fini = grub_video_mbfb_get_info_and_fini,
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
  .swap_buffers = grub_video_fb_swap_buffers,
  .create_render_target = grub_video_fb_create_render_target,
  .delete_render_target = grub_video_fb_delete_render_target,
  .set_active_render_target = grub_video_fb_set_active_render_target,
  .get_active_render_target = grub_video_fb_get_active_render_target,

  .next = 0
};

void
grub_video_multiboot_fb_init (void)
{
  if ((grub_multiboot_info->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO)
      && grub_multiboot_info->framebuffer_type
         != MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT)
  {
    mbi = grub_multiboot_info;
    grub_video_register (&grub_video_mbfb_adapter);
  }
}

void
grub_video_multiboot_fb_fini (void)
{
  if (mbi)
    grub_video_unregister (&grub_video_mbfb_adapter);
}
