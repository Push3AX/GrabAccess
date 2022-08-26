/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013 Free Software Foundation, Inc.
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

#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#include <grub/misc.h>
#include <grub/datetime.h>
#include <grub/test.h>

static void
date_test (grub_int32_t v)
{
  struct grub_datetime dt;
  time_t t = v;
  struct tm *g;
  int w;

  g = gmtime (&t);

  grub_unixtime2datetime (v, &dt);

  w = grub_get_weekday (&dt);

  grub_test_assert (g->tm_sec == dt.second, "time %d bad second: %d vs %d", v,
		    g->tm_sec, dt.second);
  grub_test_assert (g->tm_min == dt.minute, "time %d bad minute: %d vs %d", v,
		    g->tm_min, dt.minute);
  grub_test_assert (g->tm_hour == dt.hour, "time %d bad hour: %d vs %d", v,
		    g->tm_hour, dt.hour);
  grub_test_assert (g->tm_mday == dt.day, "time %d bad day: %d vs %d", v,
		    g->tm_mday, dt.day);
  grub_test_assert (g->tm_mon + 1 == dt.month, "time %d bad month: %d vs %d", v,
		    g->tm_mon + 1, dt.month);
  grub_test_assert (g->tm_year + 1900 == dt.year,
		    "time %d bad year: %d vs %d", v,
		    g->tm_year + 1900, dt.year);
  grub_test_assert (g->tm_wday == w, "time %d bad week day: %d vs %d", v,
		    g->tm_wday, w);
}

static void
date_test_iter (void)
{
  grub_int32_t tests[] = { -1, 0, +1, -2133156255, GRUB_INT32_MIN,
			   GRUB_INT32_MAX };
  unsigned i;

  for (i = 0; i < ARRAY_SIZE (tests); i++)
    date_test (tests[i]);
  srand (42);
  for (i = 0; i < 1000000; i++)
    {
      grub_int32_t x = rand ();
      date_test (x);
      date_test (-x);
    }
}

GRUB_UNIT_TEST ("date_unit_test", date_test_iter);
