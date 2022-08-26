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

#include <grub/time.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/env.h>
#include <grub/test.h>
#include <grub/mm.h>
#include <grub/datetime.h>
#include <grub/time.h>

GRUB_MOD_LICENSE ("GPLv3+");

static void
sleep_test (void)
{
  struct grub_datetime st, en;
  grub_int64_t stu = 0, enu = 0;
  int is_delayok;
  grub_test_assert (!grub_get_datetime (&st), "Couldn't retrieve start time");
  grub_millisleep (10000);
  grub_test_assert (!grub_get_datetime (&en), "Couldn't retrieve end time");
  grub_test_assert (grub_datetime2unixtime (&st, &stu), "Invalid date");
  grub_test_assert (grub_datetime2unixtime (&en, &enu), "Invalid date");
  is_delayok = (enu - stu >= 9 && enu - stu <= 11);
#ifdef __arm__
  /* Ignore QEMU bug */
  if (enu - stu >= 15 && enu - stu <= 17)
    is_delayok = 1;
#endif
  grub_test_assert (is_delayok, "Interval out of range: %" PRIdGRUB_INT64_T, enu - stu);
}

GRUB_FUNCTIONAL_TEST (sleep_test, sleep_test);
