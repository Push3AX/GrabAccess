/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2012 Free Software Foundation, Inc.
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

GRUB_MOD_LICENSE ("GPLv3+");

#define MSG "cmp test failed"

/* Functional test main method.  */
static void
cmp_test (void)
{
  const char *s1 = "a";
  const char *s2 = "aa";
  const char *s3 = "â";

  grub_test_assert (grub_strlen (s1) == 1, MSG);
  grub_test_assert (grub_strlen (s2) == 2, MSG);
  grub_test_assert (grub_strlen (s3) == 2, MSG);

  grub_test_assert (grub_strcmp (s1, s1) == 0, MSG);
  grub_test_assert (grub_strcmp (s1, s2) < 0, MSG);
  grub_test_assert (grub_strcmp (s1, s3) < 0, MSG);

  grub_test_assert (grub_strcmp (s2, s1) > 0, MSG);
  grub_test_assert (grub_strcmp (s2, s2) == 0, MSG);
  grub_test_assert (grub_strcmp (s2, s3) < 0, MSG);

  grub_test_assert (grub_strcmp (s3, s1) > 0, MSG);
  grub_test_assert (grub_strcmp (s3, s2) > 0, MSG);
  grub_test_assert (grub_strcmp (s3, s3) == 0, MSG);

  grub_test_assert (grub_strcasecmp (s1, s1) == 0, MSG);
  grub_test_assert (grub_strcasecmp (s1, s2) < 0, MSG);
  grub_test_assert (grub_strcasecmp (s1, s3) < 0, MSG);

  grub_test_assert (grub_strcasecmp (s2, s1) > 0, MSG);
  grub_test_assert (grub_strcasecmp (s2, s2) == 0, MSG);
  grub_test_assert (grub_strcasecmp (s2, s3) < 0, MSG);

  grub_test_assert (grub_strcasecmp (s3, s1) > 0, MSG);
  grub_test_assert (grub_strcasecmp (s3, s2) > 0, MSG);
  grub_test_assert (grub_strcasecmp (s3, s3) == 0, MSG);

  grub_test_assert (grub_memcmp (s1, s1, 2) == 0, MSG);
  grub_test_assert (grub_memcmp (s1, s2, 2) < 0, MSG);
  grub_test_assert (grub_memcmp (s1, s3, 2) < 0, MSG);

  grub_test_assert (grub_memcmp (s2, s1, 2) > 0, MSG);
  grub_test_assert (grub_memcmp (s2, s2, 2) == 0, MSG);
  grub_test_assert (grub_memcmp (s2, s3, 2) < 0, MSG);

  grub_test_assert (grub_memcmp (s3, s1, 2) > 0, MSG);
  grub_test_assert (grub_memcmp (s3, s2, 2) > 0, MSG);
  grub_test_assert (grub_memcmp (s3, s3, 2) == 0, MSG);

  grub_test_assert (grub_memcmp (s1, s1, 1) == 0, MSG);
  grub_test_assert (grub_memcmp (s1, s2, 1) == 0, MSG);
  grub_test_assert (grub_memcmp (s1, s3, 1) < 0, MSG);

  grub_test_assert (grub_memcmp (s2, s1, 1) == 0, MSG);
  grub_test_assert (grub_memcmp (s2, s2, 1) == 0, MSG);
  grub_test_assert (grub_memcmp (s2, s3, 1) < 0, MSG);

  grub_test_assert (grub_memcmp (s3, s1, 1) > 0, MSG);
  grub_test_assert (grub_memcmp (s3, s2, 1) > 0, MSG);
  grub_test_assert (grub_memcmp (s3, s3, 1) == 0, MSG);

  grub_test_assert (grub_strncmp (s1, s1, 2) == 0, MSG);
  grub_test_assert (grub_strncmp (s1, s2, 2) < 0, MSG);
  grub_test_assert (grub_strncmp (s1, s3, 2) < 0, MSG);

  grub_test_assert (grub_strncmp (s2, s1, 2) > 0, MSG);
  grub_test_assert (grub_strncmp (s2, s2, 2) == 0, MSG);
  grub_test_assert (grub_strncmp (s2, s3, 2) < 0, MSG);

  grub_test_assert (grub_strncmp (s3, s1, 2) > 0, MSG);
  grub_test_assert (grub_strncmp (s3, s2, 2) > 0, MSG);
  grub_test_assert (grub_strncmp (s3, s3, 2) == 0, MSG);

  grub_test_assert (grub_strncmp (s1, s1, 1) == 0, MSG);
  grub_test_assert (grub_strncmp (s1, s2, 1) == 0, MSG);
  grub_test_assert (grub_strncmp (s1, s3, 1) < 0, MSG);

  grub_test_assert (grub_strncmp (s2, s1, 1) == 0, MSG);
  grub_test_assert (grub_strncmp (s2, s2, 1) == 0, MSG);
  grub_test_assert (grub_strncmp (s2, s3, 1) < 0, MSG);

  grub_test_assert (grub_strncmp (s3, s1, 1) > 0, MSG);
  grub_test_assert (grub_strncmp (s3, s2, 1) > 0, MSG);
  grub_test_assert (grub_strncmp (s3, s3, 1) == 0, MSG);

  grub_test_assert (grub_strncasecmp (s1, s1, 2) == 0, MSG);
  grub_test_assert (grub_strncasecmp (s1, s2, 2) < 0, MSG);
  grub_test_assert (grub_strncasecmp (s1, s3, 2) < 0, MSG);

  grub_test_assert (grub_strncasecmp (s2, s1, 2) > 0, MSG);
  grub_test_assert (grub_strncasecmp (s2, s2, 2) == 0, MSG);
  grub_test_assert (grub_strncasecmp (s2, s3, 2) < 0, MSG);

  grub_test_assert (grub_strncasecmp (s3, s1, 2) > 0, MSG);
  grub_test_assert (grub_strncasecmp (s3, s2, 2) > 0, MSG);
  grub_test_assert (grub_strncasecmp (s3, s3, 2) == 0, MSG);

  grub_test_assert (grub_strncasecmp (s1, s1, 1) == 0, MSG);
  grub_test_assert (grub_strncasecmp (s1, s2, 1) == 0, MSG);
  grub_test_assert (grub_strncasecmp (s1, s3, 1) < 0, MSG);

  grub_test_assert (grub_strncasecmp (s2, s1, 1) == 0, MSG);
  grub_test_assert (grub_strncasecmp (s2, s2, 1) == 0, MSG);
  grub_test_assert (grub_strncasecmp (s2, s3, 1) < 0, MSG);

  grub_test_assert (grub_strncasecmp (s3, s1, 1) > 0, MSG);
  grub_test_assert (grub_strncasecmp (s3, s2, 1) > 0, MSG);
  grub_test_assert (grub_strncasecmp (s3, s3, 1) == 0, MSG);

  grub_test_assert (strlen (s1) == 1, MSG);
  grub_test_assert (strlen (s2) == 2, MSG);
  grub_test_assert (strlen (s3) == 2, MSG);

  grub_test_assert (strcmp (s1, s1) == 0, MSG);
  grub_test_assert (strcmp (s1, s2) < 0, MSG);
  grub_test_assert (strcmp (s1, s3) < 0, MSG);

  grub_test_assert (strcmp (s2, s1) > 0, MSG);
  grub_test_assert (strcmp (s2, s2) == 0, MSG);
  grub_test_assert (strcmp (s2, s3) < 0, MSG);

  grub_test_assert (strcmp (s3, s1) > 0, MSG);
  grub_test_assert (strcmp (s3, s2) > 0, MSG);
  grub_test_assert (strcmp (s3, s3) == 0, MSG);

  grub_test_assert (memcmp (s1, s1, 2) == 0, MSG);
  grub_test_assert (memcmp (s1, s2, 2) < 0, MSG);
  grub_test_assert (memcmp (s1, s3, 2) < 0, MSG);

  grub_test_assert (memcmp (s2, s1, 2) > 0, MSG);
  grub_test_assert (memcmp (s2, s2, 2) == 0, MSG);
  grub_test_assert (memcmp (s2, s3, 2) < 0, MSG);

  grub_test_assert (memcmp (s3, s1, 2) > 0, MSG);
  grub_test_assert (memcmp (s3, s2, 2) > 0, MSG);
  grub_test_assert (memcmp (s3, s3, 2) == 0, MSG);

  grub_test_assert (memcmp (s1, s1, 1) == 0, MSG);
  grub_test_assert (memcmp (s1, s2, 1) == 0, MSG);
  grub_test_assert (memcmp (s1, s3, 1) < 0, MSG);

  grub_test_assert (memcmp (s2, s1, 1) == 0, MSG);
  grub_test_assert (memcmp (s2, s2, 1) == 0, MSG);
  grub_test_assert (memcmp (s2, s3, 1) < 0, MSG);

  grub_test_assert (memcmp (s3, s1, 1) > 0, MSG);
  grub_test_assert (memcmp (s3, s2, 1) > 0, MSG);
  grub_test_assert (memcmp (s3, s3, 1) == 0, MSG);

  grub_test_assert (strncmp (s1, s1, 2) == 0, MSG);
  grub_test_assert (strncmp (s1, s2, 2) < 0, MSG);
  grub_test_assert (strncmp (s1, s3, 2) < 0, MSG);

  grub_test_assert (strncmp (s2, s1, 2) > 0, MSG);
  grub_test_assert (strncmp (s2, s2, 2) == 0, MSG);
  grub_test_assert (strncmp (s2, s3, 2) < 0, MSG);

  grub_test_assert (strncmp (s3, s1, 2) > 0, MSG);
  grub_test_assert (strncmp (s3, s2, 2) > 0, MSG);
  grub_test_assert (strncmp (s3, s3, 2) == 0, MSG);

  grub_test_assert (strncmp (s1, s1, 1) == 0, MSG);
  grub_test_assert (strncmp (s1, s2, 1) == 0, MSG);
  grub_test_assert (strncmp (s1, s3, 1) < 0, MSG);

  grub_test_assert (strncmp (s2, s1, 1) == 0, MSG);
  grub_test_assert (strncmp (s2, s2, 1) == 0, MSG);
  grub_test_assert (strncmp (s2, s3, 1) < 0, MSG);

  grub_test_assert (strncmp (s3, s1, 1) > 0, MSG);
  grub_test_assert (strncmp (s3, s2, 1) > 0, MSG);
  grub_test_assert (strncmp (s3, s3, 1) == 0, MSG);

  grub_test_assert (strncasecmp (s1, s1, 2) == 0, MSG);
  grub_test_assert (strncasecmp (s1, s2, 2) < 0, MSG);
  grub_test_assert (strncasecmp (s1, s3, 2) < 0, MSG);

  grub_test_assert (strncasecmp (s2, s1, 2) > 0, MSG);
  grub_test_assert (strncasecmp (s2, s2, 2) == 0, MSG);
  grub_test_assert (strncasecmp (s2, s3, 2) < 0, MSG);

  grub_test_assert (strncasecmp (s3, s1, 2) > 0, MSG);
  grub_test_assert (strncasecmp (s3, s2, 2) > 0, MSG);
  grub_test_assert (strncasecmp (s3, s3, 2) == 0, MSG);

  grub_test_assert (strncasecmp (s1, s1, 1) == 0, MSG);
  grub_test_assert (strncasecmp (s1, s2, 1) == 0, MSG);
  grub_test_assert (strncasecmp (s1, s3, 1) < 0, MSG);

  grub_test_assert (strncasecmp (s2, s1, 1) == 0, MSG);
  grub_test_assert (strncasecmp (s2, s2, 1) == 0, MSG);
  grub_test_assert (strncasecmp (s2, s3, 1) < 0, MSG);

  grub_test_assert (strncasecmp (s3, s1, 1) > 0, MSG);
  grub_test_assert (strncasecmp (s3, s2, 1) > 0, MSG);
  grub_test_assert (strncasecmp (s3, s3, 1) == 0, MSG);
}

/* Register example_test method as a functional test.  */
GRUB_UNIT_TEST ("cmp_test", cmp_test);
