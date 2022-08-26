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

#include <grub/test.h>
#include <grub/dl.h>
#include <grub/misc.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_uint64_t vectors[][2] = {
  { 0xffffffffffffffffULL, 1},
  { 1, 0xffffffffffffffffULL},
  { 0xffffffffffffffffULL, 0xffffffffffffffffULL},
  { 1, 1 },
  { 2, 1 }
};

static void
test64(grub_uint64_t a, grub_uint64_t b)
{
  grub_uint64_t r1 = a * b, r2 = 0, r3;
  int i;
  for (i = 0; i < 64; i++)
    if ((a & (1LL << i)))
      r2 += b << i;
  r3 = ((grub_int64_t) a) * ((grub_int64_t) b);
  grub_test_assert (r1 == r2,
		    "multiplication mismatch (u): 0x%llx x 0x%llx = 0x%llx != 0x%llx",
		    (long long) a, (long long) b, (long long) r2, (long long) r1);
  grub_test_assert (r3 == r2,
		    "multiplication mismatch (s): 0x%llx x 0x%llx = 0x%llx != 0x%llx",
		    (long long) a, (long long) b, (long long) r2, (long long) r3);
}

static void
mul_test (void)
{
  grub_uint64_t a = 404, b = 7;
  grub_size_t i;

  for (i = 0; i < ARRAY_SIZE (vectors); i++)
    {
      test64 (vectors[i][0], vectors[i][1]);
    }
  for (i = 0; i < 40000; i++)
    {
      a = 17 * a + 13 * b;
      b = 23 * a + 29 * b;
      if (b == 0)
	b = 1;
      if (a == 0)
	a = 1;
      test64 (a, b);
    }
}

/* Register example_test method as a functional test.  */
GRUB_FUNCTIONAL_TEST (mul_test, mul_test);
