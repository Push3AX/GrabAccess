/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2011,2012,2013  Free Software Foundation, Inc.
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
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/time.h>
#include <grub/speaker.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define BASE_TIME 250
#define DIH 1
#define DAH 3
#define END 0

static const char codes[0x80][6] =
  {
    ['0'] = { DAH, DAH, DAH, DAH, DAH, END },
    ['1'] = { DIH, DAH, DAH, DAH, DAH, END },
    ['2'] = { DIH, DIH, DAH, DAH, DAH, END },
    ['3'] = { DIH, DIH, DIH, DAH, DAH, END },
    ['4'] = { DIH, DIH, DIH, DIH, DAH, END },
    ['5'] = { DIH, DIH, DIH, DIH, DIH, END },
    ['6'] = { DAH, DIH, DIH, DIH, DIH, END },
    ['7'] = { DAH, DAH, DIH, DIH, DIH, END },
    ['8'] = { DAH, DAH, DAH, DIH, DIH, END },
    ['9'] = { DAH, DAH, DAH, DAH, DIH, END },
    ['a'] = { DIH, DAH, END },
    ['b'] = { DAH, DIH, DIH, DIH, END },
    ['c'] = { DAH, DIH, DAH, DIH, END },
    ['d'] = { DAH, DIH, DIH, END },
    ['e'] = { DIH, END },
    ['f'] = { DIH, DIH, DAH, DIH, END },
    ['g'] = { DAH, DAH, DIH, END },
    ['h'] = { DIH, DIH, DIH, DIH, END },
    ['i'] = { DIH, DIH, END },
    ['j'] = { DIH, DAH, DAH, DAH, END },
    ['k'] = { DAH, DIH, DAH, END },
    ['l'] = { DIH, DAH, DIH, DIH, END },
    ['m'] = { DAH, DAH, END },
    ['n'] = { DAH, DIH, END },
    ['o'] = { DAH, DAH, DAH, END },
    ['p'] = { DIH, DAH, DAH, DIH, END },
    ['q'] = { DAH, DAH, DIH, DAH, END },
    ['r'] = { DIH, DAH, DIH, END },
    ['s'] = { DIH, DIH, DIH, END },
    ['t'] = { DAH, END },
    ['u'] = { DIH, DIH, DAH, END },
    ['v'] = { DIH, DIH, DIH, DAH, END },
    ['w'] = { DIH, DAH, DAH, END },
    ['x'] = { DAH, DIH, DIH, DAH, END },
    ['y'] = { DAH, DIH, DAH, DAH, END },
    ['z'] = { DAH, DAH, DIH, DIH, END }
  };

static void
grub_audio_tone (int length)
{
  grub_speaker_beep_on (1000);
  grub_millisleep (length);
  grub_speaker_beep_off ();
}

static void
grub_audio_putchar (struct grub_term_output *term __attribute__ ((unused)),
		    const struct grub_unicode_glyph *c_in)
{
  grub_uint8_t c;
  int i;

  /* For now, do not try to use a surrogate pair.  */
  if (c_in->base > 0x7f)
    c = '?';
  else
    c = grub_tolower (c_in->base);
  for (i = 0; codes[c][i]; i++)
    {
      grub_audio_tone (codes[c][i] * BASE_TIME);
      grub_millisleep (BASE_TIME);
    }
  grub_millisleep (2 * BASE_TIME);
}


static int
dummy (void)
{
  return 0;
}

static struct grub_term_output grub_audio_term_output =
  {
   .name = "morse",
   .init = (void *) dummy,
   .fini = (void *) dummy,
   .putchar = grub_audio_putchar,
   .getwh = (void *) dummy,
   .getxy = (void *) dummy,
   .gotoxy = (void *) dummy,
   .cls = (void *) dummy,
   .setcolorstate = (void *) dummy,
   .setcursor = (void *) dummy,
   .flags = GRUB_TERM_CODE_TYPE_ASCII | GRUB_TERM_DUMB,
   .progress_update_divisor = GRUB_PROGRESS_NO_UPDATE
  };

GRUB_MOD_INIT (morse)
{
  grub_term_register_output ("audio", &grub_audio_term_output);
}

GRUB_MOD_FINI (morse)
{
  grub_term_unregister_output (&grub_audio_term_output);
}
