/* terminfo.c - simple terminfo module */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2004,2005,2007  Free Software Foundation, Inc.
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

/*
 * This file contains various functions dealing with different
 * terminal capabilities. For example, vt52 and vt100.
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/term.h>
#include <grub/terminfo.h>
#include <grub/tparm.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/time.h>
#if defined(__powerpc__) && defined(GRUB_MACHINE_IEEE1275)
#include <grub/ieee1275/ieee1275.h>
#endif

GRUB_MOD_LICENSE ("GPLv3+");

#define ANSI_CSI 0x9b
#define ANSI_CSI_STR "\x9b"

static struct grub_term_output *terminfo_outputs;

/* Get current terminfo name.  */
char *
grub_terminfo_get_current (struct grub_term_output *term)
{
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;
  return data->name;
}

/* Free *PTR and set *PTR to NULL, to prevent double-free.  */
static void
grub_terminfo_free (char **ptr)
{
  grub_free (*ptr);
  *ptr = 0;
}

static void
grub_terminfo_all_free (struct grub_term_output *term)
{
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;

  /* Free previously allocated memory.  */
  grub_terminfo_free (&data->name);
  grub_terminfo_free (&data->gotoxy);
  grub_terminfo_free (&data->cls);
  grub_terminfo_free (&data->reverse_video_on);
  grub_terminfo_free (&data->reverse_video_off);
  grub_terminfo_free (&data->cursor_on);
  grub_terminfo_free (&data->cursor_off);
}

/* Set current terminfo type.  */
grub_err_t
grub_terminfo_set_current (struct grub_term_output *term,
			   const char *str)
{
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;
  /* TODO
   * Lookup user specified terminfo type. If found, set term variables
   * as appropriate. Otherwise return an error.
   *
   * How should this be done?
   *  a. A static table included in this module.
   *     - I do not like this idea.
   *  b. A table stored in the configuration directory.
   *     - Users must convert their terminfo settings if we have not already.
   *  c. Look for terminfo files in the configuration directory.
   *     - /usr/share/terminfo is 6.3M on my system.
   *     - /usr/share/terminfo is not on most users boot partition.
   *     + Copying the terminfo files you want to use to the grub
   *       configuration directory is easier then (b).
   *  d. Your idea here.
   */

  grub_terminfo_all_free (term);

  if (grub_strcmp ("vt100", str) == 0)
    {
      data->name              = grub_strdup ("vt100");
      data->gotoxy            = grub_strdup ("\e[%i%p1%d;%p2%dH");
      data->cls               = grub_strdup ("\e[H\e[J");
      data->reverse_video_on  = grub_strdup ("\e[7m");
      data->reverse_video_off = grub_strdup ("\e[m");
      data->cursor_on         = grub_strdup ("\e[?25h");
      data->cursor_off        = grub_strdup ("\e[?25l");
      data->setcolor          = NULL;
      return grub_errno;
    }

  if (grub_strcmp ("vt100-color", str) == 0)
    {
      data->name              = grub_strdup ("vt100-color");
      data->gotoxy            = grub_strdup ("\e[%i%p1%d;%p2%dH");
      data->cls               = grub_strdup ("\e[H\e[J");
      data->reverse_video_on  = grub_strdup ("\e[7m");
      data->reverse_video_off = grub_strdup ("\e[m");
      data->cursor_on         = grub_strdup ("\e[?25h");
      data->cursor_off        = grub_strdup ("\e[?25l");
      data->setcolor          = grub_strdup ("\e[3%p1%dm\e[4%p2%dm");
      return grub_errno;
    }

  if (grub_strcmp ("arc", str) == 0)
    {
      data->name              = grub_strdup ("arc");
      data->gotoxy            = grub_strdup (ANSI_CSI_STR "%i%p1%d;%p2%dH");
      data->cls               = grub_strdup (ANSI_CSI_STR "2J");
      data->reverse_video_on  = grub_strdup (ANSI_CSI_STR "7m");
      data->reverse_video_off = grub_strdup (ANSI_CSI_STR "0m");
      data->cursor_on         = 0;
      data->cursor_off        = 0;
      data->setcolor          = grub_strdup (ANSI_CSI_STR "3%p1%dm"
					     ANSI_CSI_STR "4%p2%dm");
      return grub_errno;
    }

  if (grub_strcmp ("ieee1275", str) == 0
      || grub_strcmp ("ieee1275-nocursor", str) == 0)
    {
      data->name              = grub_strdup ("ieee1275");
      data->gotoxy            = grub_strdup ("\e[%i%p1%d;%p2%dH");
      /* Clear the screen.  Using serial console, screen(1) only recognizes the
       * ANSI escape sequence.  Using video console, Apple Open Firmware
       * (version 3.1.1) only recognizes the literal ^L.  So use both.  */
      data->cls               = grub_strdup ("\x0c\e[2J\e[m");
      data->reverse_video_on  = grub_strdup ("\e[7m");
      data->reverse_video_off = grub_strdup ("\e[m");
      if (grub_strcmp ("ieee1275", str) == 0)
	{
	  data->cursor_on         = grub_strdup ("\e[?25h");
	  data->cursor_off        = grub_strdup ("\e[?25l");
	}
      else
	{
	  data->cursor_on         = 0;
	  data->cursor_off        = 0;
	}
      data->setcolor          = grub_strdup ("\e[3%p1%dm\e[4%p2%dm");
      return grub_errno;
    }

  if (grub_strcmp ("dumb", str) == 0)
    {
      data->name              = grub_strdup ("dumb");
      data->gotoxy            = NULL;
      data->cls               = NULL;
      data->reverse_video_on  = NULL;
      data->reverse_video_off = NULL;
      data->cursor_on         = NULL;
      data->cursor_off        = NULL;
      data->setcolor          = NULL;
      return grub_errno;
    }

  return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("unknown terminfo type `%s'"),
		     str);
}

grub_err_t
grub_terminfo_output_register (struct grub_term_output *term,
			       const char *type)
{
  grub_err_t err;
  struct grub_terminfo_output_state *data;

  err = grub_terminfo_set_current (term, type);

  if (err)
    return err;

  data = (struct grub_terminfo_output_state *) term->data;
  data->next = terminfo_outputs;
  terminfo_outputs = term;

  return GRUB_ERR_NONE;
}

grub_err_t
grub_terminfo_output_unregister (struct grub_term_output *term)
{
  struct grub_term_output **ptr;

  for (ptr = &terminfo_outputs; *ptr;
       ptr = &((struct grub_terminfo_output_state *) (*ptr)->data)->next)
    if (*ptr == term)
      {
	grub_terminfo_all_free (term);
	*ptr = ((struct grub_terminfo_output_state *) (*ptr)->data)->next;
	return GRUB_ERR_NONE;
      }
  return grub_error (GRUB_ERR_BUG, "terminal not found");
}

/* Wrapper for grub_putchar to write strings.  */
static void
putstr (struct grub_term_output *term, const char *str)
{
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;
  while (*str)
    data->put (term, *str++);
}

struct grub_term_coordinate
grub_terminfo_getxy (struct grub_term_output *term)
{
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;

  return data->pos;
}

void
grub_terminfo_gotoxy (struct grub_term_output *term,
		      struct grub_term_coordinate pos)
{
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;

  if (pos.x > grub_term_width (term) || pos.y > grub_term_height (term))
    {
      grub_error (GRUB_ERR_BUG, "invalid point (%u,%u)", pos.x, pos.y);
      return;
    }

  if (data->gotoxy)
    putstr (term, grub_terminfo_tparm (data->gotoxy, pos.y, pos.x));
  else
    {
      if ((pos.y == data->pos.y) && (pos.x == data->pos.x - 1))
	data->put (term, '\b');
    }

  data->pos = pos;
}

/* Clear the screen.  */
void
grub_terminfo_cls (struct grub_term_output *term)
{
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;

  putstr (term, grub_terminfo_tparm (data->cls));
  grub_terminfo_gotoxy (term, (struct grub_term_coordinate) { 0, 0 });
}

void
grub_terminfo_setcolorstate (struct grub_term_output *term,
			     const grub_term_color_state state)
{
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;

  if (data->setcolor)
    {
      int fg;
      int bg;
      /* Map from VGA to terminal colors.  */
      const int colormap[8] 
	= { 0, /* Black. */
	    4, /* Blue. */
	    2, /* Green. */
	    6, /* Cyan. */
	    1, /* Red.  */
	    5, /* Magenta.  */
	    3, /* Yellow.  */
	    7, /* White.  */
      };

      switch (state)
	{
	case GRUB_TERM_COLOR_STANDARD:
	case GRUB_TERM_COLOR_NORMAL:
	  fg = grub_term_normal_color & 0x0f;
	  bg = grub_term_normal_color >> 4;
	  break;
	case GRUB_TERM_COLOR_HIGHLIGHT:
	  fg = grub_term_highlight_color & 0x0f;
	  bg = grub_term_highlight_color >> 4;
	  break;
	default:
	  return;
	}

      putstr (term, grub_terminfo_tparm (data->setcolor, colormap[fg & 7],
					 colormap[bg & 7]));
      return;
    }

  switch (state)
    {
    case GRUB_TERM_COLOR_STANDARD:
    case GRUB_TERM_COLOR_NORMAL:
      putstr (term, grub_terminfo_tparm (data->reverse_video_off));
      break;
    case GRUB_TERM_COLOR_HIGHLIGHT:
      putstr (term, grub_terminfo_tparm (data->reverse_video_on));
      break;
    default:
      break;
    }
}

void
grub_terminfo_setcursor (struct grub_term_output *term, const int on)
{
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;

  if (on)
    putstr (term, grub_terminfo_tparm (data->cursor_on));
  else
    putstr (term, grub_terminfo_tparm (data->cursor_off));
}

/* The terminfo version of putchar.  */
void
grub_terminfo_putchar (struct grub_term_output *term,
		       const struct grub_unicode_glyph *c)
{
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;

  /* Keep track of the cursor.  */
  switch (c->base)
    {
    case '\a':
      break;

    case '\b':
    case 127:
      if (data->pos.x > 0)
	data->pos.x--;
    break;

    case '\n':
      if (data->pos.y < grub_term_height (term) - 1)
	data->pos.y++;
      break;

    case '\r':
      data->pos.x = 0;
      break;

    default:
      if ((int) data->pos.x + c->estimated_width >= (int) grub_term_width (term) + 1)
	{
	  data->pos.x = 0;
	  if (data->pos.y < grub_term_height (term) - 1)
	    data->pos.y++;
	  data->put (term, '\r');
	  data->put (term, '\n');
	}
      data->pos.x += c->estimated_width;
      break;
    }

  data->put (term, c->base);
}

struct grub_term_coordinate
grub_terminfo_getwh (struct grub_term_output *term)
{
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;

  return data->size;
}

static void
grub_terminfo_readkey (struct grub_term_input *term, int *keys, int *len, int max_len,
		       int (*readkey) (struct grub_term_input *term))
{
  int c;

#define CONTINUE_READ						\
  {								\
    grub_uint64_t start;					\
    /* On 9600 we have to wait up to 12 milliseconds.  */	\
    start = grub_get_time_ms ();				\
    do								\
      c = readkey (term);					\
    while (c == -1 && grub_get_time_ms () - start < 100);	\
    if (c == -1)						\
      return;							\
								\
    if (*len >= max_len)                                       \
      return;                                                   \
                                                                \
    keys[*len] = c;						\
    (*len)++;							\
  }

  c = readkey (term);
  if (c < 0)
    {
      *len = 0;
      return;
    }
  *len = 1;
  keys[0] = c;
  if (c != ANSI_CSI && c != GRUB_TERM_ESC)
    {
      /* Backspace: Ctrl-h.  */
      if (c == 0x7f)
	c = GRUB_TERM_BACKSPACE;
      if (c < 0x20 && c != GRUB_TERM_TAB && c!= GRUB_TERM_BACKSPACE && c != '\n' && c != '\r')
	c = GRUB_TERM_CTRL | (c - 1 + 'a');
      *len = 1;
      keys[0] = c;
      return;
    }

  {
    static struct
    {
      char key;
      unsigned ascii;
    }
    three_code_table[] =
      {
	{'4', GRUB_TERM_KEY_DC},
	{'A', GRUB_TERM_KEY_UP},
	{'B', GRUB_TERM_KEY_DOWN},
	{'C', GRUB_TERM_KEY_RIGHT},
	{'D', GRUB_TERM_KEY_LEFT},
	{'F', GRUB_TERM_KEY_END},
	{'H', GRUB_TERM_KEY_HOME},
	{'K', GRUB_TERM_KEY_END},
	{'P', GRUB_TERM_KEY_DC},
	{'?', GRUB_TERM_KEY_PPAGE},
	{'/', GRUB_TERM_KEY_NPAGE},
	{'@', GRUB_TERM_KEY_INSERT},
      };

    static unsigned four_code_table[] =
      {
	[1] = GRUB_TERM_KEY_HOME,
	[3] = GRUB_TERM_KEY_DC,
	[5] = GRUB_TERM_KEY_PPAGE,
	[6] = GRUB_TERM_KEY_NPAGE,
	[7] = GRUB_TERM_KEY_HOME,
	[8] = GRUB_TERM_KEY_END,
	[17] = GRUB_TERM_KEY_F6,
	[18] = GRUB_TERM_KEY_F7,
	[19] = GRUB_TERM_KEY_F8,
	[20] = GRUB_TERM_KEY_F9,
	[21] = GRUB_TERM_KEY_F10,
	[23] = GRUB_TERM_KEY_F11,
	[24] = GRUB_TERM_KEY_F12,
      };
    char fx_key[] = 
      { 'P', 'Q', 'w', 'x', 't', 'u',
        'q', 'r', 'p', 'M', 'A', 'B', 'H', 'F' };
    unsigned fx_code[] = 
	{ GRUB_TERM_KEY_F1, GRUB_TERM_KEY_F2, GRUB_TERM_KEY_F3,
	  GRUB_TERM_KEY_F4, GRUB_TERM_KEY_F5, GRUB_TERM_KEY_F6,
	  GRUB_TERM_KEY_F7, GRUB_TERM_KEY_F8, GRUB_TERM_KEY_F9,
	  GRUB_TERM_KEY_F10, GRUB_TERM_KEY_F11, GRUB_TERM_KEY_F12,
	  GRUB_TERM_KEY_HOME, GRUB_TERM_KEY_END };
    unsigned i;

    if (c == GRUB_TERM_ESC)
      {
	CONTINUE_READ;

	if (c == 'O')
	  {
	    CONTINUE_READ;

	    for (i = 0; i < ARRAY_SIZE (fx_key); i++)
	      if (fx_key[i] == c)
		{
		  keys[0] = fx_code[i];
		  *len = 1;
		  return;
		}
	  }

	if (c != '[')
	  return;
      }

    CONTINUE_READ;
	
    for (i = 0; i < ARRAY_SIZE (three_code_table); i++)
      if (three_code_table[i].key == c)
	{
	  keys[0] = three_code_table[i].ascii;
	  *len = 1;
	  return;
	}

    switch (c)
      {
      case '[':
	CONTINUE_READ;
	if (c >= 'A' && c <= 'E')
	  {
	    keys[0] = GRUB_TERM_KEY_F1 + c - 'A';
	    *len = 1;
	    return;
	  }
	return;
      case 'O':
	CONTINUE_READ;
	for (i = 0; i < ARRAY_SIZE (fx_key); i++)
	  if (fx_key[i] == c)
	    {
	      keys[0] = fx_code[i];
	      *len = 1;
	      return;
	    }
	return;

      case '0':
	{
	  int num = 0;
	  CONTINUE_READ;
	  if (c != '0' && c != '1')
	    return;
	  num = (c - '0') * 10;
	  CONTINUE_READ;
	  if (c < '0' || c > '9')
	    return;
	  num += (c - '0');
	  if (num == 0 || num > 12)
	    return;
	  CONTINUE_READ;
	  if (c != 'q')
	    return;
	  keys[0] = fx_code[num - 1];
	  *len = 1;
	  return;
	}	  

      case '1' ... '9':
	{
	  unsigned val = c - '0';
	  CONTINUE_READ;
	  if (c >= '0' && c <= '9')
	    {
	      val = val * 10 + (c - '0');
	      CONTINUE_READ;
	    }
	  if (c != '~')
	    return;
	  if (val >= ARRAY_SIZE (four_code_table)
	      || four_code_table[val] == 0)
	    return;
	  keys[0] = four_code_table[val];
	  *len = 1;
	  return;
	}
	default:
	  return;
      }
  }
#undef CONTINUE_READ
}

/* The terminfo version of getkey.  */
int
grub_terminfo_getkey (struct grub_term_input *termi)
{
  struct grub_terminfo_input_state *data
    = (struct grub_terminfo_input_state *) (termi->data);
  if (data->npending)
    {
      int ret;
      data->npending--;
      ret = data->input_buf[0];
      grub_memmove (data->input_buf, data->input_buf + 1, data->npending
		    * sizeof (data->input_buf[0]));
      return ret;
    }

  grub_terminfo_readkey (termi, data->input_buf, &data->npending,
			 GRUB_TERMINFO_READKEY_MAX_LEN, data->readkey);

#if defined(__powerpc__) && defined(GRUB_MACHINE_IEEE1275)
  if (data->npending == 1 && data->input_buf[0] == GRUB_TERM_ESC
      && grub_ieee1275_test_flag (GRUB_IEEE1275_FLAG_BROKEN_REPEAT)
      && grub_get_time_ms () - data->last_key_time < 1000
      && (data->last_key & GRUB_TERM_EXTENDED))
    {
      data->npending = 0;
      data->last_key_time = grub_get_time_ms ();
      return data->last_key;
    }
#endif

  if (data->npending)
    {
      int ret;
      data->npending--;
      ret = data->input_buf[0];
#if defined(__powerpc__) && defined(GRUB_MACHINE_IEEE1275)
      if (grub_ieee1275_test_flag (GRUB_IEEE1275_FLAG_BROKEN_REPEAT))
	{
	  data->last_key = ret;
	  data->last_key_time = grub_get_time_ms ();
	}
#endif
      grub_memmove (data->input_buf, data->input_buf + 1, data->npending
		    * sizeof (data->input_buf[0]));
      return ret;
    }

  return GRUB_TERM_NO_KEY;
}

grub_err_t
grub_terminfo_input_init (struct grub_term_input *termi)
{
  struct grub_terminfo_input_state *data
    = (struct grub_terminfo_input_state *) (termi->data);
  data->npending = 0;

  return GRUB_ERR_NONE;
}

grub_err_t
grub_terminfo_output_init (struct grub_term_output *term)
{
  grub_terminfo_cls (term);
  return GRUB_ERR_NONE;
}

/* GRUB Command.  */

static grub_err_t
print_terminfo (void)
{
  const char *encoding_names[(GRUB_TERM_CODE_TYPE_MASK 
			      >> GRUB_TERM_CODE_TYPE_SHIFT) + 1]
    = {
    /* VGA and glyph descriptor types are just for completeness,
       they are not used on terminfo terminals.
    */
    [GRUB_TERM_CODE_TYPE_ASCII >> GRUB_TERM_CODE_TYPE_SHIFT] = _("ASCII"),
    [GRUB_TERM_CODE_TYPE_CP437 >> GRUB_TERM_CODE_TYPE_SHIFT] = "CP-437",
    [GRUB_TERM_CODE_TYPE_UTF8_LOGICAL >> GRUB_TERM_CODE_TYPE_SHIFT]
    = _("UTF-8"),
    [GRUB_TERM_CODE_TYPE_UTF8_VISUAL >> GRUB_TERM_CODE_TYPE_SHIFT]
    /* TRANSLATORS: visually ordered UTF-8 is a non-compliant encoding
       based on UTF-8 with right-to-left languages written in reverse.
       Used on some terminals. Normal UTF-8 is refered as
       "logically-ordered UTF-8" by opposition.  */
    = _("visually-ordered UTF-8"),
    [GRUB_TERM_CODE_TYPE_VISUAL_GLYPHS >> GRUB_TERM_CODE_TYPE_SHIFT]
    = "Glyph descriptors",
    _("Unknown encoding"), _("Unknown encoding"), _("Unknown encoding")
  };
  struct grub_term_output *cur;

  grub_puts_ (N_("Current terminfo types:"));
  for (cur = terminfo_outputs; cur;
       cur = ((struct grub_terminfo_output_state *) cur->data)->next)
    grub_printf ("%s: %s\t%s\t%dx%d\n", cur->name,
		 grub_terminfo_get_current(cur),
		 encoding_names[(cur->flags & GRUB_TERM_CODE_TYPE_MASK)
				>> GRUB_TERM_CODE_TYPE_SHIFT],
		 ((struct grub_terminfo_output_state *) cur->data)->pos.x,
	         ((struct grub_terminfo_output_state *) cur->data)->pos.y);

  return GRUB_ERR_NONE;
}

static const struct grub_arg_option options[] =
{
  {"ascii", 'a', 0, N_("Terminal is ASCII-only [default]."),  0, ARG_TYPE_NONE},
  {"utf8",  'u', 0, N_("Terminal is logical-ordered UTF-8."), 0, ARG_TYPE_NONE},
  {"visual-utf8", 'v', 0, N_("Terminal is visually-ordered UTF-8."), 0,
   ARG_TYPE_NONE},
  {"geometry", 'g', 0, N_("Terminal has specified geometry."),
   /* TRANSLATORS: "x" has to be entered in, like an identifier, so please don't
      use better Unicode codepoints.  */
   N_("WIDTHxHEIGHT."), ARG_TYPE_STRING},
  {0, 0, 0, 0, 0, 0}
};

enum
  {
    OPTION_ASCII,
    OPTION_UTF8,
    OPTION_VISUAL_UTF8,
    OPTION_GEOMETRY
  };

static grub_err_t
grub_cmd_terminfo (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_term_output *cur;
  int encoding = GRUB_TERM_CODE_TYPE_ASCII;
  struct grub_arg_list *state = ctxt->state;
  int w = 0, h = 0;

  if (argc == 0)
    return print_terminfo ();

  if (state[OPTION_ASCII].set)
    encoding = GRUB_TERM_CODE_TYPE_ASCII;

  if (state[OPTION_UTF8].set)
    encoding = GRUB_TERM_CODE_TYPE_UTF8_LOGICAL;

  if (state[OPTION_VISUAL_UTF8].set)
    encoding = GRUB_TERM_CODE_TYPE_UTF8_VISUAL;

  if (state[OPTION_GEOMETRY].set)
    {
      const char *ptr = state[OPTION_GEOMETRY].arg;
      w = grub_strtoul (ptr, &ptr, 0);
      if (grub_errno)
	return grub_errno;
      if (*ptr != 'x')
	return grub_error (GRUB_ERR_BAD_ARGUMENT,
			   N_("incorrect terminal dimensions specification"));
      ptr++;
      h = grub_strtoul (ptr, &ptr, 0);
      if (grub_errno)
	return grub_errno;
    }

  for (cur = terminfo_outputs; cur;
       cur = ((struct grub_terminfo_output_state *) cur->data)->next)
    if (grub_strcmp (args[0], cur->name) == 0
	|| (grub_strcmp (args[0], "ofconsole") == 0
	    && grub_strcmp ("console", cur->name) == 0))
      {
	cur->flags = (cur->flags & ~GRUB_TERM_CODE_TYPE_MASK) | encoding;

	if (w && h)
	  {
	    struct grub_terminfo_output_state *data
	      = (struct grub_terminfo_output_state *) cur->data;
	    data->size.x = w;
	    data->size.y = h;
	  }

	if (argc == 1)
	  return GRUB_ERR_NONE;

	return grub_terminfo_set_current (cur, args[1]);
      }

  return grub_error (GRUB_ERR_BAD_ARGUMENT,
		     N_("terminal %s isn't found or it's not handled by terminfo"),
		     args[0]);
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(terminfo)
{
  cmd = grub_register_extcmd ("terminfo", grub_cmd_terminfo, 0,
			      N_("[[-a|-u|-v] [-g WxH] [TERM] [TYPE]]"),
			      N_("Set terminfo type of TERM to TYPE.\n"),
			      options);
}

GRUB_MOD_FINI(terminfo)
{
  grub_unregister_extcmd (cmd);
}
