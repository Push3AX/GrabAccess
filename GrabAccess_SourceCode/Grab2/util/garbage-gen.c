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

/* Standard random generator is slow. For FS testing we need just some
   garbage files, we don't need them to be high-quality random.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

static unsigned long long buffer[1048576];

int
main (int argc, char **argv)
{
  unsigned long long high = 0, low = 1;
  unsigned long i, j;
  unsigned long long cnt = strtoull (argv[1], 0, 0);
  struct timeval tv;
  gettimeofday (&tv, NULL);
  high = tv.tv_sec;
  low = tv.tv_usec;
  if (!high)
    high = 1;
  if (!low)
    low = 2;

  for (j = 0; j < (cnt + sizeof (buffer) - 1)  / sizeof (buffer); j++)
    {
      for (i = 0; i < sizeof (buffer) / sizeof (buffer[0]); i += 2)
	{
	  int c1 = 0, c2 = 0;
	  buffer[i] = low;
	  buffer[i+1] = high;
	  if (low & (1ULL << 63))
	    c1 = 1;
	  low <<= 1;
	  if (high & (1ULL << 63))
	    c2 = 1;
	  high = (high << 1) | c1;
	  if (c2)
	    low ^= 0x87;
	}
      if (sizeof (buffer) < cnt - sizeof (buffer) * j)
	fwrite (buffer, 1, sizeof (buffer), stdout);
      else
	fwrite (buffer, 1, cnt - sizeof (buffer) * j, stdout);
    }
  return 0;
}
