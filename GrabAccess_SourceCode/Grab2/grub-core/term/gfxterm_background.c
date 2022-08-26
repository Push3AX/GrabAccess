/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008,2009,2013  Free Software Foundation, Inc.
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

#include <grub/term.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/font.h>
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/video.h>
#include <grub/gfxterm.h>
#include <grub/bitmap.h>
#include <grub/command.h>
#include <grub/extcmd.h>
#include <grub/bitmap_scale.h>
#include <grub/i18n.h>
#include <grub/color.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* Option array indices.  */
enum
  {
    BACKGROUND_CMD_ARGINDEX_MODE = 0
  };

static const struct grub_arg_option background_image_cmd_options[] =
  {
    {"mode", 'm', 0, N_("Background image mode."),
    /* TRANSLATORS: This refers to background image mode (stretched or 
       in left-top corner). Note that GRUB will accept only original
       keywords stretch and normal, not the translated ones.
       So please put both in translation
       e.g. stretch(=%STRETCH%)|normal(=%NORMAL%).
       The percents mark the translated version. Since many people
       may not know the word stretch or normal I recommend
       putting the translation either here or in "Background image mode."
       string.  */
     N_("stretch|normal"),
     ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
  };

static grub_err_t
grub_gfxterm_background_image_cmd (grub_extcmd_context_t ctxt,
                                   int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;

  /* Check that we have video adapter active.  */
  if (grub_video_get_info(NULL) != GRUB_ERR_NONE)
    return grub_errno;

  /* Destroy existing background bitmap if loaded.  */
  if (grub_gfxterm_background.bitmap)
    {
      grub_video_bitmap_destroy (grub_gfxterm_background.bitmap);
      grub_gfxterm_background.bitmap = 0;
      grub_gfxterm_background.blend_text_bg = 0;

      /* Mark whole screen as dirty.  */
      grub_gfxterm_schedule_repaint ();
    }

  /* If filename was provided, try to load that.  */
  if (argc >= 1)
    {
      /* Try to load new one.  */
      grub_video_bitmap_load (&grub_gfxterm_background.bitmap, args[0]);
      if (grub_errno != GRUB_ERR_NONE)
        return grub_errno;

      /* Determine if the bitmap should be scaled to fit the screen.  */
      if (!state[BACKGROUND_CMD_ARGINDEX_MODE].set
          || grub_strcmp (state[BACKGROUND_CMD_ARGINDEX_MODE].arg,
                          "stretch") == 0)
          {
	    unsigned int width, height;
	    grub_gfxterm_get_dimensions (&width, &height);
            if (width
		!= grub_video_bitmap_get_width (grub_gfxterm_background.bitmap)
                || height
		!= grub_video_bitmap_get_height (grub_gfxterm_background.bitmap))
              {
                struct grub_video_bitmap *scaled_bitmap;
                grub_video_bitmap_create_scaled (&scaled_bitmap,
                                                 width, 
                                                 height,
                                                 grub_gfxterm_background.bitmap,
                                                 GRUB_VIDEO_BITMAP_SCALE_METHOD_BEST);
                if (grub_errno == GRUB_ERR_NONE)
                  {
                    /* Replace the original bitmap with the scaled one.  */
                    grub_video_bitmap_destroy (grub_gfxterm_background.bitmap);
                    grub_gfxterm_background.bitmap = scaled_bitmap;
                  }
              }
          }

      /* If bitmap was loaded correctly, display it.  */
      if (grub_gfxterm_background.bitmap)
        {
	  grub_gfxterm_background.blend_text_bg = 1;

          /* Mark whole screen as dirty.  */
	  grub_gfxterm_schedule_repaint ();
        }
    }

  /* All was ok.  */
  grub_errno = GRUB_ERR_NONE;
  return grub_errno;
}

static grub_err_t
grub_gfxterm_background_color_cmd (grub_command_t cmd __attribute__ ((unused)),
                                   int argc, char **args)
{
  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));

  /* Check that we have video adapter active.  */
  if (grub_video_get_info (NULL) != GRUB_ERR_NONE)
    return grub_errno;

  if (grub_video_parse_color (args[0],
			      &grub_gfxterm_background.default_bg_color)
      != GRUB_ERR_NONE)
    return grub_errno;

  /* Destroy existing background bitmap if loaded.  */
  if (grub_gfxterm_background.bitmap)
    {
      grub_video_bitmap_destroy (grub_gfxterm_background.bitmap);
      grub_gfxterm_background.bitmap = 0;

      /* Mark whole screen as dirty.  */
      grub_gfxterm_schedule_repaint ();
    }

  /* Set the background and border colors.  The background color needs to be
     compatible with the text layer.  */
  grub_gfxterm_video_update_color ();
  grub_gfxterm_background.blend_text_bg = 1;

  /* Mark whole screen as dirty.  */
  grub_gfxterm_schedule_repaint ();

  return GRUB_ERR_NONE;
}

static grub_extcmd_t background_image_cmd_handle;
static grub_command_t background_color_cmd_handle;

GRUB_MOD_INIT(gfxterm_background)
{
  background_image_cmd_handle =
    grub_register_extcmd ("background_image",
                          grub_gfxterm_background_image_cmd, 0,
                          N_("[-m (stretch|normal)] FILE"),
                          N_("Load background image for active terminal."),
                          background_image_cmd_options);
  background_color_cmd_handle =
    grub_register_command ("background_color",
                           grub_gfxterm_background_color_cmd,
                           N_("COLOR"),
                           N_("Set background color for active terminal."));
}

GRUB_MOD_FINI(gfxterm_background)
{
  grub_unregister_command (background_color_cmd_handle);
  grub_unregister_extcmd (background_image_cmd_handle);
}
