/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2008  Free Software Foundation, Inc.
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

#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/normal.h>
#include <grub/term.h>
#include <grub/i18n.h>

/* Borrowed from GRUB Legacy */
static const char *color_list[16] =
{
  "black",
  "blue",
  "green",
  "cyan",
  "red",
  "magenta",
  "brown",
  "light-gray",
  "dark-gray",
  "light-blue",
  "light-green",
  "light-cyan",
  "light-red",
  "light-magenta",
  "yellow",
  "white"
};

static int
parse_color_name (grub_uint8_t *ret, char *name)
{
  grub_uint8_t i;
  for (i = 0; i < ARRAY_SIZE(color_list); i++)
    if (! grub_strcmp (name, color_list[i]))
      {
        *ret = i;
        return 0;
      }
  return -1;
}

int
grub_parse_color_name_pair (grub_uint8_t *color, const char *name)
{
  int result = 1;
  grub_uint8_t fg, bg;
  char *fg_name, *bg_name;

  /* nothing specified by user */
  if (name == NULL)
    return result;

  fg_name = grub_strdup (name);
  if (fg_name == NULL)
    {
      /* "out of memory" message was printed by grub_strdup() */
      grub_wait_after_message ();
      return result;
    }

  bg_name = grub_strchr (fg_name, '/');
  if (bg_name == NULL)
    {
      grub_printf_ (N_("Warning: syntax error (missing slash) in `%s'\n"), fg_name);
      grub_wait_after_message ();
      goto free_and_return;
    }

  *(bg_name++) = '\0';

  if (parse_color_name (&fg, fg_name) == -1)
    {
      grub_printf_ (N_("Warning: invalid foreground color `%s'\n"), fg_name);
      grub_wait_after_message ();
      goto free_and_return;
    }
  if (parse_color_name (&bg, bg_name) == -1)
    {
      grub_printf_ (N_("Warning: invalid background color `%s'\n"), bg_name);
      grub_wait_after_message ();
      goto free_and_return;
    }

  *color = (bg << 4) | fg;
  result = 0;

free_and_return:
  grub_free (fg_name);
  return result;
}

static void
set_colors (void)
{
  struct grub_term_output *term;

  FOR_ACTIVE_TERM_OUTPUTS(term)
  {
    /* Propagates `normal' color to terminal current color.  */
    grub_term_setcolorstate (term, GRUB_TERM_COLOR_NORMAL);
  }
}

/* Replace default `normal' colors with the ones specified by user (if any).  */
char *
grub_env_write_color_normal (struct grub_env_var *var __attribute__ ((unused)),
			     const char *val)
{
  if (grub_parse_color_name_pair (&grub_term_normal_color, val))
    return NULL;

  set_colors ();

  return grub_strdup (val);
}

/* Replace default `highlight' colors with the ones specified by user (if any).  */
char *
grub_env_write_color_highlight (struct grub_env_var *var __attribute__ ((unused)),
				const char *val)
{
  if (grub_parse_color_name_pair (&grub_term_highlight_color, val))
    return NULL;

  set_colors ();

  return grub_strdup (val);
}
