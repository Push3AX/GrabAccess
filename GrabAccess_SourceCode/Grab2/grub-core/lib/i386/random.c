/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2016 Free Software Foundation, Inc.
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

#include <grub/random.h>
#include <grub/i386/io.h>
#include <grub/i386/tsc.h>
#include <grub/i386/pmtimer.h>
#include <grub/acpi.h>

static int have_tsc = -1, have_pmtimer = -1;
static grub_port_t pmtimer_port;

static int
detect_pmtimer (void)
{
  struct grub_acpi_fadt *fadt;
  fadt = grub_acpi_find_fadt ();
  if (!fadt)
    return 0;
  pmtimer_port = fadt->pmtimer;
  if (!pmtimer_port)
    return 0;
  return 1;
}

static int
pmtimer_tsc_get_random_bit (void)
{
  /* It's hard to come up with figures about pmtimer and tsc jitter but
     50 ppm seems to be typical. So we need 10^6/50 tsc cycles to get drift
     of one tsc cycle. With TSC at least of 800 MHz it means 1/(50*800)
     = 1/40000 s or about 3579545 / 40000 = 90 pmtimer ticks.
     This gives us rate of 40000 bit/s or 5 kB/s.
   */
  grub_uint64_t tsc_diff;
  tsc_diff = grub_pmtimer_wait_count_tsc (pmtimer_port, 90);
  if (tsc_diff == 0)
    {
      have_pmtimer = 0;
      return -1;
    }
  return tsc_diff & 1;
}

static int
pmtimer_tsc_get_random_byte (void)
{
  grub_uint8_t ret = 0;
  int i, c;
  for (i = 0; i < 8; i++)
    {
      c = pmtimer_tsc_get_random_bit ();
      if (c < 0)
	return -1;
      ret |= c << i;
    }
  return ret;
}

static int
pmtimer_fill_buffer (void *buffer, grub_size_t sz)
{
  grub_uint8_t *p = buffer;
  int c;
  while (sz)
    {
      c = pmtimer_tsc_get_random_byte ();
      if (c < 0)
	return 0;
      *p++ = c;
      sz--;
    }
  return 1;
}

int
grub_crypto_arch_get_random (void *buffer, grub_size_t sz)
{
  if (have_tsc == -1)
    have_tsc = grub_cpu_is_tsc_supported ();
  if (!have_tsc)
    return 0;
  if (have_pmtimer == -1)
    have_pmtimer = detect_pmtimer ();
  if (!have_pmtimer)
    return 0;
  return pmtimer_fill_buffer (buffer, sz);
}
