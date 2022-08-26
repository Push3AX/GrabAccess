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
#include <grub/i386/pmtimer.h>
#include <grub/acpi.h>
#include <grub/cpu/io.h>

/*
 * Define GRUB_PMTIMER_IGNORE_BAD_READS if you're trying to test a timer that's
 * present but doesn't keep time well.
 */
// #define GRUB_PMTIMER_IGNORE_BAD_READS

grub_uint64_t
grub_pmtimer_wait_count_tsc (grub_port_t pmtimer,
			     grub_uint16_t num_pm_ticks)
{
  grub_uint32_t start;
  grub_uint64_t cur, end;
  grub_uint64_t start_tsc;
  grub_uint64_t end_tsc;
  unsigned int num_iter = 0;
#ifndef GRUB_PMTIMER_IGNORE_BAD_READS
  int bad_reads = 0;
#endif

  /*
   * Some timers are 24-bit and some are 32-bit, but it doesn't make much
   * difference to us.  Caring which one we have isn't really worth it since
   * the low-order digits will give us enough data to calibrate TSC.  So just
   * mask the top-order byte off.
   */
  cur = start = grub_inl (pmtimer) & 0xffffffUL;
  end = start + num_pm_ticks;
  start_tsc = grub_get_tsc ();
  while (1)
    {
      cur &= 0xffffffffff000000ULL;
      cur |= grub_inl (pmtimer) & 0xffffffUL;

      end_tsc = grub_get_tsc();

#ifndef GRUB_PMTIMER_IGNORE_BAD_READS
      /*
       * If we get 10 reads in a row that are obviously dead pins, there's no
       * reason to do this thousands of times.
       */
      if (cur == 0xffffffUL || cur == 0)
	{
	  bad_reads++;
	  grub_dprintf ("pmtimer",
			"pmtimer: 0x%"PRIxGRUB_UINT64_T" bad_reads: %d\n",
			cur, bad_reads);
	  grub_dprintf ("pmtimer", "timer is broken; giving up.\n");

	  if (bad_reads == 10)
	    return 0;
	}
#endif

      if (cur < start)
	cur += 0x1000000;

      if (cur >= end)
	{
	  grub_dprintf ("pmtimer", "pmtimer delta is 0x%"PRIxGRUB_UINT64_T"\n",
			cur - start);
	  grub_dprintf ("pmtimer", "tsc delta is 0x%"PRIxGRUB_UINT64_T"\n",
			end_tsc - start_tsc);
	  return end_tsc - start_tsc;
	}

      /*
       * Check for broken PM timer.  1ms at 10GHz should be 1E+7 TSCs; at
       * 250MHz it should be 2.5E6.  So if after 4E+7 TSCs on a 10GHz machine,
       * we should have seen pmtimer show 4ms of change (i.e. cur =~
       * start+14320); on a 250MHz machine that should be 16ms (start+57280).
       * If after this a time we still don't have 1ms on pmtimer, then pmtimer
       * is broken.
       *
       * Likewise, if our code is perfectly efficient and introduces no delays
       * whatsoever, on a 10GHz system we should see a TSC delta of 3580 in
       * ~3580 iterations.  On a 250MHz machine that should be ~900 iterations.
       *
       * With those factors in mind, there are two limits here.  There's a hard
       * limit here at 8x our desired pm timer delta, picked as an arbitrarily
       * large value that's still not a lot of time to humans, because if we
       * get that far this is either an implausibly fast machine or the pmtimer
       * is not running.  And there's another limit on 4x our 10GHz tsc delta
       * without seeing cur converge on our target value.
       */
      if ((++num_iter > (grub_uint32_t)num_pm_ticks << 3UL) ||
	  end_tsc - start_tsc > 40000000)
	{
	  grub_dprintf ("pmtimer",
			"pmtimer delta is 0x%"PRIxGRUB_UINT64_T" (%u iterations)\n",
			cur - start, num_iter);
	  grub_dprintf ("pmtimer",
			"tsc delta is implausible: 0x%"PRIxGRUB_UINT64_T"\n",
			end_tsc - start_tsc);
	  return 0;
	}
    }
}

int
grub_tsc_calibrate_from_pmtimer (void)
{
  struct grub_acpi_fadt *fadt;
  grub_port_t pmtimer;
  grub_uint64_t tsc_diff;

  fadt = grub_acpi_find_fadt ();
  if (!fadt)
    {
      grub_dprintf ("pmtimer", "No FADT found; not using pmtimer.\n");
      return 0;
    }
  pmtimer = fadt->pmtimer;
  if (!pmtimer)
    {
      grub_dprintf ("pmtimer", "FADT does not specify pmtimer; skipping.\n");
      return 0;
    }

  /*
   * It's 3.579545 MHz clock. Wait 1 ms.
   */
  tsc_diff = grub_pmtimer_wait_count_tsc (pmtimer, 3580);
  if (tsc_diff == 0)
    return 0;
  grub_tsc_rate = grub_divmod64 ((1ULL << 32), tsc_diff, 0);
  return 1;
}
