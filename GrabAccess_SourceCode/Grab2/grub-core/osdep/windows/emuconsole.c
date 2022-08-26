/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008,2013  Free Software Foundation, Inc.
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

#include <config.h>
#include <config-util.h>

#include <grub/term.h>
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/err.h>

#include <grub/emu/console.h>

#include <windows.h>

static HANDLE hStdin, hStdout;
static DWORD orig_mode;
static int saved_orig;


static void
grub_console_putchar (struct grub_term_output *term __attribute__ ((unused)),
		      const struct grub_unicode_glyph *c)
{
  TCHAR str[2 + 30];
  unsigned i, j;
  DWORD written;

  /* For now, do not try to use a surrogate pair.  */
  if (c->base > 0xffff)
    str[0] = '?';
  else
    str[0] = (c->base & 0xffff);
  j = 1;
  for (i = 0; i < c->ncomb && j+1 < ARRAY_SIZE (str); i++)
    if (c->base < 0xffff)
      str[j++] = grub_unicode_get_comb (c)[i].code;
  str[j] = 0;

  WriteConsole (hStdout, str, j, &written, NULL);
}

const unsigned windows_codes[] =
  {
    /* 0x21 */ [VK_PRIOR] = GRUB_TERM_KEY_PPAGE,
    /* 0x22 */ [VK_NEXT] = GRUB_TERM_KEY_NPAGE,
    /* 0x23 */ [VK_END] = GRUB_TERM_KEY_END,
    /* 0x24 */ [VK_HOME] = GRUB_TERM_KEY_HOME,
    /* 0x25 */ [VK_LEFT] = GRUB_TERM_KEY_LEFT,
    /* 0x26 */ [VK_UP] = GRUB_TERM_KEY_UP,
    /* 0x27 */ [VK_RIGHT] = GRUB_TERM_KEY_RIGHT,
    /* 0x28 */ [VK_DOWN] = GRUB_TERM_KEY_DOWN,
    /* 0x2e */ [VK_DELETE] = GRUB_TERM_KEY_DC,
    /* 0x70 */ [VK_F1] = GRUB_TERM_KEY_F1,
    /* 0x71 */ [VK_F2] = GRUB_TERM_KEY_F2,
    /* 0x72 */ [VK_F3] = GRUB_TERM_KEY_F3,
    /* 0x73 */ [VK_F4] = GRUB_TERM_KEY_F4,
    /* 0x74 */ [VK_F5] = GRUB_TERM_KEY_F5,
    /* 0x75 */ [VK_F6] = GRUB_TERM_KEY_F6,
    /* 0x76 */ [VK_F7] = GRUB_TERM_KEY_F7,
    /* 0x77 */ [VK_F8] = GRUB_TERM_KEY_F8,
    /* 0x78 */ [VK_F9] = GRUB_TERM_KEY_F9,
    /* 0x79 */ [VK_F10] = GRUB_TERM_KEY_F10,
    /* 0x7a */ [VK_F11] = GRUB_TERM_KEY_F11,
    /* 0x7b */ [VK_F12] = GRUB_TERM_KEY_F12,
  };


static int
grub_console_getkey (struct grub_term_input *term __attribute__ ((unused)))
{
  while (1)
    {
      DWORD nev;
      INPUT_RECORD ir;
      int ret;

      if (!GetNumberOfConsoleInputEvents (hStdin, &nev))
	return GRUB_TERM_NO_KEY;

      if (nev == 0)
	return GRUB_TERM_NO_KEY;

      if (!ReadConsoleInput (hStdin, &ir, 1,
			     &nev))
	return GRUB_TERM_NO_KEY;

      if (ir.EventType != KEY_EVENT)
	continue;

      if (!ir.Event.KeyEvent.bKeyDown)
	continue;
      ret = ir.Event.KeyEvent.uChar.UnicodeChar;
      if (ret == 0)
	{
	  unsigned kc = ir.Event.KeyEvent.wVirtualKeyCode;
	  if (kc < ARRAY_SIZE (windows_codes) && windows_codes[kc])
	    ret = windows_codes[kc];
	  else
	    continue;
	  if (ir.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED)
	    ret |= GRUB_TERM_SHIFT;
	}
      /* Workaround for AltGr bug.  */
      if (ir.Event.KeyEvent.dwControlKeyState & RIGHT_ALT_PRESSED)
	return ret;
      if (ir.Event.KeyEvent.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
	ret |= GRUB_TERM_ALT;
      if (ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
	ret |= GRUB_TERM_CTRL;
      return ret;
    }
}

static struct grub_term_coordinate
grub_console_getwh (struct grub_term_output *term __attribute__ ((unused)))
{
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  csbi.dwSize.X = 80;
  csbi.dwSize.Y = 25;

  GetConsoleScreenBufferInfo (hStdout, &csbi);

  return (struct grub_term_coordinate) { csbi.dwSize.X, csbi.dwSize.Y };
}

static struct grub_term_coordinate
grub_console_getxy (struct grub_term_output *term __attribute__ ((unused)))
{
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  GetConsoleScreenBufferInfo (hStdout, &csbi);

  return (struct grub_term_coordinate) { csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y };
}

static void
grub_console_gotoxy (struct grub_term_output *term __attribute__ ((unused)),
		     struct grub_term_coordinate pos)
{
  COORD coord = { pos.x, pos.y };

  SetConsoleCursorPosition (hStdout, coord);
}

static void
grub_console_cls (struct grub_term_output *term)
{
  int tsz;
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  struct grub_unicode_glyph c =
    {
      .base = ' ',
      .variant = 0,
      .attributes = 0,
      .ncomb = 0,
      .estimated_width = 1
    };

  GetConsoleScreenBufferInfo (hStdout, &csbi);

  SetConsoleTextAttribute (hStdout, 0);
  grub_console_gotoxy (term, (struct grub_term_coordinate) { 0, 0 });
  tsz = csbi.dwSize.X * csbi.dwSize.Y;

  while (tsz--)
    grub_console_putchar (term, &c);

  grub_console_gotoxy (term, (struct grub_term_coordinate) { 0, 0 });
  SetConsoleTextAttribute (hStdout, csbi.wAttributes);
}

static void
grub_console_setcolorstate (struct grub_term_output *term
			    __attribute__ ((unused)),
			    grub_term_color_state state)
{


  switch (state) {
    case GRUB_TERM_COLOR_STANDARD:
      SetConsoleTextAttribute (hStdout, GRUB_TERM_DEFAULT_STANDARD_COLOR
			       & 0x7f);
      break;
    case GRUB_TERM_COLOR_NORMAL:
      SetConsoleTextAttribute (hStdout, grub_term_normal_color & 0x7f);
      break;
    case GRUB_TERM_COLOR_HIGHLIGHT:
      SetConsoleTextAttribute (hStdout, grub_term_highlight_color & 0x7f);
      break;
    default:
      break;
  }
}

static void
grub_console_setcursor (struct grub_term_output *term __attribute__ ((unused)),
			int on)
{
  CONSOLE_CURSOR_INFO ci;
  ci.dwSize = 5;
  ci.bVisible = on;
  SetConsoleCursorInfo (hStdout, &ci);
}

static grub_err_t
grub_efi_console_init (struct grub_term_output *term)
{
  grub_console_setcursor (term, 1);
  return 0;
}

static grub_err_t
grub_efi_console_fini (struct grub_term_output *term)
{
  grub_console_setcursor (term, 1);
  return 0;
}


static grub_err_t
grub_console_init_input (struct grub_term_input *term)
{
  if (!saved_orig)
    {
      GetConsoleMode (hStdin, &orig_mode);
    }

  saved_orig = 1;

  SetConsoleMode (hStdin, orig_mode & ~ENABLE_ECHO_INPUT
		  & ~ENABLE_LINE_INPUT & ~ENABLE_PROCESSED_INPUT);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_console_fini_input (struct grub_term_input *term
		       __attribute__ ((unused)))
{
  SetConsoleMode (hStdin, orig_mode);
  saved_orig = 0;
  return GRUB_ERR_NONE;
}


static struct grub_term_input grub_console_term_input =
  {
    .name = "console",
    .getkey = grub_console_getkey,
    .init = grub_console_init_input,
    .fini = grub_console_fini_input,
  };

static struct grub_term_output grub_console_term_output =
  {
    .name = "console",
    .init = grub_efi_console_init,
    .fini = grub_efi_console_fini,
    .putchar = grub_console_putchar,
    .getwh = grub_console_getwh,
    .getxy = grub_console_getxy,
    .gotoxy = grub_console_gotoxy,
    .cls = grub_console_cls,
    .setcolorstate = grub_console_setcolorstate,
    .setcursor = grub_console_setcursor,
    .flags = GRUB_TERM_CODE_TYPE_VISUAL_GLYPHS,
    .progress_update_divisor = GRUB_PROGRESS_FAST
  };

void
grub_console_init (void)
{
  hStdin = GetStdHandle (STD_INPUT_HANDLE);
  hStdout = GetStdHandle (STD_OUTPUT_HANDLE);

  grub_term_register_input ("console", &grub_console_term_input);
  grub_term_register_output ("console", &grub_console_term_output);
}

void
grub_console_fini (void)
{
  if (saved_orig)
    {
      SetConsoleMode (hStdin, orig_mode);
      saved_orig = 0;
    }
  grub_term_unregister_input (&grub_console_term_input);
  grub_term_unregister_output (&grub_console_term_output);
}
