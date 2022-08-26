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
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define DEFAULT_VIDEO_MODE	"auto"
#define DEFAULT_BORDER_WIDTH	10

#define DEFAULT_STANDARD_COLOR  0x07

struct grub_dirty_region
{
  int top_left_x;
  int top_left_y;
  int bottom_right_x;
  int bottom_right_y;
};

struct grub_colored_char
{
  /* An Unicode codepoint.  */
  struct grub_unicode_glyph code;

  /* Color values.  */
  grub_video_color_t fg_color;
  grub_video_color_t bg_color;
};

struct grub_virtual_screen
{
  /* Dimensions of the virtual screen in pixels.  */
  unsigned int width;
  unsigned int height;

  /* Offset in the display in pixels.  */
  unsigned int offset_x;
  unsigned int offset_y;

  /* TTY Character sizes in pixes.  */
  unsigned int normal_char_width;
  unsigned int normal_char_height;

  /* Virtual screen TTY size in characters.  */
  unsigned int columns;
  unsigned int rows;

  /* Current cursor location in characters.  */
  unsigned int cursor_x;
  unsigned int cursor_y;

  /* Current cursor state. */
  int cursor_state;

  /* Font settings. */
  grub_font_t font;

  /* Terminal color settings.  */
  grub_uint8_t standard_color_setting;
  grub_uint8_t term_color;

  /* Color settings.  */
  grub_video_color_t fg_color;
  grub_video_color_t bg_color;
  grub_video_color_t bg_color_display;

  /* Text buffer for virtual screen.  Contains (columns * rows) number
     of entries.  */
  struct grub_colored_char *text_buffer;

  int total_scroll;

  int functional;
};

struct grub_gfxterm_window
{
  unsigned x;
  unsigned y;
  unsigned width;
  unsigned height;
  int double_repaint;
};

static struct grub_video_render_target *render_target;
void (*grub_gfxterm_decorator_hook) (void) = NULL;
static struct grub_gfxterm_window window;
static struct grub_virtual_screen virtual_screen;
static int repaint_scheduled = 0;
static int repaint_was_scheduled = 0;

static void destroy_window (void);

static struct grub_video_render_target *text_layer;

struct grub_gfxterm_background grub_gfxterm_background;

static struct grub_dirty_region dirty_region;

static void dirty_region_reset (void);

static int dirty_region_is_empty (void);

static void dirty_region_add (int x, int y,
                              unsigned int width, unsigned int height);

static unsigned int calculate_normal_character_width (grub_font_t font);

static unsigned char calculate_character_width (struct grub_font_glyph *glyph);

static void grub_gfxterm_refresh (struct grub_term_output *term __attribute__ ((unused)));

static grub_size_t
grub_gfxterm_getcharwidth (struct grub_term_output *term __attribute__ ((unused)),
			   const struct grub_unicode_glyph *c);

static void
set_term_color (grub_uint8_t term_color)
{
  struct grub_video_render_target *old_target;

  /* Save previous target and switch to text layer.  */
  grub_video_get_active_render_target (&old_target);
  grub_video_set_active_render_target (text_layer);

  /* Map terminal color to text layer compatible video colors.  */
  virtual_screen.fg_color = grub_video_map_color(term_color & 0x0f);

  /* Special case: use black as transparent color.  */
  if (((term_color >> 4) & 0x0f) == 0)
    {
      virtual_screen.bg_color = grub_video_map_rgba(0, 0, 0, 0);
    }
  else
    {
      virtual_screen.bg_color = grub_video_map_color((term_color >> 4) & 0x0f);
    }

  /* Restore previous target.  */
  grub_video_set_active_render_target (old_target);
}

static void
clear_char (struct grub_colored_char *c)
{
  grub_unicode_destroy_glyph (&c->code);
  grub_unicode_set_glyph_from_code (&c->code, ' ');
  c->fg_color = virtual_screen.fg_color;
  c->bg_color = virtual_screen.bg_color;
}

static void
grub_virtual_screen_free (void)
{
  virtual_screen.functional = 0;

  /* If virtual screen has been allocated, free it.  */
  if (virtual_screen.text_buffer != 0)
    {
      unsigned i;
      for (i = 0;
	   i < virtual_screen.columns * virtual_screen.rows;
	   i++)
	grub_unicode_destroy_glyph (&virtual_screen.text_buffer[i].code);
      grub_free (virtual_screen.text_buffer);
    }

  /* Reset virtual screen data.  */
  grub_memset (&virtual_screen, 0, sizeof (virtual_screen));

  /* Free render targets.  */
  grub_video_delete_render_target (text_layer);
  text_layer = 0;
}

static grub_err_t
grub_virtual_screen_setup (unsigned int x, unsigned int y,
                           unsigned int width, unsigned int height,
			   grub_font_t font)
{
  unsigned int i;

  /* Free old virtual screen.  */
  grub_virtual_screen_free ();

  /* Initialize with default data.  */
  virtual_screen.font = font;
  virtual_screen.width = width;
  virtual_screen.height = height;
  virtual_screen.offset_x = x;
  virtual_screen.offset_y = y;
  virtual_screen.normal_char_width =
    calculate_normal_character_width (virtual_screen.font);
  virtual_screen.normal_char_height =
    grub_font_get_max_char_height (virtual_screen.font);
  if (virtual_screen.normal_char_height == 0)
    virtual_screen.normal_char_height = 16;
  virtual_screen.cursor_x = 0;
  virtual_screen.cursor_y = 0;
  virtual_screen.cursor_state = 1;
  virtual_screen.total_scroll = 0;

  /* Calculate size of text buffer.  */
  virtual_screen.columns = virtual_screen.width / virtual_screen.normal_char_width;
  virtual_screen.rows = virtual_screen.height / virtual_screen.normal_char_height;

  /*
   * There must be a minimum number of rows and columns for the screen to
   * make sense. Arbitrarily pick half of 80x24. If either dimensions is 0
   * we would allocate 0 bytes for the text_buffer.
   */
  if (virtual_screen.columns < 40 || virtual_screen.rows < 12)
    return grub_error (GRUB_ERR_BAD_FONT,
		       "font: glyphs too large to fit on screen");

  /* Allocate memory for text buffer.  */
  virtual_screen.text_buffer =
    (struct grub_colored_char *) grub_malloc (virtual_screen.columns
                                              * virtual_screen.rows
                                              * sizeof (*virtual_screen.text_buffer));
  if (grub_errno != GRUB_ERR_NONE)
    return grub_errno;

  /* Create new render target for text layer.  */
  grub_video_create_render_target (&text_layer,
                                   virtual_screen.width,
                                   virtual_screen.height,
                                   GRUB_VIDEO_MODE_TYPE_INDEX_COLOR
                                   | GRUB_VIDEO_MODE_TYPE_ALPHA);
  if (grub_errno != GRUB_ERR_NONE)
    return grub_errno;

  /* As we want to have colors compatible with rendering target,
     we can only have those after mode is initialized.  */
  grub_video_set_active_render_target (text_layer);

  virtual_screen.standard_color_setting = DEFAULT_STANDARD_COLOR;

  virtual_screen.term_color = virtual_screen.standard_color_setting;

  set_term_color (virtual_screen.term_color);

  grub_video_set_active_render_target (render_target);

  virtual_screen.bg_color_display =
    grub_video_map_rgba_color (grub_gfxterm_background.default_bg_color);

  /* Clear out text buffer. */
  for (i = 0; i < virtual_screen.columns * virtual_screen.rows; i++)
    {
      virtual_screen.text_buffer[i].code.ncomb = 0;
      clear_char (&(virtual_screen.text_buffer[i]));
    }
  if (grub_errno)
    return grub_errno;

  virtual_screen.functional = 1;

  return GRUB_ERR_NONE;
}

void
grub_gfxterm_schedule_repaint (void)
{
  repaint_scheduled = 1;
}

grub_err_t
grub_gfxterm_set_window (struct grub_video_render_target *target,
			 int x, int y, int width, int height,
			 int double_repaint,
			 grub_font_t font, int border_width)
{
  /* Clean up any prior instance.  */
  destroy_window ();

  /* Set the render target.  */
  render_target = target;

  /* Create virtual screen.  */
  if (grub_virtual_screen_setup (border_width, border_width, 
                                 width - 2 * border_width, 
                                 height - 2 * border_width, 
                                 font) 
      != GRUB_ERR_NONE)
    {
      return grub_errno;
    }

  /* Set window bounds.  */
  window.x = x;
  window.y = y;
  window.width = width;
  window.height = height;
  window.double_repaint = double_repaint;

  dirty_region_reset ();
  grub_gfxterm_schedule_repaint ();

  return grub_errno;
}

static grub_err_t
grub_gfxterm_fullscreen (void)
{
  const char *font_name;
  struct grub_video_mode_info mode_info;
  grub_video_color_t color;
  grub_err_t err;
  int double_redraw;
  grub_font_t font;

  err = grub_video_get_info (&mode_info);
  /* Figure out what mode we ended up.  */
  if (err)
    return err;

  grub_video_set_active_render_target (GRUB_VIDEO_RENDER_TARGET_DISPLAY);

  double_redraw = mode_info.mode_type & GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED
    && !(mode_info.mode_type & GRUB_VIDEO_MODE_TYPE_UPDATING_SWAP);

  /* Make sure screen is set to the default background color.  */
  color = grub_video_map_rgba_color (grub_gfxterm_background.default_bg_color);
  grub_video_fill_rect (color, 0, 0, mode_info.width, mode_info.height);
  if (double_redraw)
    {
      grub_video_swap_buffers ();
      grub_video_fill_rect (color, 0, 0, mode_info.width, mode_info.height);
    }

  /* Select the font to use.  */
  font_name = grub_env_get ("gfxterm_font");
  if (! font_name)
    font_name = "";   /* Allow fallback to any font.  */

  font = grub_font_get (font_name);
  if (!font)
    return grub_error (GRUB_ERR_BAD_FONT, "no font loaded");

  grub_gfxterm_decorator_hook = NULL;

  return grub_gfxterm_set_window (GRUB_VIDEO_RENDER_TARGET_DISPLAY,
				  0, 0, mode_info.width, mode_info.height,
				  double_redraw,
				  font, DEFAULT_BORDER_WIDTH);
}

static grub_err_t
grub_gfxterm_term_init (struct grub_term_output *term __attribute__ ((unused)))
{
  char *tmp;
  grub_err_t err;
  const char *modevar;

  /* Parse gfxmode environment variable if set.  */
  modevar = grub_env_get ("gfxmode");
  if (! modevar || *modevar == 0)
    err = grub_video_set_mode (DEFAULT_VIDEO_MODE,
			       GRUB_VIDEO_MODE_TYPE_PURE_TEXT, 0);
  else
    {
      tmp = grub_xasprintf ("%s;" DEFAULT_VIDEO_MODE, modevar);
      if (!tmp)
	return grub_errno;
      err = grub_video_set_mode (tmp, GRUB_VIDEO_MODE_TYPE_PURE_TEXT, 0);
      grub_free (tmp);
    }

  if (err)
    return err;

  err = grub_gfxterm_fullscreen ();
  if (err)
    grub_video_restore ();

  return err;
}

static void
destroy_window (void)
{
  grub_virtual_screen_free ();
}

static grub_err_t
grub_gfxterm_term_fini (struct grub_term_output *term __attribute__ ((unused)))
{
  unsigned i;
  destroy_window ();
  grub_video_restore ();

  for (i = 0; i < virtual_screen.columns * virtual_screen.rows; i++)
    {
      grub_unicode_destroy_glyph (&virtual_screen.text_buffer[i].code);
      virtual_screen.text_buffer[i].code.ncomb = 0;
      virtual_screen.text_buffer[i].code.base = 0;
    }

  /* Clear error state.  */
  grub_errno = GRUB_ERR_NONE;
  return GRUB_ERR_NONE;
}

static void
redraw_screen_rect (unsigned int x, unsigned int y,
                    unsigned int width, unsigned int height)
{
  grub_video_color_t color;
  grub_video_rect_t saved_view;

  grub_video_set_active_render_target (render_target);
  /* Save viewport and set it to our window.  */
  grub_video_get_viewport ((unsigned *) &saved_view.x, 
                           (unsigned *) &saved_view.y, 
                           (unsigned *) &saved_view.width, 
                           (unsigned *) &saved_view.height);
  grub_video_set_viewport (window.x, window.y, window.width, window.height);

  if (grub_gfxterm_background.bitmap)
    {
      /* Render bitmap as background.  */
      grub_video_blit_bitmap (grub_gfxterm_background.bitmap,
			      GRUB_VIDEO_BLIT_REPLACE, x, y,
			      x, y,
                              width, height);

      /* If bitmap is smaller than requested blit area, use background
         color.  */
      color = virtual_screen.bg_color_display;

      /* Fill right side of the bitmap if needed.  */
      if ((x + width >= grub_gfxterm_background.bitmap->mode_info.width)
	  && (y < grub_gfxterm_background.bitmap->mode_info.height))
        {
          int w = (x + width) - grub_gfxterm_background.bitmap->mode_info.width;
          int h = height;
          unsigned int tx = x;

          if (y + height >= grub_gfxterm_background.bitmap->mode_info.height)
            {
              h = grub_gfxterm_background.bitmap->mode_info.height - y;
            }

          if (grub_gfxterm_background.bitmap->mode_info.width > tx)
            {
              tx = grub_gfxterm_background.bitmap->mode_info.width;
            }

          /* Render background layer.  */
	  grub_video_fill_rect (color, tx, y, w, h);
        }

      /* Fill bottom side of the bitmap if needed.  */
      if (y + height >= grub_gfxterm_background.bitmap->mode_info.height)
        {
          int h = (y + height) - grub_gfxterm_background.bitmap->mode_info.height;
          unsigned int ty = y;

          if (grub_gfxterm_background.bitmap->mode_info.height > ty)
            {
              ty = grub_gfxterm_background.bitmap->mode_info.height;
            }

          /* Render background layer.  */
	  grub_video_fill_rect (color, x, ty, width, h);
        }
    }
  else
    {
      /* Render background layer.  */
      color = virtual_screen.bg_color_display;
      grub_video_fill_rect (color, x, y, width, height);
    }

  if (grub_gfxterm_background.blend_text_bg)
    /* Render text layer as blended.  */
    grub_video_blit_render_target (text_layer, GRUB_VIDEO_BLIT_BLEND, x, y,
                                   x - virtual_screen.offset_x,
                                   y - virtual_screen.offset_y,
                                   width, height);
  else
    /* Render text layer as replaced (to get texts background color).  */
    grub_video_blit_render_target (text_layer, GRUB_VIDEO_BLIT_REPLACE, x, y,
                                   x - virtual_screen.offset_x,
                                   y - virtual_screen.offset_y,
                                   width, height);

  /* Restore saved viewport.  */
  grub_video_set_viewport (saved_view.x, saved_view.y,
                           saved_view.width, saved_view.height);
  grub_video_set_active_render_target (render_target);
}

static void
dirty_region_reset (void)
{
  dirty_region.top_left_x = -1;
  dirty_region.top_left_y = -1;
  dirty_region.bottom_right_x = -1;
  dirty_region.bottom_right_y = -1;
  repaint_was_scheduled = 0;
}

static int
dirty_region_is_empty (void)
{
  if ((dirty_region.top_left_x == -1)
      || (dirty_region.top_left_y == -1)
      || (dirty_region.bottom_right_x == -1)
      || (dirty_region.bottom_right_y == -1))
    return 1;
  return 0;
}

static void
dirty_region_add_real (int x, int y, unsigned int width, unsigned int height)
{
  if (dirty_region_is_empty ())
    {
      dirty_region.top_left_x = x;
      dirty_region.top_left_y = y;
      dirty_region.bottom_right_x = x + width - 1;
      dirty_region.bottom_right_y = y + height - 1;
    }
  else
    {
      if (x < dirty_region.top_left_x)
        dirty_region.top_left_x = x;
      if (y < dirty_region.top_left_y)
        dirty_region.top_left_y = y;
      if ((x + (int)width - 1) > dirty_region.bottom_right_x)
        dirty_region.bottom_right_x = x + width - 1;
      if ((y + (int)height - 1) > dirty_region.bottom_right_y)
        dirty_region.bottom_right_y = y + height - 1;
    }
}

static void
dirty_region_add (int x, int y, unsigned int width, unsigned int height)
{
  if ((width == 0) || (height == 0))
    return;

  if (repaint_scheduled)
    {
      dirty_region_add_real (0, 0,
			     window.width, window.height);
      repaint_scheduled = 0;
      repaint_was_scheduled = 1;
    }
  dirty_region_add_real (x, y, width, height);
}

static void
dirty_region_add_virtualscreen (void)
{
  /* Mark virtual screen as dirty.  */
  dirty_region_add (virtual_screen.offset_x, virtual_screen.offset_y,
                    virtual_screen.width, virtual_screen.height);
}


static void
dirty_region_redraw (void)
{
  int x;
  int y;
  int width;
  int height;

  if (dirty_region_is_empty ())
    return;

  x = dirty_region.top_left_x;
  y = dirty_region.top_left_y;

  width = dirty_region.bottom_right_x - x + 1;
  height = dirty_region.bottom_right_y - y + 1;

  if (repaint_was_scheduled && grub_gfxterm_decorator_hook)
    grub_gfxterm_decorator_hook ();

  redraw_screen_rect (x, y, width, height);
}

static inline void
paint_char (unsigned cx, unsigned cy)
{
  struct grub_colored_char *p;
  struct grub_font_glyph *glyph;
  grub_video_color_t color;
  grub_video_color_t bgcolor;
  unsigned int x;
  unsigned int y;
  int ascent;
  unsigned int height;
  unsigned int width;

  if (cy + virtual_screen.total_scroll >= virtual_screen.rows)
    return;

  /* Find out active character.  */
  p = (virtual_screen.text_buffer
       + cx + (cy * virtual_screen.columns));

  if (!p->code.base)
    return;

  /* Get glyph for character.  */
  glyph = grub_font_construct_glyph (virtual_screen.font, &p->code);
  if (!glyph)
    {
      grub_errno = GRUB_ERR_NONE;
      return;
    }
  ascent = grub_font_get_ascent (virtual_screen.font);

  width = virtual_screen.normal_char_width * calculate_character_width(glyph);
  height = virtual_screen.normal_char_height;

  color = p->fg_color;
  bgcolor = p->bg_color;

  x = cx * virtual_screen.normal_char_width;
  y = (cy + virtual_screen.total_scroll) * virtual_screen.normal_char_height;

  /* Render glyph to text layer.  */
  grub_video_set_active_render_target (text_layer);
  grub_video_fill_rect (bgcolor, x, y, width, height);
  grub_font_draw_glyph (glyph, color, x, y + ascent);
  grub_video_set_active_render_target (render_target);

  /* Mark character to be drawn.  */
  dirty_region_add (virtual_screen.offset_x + x, virtual_screen.offset_y + y,
                    width, height);
}

static inline void
write_char (void)
{
  paint_char (virtual_screen.cursor_x, virtual_screen.cursor_y);
}

static inline void
draw_cursor (int show)
{
  unsigned int x;
  unsigned int y;
  unsigned int width;
  unsigned int height;
  unsigned int ascent;
  grub_video_color_t color;
  
  write_char ();

  if (!show)
    return;

  if (virtual_screen.cursor_y + virtual_screen.total_scroll
      >= virtual_screen.rows)
    return;

  /* Ensure that cursor doesn't go outside of character box.  */
  ascent = grub_font_get_ascent(virtual_screen.font);
  if (ascent > virtual_screen.normal_char_height - 2)
    ascent = virtual_screen.normal_char_height - 2;

  /* Determine cursor properties and position on text layer. */
  x = virtual_screen.cursor_x * virtual_screen.normal_char_width;
  width = virtual_screen.normal_char_width;
  color = virtual_screen.fg_color;
  y = ((virtual_screen.cursor_y + virtual_screen.total_scroll)
       * virtual_screen.normal_char_height
       + ascent);
  height = 2;
  
  /* Render cursor to text layer.  */
  grub_video_set_active_render_target (text_layer);
  grub_video_fill_rect (color, x, y, width, height);
  grub_video_set_active_render_target (render_target);
  
  /* Mark cursor to be redrawn.  */
  dirty_region_add (virtual_screen.offset_x + x,
		    virtual_screen.offset_y + y,
		    width, height);
}

static void
real_scroll (void)
{
  unsigned int i, j, was_scroll;
  grub_video_color_t color;

  if (!virtual_screen.total_scroll)
    return;

  /* If we have bitmap, re-draw screen, otherwise scroll physical screen too.  */
  if (grub_gfxterm_background.bitmap)
    {
      /* Scroll physical screen.  */
      grub_video_set_active_render_target (text_layer);
      color = virtual_screen.bg_color;
      grub_video_scroll (color, 0, -virtual_screen.normal_char_height
			 * virtual_screen.total_scroll);

      /* Mark virtual screen to be redrawn.  */
      dirty_region_add_virtualscreen ();
    }
  else
    {
      grub_video_rect_t saved_view;

      /* Remove cursor.  */
      draw_cursor (0);

      grub_video_set_active_render_target (render_target);

      i = window.double_repaint ? 2 : 1;

      color = virtual_screen.bg_color_display;

      while (i--)
	{
	  /* Save viewport and set it to our window.  */
	  grub_video_get_viewport ((unsigned *) &saved_view.x, 
				   (unsigned *) &saved_view.y, 
				   (unsigned *) &saved_view.width, 
				   (unsigned *) &saved_view.height);

	  grub_video_set_viewport (window.x, window.y, window.width,
				   window.height);

	  /* Clear new border area.  */
	  grub_video_fill_rect (color,
				virtual_screen.offset_x,
				virtual_screen.offset_y,
				virtual_screen.width,
				virtual_screen.normal_char_height
				* virtual_screen.total_scroll);

	  grub_video_set_active_render_target (render_target);
	  dirty_region_redraw ();

	  /* Scroll physical screen.  */
	  grub_video_scroll (color, 0, -virtual_screen.normal_char_height
			     * virtual_screen.total_scroll);

	  /* Restore saved viewport.  */
	  grub_video_set_viewport (saved_view.x, saved_view.y,
				   saved_view.width, saved_view.height);

	  if (i)
	    grub_video_swap_buffers ();
	}
      dirty_region_reset ();

      /* Scroll physical screen.  */
      grub_video_set_active_render_target (text_layer);
      color = virtual_screen.bg_color;
      grub_video_scroll (color, 0, -virtual_screen.normal_char_height
			 * virtual_screen.total_scroll);

      grub_video_set_active_render_target (render_target);

    }

  was_scroll = virtual_screen.total_scroll;
  virtual_screen.total_scroll = 0;

  if (was_scroll > virtual_screen.rows)
    was_scroll = virtual_screen.rows;

  /* Draw shadow part.  */
  for (i = virtual_screen.rows - was_scroll;
       i < virtual_screen.rows; i++)
    for (j = 0; j < virtual_screen.columns; j++)
      paint_char (j, i);

  /* Draw cursor if visible.  */
  if (virtual_screen.cursor_state)
    draw_cursor (1);
}

static void
scroll_up (void)
{
  unsigned int i;

  /* Clear first line in text buffer.  */
  for (i = 0; i < virtual_screen.columns; i++)
    grub_unicode_destroy_glyph (&virtual_screen.text_buffer[i].code);
  
  /* Scroll text buffer with one line to up.  */
  grub_memmove (virtual_screen.text_buffer,
                virtual_screen.text_buffer + virtual_screen.columns,
                sizeof (*virtual_screen.text_buffer)
                * virtual_screen.columns
                * (virtual_screen.rows - 1));

  /* Clear last line in text buffer.  */
  for (i = virtual_screen.columns * (virtual_screen.rows - 1);
       i < virtual_screen.columns * virtual_screen.rows;
       i++)
    clear_char (&(virtual_screen.text_buffer[i]));

  virtual_screen.total_scroll++;
}

static void
grub_gfxterm_putchar (struct grub_term_output *term,
		      const struct grub_unicode_glyph *c)
{
  if (!virtual_screen.functional)
    return;

  if (c->base == '\a')
    /* FIXME */
    return;

  /* Erase current cursor, if any.  */
  if (virtual_screen.cursor_state)
    draw_cursor (0);

  if (c->base == '\b' || c->base == '\n' || c->base == '\r')
    {
      switch (c->base)
        {
        case '\b':
          if (virtual_screen.cursor_x > 0)
            virtual_screen.cursor_x--;
          break;

        case '\n':
          if (virtual_screen.cursor_y >= virtual_screen.rows - 1)
            scroll_up ();
          else
            virtual_screen.cursor_y++;
          break;

        case '\r':
          virtual_screen.cursor_x = 0;
          break;
        }
    }
  else
    {
      struct grub_colored_char *p;
      unsigned char char_width;

      /* Calculate actual character width for glyph. This is number of
         times of normal_font_width.  */
      char_width = grub_gfxterm_getcharwidth (term, c);

      /* If we are about to exceed line length, wrap to next line.  */
      if (virtual_screen.cursor_x + char_width > virtual_screen.columns)
	{
          if (virtual_screen.cursor_y >= virtual_screen.rows - 1)
            scroll_up ();
          else
            virtual_screen.cursor_y++;
	}

      /* Find position on virtual screen, and fill information.  */
      p = (virtual_screen.text_buffer +
           virtual_screen.cursor_x +
           virtual_screen.cursor_y * virtual_screen.columns);
      grub_unicode_destroy_glyph (&p->code);
      grub_unicode_set_glyph (&p->code, c);
      grub_errno = GRUB_ERR_NONE;
      p->fg_color = virtual_screen.fg_color;
      p->bg_color = virtual_screen.bg_color;

      /* If we have large glyph, add fixup info.  */
      if (char_width > 1)
        {
          unsigned i;

          for (i = 1; i < char_width && p + i < 
		 virtual_screen.text_buffer + virtual_screen.columns
		 * virtual_screen.rows; i++)
	      {
		grub_unicode_destroy_glyph (&p[i].code);
		p[i].code.base = 0;
	      }
        }

      /* Draw glyph.  */
      write_char ();

      /* Make sure we scroll screen when needed and wrap line correctly.  */
      virtual_screen.cursor_x += char_width;
      if (virtual_screen.cursor_x >= virtual_screen.columns)
        {
          virtual_screen.cursor_x = 0;

          if (virtual_screen.cursor_y >= virtual_screen.rows - 1)
            scroll_up ();
          else
            virtual_screen.cursor_y++;
        }
    }

  /* Redraw cursor if it should be visible.  */
  /* Note: This will redraw the character as well, which means that the
     above call to write_char is redundant when the cursor is showing.  */
  if (virtual_screen.cursor_state)
    draw_cursor (1);
}

/* Use ASCII characters to determine normal character width.  */
static unsigned int
calculate_normal_character_width (grub_font_t font)
{
  struct grub_font_glyph *glyph;
  unsigned int width = 0;
  unsigned int i;

  /* Get properties of every printable ASCII character.  */
  for (i = 32; i < 127; i++)
    {
      glyph = grub_font_get_glyph (font, i);

      /* Skip unknown characters.  Should never happen on normal conditions.  */
      if (! glyph)
	continue;

      if (glyph->device_width > width)
	width = glyph->device_width;
    }
  if (!width)
    return 8;

  return width;
}

static unsigned char
calculate_character_width (struct grub_font_glyph *glyph)
{
  if (! glyph || glyph->device_width == 0)
    return 1;

  return (glyph->device_width
          + (virtual_screen.normal_char_width - 1))
         / virtual_screen.normal_char_width;
}

static grub_size_t
grub_gfxterm_getcharwidth (struct grub_term_output *term __attribute__ ((unused)),
			   const struct grub_unicode_glyph *c)
{
  int dev_width;
  dev_width = grub_font_get_constructed_device_width (virtual_screen.font, c);

  if (dev_width == 0)
    return 1;

  return (dev_width + (virtual_screen.normal_char_width - 1))
    / virtual_screen.normal_char_width;
}

static struct grub_term_coordinate
grub_virtual_screen_getwh (struct grub_term_output *term __attribute__ ((unused)))
{
  return (struct grub_term_coordinate) { virtual_screen.columns, virtual_screen.rows };
}

static struct grub_term_coordinate
grub_virtual_screen_getxy (struct grub_term_output *term __attribute__ ((unused)))
{
  return (struct grub_term_coordinate) { virtual_screen.cursor_x, virtual_screen.cursor_y };
}

static void
grub_gfxterm_gotoxy (struct grub_term_output *term __attribute__ ((unused)),
		     struct grub_term_coordinate pos)
{
  if (pos.x >= virtual_screen.columns)
    pos.x = virtual_screen.columns - 1;

  if (pos.y >= virtual_screen.rows)
    pos.y = virtual_screen.rows - 1;

  /* Erase current cursor, if any.  */
  if (virtual_screen.cursor_state)
    draw_cursor (0);

  virtual_screen.cursor_x = pos.x;
  virtual_screen.cursor_y = pos.y;

  /* Draw cursor if visible.  */
  if (virtual_screen.cursor_state)
    draw_cursor (1);
}

static void
grub_virtual_screen_cls (struct grub_term_output *term __attribute__ ((unused)))
{
  grub_uint32_t i;

  for (i = 0; i < virtual_screen.columns * virtual_screen.rows; i++)
    clear_char (&(virtual_screen.text_buffer[i]));

  virtual_screen.cursor_x = virtual_screen.cursor_y = 0;
}

static void
grub_gfxterm_cls (struct grub_term_output *term)
{
  grub_video_color_t color;

  /* Clear virtual screen.  */
  grub_virtual_screen_cls (term);

  /* Clear text layer.  */
  grub_video_set_active_render_target (text_layer);
  color = virtual_screen.bg_color;
  grub_video_fill_rect (color, 0, 0,
                        virtual_screen.width, virtual_screen.height);
  grub_video_set_active_render_target (render_target);

  /* Mark virtual screen to be redrawn.  */
  dirty_region_add_virtualscreen ();

  grub_gfxterm_refresh (term);
}

static void
grub_virtual_screen_setcolorstate (struct grub_term_output *term __attribute__ ((unused)),
				   grub_term_color_state state)
{
  switch (state)
    {
    case GRUB_TERM_COLOR_STANDARD:
      virtual_screen.term_color = virtual_screen.standard_color_setting;
      break;

    case GRUB_TERM_COLOR_NORMAL:
      virtual_screen.term_color = grub_term_normal_color;
      break;

    case GRUB_TERM_COLOR_HIGHLIGHT:
      virtual_screen.term_color = grub_term_highlight_color;
      break;

    default:
      break;
    }

  /* Change color to virtual terminal.  */
  set_term_color (virtual_screen.term_color);
}

static void
grub_gfxterm_setcursor (struct grub_term_output *term __attribute__ ((unused)),
			int on)
{
  if (virtual_screen.cursor_state != on)
    {
      if (virtual_screen.cursor_state)
	draw_cursor (0);
      else
	draw_cursor (1);

      virtual_screen.cursor_state = on;
    }
}

static void
grub_gfxterm_refresh (struct grub_term_output *term __attribute__ ((unused)))
{
  real_scroll ();

  /* Redraw only changed regions.  */
  dirty_region_redraw ();

  grub_video_swap_buffers ();

  if (window.double_repaint)
    dirty_region_redraw ();
  dirty_region_reset ();
}

static struct grub_term_output grub_video_term =
  {
    .name = "gfxterm",
    .init = grub_gfxterm_term_init,
    .fini = grub_gfxterm_term_fini,
    .putchar = grub_gfxterm_putchar,
    .getcharwidth = grub_gfxterm_getcharwidth,
    .getwh = grub_virtual_screen_getwh,
    .getxy = grub_virtual_screen_getxy,
    .gotoxy = grub_gfxterm_gotoxy,
    .cls = grub_gfxterm_cls,
    .setcolorstate = grub_virtual_screen_setcolorstate,
    .setcursor = grub_gfxterm_setcursor,
    .refresh = grub_gfxterm_refresh,
    .fullscreen = grub_gfxterm_fullscreen,
    .flags = GRUB_TERM_CODE_TYPE_VISUAL_GLYPHS,
    .progress_update_divisor = GRUB_PROGRESS_SLOW,
    .next = 0
  };

void
grub_gfxterm_video_update_color (void)
{
  struct grub_video_render_target *old_target;

  grub_video_get_active_render_target (&old_target);
  grub_video_set_active_render_target (text_layer);
  virtual_screen.bg_color = grub_video_map_rgba_color (grub_gfxterm_background.default_bg_color);
  grub_video_set_active_render_target (old_target);
  virtual_screen.bg_color_display =
    grub_video_map_rgba_color (grub_gfxterm_background.default_bg_color);
}

void
grub_gfxterm_get_dimensions (unsigned *width, unsigned *height)
{
  *width = window.width;
  *height = window.height;
}

GRUB_MOD_INIT(gfxterm)
{
  grub_term_register_output ("gfxterm", &grub_video_term);
}

GRUB_MOD_FINI(gfxterm)
{
  grub_term_unregister_output (&grub_video_term);
}
