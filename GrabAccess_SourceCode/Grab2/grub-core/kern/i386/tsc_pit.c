/* kern/i386/tsc.c - x86 TSC time source implementation
 * Requires Pentium or better x86 CPU that supports the RDTSC instruction.
 * This module uses the PIT to calibrate the TSC to
 * real time.
 *
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008  Free Software Foundation, Inc.
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

#include <grub/types.h>
#include <grub/time.h>
#include <grub/misc.h>
#include <grub/i386/tsc.h>
#include <grub/i386/pit.h>
#include <grub/cpu/io.h>

static int
grub_pit_wait (void)
{
  int ret = 0;

  /* Disable timer2 gate and speaker.  */
  grub_outb (grub_inb (GRUB_PIT_SPEAKER_PORT)
	     & ~ (GRUB_PIT_SPK_DATA | GRUB_PIT_SPK_TMR2),
             GRUB_PIT_SPEAKER_PORT);

  /* Set tics.  */
  grub_outb (GRUB_PIT_CTRL_SELECT_2 | GRUB_PIT_CTRL_READLOAD_WORD,
	     GRUB_PIT_CTRL);
  /* 0xffff ticks: 55ms. */
  grub_outb (0xff, GRUB_PIT_COUNTER_2);
  grub_outb (0xff, GRUB_PIT_COUNTER_2);

  /* Enable timer2 gate, keep speaker disabled.  */
  grub_outb ((grub_inb (GRUB_PIT_SPEAKER_PORT) & ~ GRUB_PIT_SPK_DATA)
	     | GRUB_PIT_SPK_TMR2,
             GRUB_PIT_SPEAKER_PORT);

  if ((grub_inb (GRUB_PIT_SPEAKER_PORT) & GRUB_PIT_SPK_TMR2_LATCH) == 0x00) {
    ret = 1;
    /* Wait.  */
    while ((grub_inb (GRUB_PIT_SPEAKER_PORT) & GRUB_PIT_SPK_TMR2_LATCH) == 0x00);
  }

  /* Disable timer2 gate and speaker.  */
  grub_outb (grub_inb (GRUB_PIT_SPEAKER_PORT)
	     & ~ (GRUB_PIT_SPK_DATA | GRUB_PIT_SPK_TMR2),
             GRUB_PIT_SPEAKER_PORT);

  return ret;
}

/* Calibrate the TSC based on the RTC.  */
int
grub_tsc_calibrate_from_pit (void)
{
  /* First calibrate the TSC rate (relative, not absolute time). */
  grub_uint64_t start_tsc, end_tsc;

  start_tsc = grub_get_tsc ();
  if (!grub_pit_wait ())
    return 0;
  end_tsc = grub_get_tsc ();

  grub_tsc_rate = 0;
  if (end_tsc > start_tsc)
    grub_tsc_rate = grub_divmod64 ((55ULL << 32), end_tsc - start_tsc, 0);
  if (grub_tsc_rate == 0)
    return 0;
  return 1;
}
