/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008,2009  Free Software Foundation, Inc.
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

#include <grub/video.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* The list of video adapters registered to system.  */
grub_video_adapter_t grub_video_adapter_list = NULL;

/* Active video adapter.  */
grub_video_adapter_t grub_video_adapter_active;

/* Restore back to initial mode (where applicable).  */
grub_err_t
grub_video_restore (void)
{
  if (grub_video_adapter_active)
    {
      grub_video_adapter_active->fini ();
      if (grub_errno != GRUB_ERR_NONE)
        return grub_errno;

      grub_video_adapter_active = 0;
    }
  return GRUB_ERR_NONE;
}

/* Get information about active video mode.  */
grub_err_t
grub_video_get_info (struct grub_video_mode_info *mode_info)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  /* If mode_info is NULL just report that video adapter is active.  */
  if (! mode_info)
    {
      grub_errno = GRUB_ERR_NONE;
      return grub_errno;
    }

  return grub_video_adapter_active->get_info (mode_info);
}

grub_video_driver_id_t
grub_video_get_driver_id (void)
{
  if (! grub_video_adapter_active)
    return GRUB_VIDEO_DRIVER_NONE;
  return grub_video_adapter_active->id;
}

/* Get information about active video mode.  */
grub_err_t
grub_video_get_info_and_fini (struct grub_video_mode_info *mode_info,
			      void **framebuffer)
{
  grub_err_t err;

  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  err = grub_video_adapter_active->get_info_and_fini (mode_info, framebuffer);
  if (err)
    return err;

  grub_video_adapter_active = 0;
  return GRUB_ERR_NONE;
}

/* Determine optimized blitting formation for specified video mode info.  */
enum grub_video_blit_format
grub_video_get_blit_format (struct grub_video_mode_info *mode_info)
{
  /* Check if we have any known 32 bit modes.  */
  if (mode_info->bpp == 32)
    {
      if ((mode_info->red_mask_size == 8)
	  && (mode_info->red_field_pos == 16)
	  && (mode_info->green_mask_size == 8)
	  && (mode_info->green_field_pos == 8)
	  && (mode_info->blue_mask_size == 8)
	  && (mode_info->blue_field_pos == 0))
	{
	  return GRUB_VIDEO_BLIT_FORMAT_BGRA_8888;
	}
      else if ((mode_info->red_mask_size == 8)
	       && (mode_info->red_field_pos == 0)
	       && (mode_info->green_mask_size == 8)
	       && (mode_info->green_field_pos == 8)
	       && (mode_info->blue_mask_size == 8)
	       && (mode_info->blue_field_pos == 16))
	{
	  return GRUB_VIDEO_BLIT_FORMAT_RGBA_8888;
	}
    }
  /* Check if we have any known 24 bit modes.  */
  else if (mode_info->bpp == 24)
    {
      if ((mode_info->red_mask_size == 8)
	  && (mode_info->red_field_pos == 16)
	  && (mode_info->green_mask_size == 8)
	  && (mode_info->green_field_pos == 8)
	  && (mode_info->blue_mask_size == 8)
	  && (mode_info->blue_field_pos == 0))
	{
	  return GRUB_VIDEO_BLIT_FORMAT_BGR_888;
	}
      else if ((mode_info->red_mask_size == 8)
	       && (mode_info->red_field_pos == 0)
	       && (mode_info->green_mask_size == 8)
	       && (mode_info->green_field_pos == 8)
	       && (mode_info->blue_mask_size == 8)
	       && (mode_info->blue_field_pos == 16))
	{
	  return GRUB_VIDEO_BLIT_FORMAT_RGB_888;
	}
    }
  /* Check if we have any known 16 bit modes.  */
  else if (mode_info->bpp == 16)
    {
      if ((mode_info->red_mask_size == 5)
	  && (mode_info->red_field_pos == 11)
	  && (mode_info->green_mask_size == 6)
	  && (mode_info->green_field_pos == 5)
	  && (mode_info->blue_mask_size == 5)
	  && (mode_info->blue_field_pos == 0))
	{
	  return GRUB_VIDEO_BLIT_FORMAT_BGR_565;
	}
      else if ((mode_info->red_mask_size == 5)
	       && (mode_info->red_field_pos == 0)
	       && (mode_info->green_mask_size == 6)
	       && (mode_info->green_field_pos == 5)
	       && (mode_info->blue_mask_size == 5)
	       && (mode_info->blue_field_pos == 11))
	{
	  return GRUB_VIDEO_BLIT_FORMAT_RGB_565;
	}
    }
  else if (mode_info->bpp == 1)
    return GRUB_VIDEO_BLIT_FORMAT_1BIT_PACKED;

  /* Backup route.  Unknown format.  */

  /* If there are more than 8 bits per color, assume RGB(A) mode.  */
  if (mode_info->bpp > 8)
    {
      if (mode_info->reserved_mask_size > 0)
	{
	  return GRUB_VIDEO_BLIT_FORMAT_RGBA;
	}
      else
	{
	  return GRUB_VIDEO_BLIT_FORMAT_RGB;
	}
    }

  /* Assume as indexcolor mode.  */
  return GRUB_VIDEO_BLIT_FORMAT_INDEXCOLOR;
}

/* Set new indexed color palette entries.  */
grub_err_t
grub_video_set_palette (unsigned int start, unsigned int count,
                        struct grub_video_palette_data *palette_data)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->set_palette (start, count, palette_data);
}

/* Get indexed color palette entries.  */
grub_err_t
grub_video_get_palette (unsigned int start, unsigned int count,
                        struct grub_video_palette_data *palette_data)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->get_palette (start, count, palette_data);
}

/* Set viewport dimensions.  */
grub_err_t
grub_video_set_viewport (unsigned int x, unsigned int y,
                         unsigned int width, unsigned int height)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->set_viewport (x, y, width, height);
}

/* Get viewport dimensions.  */
grub_err_t
grub_video_get_viewport (unsigned int *x, unsigned int *y,
                         unsigned int *width, unsigned int *height)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->get_viewport (x, y, width, height);
}

/* Set region dimensions.  */
grub_err_t
grub_video_set_region (unsigned int x, unsigned int y,
                       unsigned int width, unsigned int height)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->set_region (x, y, width, height);
}

/* Get region dimensions.  */
grub_err_t
grub_video_get_region (unsigned int *x, unsigned int *y,
                       unsigned int *width, unsigned int *height)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->get_region (x, y, width, height);
}

/* Set status of the intersection of the viewport and the region.  */
grub_err_t
grub_video_set_area_status (grub_video_area_status_t area_status)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->set_area_status (area_status);
}

/* Get status of the intersection of the viewport and the region.  */
grub_err_t
grub_video_get_area_status (grub_video_area_status_t *area_status)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->get_area_status (area_status);
}

/* Map color name to adapter specific color.  */
grub_video_color_t
grub_video_map_color (grub_uint32_t color_name)
{
  if (! grub_video_adapter_active)
    return 0;

  return grub_video_adapter_active->map_color (color_name);
}

/* Map RGB value to adapter specific color.  */
grub_video_color_t
grub_video_map_rgb (grub_uint8_t red, grub_uint8_t green, grub_uint8_t blue)
{
  if (! grub_video_adapter_active)
    return 0;

  return grub_video_adapter_active->map_rgb (red, green, blue);
}

/* Map RGBA value to adapter specific color.  */
grub_video_color_t
grub_video_map_rgba (grub_uint8_t red, grub_uint8_t green, grub_uint8_t blue,
                     grub_uint8_t alpha)
{
  if (! grub_video_adapter_active)
    return 0;

  return grub_video_adapter_active->map_rgba (red, green, blue, alpha);
}

/* Unmap video color back to RGBA components.  */
grub_err_t
grub_video_unmap_color (grub_video_color_t color, grub_uint8_t *red,
                        grub_uint8_t *green, grub_uint8_t *blue,
                        grub_uint8_t *alpha)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->unmap_color (color,
                                                 red,
                                                 green,
                                                 blue,
                                                 alpha);
}

/* Fill rectangle using specified color.  */
grub_err_t
grub_video_fill_rect (grub_video_color_t color, int x, int y,
                      unsigned int width, unsigned int height)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->fill_rect (color, x, y, width, height);
}

/* Blit bitmap to screen.  */
grub_err_t
grub_video_blit_bitmap (struct grub_video_bitmap *bitmap,
                        enum grub_video_blit_operators oper,
                        int x, int y, int offset_x, int offset_y,
                        unsigned int width, unsigned int height)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->blit_bitmap (bitmap, oper, x, y,
                                                 offset_x, offset_y,
                                                 width, height);
}

/* Blit render target to active render target.  */
grub_err_t
grub_video_blit_render_target (struct grub_video_render_target *target,
                               enum grub_video_blit_operators oper,
                               int x, int y, int offset_x, int offset_y,
                               unsigned int width, unsigned int height)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->blit_render_target (target, oper, x, y,
                                                        offset_x, offset_y,
                                                        width, height);
}

/* Scroll viewport and fill new areas with specified color.  */
grub_err_t
grub_video_scroll (grub_video_color_t color, int dx, int dy)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->scroll (color, dx, dy);
}

/* Swap buffers (swap active render target).  */
grub_err_t
grub_video_swap_buffers (void)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->swap_buffers ();
}

/* Create new render target.  */
grub_err_t
grub_video_create_render_target (struct grub_video_render_target **result,
                                 unsigned int width, unsigned int height,
                                 unsigned int mode_type)
{
  *result = 0;
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->create_render_target (result,
                                                          width, height,
                                                          mode_type);
}

/* Delete render target.  */
grub_err_t
grub_video_delete_render_target (struct grub_video_render_target *target)
{
  if (!target)
    return GRUB_ERR_NONE;
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->delete_render_target (target);
}

/* Set active render target.  */
grub_err_t
grub_video_set_active_render_target (struct grub_video_render_target *target)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->set_active_render_target (target);
}

/* Get active render target.  */
grub_err_t
grub_video_get_active_render_target (struct grub_video_render_target **target)
{
  if (! grub_video_adapter_active)
    return grub_error (GRUB_ERR_BAD_DEVICE, "no video mode activated");

  return grub_video_adapter_active->get_active_render_target (target);
}

grub_err_t
grub_video_edid_checksum (struct grub_video_edid_info *edid_info)
{
  const char *edid_bytes = (const char *) edid_info;
  int i;
  char checksum = 0;

  /* Check EDID checksum.  */
  for (i = 0; i < 128; ++i)
    checksum += edid_bytes[i];

  if (checksum != 0)
    return grub_error (GRUB_ERR_BAD_DEVICE,
		       "invalid EDID checksum %d", checksum);

  grub_errno = GRUB_ERR_NONE;
  return grub_errno;
}

grub_err_t
grub_video_edid_preferred_mode (struct grub_video_edid_info *edid_info,
				unsigned int *width, unsigned int *height)
{
  /* Bit 1 in the Feature Support field indicates that the first
     Detailed Timing Description is the preferred timing mode.  */
  if (edid_info->version == 1 /* we don't understand later versions */
      && (edid_info->feature_support
	  & GRUB_VIDEO_EDID_FEATURE_PREFERRED_TIMING_MODE)
      && edid_info->detailed_timings[0].pixel_clock)
    {
      *width = edid_info->detailed_timings[0].horizontal_active_lo
	       | (((unsigned int)
		   (edid_info->detailed_timings[0].horizontal_hi & 0xf0))
		  << 4);
      *height = edid_info->detailed_timings[0].vertical_active_lo
		| (((unsigned int)
		    (edid_info->detailed_timings[0].vertical_hi & 0xf0))
		   << 4);
      if (*width && *height)
	return GRUB_ERR_NONE;
    }

  return grub_error (GRUB_ERR_BAD_DEVICE, "no preferred mode available");
}

/* Parse <width>x<height>[x<depth>]*/
static grub_err_t
parse_modespec (const char *current_mode, int *width, int *height, int *depth)
{
  const char *value;
  const char *param = current_mode;

  *width = *height = *depth = -1;

  if (grub_strcmp (param, "auto") == 0)
    {
      *width = *height = 0;
      return GRUB_ERR_NONE;
    }

  /* Find width value.  */
  value = param;
  param = grub_strchr(param, 'x');
  if (param == NULL)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("invalid video mode specification `%s'"),
		       current_mode);

  param++;
  
  *width = grub_strtoul (value, 0, 0);
  if (grub_errno != GRUB_ERR_NONE)
      return grub_error (GRUB_ERR_BAD_ARGUMENT,
			 N_("invalid video mode specification `%s'"),
			 current_mode);
  
  /* Find height value.  */
  value = param;
  param = grub_strchr(param, 'x');
  if (param == NULL)
    {
      *height = grub_strtoul (value, 0, 0);
      if (grub_errno != GRUB_ERR_NONE)
	return grub_error (GRUB_ERR_BAD_ARGUMENT,
			   N_("invalid video mode specification `%s'"),
			   current_mode);
    }
  else
    {
      /* We have optional color depth value.  */
      param++;
      
      *height = grub_strtoul (value, 0, 0);
      if (grub_errno != GRUB_ERR_NONE)
	return grub_error (GRUB_ERR_BAD_ARGUMENT,
			   N_("invalid video mode specification `%s'"),
			   current_mode);
      
      /* Convert color depth value.  */
      value = param;
      *depth = grub_strtoul (value, 0, 0);
      if (grub_errno != GRUB_ERR_NONE)
	return grub_error (GRUB_ERR_BAD_ARGUMENT,
			   N_("invalid video mode specification `%s'"),
			   current_mode);
    }
  return GRUB_ERR_NONE;
}

grub_err_t
grub_video_set_mode (const char *modestring,
		     unsigned int modemask,
		     unsigned int modevalue)
{
  char *tmp;
  char *next_mode;
  char *current_mode;
  char *modevar;

  if (grub_video_adapter_active && grub_video_adapter_active->id == GRUB_VIDEO_ADAPTER_CAPTURE)
    return GRUB_ERR_NONE;

  modevalue &= modemask;

  /* Take copy of env.var. as we don't want to modify that.  */
  modevar = grub_strdup (modestring);

  /* Initialize next mode.  */
  next_mode = modevar;

  if (! modevar)
    return grub_errno;

  if (grub_memcmp (next_mode, "keep", sizeof ("keep")) == 0
      || grub_memcmp (next_mode, "keep,", sizeof ("keep,") - 1) == 0
      || grub_memcmp (next_mode, "keep;", sizeof ("keep;") - 1) == 0)
    {
      int suitable = 1;
      grub_err_t err;

      if (grub_video_adapter_active)
	{
	  struct grub_video_mode_info mode_info;
	  grub_memset (&mode_info, 0, sizeof (mode_info));
	  err = grub_video_get_info (&mode_info);
	  if (err)
	    {
	      suitable = 0;
	      grub_errno = GRUB_ERR_NONE;
	    }
	  if ((mode_info.mode_type & modemask) != modevalue)
	    suitable = 0;
	}
      else if (((GRUB_VIDEO_MODE_TYPE_PURE_TEXT & modemask) != 0)
	       && ((GRUB_VIDEO_MODE_TYPE_PURE_TEXT & modevalue) == 0))
	suitable = 0;

      if (suitable)
	{
	  grub_free (modevar);
	  return GRUB_ERR_NONE;
	}
      next_mode += sizeof ("keep") - 1;
      if (! *next_mode)
	{
	  grub_free (modevar);

	  /* TRANSLATORS: This doesn't imply that there is no available video
	     mode at all. All modes may have been filtered out by some criteria.
	   */
	  return grub_error (GRUB_ERR_BAD_ARGUMENT,
			     N_("no suitable video mode found"));
	}

      /* Skip separator. */
      next_mode++;
    }

  /* De-activate last set video adapter.  */
  if (grub_video_adapter_active)
    {
      /* Finalize adapter.  */
      grub_video_adapter_active->fini ();
      if (grub_errno != GRUB_ERR_NONE)
	grub_errno = GRUB_ERR_NONE;

      /* Mark active adapter as not set.  */
      grub_video_adapter_active = 0;
    }

  /* Loop until all modes has been tested out.  */
  while (next_mode != NULL)
    {
      int width = -1;
      int height = -1;
      int depth = -1;
      grub_err_t err;
      unsigned int flags = modevalue;
      unsigned int flagmask = modemask;

      /* Use last next_mode as current mode.  */
      tmp = next_mode;

      /* Save position of next mode and separate modes.  */
      for (; *next_mode; next_mode++)
	if (*next_mode == ',' || *next_mode == ';')
	  break;
      if (*next_mode)
	{
	  *next_mode = 0;
	  next_mode++;
	}
      else
	next_mode = 0;

      /* Skip whitespace.  */
      while (grub_isspace (*tmp))
	tmp++;

      /* Initialize token holders.  */
      current_mode = tmp;

      /* XXX: we assume that we're in pure text mode if
	 no video mode is initialized. Is it always true? */
      if (grub_strcmp (current_mode, "text") == 0)
	{
	  struct grub_video_mode_info mode_info;

	  grub_memset (&mode_info, 0, sizeof (mode_info));
	  if (((GRUB_VIDEO_MODE_TYPE_PURE_TEXT & modemask) == 0)
	      || ((GRUB_VIDEO_MODE_TYPE_PURE_TEXT & modevalue) != 0))
	    {
	      /* Valid mode found from adapter, and it has been activated.
		 Specify it as active adapter.  */
	      grub_video_adapter_active = NULL;

	      /* Free memory.  */
	      grub_free (modevar);

	      return GRUB_ERR_NONE;
	    }
	  else
	    continue;
	}

      err = parse_modespec (current_mode, &width, &height, &depth);
      if (err)
	{
	  /* Free memory before returning.  */
	  grub_free (modevar);

	  return err;
	}

      /* Try out video mode.  */

      /* If user requested specific depth check if this depth is supported.  */
      if (depth != -1 && (flagmask & GRUB_VIDEO_MODE_TYPE_DEPTH_MASK)
	  &&
	  (((flags & GRUB_VIDEO_MODE_TYPE_DEPTH_MASK)
	    != ((depth << GRUB_VIDEO_MODE_TYPE_DEPTH_POS)
		& GRUB_VIDEO_MODE_TYPE_DEPTH_MASK))))
	continue;

      if (depth != -1)
	{
	  flags |= (depth << GRUB_VIDEO_MODE_TYPE_DEPTH_POS)
	    & GRUB_VIDEO_MODE_TYPE_DEPTH_MASK;
	  flagmask |= GRUB_VIDEO_MODE_TYPE_DEPTH_MASK;
	}

      /* Try to initialize requested mode.  Ignore any errors.  */
      grub_video_adapter_t p;

      /* Loop thru all possible video adapter trying to find requested mode.  */
      for (p = grub_video_adapter_list; p; p = p->next)
	{
	  struct grub_video_mode_info mode_info;

	  grub_memset (&mode_info, 0, sizeof (mode_info));

	  /* Try to initialize adapter, if it fails, skip to next adapter.  */
	  err = p->init ();
	  if (err != GRUB_ERR_NONE)
	    {
	      grub_errno = GRUB_ERR_NONE;
	      continue;
	    }

	  /* Try to initialize video mode.  */
	  err = p->setup (width, height, flags, flagmask);
	  if (err != GRUB_ERR_NONE)
	    {
	      p->fini ();
	      grub_errno = GRUB_ERR_NONE;
	      continue;
	    }

	  err = p->get_info (&mode_info);
	  if (err != GRUB_ERR_NONE)
	    {
	      p->fini ();
	      grub_errno = GRUB_ERR_NONE;
	      continue;
	    }

	  flags = mode_info.mode_type & ~GRUB_VIDEO_MODE_TYPE_DEPTH_MASK;
	  flags |= (mode_info.bpp << GRUB_VIDEO_MODE_TYPE_DEPTH_POS)
	    & GRUB_VIDEO_MODE_TYPE_DEPTH_MASK;

	  /* Check that mode is suitable for upper layer.  */
	  if ((flags & GRUB_VIDEO_MODE_TYPE_PURE_TEXT)
	      ? (((GRUB_VIDEO_MODE_TYPE_PURE_TEXT & modemask) != 0)
		 && ((GRUB_VIDEO_MODE_TYPE_PURE_TEXT & modevalue) == 0))
	      : ((flags & modemask) != modevalue))
	    {
	      p->fini ();
	      grub_errno = GRUB_ERR_NONE;
	      continue;
	    }

	  /* Valid mode found from adapter, and it has been activated.
	     Specify it as active adapter.  */
	  grub_video_adapter_active = p;

	  /* Free memory.  */
	  grub_free (modevar);

	  return GRUB_ERR_NONE;
	}

    }

  /* Free memory.  */
  grub_free (modevar);

  return grub_error (GRUB_ERR_BAD_ARGUMENT,
		     N_("no suitable video mode found"));
}
