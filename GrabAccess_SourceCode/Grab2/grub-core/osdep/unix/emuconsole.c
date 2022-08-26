/*  console.c -- console for GRUB.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/time.h>
#include <grub/terminfo.h>
#include <grub/dl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <langinfo.h>

#include <grub/emu/console.h>

extern struct grub_terminfo_output_state grub_console_terminfo_output;
static int original_fl;
static int saved_orig;
static struct termios orig_tty;
static struct termios new_tty;

static void
put (struct grub_term_output *term __attribute__ ((unused)), const int c)
{
  char chr = c;
  ssize_t actual;

  actual = write (STDOUT_FILENO, &chr, 1);
  if (actual < 1)
    {
      /* We cannot do anything about this, but some systems require us to at
	 least pretend to check the result.  */
    }
}

static int
readkey (struct grub_term_input *term __attribute__ ((unused)))
{
  grub_uint8_t c;
  ssize_t actual;

  actual = read (STDIN_FILENO, &c, 1);
  if (actual > 0)
    return c;
  return -1;
}

static grub_err_t
grub_console_init_input (struct grub_term_input *term)
{
  if (!saved_orig)
    {
      original_fl = fcntl (STDIN_FILENO, F_GETFL);
      fcntl (STDIN_FILENO, F_SETFL, original_fl | O_NONBLOCK);
    }

  saved_orig = 1;

  tcgetattr(STDIN_FILENO, &orig_tty);
  new_tty = orig_tty;
  new_tty.c_lflag &= ~(ICANON | ECHO);
  new_tty.c_cc[VMIN] = 1;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tty);

  return grub_terminfo_input_init (term);
}

static grub_err_t
grub_console_fini_input (struct grub_term_input *term
		       __attribute__ ((unused)))
{
  fcntl (STDIN_FILENO, F_SETFL, original_fl);
  tcsetattr(STDIN_FILENO, TCSANOW, &orig_tty);
  saved_orig = 0;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_console_init_output (struct grub_term_output *term)
{
  struct winsize size;
  if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &size) >= 0)
    {
      grub_console_terminfo_output.size.x = size.ws_col;
      grub_console_terminfo_output.size.y = size.ws_row;
    }
  else
    {
      grub_console_terminfo_output.size.x = 80;
      grub_console_terminfo_output.size.y = 24;
    }

  grub_terminfo_output_init (term);

  return 0;
}



struct grub_terminfo_input_state grub_console_terminfo_input =
  {
    .readkey = readkey
  };

struct grub_terminfo_output_state grub_console_terminfo_output =
  {
    .put = put,
    .size = { 80, 24 }
  };

static struct grub_term_input grub_console_term_input =
  {
    .name = "console",
    .init = grub_console_init_input,
    .fini = grub_console_fini_input,
    .getkey = grub_terminfo_getkey,
    .data = &grub_console_terminfo_input
  };

static struct grub_term_output grub_console_term_output =
  {
    .name = "console",
    .init = grub_console_init_output,
    .putchar = grub_terminfo_putchar,
    .getxy = grub_terminfo_getxy,
    .getwh = grub_terminfo_getwh,
    .gotoxy = grub_terminfo_gotoxy,
    .cls = grub_terminfo_cls,
    .setcolorstate = grub_terminfo_setcolorstate,
    .setcursor = grub_terminfo_setcursor,
    .data = &grub_console_terminfo_output,
    .progress_update_divisor = GRUB_PROGRESS_FAST
  };

void
grub_console_init (void)
{
  const char *cs = nl_langinfo (CODESET);
  if (cs && grub_strcasecmp (cs, "UTF-8"))
    grub_console_term_output.flags = GRUB_TERM_CODE_TYPE_UTF8_LOGICAL;
  else
    grub_console_term_output.flags = GRUB_TERM_CODE_TYPE_ASCII;
  grub_term_register_input ("console", &grub_console_term_input);
  grub_term_register_output ("console", &grub_console_term_output);
  grub_terminfo_init ();
  grub_terminfo_output_register (&grub_console_term_output, "vt100-color");
}

void
grub_console_fini (void)
{
  if (saved_orig)
    {
      fcntl (STDIN_FILENO, F_SETFL, original_fl);
      tcsetattr(STDIN_FILENO, TCSANOW, &orig_tty);
    }
  saved_orig = 0;
}
