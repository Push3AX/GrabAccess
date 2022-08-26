/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2011 Free Software Foundation, Inc.
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
#include <string.h>
#include <grub/test.h>
#include <grub/misc.h>

#define MSG "printf test failed: %s, %s", real, expected

#if defined(__GNUC__) && __GNUC__ >= 7
#pragma GCC diagnostic ignored "-Wformat-truncation="
#endif

static void
printf_test (void)
{
  char real[512];
  char expected[512];
  char *null = NULL;

  grub_snprintf (real, sizeof (real), "%s", null);
  snprintf (expected, sizeof (expected), "%s", null);
  grub_test_assert (strcmp (real, expected) == 0, MSG);

  grub_snprintf (real, sizeof (real), "%10s", null);
  snprintf (expected, sizeof (expected), "%10s", null);
  grub_test_assert (strcmp (real, expected) == 0, MSG);

  grub_snprintf (real, sizeof (real), "%-10s", null);
  snprintf (expected, sizeof (expected), "%-10s", null);
  grub_test_assert (strcmp (real, expected) == 0, MSG);

  grub_snprintf (real, sizeof (real), "%d%%", 10);
  snprintf (expected, sizeof (expected), "%d%%", 10);
  grub_test_assert (strcmp (real, expected) == 0, MSG);

  grub_snprintf (real, sizeof (real), "%d %%", 10);
  snprintf (expected, sizeof (expected), "%d %%", 10);
  grub_test_assert (strcmp (real, expected) == 0, MSG);

  grub_snprintf (real, sizeof (real), "%%");
  snprintf (expected, sizeof (expected), "%%");
  grub_test_assert (strcmp (real, expected) == 0, MSG);

  grub_snprintf (real, sizeof (real), "%d %d %d", 1, 2, 3);
  snprintf (expected, sizeof (expected), "%d %d %d", 1, 2, 3);
  grub_test_assert (strcmp (real, expected) == 0, MSG);
  grub_snprintf (real, sizeof (real), "%3$d %2$d %1$d", 1, 2, 3);
  snprintf (expected, sizeof (expected), "%3$d %2$d %1$d", 1, 2, 3);
  grub_test_assert (strcmp (real, expected) == 0, MSG);
  grub_snprintf (real, sizeof (real), "%d %lld %d", 1, 2LL, 3);
  snprintf (expected, sizeof (expected), "%d %lld %d", 1, 2LL, 3);
  grub_test_assert (strcmp (real, expected) == 0, MSG);
  grub_snprintf (real, sizeof (real), "%3$d %2$lld %1$d", 1, 2LL, 3);
  snprintf (expected, sizeof (expected), "%3$d %2$lld %1$d", 1, 2LL, 3);
  grub_test_assert (strcmp (real, expected) == 0, MSG);
  grub_snprintf (real, sizeof (real), "%%0%dd ", 1);
  snprintf (expected, sizeof (expected), "%%0%dd ", 1);
  grub_test_assert (strcmp (real, expected) == 0, MSG);
}

GRUB_UNIT_TEST ("printf_unit_test", printf_test);
