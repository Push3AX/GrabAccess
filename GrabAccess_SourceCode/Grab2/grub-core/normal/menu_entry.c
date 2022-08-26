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

#include <grub/normal.h>
#include <grub/term.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/loader.h>
#include <grub/command.h>
#include <grub/parser.h>
#include <grub/script_sh.h>
#include <grub/auth.h>
#include <grub/i18n.h>
#include <grub/charset.h>
#include <grub/safemath.h>

enum update_mode
  {
    NO_LINE,
    SINGLE_LINE,
    ALL_LINES
  };

struct line
{
  /* The line buffer.  */
  grub_uint32_t *buf;
  /* The length of the line.  */
  int len;
  /* The maximum length of the line.  */
  int max_len;
  struct grub_term_pos **pos;
};

struct per_term_screen
{
  struct grub_term_output *term;
  int y_line_start;
  struct grub_term_screen_geometry geo;
  /* Scratch variables used when updating. Having them here avoids
     loads of small mallocs.  */
  int orig_num;
  int down;
  enum update_mode mode;
};

struct screen
{
  /* The array of lines.  */
  struct line *lines;
  /* The number of lines.  */
  int num_lines;
  /* The current column.  */
  int column;
  /* The real column.  */
  int real_column;
  /* The current line.  */
  int line;
  /* The kill buffer.  */
  char *killed_text;
  /* The flag of a completion window.  */
  int completion_shown;

  int submenu;

  struct per_term_screen *terms;
  unsigned nterms;
};

/* Used for storing completion items temporarily.  */
static struct {
  char *buf;
  grub_size_t len;
  grub_size_t max_len;
} completion_buffer;
static int completion_type;

/* Initialize a line.  */
static int
init_line (struct screen *screen, struct line *linep)
{
  linep->len = 0;
  linep->max_len = 80;
  linep->buf = grub_calloc (linep->max_len + 1, sizeof (linep->buf[0]));
  linep->pos = grub_calloc (screen->nterms, sizeof (linep->pos[0]));
  if (! linep->buf || !linep->pos)
    {
      grub_free (linep->buf);
      grub_free (linep->pos);
      return 0;
    }

  return 1;
}

/* Allocate extra space if necessary.  */
static int
ensure_space (struct line *linep, int extra)
{
  if (linep->max_len < linep->len + extra)
    {
      grub_size_t sz0, sz1;

      if (grub_add (linep->len, extra, &sz0) ||
	  grub_mul (sz0, 2, &sz0) ||
	  grub_add (sz0, 1, &sz1) ||
	  grub_mul (sz1, sizeof (linep->buf[0]), &sz1))
	return 0;

      linep->buf = grub_realloc (linep->buf, sz1);
      if (! linep->buf)
	return 0;
      linep->max_len = sz0;
    }

  return 1;
}

/* Return the number of lines occupied by this line on the screen.  */
static int
get_logical_num_lines (struct line *linep, struct per_term_screen *term_screen)
{
  grub_size_t width = grub_getstringwidth (linep->buf, linep->buf + linep->len,
					   term_screen->term);

  /* Empty line still consumes space on screen */
  return width ? (width + (unsigned) term_screen->geo.entry_width - 1) /
		 (unsigned) term_screen->geo.entry_width
	       : 1;
}

static void
advance_to (struct screen *screen, int c)
{
  if (c > screen->lines[screen->line].len)
    c = screen->lines[screen->line].len;

  screen->column = grub_unicode_get_comb_end (screen->lines[screen->line].buf
					      + screen->lines[screen->line].len,
					      screen->lines[screen->line].buf
					      + c)
    - screen->lines[screen->line].buf;
}

/* Print an empty line.  */
static void
print_empty_line (int y, struct per_term_screen *term_screen)
{
  int i;

  grub_term_gotoxy (term_screen->term,
		    (struct grub_term_coordinate) { term_screen->geo.first_entry_x,
			y + term_screen->geo.first_entry_y });

  for (i = 0; i < term_screen->geo.entry_width + 1; i++)
    grub_putcode (' ', term_screen->term);
}

static void
print_updown (int upflag, int downflag, struct per_term_screen *term_screen)
{
  grub_term_gotoxy (term_screen->term,
		    (struct grub_term_coordinate) { term_screen->geo.first_entry_x
			+ term_screen->geo.entry_width + 1
			+ term_screen->geo.border,
			term_screen->geo.first_entry_y });

  if (upflag && downflag)
    grub_putcode (GRUB_UNICODE_UPDOWNARROW, term_screen->term);
  else if (upflag)
    grub_putcode (GRUB_UNICODE_UPARROW, term_screen->term);
  else if (downflag)
    grub_putcode (GRUB_UNICODE_DOWNARROW, term_screen->term);
  else
    grub_putcode (' ', term_screen->term);
}

/* Print an up arrow.  */
static void
print_up (int flag, struct per_term_screen *term_screen)
{
  grub_term_gotoxy (term_screen->term,
		    (struct grub_term_coordinate) { term_screen->geo.first_entry_x
			+ term_screen->geo.entry_width + 1
			+ term_screen->geo.border,
			term_screen->geo.first_entry_y });

  if (flag)
    grub_putcode (GRUB_UNICODE_UPARROW, term_screen->term);
  else
    grub_putcode (' ', term_screen->term);
}

/* Print a down arrow.  */
static void
print_down (int flag, struct per_term_screen *term_screen)
{
  grub_term_gotoxy (term_screen->term,
		    (struct grub_term_coordinate) { term_screen->geo.first_entry_x
			+ term_screen->geo.entry_width + 1
			+ term_screen->geo.border,
			term_screen->geo.first_entry_y
			+ term_screen->geo.num_entries - 1 });

  if (flag)
    grub_putcode (GRUB_UNICODE_DOWNARROW, term_screen->term);
  else
    grub_putcode (' ', term_screen->term);
}

/* Draw the lines of the screen SCREEN.  */
static void
update_screen (struct screen *screen, struct per_term_screen *term_screen,
	       int region_start, int region_column __attribute__ ((unused)),
	       int up, int down, enum update_mode mode)
{
  int up_flag = 0;
  int down_flag = 0;
  int y;
  int i;
  struct line *linep;

  y = term_screen->y_line_start;
  linep = screen->lines;

  for (i = 0; i < screen->line; i++, linep++)
    y += get_logical_num_lines (linep, term_screen);
  linep = screen->lines + screen->line;
  grub_size_t t = grub_getstringwidth (linep->buf, linep->buf + screen->column,
				       term_screen->term);
  y += t / (unsigned) term_screen->geo.entry_width;
  if (t % (unsigned) term_screen->geo.entry_width == 0
      && t != 0 &&  screen->column == linep->len)
    y--;
  /* Check if scrolling is necessary.  */
  if (y < 0 || y >= term_screen->geo.num_entries)
    {
      int delta;
      if (y < 0)
	delta = -y;
      else
	delta = term_screen->geo.num_entries - 1 - y;
      term_screen->y_line_start += delta;

      region_start = 0;
      up = 1;
      down = 1;
      mode = ALL_LINES;
    }

  grub_term_setcursor (term_screen->term, 0);

  if (mode != NO_LINE)
    {
      /* Draw lines. This code is tricky, because this must calculate logical
	 positions.  */
      y = term_screen->y_line_start;
      i = 0;
      linep = screen->lines;
      while (1)
 	{
	  int add;
	  add = get_logical_num_lines (linep, term_screen);
	  if (y + add > 0)
	    break;
	  i++;
	  linep++;
	  y += add;
 	}

      if (y < 0 || i > 0)
	up_flag = 1;

      do
	{
	  struct grub_term_pos **pos;

	  if (linep >= screen->lines + screen->num_lines)
	    break;

	  pos = linep->pos + (term_screen - screen->terms);

	  if (!*pos)
	    *pos = grub_calloc (linep->len + 1, sizeof (**pos));

	  if (i == region_start || linep == screen->lines + screen->line
	      || (i > region_start && mode == ALL_LINES))
	    {
	      grub_term_gotoxy (term_screen->term,
				(struct grub_term_coordinate) { term_screen->geo.first_entry_x,
				    (y < 0 ? 0 : y)
				    + term_screen->geo.first_entry_y });

	      grub_print_ucs4_menu (linep->buf,
				    linep->buf + linep->len,
				    term_screen->geo.first_entry_x,
				    term_screen->geo.right_margin,
				    term_screen->term,
				    (y < 0) ? -y : 0,
				    term_screen->geo.num_entries
				    - ((y > 0) ? y : 0), '\\',
				    *pos);
	    }
	  y += get_logical_num_lines (linep, term_screen);
	  if (y >= term_screen->geo.num_entries)
	    {
	      if (i + 1 < screen->num_lines)
		down_flag = 1;
	    }

	  linep++;
	  i++;

	  if (mode == ALL_LINES && i == screen->num_lines)
	    for (; y < term_screen->geo.num_entries; y++)
	      print_empty_line (y, term_screen);
	}
      while (y < term_screen->geo.num_entries);

      /* Draw up and down arrows.  */
      
      if (term_screen->geo.num_entries == 1)
	{
	  if (up || down)
	    print_updown (up_flag, down_flag, term_screen);
	}
      else
	{
	  if (up)
	    print_up (up_flag, term_screen);
	  if (down)
	    print_down (down_flag, term_screen);
	}
    }

  /* Place the cursor.  */
  if (screen->lines[screen->line].pos[term_screen - screen->terms])
    {
      const struct grub_term_pos *cpos;
      for (cpos = &(screen->lines[screen->line].pos[term_screen - screen->terms])[screen->column];
	   cpos >= &(screen->lines[screen->line].pos[term_screen - screen->terms])[0];
	   cpos--)
	if (cpos->valid)
	  break;
      y = term_screen->y_line_start;
      for (i = 0; i < screen->line; i++)
	y += get_logical_num_lines (screen->lines + i, term_screen);
      if (cpos >= &(screen->lines[screen->line].pos[term_screen - screen->terms])[0])
	grub_term_gotoxy (term_screen->term, 
			  (struct grub_term_coordinate) { cpos->x + term_screen->geo.first_entry_x,
			      cpos->y + y
			      + term_screen->geo.first_entry_y });
      else
	grub_term_gotoxy (term_screen->term, 
			  (struct grub_term_coordinate) { term_screen->geo.first_entry_x,
			      y + term_screen->geo.first_entry_y });

    }

  grub_term_setcursor (term_screen->term, 1);

  grub_term_refresh (term_screen->term);
}

static void
update_screen_all (struct screen *screen,
		   int region_start, int region_column,
		   int up, int down, enum update_mode mode)
{
  unsigned i;
  for (i = 0; i < screen->nterms; i++)
    update_screen (screen, &screen->terms[i], region_start, region_column,
		   up, down, mode);
}

static int
insert_string (struct screen *screen, const char *s, int update)
{
  int region_start = screen->num_lines;
  int region_column = 0;
  unsigned i;

  for (i = 0; i < screen->nterms; i++)
    {
      screen->terms[i].down = 0;
      screen->terms[i].mode = NO_LINE;
    }

  while (*s)
    {
      if (*s == '\n')
	{
	  /* LF is special because it creates a new line.  */
	  struct line *current_linep;
	  struct line *next_linep;
	  int size;

	  /* Make a new line.  */
	  screen->num_lines++;
	  screen->lines = grub_realloc (screen->lines,
					screen->num_lines
					* sizeof (screen->lines[0]));
	  if (! screen->lines)
	    return 0;

	  /* Shift down if not appending after the last line. */
	  if (screen->line < screen->num_lines - 2)
	    grub_memmove (screen->lines + screen->line + 2,
			  screen->lines + screen->line + 1,
			  ((screen->num_lines - screen->line - 2)
			   * sizeof (struct line)));

	  if (! init_line (screen, screen->lines + screen->line + 1))
	    return 0;

	  /* Fold the line.  */
	  current_linep = screen->lines + screen->line;
	  next_linep = current_linep + 1;
	  size = current_linep->len - screen->column;

	  if (! ensure_space (next_linep, size))
	    return 0;

	  grub_memmove (next_linep->buf,
			current_linep->buf + screen->column,
			size * sizeof (next_linep->buf[0]));
	  current_linep->len = screen->column;
	  for (i = 0; i < screen->nterms; i++)
	    {
	      grub_free (current_linep->pos[i]);
	      current_linep->pos[i] = 0;
	    }
	  next_linep->len = size;

	  /* Update a dirty region.  */
	  if (region_start > screen->line)
	    {
	      region_start = screen->line;
	      region_column = screen->column;
	    }

	  for (i = 0; i < screen->nterms; i++)
	    {
	      screen->terms[i].mode = ALL_LINES;
	      screen->terms[i].down = 1; /* XXX not optimal.  */
	    }

	  /* Move the cursor.  */
	  screen->column = screen->real_column = 0;
	  screen->line++;
	  s++;
	}
      else
	{
	  /* All but LF.  */
	  const char *p;
	  struct line *current_linep;
	  int size;
	  grub_uint32_t *unicode_msg;

	  /* Find a string delimited by LF.  */
	  p = grub_strchr (s, '\n');
	  if (! p)
	    p = s + grub_strlen (s);

	  /* Insert the string.  */
	  current_linep = screen->lines + screen->line;
	  unicode_msg = grub_calloc (p - s, sizeof (grub_uint32_t));

	  if (!unicode_msg)
	    return 0;

	  size = grub_utf8_to_ucs4 (unicode_msg, (p - s),
				    (grub_uint8_t *) s, (p - s), 0);

	  if (! ensure_space (current_linep, size))
	    {
	      grub_free (unicode_msg);
	      return 0;
	    }

	  grub_memmove (current_linep->buf + screen->column + size,
			current_linep->buf + screen->column,
			(current_linep->len - screen->column)
			* sizeof (current_linep->buf[0]));
	  grub_memmove (current_linep->buf + screen->column,
			unicode_msg,
			size * sizeof (current_linep->buf[0]));
	  grub_free (unicode_msg);

	  for (i = 0; i < screen->nterms; i++)
	    {
	      grub_free (current_linep->pos[i]);
	      current_linep->pos[i] = 0;
	    }

	  for (i = 0; i < screen->nterms; i++)
	    screen->terms[i].orig_num = get_logical_num_lines (current_linep,
						 &screen->terms[i]);
	  current_linep->len += size;

	  /* Update the dirty region.  */
	  if (region_start > screen->line)
	    {
	      region_start = screen->line;
	      region_column = screen->column;
	    }

	  for (i = 0; i < screen->nterms; i++)
	    {
	      int new_num = get_logical_num_lines (current_linep,
						   &screen->terms[i]);
	      if (screen->terms[i].orig_num != new_num)
		{
		  screen->terms[i].mode = ALL_LINES;
		  screen->terms[i].down = 1; /* XXX not optimal.  */
		}
	      else if (screen->terms[i].mode != ALL_LINES)
		screen->terms[i].mode = SINGLE_LINE;
	    }

	  /* Move the cursor.  */
	  advance_to (screen, screen->column + size);

	  screen->real_column = screen->column;
	  s = p;
	}
    }

  if (update)
    for (i = 0; i < screen->nterms; i++)
      update_screen (screen, &screen->terms[i],
		     region_start, region_column, 0, screen->terms[i].down,
		     screen->terms[i].mode);

  return 1;
}

/* Release the resource allocated for SCREEN.  */
static void
destroy_screen (struct screen *screen)
{
  int i;

  if (screen->lines)
    for (i = 0; i < screen->num_lines; i++)
      {
	struct line *linep = screen->lines + i;

	if (linep)
	  {
	    unsigned j;
	    if (linep->pos)
	      for (j = 0; j < screen->nterms; j++)
		grub_free (linep->pos[j]);

	    grub_free (linep->buf);
	    grub_free (linep->pos);
	  }
      }

  grub_free (screen->killed_text);
  grub_free (screen->lines);
  grub_free (screen->terms);
  grub_free (screen);
}

/* Make a new screen.  */
static struct screen *
make_screen (grub_menu_entry_t entry)
{
  struct screen *screen;
  unsigned i;

  /* Initialize the screen.  */
  screen = grub_zalloc (sizeof (*screen));
  if (! screen)
    return 0;

  screen->submenu = entry->flag & GRUB_MENU_FLAG_SUBMENU;

  screen->num_lines = 1;
  screen->lines = grub_malloc (sizeof (struct line));
  if (! screen->lines)
    goto fail;

  /* Initialize the first line which must be always present.  */
  if (! init_line (screen, screen->lines))
    goto fail;

  insert_string (screen, (char *) entry->sourcecode, 0);

  /* Reset the cursor position.  */
  screen->column = 0;
  screen->real_column = 0;
  screen->line = 0;
  for (i = 0; i < screen->nterms; i++)
    {
      screen->terms[i].y_line_start = 0;
    }

  return screen;

 fail:
  destroy_screen (screen);
  return 0;
}

static int
forward_char (struct screen *screen, int update)
{
  struct line *linep;

  linep = screen->lines + screen->line;
  if (screen->column < linep->len)
    {
      screen->column = grub_unicode_get_comb_end (screen->lines[screen->line].buf
						  + screen->lines[screen->line].len,
						  screen->lines[screen->line].buf
						  + screen->column + 1)
	- screen->lines[screen->line].buf;
    }
  else if (screen->num_lines > screen->line + 1)
    {
      screen->column = 0;
      screen->line++;
    }

  screen->real_column = screen->column;

  if (update)
    update_screen_all (screen, screen->num_lines, 0, 0, 0, NO_LINE);
  return 1;
}

static int
backward_char (struct screen *screen, int update)
{
  if (screen->column > 0)
    {
      struct grub_unicode_glyph glyph;
      struct line *linep;

      linep = screen->lines + screen->line;

      screen->column--;
      screen->column = grub_unicode_get_comb_start (linep->buf, 
						    linep->buf + screen->column)
	- linep->buf;

      grub_unicode_aglomerate_comb (screen->lines[screen->line].buf + screen->column,
				    screen->lines[screen->line].len - screen->column,
				    &glyph);
      screen->column = grub_unicode_get_comb_start (linep->buf, 
						    linep->buf + screen->column)
	- linep->buf;

      grub_unicode_destroy_glyph (&glyph);
    }
  else if (screen->line > 0)
    {
      screen->line--;
      screen->column = screen->lines[screen->line].len;
    }

  screen->real_column = screen->column;

  if (update)
    update_screen_all (screen, screen->num_lines, 0, 0, 0, NO_LINE);

  return 1;
}

static int
previous_line (struct screen *screen, int update)
{
  if (screen->line > 0)
    {
      struct line *linep;
      int col;

      screen->line--;

      linep = screen->lines + screen->line;
      if (linep->len < screen->real_column)
	col = linep->len;
      else
	col = screen->real_column;

      screen->column = 0;
      advance_to (screen, col);
    }
  else
    {
      screen->column = 0;
    }

  if (update)
    update_screen_all (screen, screen->num_lines, 0, 0, 0, NO_LINE);

  return 1;
}

static int
next_line (struct screen *screen, int update)
{
  if (screen->line < screen->num_lines - 1)
    {
      struct line *linep;
      int c;

      /* How many physical lines from the current position
	 to the last physical line?  */
      linep = screen->lines + screen->line;

      screen->line++;
      if ((linep + 1)->len < screen->real_column)
	c = (linep + 1)->len;
      else
	c = screen->real_column;
      screen->column = 0;

      advance_to (screen, c);
    }
  else
    advance_to (screen, screen->lines[screen->line].len);

  if (update)
    update_screen_all (screen, screen->num_lines, 0, 0, 0, NO_LINE);

  return 1;
}

static int
beginning_of_line (struct screen *screen, int update)
{
  screen->column = screen->real_column = 0;

  if (update)
    update_screen_all (screen, screen->num_lines, 0, 0, 0, NO_LINE);

  return 1;
}

static int
end_of_line (struct screen *screen, int update)
{
  advance_to (screen, screen->lines[screen->line].len);

  if (update)
    update_screen_all (screen, screen->num_lines, 0, 0, 0, NO_LINE);

  return 1;
}

static int
delete_char (struct screen *screen, int update)
{
  struct line *linep;
  int start = screen->num_lines;
  int column = 0;

  linep = screen->lines + screen->line;
  if (linep->len > screen->column)
    {
      unsigned i;

      for (i = 0; i < screen->nterms; i++)
	screen->terms[i].orig_num = get_logical_num_lines (linep, &screen->terms[i]);

      grub_memmove (linep->buf + screen->column,
		    linep->buf + screen->column + 1,
		    (linep->len - screen->column - 1)
		    * sizeof (linep->buf[0]));
      linep->len--;

      for (i = 0; i < screen->nterms; i++)
	{
	  grub_free (linep->pos[i]);
	  linep->pos[i] = 0;
	}
      start = screen->line;
      column = screen->column;

      screen->real_column = screen->column;

      if (update)
	{
	  for (i = 0; i < screen->nterms; i++)
	    {
	      int new_num;
	      new_num = get_logical_num_lines (linep, &screen->terms[i]);
	      if (screen->terms[i].orig_num != new_num)
		update_screen (screen, &screen->terms[i],
			       start, column, 0, 0, ALL_LINES);
	      else
		update_screen (screen, &screen->terms[i],
			       start, column, 0, 0, SINGLE_LINE);
	    }
	}
    }
  else if (screen->num_lines > screen->line + 1)
    {
      struct line *next_linep;
      unsigned i;

      next_linep = linep + 1;
      if (! ensure_space (linep, next_linep->len))
	return 0;

      grub_memmove (linep->buf + linep->len, next_linep->buf,
		    next_linep->len * sizeof (linep->buf[0]));
      linep->len += next_linep->len;
      for (i = 0; i < screen->nterms; i++)
	{
	  grub_free (linep->pos[i]);
	  linep->pos[i] = 0;
	}

      grub_free (next_linep->buf);
      grub_memmove (next_linep,
		    next_linep + 1,
		    (screen->num_lines - screen->line - 2)
		    * sizeof (struct line));
      screen->num_lines--;

      start = screen->line;
      column = screen->column;

      screen->real_column = screen->column;
      if (update)
	update_screen_all (screen, start, column, 0, 1, ALL_LINES);
    }

  return 1;
}

static int
backward_delete_char (struct screen *screen, int update)
{
  int saved_column;
  int saved_line;

  saved_column = screen->column;
  saved_line = screen->line;

  if (! backward_char (screen, 0))
    return 0;

  if (saved_column != screen->column || saved_line != screen->line)
    if (! delete_char (screen, update))
      return 0;

  return 1;
}

static int
kill_line (struct screen *screen, int continuous, int update)
{
  struct line *linep;
  char *p;
  int size;
  int offset;

  p = screen->killed_text;
  if (! continuous && p)
    p[0] = '\0';

  linep = screen->lines + screen->line;
  size = linep->len - screen->column;

  if (p)
    offset = grub_strlen (p);
  else
    offset = 0;

  if (size > 0)
    {
      unsigned i;

      p = grub_realloc (p, offset + size + 1);
      if (! p)
	return 0;

      grub_memmove (p + offset, linep->buf + screen->column, size);
      p[offset + size] = '\0';

      screen->killed_text = p;

      for (i = 0; i < screen->nterms; i++)
	screen->terms[i].orig_num = get_logical_num_lines (linep, &screen->terms[i]);
      linep->len = screen->column;

      if (update)
	{
	  for (i = 0; i < screen->nterms; i++)
	    {
	      int new_num;
	      new_num = get_logical_num_lines (linep, &screen->terms[i]);
	      if (screen->terms[i].orig_num != new_num)
		update_screen (screen, &screen->terms[i],
			       screen->line, screen->column, 0, 1, ALL_LINES);
	      else
		update_screen (screen, &screen->terms[i],
			       screen->line, screen->column, 0, 0, SINGLE_LINE);
	    }
	}
    }
  else if (screen->line + 1 < screen->num_lines)
    {
      p = grub_realloc (p, offset + 1 + 1);
      if (! p)
	return 0;

      p[offset] = '\n';
      p[offset + 1] = '\0';

      screen->killed_text = p;

      return delete_char (screen, update);
    }

  return 1;
}

static int
yank (struct screen *screen, int update)
{
  if (screen->killed_text)
    return insert_string (screen, screen->killed_text, update);

  return 1;
}

static int
open_line (struct screen *screen, int update)
{
  if (! insert_string (screen, "\n", 0))
    return 0;

  if (! backward_char (screen, 0))
    return 0;

  if (update)
    update_screen_all (screen, screen->line, screen->column, 0, 1, ALL_LINES);

  return 1;
}

/* A completion hook to print items.  */
static void
store_completion (const char *item, grub_completion_type_t type,
		  int count __attribute__ ((unused)))
{
  char *p;

  completion_type = type;

  /* Make sure that the completion buffer has enough room.  */
  if (completion_buffer.max_len < (completion_buffer.len
				   + (int) grub_strlen (item) + 1 + 1))
    {
      grub_size_t new_len;

      new_len = completion_buffer.len + grub_strlen (item) + 80;
      p = grub_realloc (completion_buffer.buf, new_len);
      if (! p)
	{
	  /* Possibly not fatal.  */
	  grub_errno = GRUB_ERR_NONE;
	  return;
	}
      p[completion_buffer.len] = 0;
      completion_buffer.buf = p;
      completion_buffer.max_len = new_len;
    }

  p = completion_buffer.buf + completion_buffer.len;
  if (completion_buffer.len != 0)
    {
      *p++ = ' ';
      completion_buffer.len++;
    }
  grub_strcpy (p, item);
  completion_buffer.len += grub_strlen (item);
}

static int
complete (struct screen *screen, int continuous, int update)
{
  struct line *linep;
  int restore;
  char *insert;
  static int count = -1;
  unsigned i;
  grub_uint32_t *ucs4;
  grub_size_t buflen;
  grub_ssize_t ucs4len;
  char *u8;

  if (continuous)
    count++;
  else
    count = 0;

  completion_buffer.buf = 0;
  completion_buffer.len = 0;
  completion_buffer.max_len = 0;

  linep = screen->lines + screen->line;
  u8 = grub_ucs4_to_utf8_alloc (linep->buf, screen->column);
  if (!u8)
    return 1;

  insert = grub_normal_do_completion (u8, &restore, store_completion);

  if (completion_buffer.buf)
    {
      buflen = grub_strlen (completion_buffer.buf);
      ucs4 = grub_calloc (buflen + 1, sizeof (grub_uint32_t));

      if (!ucs4)
	{
	  grub_print_error ();
	  grub_errno = GRUB_ERR_NONE;
	  return 1;
	}

      ucs4len = grub_utf8_to_ucs4 (ucs4, buflen,
				   (grub_uint8_t *) completion_buffer.buf,
				   buflen, 0);
      ucs4[ucs4len] = 0;

      if (restore)
	for (i = 0; i < screen->nterms; i++)
	  {
	    unsigned width = grub_term_width (screen->terms[i].term);
	    if (width > 2)
	      width -= 2;
	    if (width > 15)
	      width -= 6;
	    unsigned num_sections = ((completion_buffer.len
				      + width - 1)
				     / width);
	    grub_uint32_t *endp;
	    struct grub_term_coordinate pos;
	    grub_uint32_t *p = ucs4;

	    pos = grub_term_getxy (screen->terms[i].term);

	    screen->completion_shown = 1;

	    grub_term_gotoxy (screen->terms[i].term,
			      (struct grub_term_coordinate) { 0,
				  screen->terms[i].geo.timeout_y });
	    if (screen->terms[i].geo.timeout_lines >= 2)
	      {
		grub_puts_terminal ("   ", screen->terms[i].term);
		switch (completion_type)
		  {
		  case GRUB_COMPLETION_TYPE_COMMAND:
		    grub_puts_terminal (_("Possible commands are:"),
					screen->terms[i].term);
		    break;
		  case GRUB_COMPLETION_TYPE_DEVICE:
		    grub_puts_terminal (_("Possible devices are:"),
					screen->terms[i].term);
		    break;
		  case GRUB_COMPLETION_TYPE_FILE:
		    grub_puts_terminal (_("Possible files are:"),
					screen->terms[i].term);
		    break;
		  case GRUB_COMPLETION_TYPE_PARTITION:
		    grub_puts_terminal (_("Possible partitions are:"),
					screen->terms[i].term);
		    break;
		  case GRUB_COMPLETION_TYPE_ENV:
		    grub_puts_terminal (_("Possible variables are:"),
					screen->terms[i].term);
		    break;
		  case GRUB_COMPLETION_TYPE_ARGUMENT:
		    grub_puts_terminal (_("Possible arguments are:"),
					screen->terms[i].term);
		    break;
		  default:
		    grub_puts_terminal (_("Possible things are:"),
					screen->terms[i].term);
		    break;
		  }

		grub_puts_terminal ("\n    ", screen->terms[i].term);
	      }

	    p += ((unsigned) count % num_sections) * width;
	    endp = p + width;

	    if (p != ucs4)
	      grub_putcode (GRUB_UNICODE_LEFTARROW, screen->terms[i].term);
	    else
	      grub_putcode (' ', screen->terms[i].term);

	    grub_print_ucs4 (p, ucs4 + ucs4len < endp ? ucs4 + ucs4len : endp,
			     0, 0, screen->terms[i].term);

	    if (ucs4 + ucs4len > endp)
	      grub_putcode (GRUB_UNICODE_RIGHTARROW, screen->terms[i].term);
	    grub_term_gotoxy (screen->terms[i].term, pos);
	  }
    }

  if (insert)
    {
      insert_string (screen, insert, update);
      count = -1;
      grub_free (insert);
    }
  else if (update)
    grub_refresh ();

  grub_free (completion_buffer.buf);
  return 1;
}

/* Clear displayed completions.  */
static void
clear_completions (struct per_term_screen *term_screen)
{
  struct grub_term_coordinate pos;
  unsigned j;
  int i;

  pos = grub_term_getxy (term_screen->term);
  grub_term_gotoxy (term_screen->term,
		    (struct grub_term_coordinate) { 0,
			term_screen->geo.timeout_y });

  for (i = 0; i < term_screen->geo.timeout_lines; i++)
    {
      for (j = 0; j < grub_term_width (term_screen->term) - 1; j++)
	grub_putcode (' ', term_screen->term);
      if (i + 1 < term_screen->geo.timeout_lines)
	grub_putcode ('\n', term_screen->term);
    }

  grub_term_gotoxy (term_screen->term, pos);
  grub_term_refresh (term_screen->term);
}

static void
clear_completions_all (struct screen *screen)
{
  unsigned i;

  for (i = 0; i < screen->nterms; i++)
    clear_completions (&screen->terms[i]);
}

/* Execute the command list in the screen SCREEN.  */
static int
run (struct screen *screen)
{
  char *script;
  int errs_before;
  grub_menu_t menu = NULL;
  char *dummy[1] = { NULL };

  grub_cls ();
  grub_dprintf ("menu", "Booting a command list");

  errs_before = grub_err_printed_errors;

  if (screen->submenu)
    {
      grub_env_context_open ();
      menu = grub_zalloc (sizeof (*menu));
      if (! menu)
	return 0;
      grub_env_set_menu (menu);
    }

  /* Execute the script, line for line.  */
  {
    int i;
    grub_size_t size = 0, tot_size = 0;

    for (i = 0; i < screen->num_lines; i++)
      tot_size += grub_get_num_of_utf8_bytes (screen->lines[i].buf,
					      screen->lines[i].len) + 1;

    script = grub_malloc (tot_size + 1);
    if (! script)
      return 0;

    for (i = 0; i < screen->num_lines; i++)
      {
	size += grub_ucs4_to_utf8 (screen->lines[i].buf, screen->lines[i].len,
				   (grub_uint8_t *) script + size,
				   tot_size - size);
	script[size++] = '\n';
      }
    script[size] = '\0';
  }
  grub_script_execute_new_scope (script, 0, dummy);
  grub_free (script);

  if (errs_before != grub_err_printed_errors)
    grub_wait_after_message ();

  if (grub_errno == GRUB_ERR_NONE && grub_loader_is_loaded ())
    /* Implicit execution of boot, only if something is loaded.  */
    grub_command_execute ("boot", 0, 0);

  if (screen->submenu)
    {
      if (menu && menu->size)
	{
	  grub_show_menu (menu, 1, 0);
	  grub_normal_free_menu (menu);
	}
      grub_env_context_close ();
    }

  if (grub_errno != GRUB_ERR_NONE)
    {
      grub_print_error ();
      grub_errno = GRUB_ERR_NONE;
      grub_wait_after_message ();
    }

  return 1;
}

/* Edit a menu entry with an Emacs-like interface.  */
void
grub_menu_entry_run (grub_menu_entry_t entry)
{
  struct screen *screen;
  int prev_c;
  grub_err_t err = GRUB_ERR_NONE;
  unsigned i;
  grub_term_output_t term;

  err = grub_auth_check_authentication (NULL);

  if (err)
    {
      grub_print_error ();
      grub_errno = GRUB_ERR_NONE;
      return;
    }

  screen = make_screen (entry);
  if (! screen)
    return;

  screen->terms = NULL;

 refresh:
  grub_free (screen->terms);
  screen->nterms = 0;
  FOR_ACTIVE_TERM_OUTPUTS(term)
    screen->nterms++;

  for (i = 0; i < (unsigned) screen->num_lines; i++)
    {
      grub_free (screen->lines[i].pos);
      screen->lines[i].pos = grub_calloc (screen->nterms, sizeof (screen->lines[i].pos[0]));
      if (! screen->lines[i].pos)
	{
	  grub_print_error ();
	  destroy_screen (screen);
	  grub_errno = GRUB_ERR_NONE;
	  return;
	}
    }

  screen->terms = grub_calloc (screen->nterms, sizeof (screen->terms[0]));
  if (!screen->terms)
    {
      grub_print_error ();
      destroy_screen (screen);
      grub_errno = GRUB_ERR_NONE;
      return;
    }
  i = 0;
  FOR_ACTIVE_TERM_OUTPUTS(term)
  {
    screen->terms[i].term = term;
    screen->terms[i].y_line_start = 0;
    i++;
  }
  /* Draw the screen.  */
  for (i = 0; i < screen->nterms; i++)
    grub_menu_init_page (0, 1, &screen->terms[i].geo, screen->terms[i].term, NULL);
  update_screen_all (screen, 0, 0, 1, 1, ALL_LINES);
  for (i = 0; i < screen->nterms; i++)
    grub_term_setcursor (screen->terms[i].term, 1);
  prev_c = '\0';

  while (1)
    {
      int c = grub_getkey ();

      if (screen->completion_shown)
	{
	  clear_completions_all (screen);
	  screen->completion_shown = 0;
	}

      if (grub_normal_exit_level)
	{
	  destroy_screen (screen);
	  return;
	}

      switch (c)
	{
	case GRUB_TERM_KEY_UP:
	case GRUB_TERM_CTRL | 'p':
	  if (! previous_line (screen, 1))
	    goto fail;
	  break;

	case GRUB_TERM_CTRL | 'n':
	case GRUB_TERM_KEY_DOWN:
	  if (! next_line (screen, 1))
	    goto fail;
	  break;

	case GRUB_TERM_CTRL | 'f':
	case GRUB_TERM_KEY_RIGHT:
	  if (! forward_char (screen, 1))
	    goto fail;
	  break;

	case GRUB_TERM_CTRL | 'b':
	case GRUB_TERM_KEY_LEFT:
	  if (! backward_char (screen, 1))
	    goto fail;
	  break;

	case GRUB_TERM_CTRL | 'a':
	case GRUB_TERM_KEY_HOME:
	  if (! beginning_of_line (screen, 1))
	    goto fail;
	  break;

	case GRUB_TERM_CTRL | 'e':
	case GRUB_TERM_KEY_END:
	  if (! end_of_line (screen, 1))
	    goto fail;
	  break;

	case GRUB_TERM_CTRL | 'i':
	case '\t':
	  if (! complete (screen, prev_c == c, 1))
	    goto fail;
	  break;

	case GRUB_TERM_CTRL | 'd':
	case GRUB_TERM_KEY_DC:
	  if (! delete_char (screen, 1))
	    goto fail;
	  break;

	case GRUB_TERM_CTRL | 'h':
	case '\b':
	  if (! backward_delete_char (screen, 1))
	    goto fail;
	  break;

	case GRUB_TERM_CTRL | 'k':
	  if (! kill_line (screen, prev_c == c, 1))
	    goto fail;
	  break;

	case GRUB_TERM_CTRL | 'u':
	  /* FIXME: What behavior is good for this key?  */
	  break;

	case GRUB_TERM_CTRL | 'y':
	  if (! yank (screen, 1))
	    goto fail;
	  break;

	case GRUB_TERM_CTRL | 'l':
	  /* FIXME: centering.  */
	  goto refresh;

	case GRUB_TERM_CTRL | 'o':
	  if (! open_line (screen, 1))
	    goto fail;
	  break;

	case '\n':
	case '\r':
	  if (! insert_string (screen, "\n", 1))
	    goto fail;
	  break;

	case GRUB_TERM_ESC:
	  destroy_screen (screen);
	  return;

	case GRUB_TERM_CTRL | 'c':
	case GRUB_TERM_KEY_F2:
	  grub_cmdline_run (1, 0);
	  goto refresh;

	case GRUB_TERM_CTRL | 'x':
	case GRUB_TERM_KEY_F10:
	  run (screen);
	  goto refresh;

	case GRUB_TERM_CTRL | 'r':
	case GRUB_TERM_CTRL | 's':
	case GRUB_TERM_CTRL | 't':
	  /* FIXME */
	  break;

	default:
	  if (grub_isprint (c))
	    {
	      char buf[2];

	      buf[0] = c;
	      buf[1] = '\0';
	      if (! insert_string (screen, buf, 1))
		goto fail;
	    }
	  break;
	}

      prev_c = c;
    }

 fail:
  destroy_screen (screen);

  grub_cls ();
  grub_print_error ();
  grub_errno = GRUB_ERR_NONE;
  grub_xputs ("\n");
  grub_printf_ (N_("Press any key to continue..."));
  (void) grub_getkey ();
}
