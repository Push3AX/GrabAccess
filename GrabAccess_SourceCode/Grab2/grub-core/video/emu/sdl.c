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
#include <SDL/SDL.h>

GRUB_MOD_LICENSE ("GPLv3+");

static SDL_Surface *window = 0;
static struct grub_video_render_target *sdl_render_target;
static struct grub_video_mode_info mode_info;

static grub_err_t
grub_video_sdl_set_palette (unsigned int start, unsigned int count,
                            struct grub_video_palette_data *palette_data);

static grub_err_t
grub_video_sdl_init (void)
{
  window = 0;

  if (SDL_Init (SDL_INIT_VIDEO) < 0)
    return grub_error (GRUB_ERR_BAD_DEVICE, "Couldn't init SDL: %s",
		       SDL_GetError ());

  grub_memset (&mode_info, 0, sizeof (mode_info));

  return grub_video_fb_init ();
}

static grub_err_t
grub_video_sdl_fini (void)
{
  SDL_Quit ();
  window = 0;

  grub_memset (&mode_info, 0, sizeof (mode_info));

  return grub_video_fb_fini ();
}

static inline unsigned int
get_mask_size (grub_uint32_t mask)
{
  unsigned i;
  for (i = 0; mask > 1U << i; i++);
  return i;
}

static grub_err_t
grub_video_sdl_setup (unsigned int width, unsigned int height,
                      unsigned int mode_type, unsigned int mode_mask)
{
  int depth;
  int flags = 0;
  grub_err_t err;

  /* Decode depth from mode_type.  If it is zero, then autodetect.  */
  depth = (mode_type & GRUB_VIDEO_MODE_TYPE_DEPTH_MASK)
          >> GRUB_VIDEO_MODE_TYPE_DEPTH_POS;

  if (depth == 0)
    depth = 32;

  if (width == 0 && height == 0)
    {
      width = 800;
      height = 600;
    }

  if ((mode_type & GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED)
      || !(mode_mask & GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED))
    flags |= SDL_DOUBLEBUF;

  window = SDL_SetVideoMode (width, height, depth, flags | SDL_HWSURFACE);
  if (! window)
    window = SDL_SetVideoMode (width, height, depth, flags | SDL_SWSURFACE);
  if (! window)
    return grub_error (GRUB_ERR_BAD_DEVICE, "Couldn't open window: %s",
		       SDL_GetError ());

  grub_memset (&sdl_render_target, 0, sizeof (sdl_render_target));

  mode_info.width = window->w;
  mode_info.height = window->h;
  mode_info.mode_type = 0;
  if (window->flags & SDL_DOUBLEBUF)
    mode_info.mode_type
      |= GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED;
  if (window->format->palette)
    mode_info.mode_type |= GRUB_VIDEO_MODE_TYPE_INDEX_COLOR;
  else
    mode_info.mode_type |= GRUB_VIDEO_MODE_TYPE_RGB;

  mode_info.bpp = window->format->BitsPerPixel;
  mode_info.bytes_per_pixel = window->format->BytesPerPixel;
  mode_info.pitch = window->pitch;

  /* In index color mode, number of colors.  In RGB mode this is 256.  */
  if (window->format->palette)
    mode_info.number_of_colors
      = 1 << window->format->BitsPerPixel;
  else
    mode_info.number_of_colors = 256;

  if (! window->format->palette)
    {
      mode_info.red_mask_size
	= get_mask_size (window->format->Rmask >> window->format->Rshift);
      mode_info.red_field_pos = window->format->Rshift;
      mode_info.green_mask_size
	= get_mask_size (window->format->Gmask >> window->format->Gshift);
      mode_info.green_field_pos = window->format->Gshift;
      mode_info.blue_mask_size
	= get_mask_size (window->format->Bmask >> window->format->Bshift);
      mode_info.blue_field_pos = window->format->Bshift;
      mode_info.reserved_mask_size
	= get_mask_size (window->format->Amask >> window->format->Ashift);
      mode_info.reserved_field_pos = window->format->Ashift;
      mode_info.blit_format
	= grub_video_get_blit_format (&mode_info);
    }

  err = grub_video_fb_create_render_target_from_pointer (&sdl_render_target,
							 &mode_info,
							 window->pixels);
  if (err)
    return err;

  /* Copy default palette to initialize emulated palette.  */
  grub_video_sdl_set_palette (0, GRUB_VIDEO_FBSTD_NUMCOLORS,
			      grub_video_fbstd_colors);

  /* Reset render target to SDL one.  */
  return grub_video_fb_set_active_render_target (sdl_render_target);
}

static grub_err_t
grub_video_sdl_set_palette (unsigned int start, unsigned int count,
                            struct grub_video_palette_data *palette_data)
{
  unsigned i;
  if (window->format->palette)
    {
      SDL_Color *tmp;
      if (start >= mode_info.number_of_colors)
	return GRUB_ERR_NONE;

      if (start + count > mode_info.number_of_colors)
	count = mode_info.number_of_colors - start;

      tmp = grub_calloc (count, sizeof (tmp[0]));
      for (i = 0; i < count; i++)
	{
	  tmp[i].r = palette_data[i].r;
	  tmp[i].g = palette_data[i].g;
	  tmp[i].b = palette_data[i].b;
	  tmp[i].unused = palette_data[i].a;
	}
      SDL_SetColors (window, tmp, start, count);
      grub_free (tmp);
    }

  return grub_video_fb_set_palette (start, count, palette_data);
}

static grub_err_t
grub_video_sdl_swap_buffers (void)
{
  if (SDL_Flip (window) < 0)
    return grub_error (GRUB_ERR_BAD_DEVICE, "couldn't swap buffers: %s",
		       SDL_GetError ());
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_sdl_set_active_render_target (struct grub_video_render_target *target)
{
  if (target == GRUB_VIDEO_RENDER_TARGET_DISPLAY)
    return grub_video_fb_set_active_render_target (sdl_render_target);

  return grub_video_fb_set_active_render_target (target);
}

static struct grub_video_adapter grub_video_sdl_adapter =
  {
    .name = "SDL Video Driver",
    .id = GRUB_VIDEO_DRIVER_SDL,

    .prio = GRUB_VIDEO_ADAPTER_PRIO_FIRMWARE,

    .init = grub_video_sdl_init,
    .fini = grub_video_sdl_fini,
    .setup = grub_video_sdl_setup,
    .get_info = grub_video_fb_get_info,
    .set_palette = grub_video_sdl_set_palette,
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
    .swap_buffers = grub_video_sdl_swap_buffers,
    .create_render_target = grub_video_fb_create_render_target,
    .delete_render_target = grub_video_fb_delete_render_target,
    .set_active_render_target = grub_video_sdl_set_active_render_target,
    .get_active_render_target = grub_video_fb_get_active_render_target,

    .next = 0
  };

GRUB_MOD_INIT(sdl)
{
  grub_video_register (&grub_video_sdl_adapter);
}

GRUB_MOD_FINI(sdl)
{
  grub_video_unregister (&grub_video_sdl_adapter);
}
