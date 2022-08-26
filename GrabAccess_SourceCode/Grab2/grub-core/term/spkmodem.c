/*  console.c -- Open Firmware console for GRUB.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2004,2005,2007,2008,2009  Free Software Foundation, Inc.
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
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/time.h>
#include <grub/terminfo.h>
#include <grub/dl.h>
#include <grub/speaker.h>

GRUB_MOD_LICENSE ("GPLv3+");

extern struct grub_terminfo_output_state grub_spkmodem_terminfo_output;

static void
make_tone (grub_uint16_t freq_count, unsigned int duration)
{
  /* Program timer 2.  */
  grub_outb (GRUB_PIT_CTRL_SELECT_2
	     | GRUB_PIT_CTRL_READLOAD_WORD
	     | GRUB_PIT_CTRL_SQUAREWAVE_GEN
	     | GRUB_PIT_CTRL_COUNT_BINARY, GRUB_PIT_CTRL);
  grub_outb (freq_count & 0xff, GRUB_PIT_COUNTER_2);		/* LSB */
  grub_outb ((freq_count >> 8) & 0xff, GRUB_PIT_COUNTER_2);	/* MSB */

  /* Start speaker.  */
  grub_outb (grub_inb (GRUB_PIT_SPEAKER_PORT)
	     | GRUB_PIT_SPK_TMR2 | GRUB_PIT_SPK_DATA,
	     GRUB_PIT_SPEAKER_PORT);

  for (; duration; duration--)
    {
      unsigned short counter, previous_counter = 0xffff;
      while (1)
	{
	  counter = grub_inb (GRUB_PIT_COUNTER_2);
	  counter |= ((grub_uint16_t) grub_inb (GRUB_PIT_COUNTER_2)) << 8;
	  if (counter > previous_counter)
	    {
	      previous_counter = counter;
	      break;
	    }
	  previous_counter = counter;
	}
    }
}

static int inited;

static void
put (struct grub_term_output *term __attribute__ ((unused)), const int c)
{
  int i;

  make_tone (GRUB_SPEAKER_PIT_FREQUENCY / 200, 4);
  for (i = 7; i >= 0; i--)
    {
      if ((c >> i) & 1)
	make_tone (GRUB_SPEAKER_PIT_FREQUENCY / 2000, 20);
      else
	make_tone (GRUB_SPEAKER_PIT_FREQUENCY / 4000, 40);
      make_tone (GRUB_SPEAKER_PIT_FREQUENCY / 1000, 10);
    }
  make_tone (GRUB_SPEAKER_PIT_FREQUENCY / 200, 0);
}

static grub_err_t
grub_spkmodem_init_output (struct grub_term_output *term)
{
  /* Some models shutdown sound when not in use and it takes for it
     around 30 ms to come back on which loses 3 bits. So generate a base
     200 Hz continously. */

  make_tone (GRUB_SPEAKER_PIT_FREQUENCY / 200, 0);
  grub_terminfo_output_init (term);

  return 0;
}

static grub_err_t
grub_spkmodem_fini_output (struct grub_term_output *term __attribute__ ((unused)))
{
  grub_speaker_beep_off ();
  inited = 0;
  return 0;
}




struct grub_terminfo_output_state grub_spkmodem_terminfo_output =
  {
    .put = put,
    .size = { 80, 24 }
  };

static struct grub_term_output grub_spkmodem_term_output =
  {
    .name = "spkmodem",
    .init = grub_spkmodem_init_output,
    .fini = grub_spkmodem_fini_output,
    .putchar = grub_terminfo_putchar,
    .getxy = grub_terminfo_getxy,
    .getwh = grub_terminfo_getwh,
    .gotoxy = grub_terminfo_gotoxy,
    .cls = grub_terminfo_cls,
    .setcolorstate = grub_terminfo_setcolorstate,
    .setcursor = grub_terminfo_setcursor,
    .flags = GRUB_TERM_CODE_TYPE_ASCII,
    .data = &grub_spkmodem_terminfo_output,
    .progress_update_divisor = GRUB_PROGRESS_NO_UPDATE
  };

GRUB_MOD_INIT (spkmodem)
{
  grub_term_register_output ("spkmodem", &grub_spkmodem_term_output);
}


GRUB_MOD_FINI (spkmodem)
{
  grub_term_unregister_output (&grub_spkmodem_term_output);
}
