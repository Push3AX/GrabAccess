/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2006
 *                2007, 2008, 2009, 2010, 2011, 2012, 2013  Free Software Foundation, Inc.
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

#include <grub/crypto.h>
#include <grub/mm.h>
#include <grub/term.h>

#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int
grub_password_get (char buf[], unsigned buf_size)
{
  FILE *in;
  struct termios s, t;
  int tty_changed = 0;
  char *ptr;

  grub_refresh ();

  /* Disable echoing. Based on glibc.  */
  in = fopen ("/dev/tty", "w+c");
  if (in == NULL)
    in = stdin;

  if (tcgetattr (fileno (in), &t) == 0)
    {
      /* Save the old one. */
      s = t;
      /* Tricky, tricky. */
      t.c_lflag &= ~(ECHO|ISIG);
      tty_changed = (tcsetattr (fileno (in), TCSAFLUSH, &t) == 0);
    }
  else
    tty_changed = 0;
  grub_memset (buf, 0, buf_size);
  if (!fgets (buf, buf_size, stdin))
    {
      if (in != stdin)
	fclose (in);
      return 0;
    }
  ptr = buf + strlen (buf) - 1;
  while (buf <= ptr && (*ptr == '\n' || *ptr == '\r'))
    *ptr-- = 0;
  /* Restore the original setting.  */
  if (tty_changed)
    (void) tcsetattr (fileno (in), TCSAFLUSH, &s);

  grub_xputs ("\n");
  grub_refresh ();

  if (in != stdin)
    fclose (in);

  return 1;
}
