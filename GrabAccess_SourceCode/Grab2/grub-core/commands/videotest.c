/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2009  Free Software Foundation, Inc.
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
#include <grub/font.h>
#include <grub/term.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/gfxmenu_view.h>
#include <grub/env.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_cmd_videotest (grub_command_t cmd __attribute__ ((unused)),
                    int argc, char **args)
{
  grub_err_t err;
  grub_video_color_t color;
  unsigned int x;
  unsigned int y;
  unsigned int width;
  unsigned int height;
  int i;
  struct grub_video_render_target *text_layer;
  grub_video_color_t palette[16];
  const char *mode = NULL;

#ifdef GRUB_MACHINE_PCBIOS
  if (grub_strcmp (cmd->name, "vbetest") == 0)
    grub_dl_load ("vbe");
#endif
  mode = grub_env_get ("gfxmode");
  if (argc)
    mode = args[0];
  if (!mode)
    mode = "auto";

  err = grub_video_set_mode (mode, GRUB_VIDEO_MODE_TYPE_PURE_TEXT, 0);
  if (err)
    return err;

  grub_video_get_viewport (&x, &y, &width, &height);

  {
    const char *str;
    int texty;
    grub_font_t sansbig;
    grub_font_t sans;
    grub_font_t sanssmall;
    grub_font_t fixed;
    struct grub_font_glyph *glyph;

    if (grub_video_create_render_target (&text_layer, width, height,
					 GRUB_VIDEO_MODE_TYPE_RGB
					 | GRUB_VIDEO_MODE_TYPE_ALPHA)
	|| !text_layer)
      goto fail;

    grub_video_set_active_render_target (text_layer);

    color = grub_video_map_rgb (0, 255, 255);
    sansbig = grub_font_get ("Unknown Regular 16");
    sans = grub_font_get ("Unknown Regular 16");
    sanssmall = grub_font_get ("Unknown Regular 16");
    fixed = grub_font_get ("Fixed 20");
    if (! sansbig || ! sans || ! sanssmall || ! fixed)
      return grub_error (GRUB_ERR_BAD_FONT, "no font loaded");

    glyph = grub_font_get_glyph (fixed, '*');
    grub_font_draw_glyph (glyph, color, 200 ,0);

    color = grub_video_map_rgb (255, 255, 255);

    texty = 32;
    grub_font_draw_string ("The quick brown fox jumped over the lazy dog.",
			   sans, color, 16, texty);
    texty += grub_font_get_descent (sans) + grub_font_get_leading (sans);

    texty += grub_font_get_ascent (fixed);
    grub_font_draw_string ("The quick brown fox jumped over the lazy dog.",
			   fixed, color, 16, texty);
    texty += grub_font_get_descent (fixed) + grub_font_get_leading (fixed);

    /* To convert Unicode characters into UTF-8 for this test, the following
       command is useful:
       echo -ne '\x00\x00\x26\x3A' | iconv -f UTF-32BE -t UTF-8 | od -t x1
       This converts the Unicode character U+263A to UTF-8.  */

    /* Characters used:
       Code point  Description                    UTF-8 encoding
       ----------- ------------------------------ --------------
       U+263A      unfilled smiley face           E2 98 BA
       U+00A1      inverted exclamation point     C2 A1
       U+00A3      British pound currency symbol  C2 A3
       U+03C4      Greek tau                      CF 84
       U+00E4      lowercase letter a with umlaut C3 A4
       U+2124      set 'Z' symbol (integers)      E2 84 A4
       U+2286      subset symbol                  E2 8A 86
       U+211D      set 'R' symbol (real numbers)  E2 84 9D  */

    str =
      "Unicode test: happy\xE2\x98\xBA \xC2\xA3 5.00"
      " \xC2\xA1\xCF\x84\xC3\xA4u! "
      " \xE2\x84\xA4\xE2\x8A\x86\xE2\x84\x9D";
    color = grub_video_map_rgb (128, 128, 255);

    /* All characters in the string exist in the 'Fixed 20' (10x20) font.  */
    texty += grub_font_get_ascent(fixed);
    grub_font_draw_string (str, fixed, color, 16, texty);
    texty += grub_font_get_descent (fixed) + grub_font_get_leading (fixed);

    texty += grub_font_get_ascent(sansbig);
    grub_font_draw_string (str, sansbig, color, 16, texty);
    texty += grub_font_get_descent (sansbig) + grub_font_get_leading (sansbig);

    texty += grub_font_get_ascent(sans);
    grub_font_draw_string (str, sans, color, 16, texty);
    texty += grub_font_get_descent (sans) + grub_font_get_leading (sans);

    texty += grub_font_get_ascent(sanssmall);
    grub_font_draw_string (str, sanssmall, color, 16, texty);
    texty += (grub_font_get_descent (sanssmall)
	      + grub_font_get_leading (sanssmall));

    glyph = grub_font_get_glyph (fixed, '*');

    for (i = 0; i < 16; i++)
      {
	color = grub_video_map_color (i);
	palette[i] = color;
	grub_font_draw_glyph (glyph, color, 16 + i * 16, 220);
      }
  }

  grub_video_set_active_render_target (GRUB_VIDEO_RENDER_TARGET_DISPLAY);

  for (i = 0; i < 5; i++)
    {

      if (i == 0 || i == 1)
	{	  
	  color = grub_video_map_rgb (0, 0, 0);
	  grub_video_fill_rect (color, 0, 0, width, height);

	  color = grub_video_map_rgb (255, 0, 0);
	  grub_video_fill_rect (color, 0, 0, 100, 100);

	  color = grub_video_map_rgb (0, 255, 0);
	  grub_video_fill_rect (color, 100, 0, 100, 100);

	  color = grub_video_map_rgb (0, 0, 255);
	  grub_video_fill_rect (color, 200, 0, 100, 100);

	  color = grub_video_map_rgb (0, 255, 255);
	  grub_video_fill_rect (color, 0, 100, 100, 100);

	  color = grub_video_map_rgb (255, 0, 255);
	  grub_video_fill_rect (color, 100, 100, 100, 100);

	  color = grub_video_map_rgb (255, 255, 0);
	  grub_video_fill_rect (color, 200, 100, 100, 100);

	  grub_video_set_viewport (x + 150, y + 150,
				   width - 150 * 2, height - 150 * 2);
	  color = grub_video_map_rgb (77, 33, 77);
	  grub_video_fill_rect (color, 0, 0, width, height);
	}

      color = grub_video_map_rgb (i, 33, 77);
      grub_video_fill_rect (color, 0, 0, width, height);
      grub_video_blit_render_target (text_layer, GRUB_VIDEO_BLIT_BLEND, 0, 0,
                                     0, 0, width, height);
      grub_video_swap_buffers ();
    }

  grub_getkey ();

  grub_video_delete_render_target (text_layer);

  grub_video_restore ();

  for (i = 0; i < 16; i++)
    grub_printf("color %d: %08x\n", i, palette[i]);

  grub_errno = GRUB_ERR_NONE;
  return grub_errno;

 fail:
  grub_video_delete_render_target (text_layer);
  grub_video_restore ();
  return grub_errno;
}

static grub_command_t cmd;
#ifdef GRUB_MACHINE_PCBIOS
static grub_command_t cmd_vbe;
#endif

GRUB_MOD_INIT(videotest)
{
  cmd = grub_register_command ("videotest", grub_cmd_videotest,
			       /* TRANSLATORS: "x" has to be entered in,
				  like an identifier, so please don't
				  use better Unicode codepoints.  */
			       N_("[WxH]"),
			       /* TRANSLATORS: Here, on the other hand, it's
				  nicer to use unicode cross instead of x.  */
			       N_("Test video subsystem in mode WxH."));
#ifdef GRUB_MACHINE_PCBIOS
  cmd_vbe = grub_register_command ("vbetest", grub_cmd_videotest,
			       0, N_("Test video subsystem."));
#endif
}

GRUB_MOD_FINI(videotest)
{
  grub_unregister_command (cmd);
#ifdef GRUB_MACHINE_PCBIOS
  grub_unregister_command (cmd_vbe);
#endif
}
