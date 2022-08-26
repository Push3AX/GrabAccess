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

/* ull version is not used on i386 other than in this test.
   Avoid requiring extra function.
  */
#if defined (__i386__)
#define SKIP_ULL 1
#endif

static grub_uint64_t vectors[] = {
  0xffffffffffffffffULL, 1, 2, 0, 0x0102030405060708ULL
};

static void
test_ui (unsigned int a)
{
  int i;
  a |= 1;
  for (i = 0; i < (int) (8 * sizeof (a)); i++)
    {
      grub_test_assert (__builtin_ctz(a << i) == i,
			"ctz mismatch: ctz(0x%llx) != 0x%x",
			(long long) (a << i), __builtin_ctz(a << i));
    }
}

static void
test_ul (unsigned long a)
{
  int i;
  a |= 1;
  for (i = 0; i < (int) (8 * sizeof (a)); i++)
    {
      grub_test_assert (__builtin_ctzl(a << i) == i,
			"ctzl mismatch: ctzl(0x%llx) != 0x%x",
			(long long) (a << i), __builtin_ctz(a << i));
    }
}

#ifndef SKIP_ULL
static void
test_ull (unsigned long long a)
{
  int i;
  a |= 1;
  for (i = 0; i < (int) (8 * sizeof (a)); i++)
    {
      grub_test_assert (__builtin_ctzll(a << i) == i,
			"ctzll mismatch: ctzll(0x%llx) != 0x%x",
			(long long) (a << i), __builtin_ctz(a << i));
    }
}
#endif

static void
test_all(grub_uint64_t a)
{
  test_ui (a);
  test_ul (a);
#ifndef SKIP_ULL
  test_ull (a);
#endif
}

static void
ctz_test (void)
{
  grub_uint64_t a = 404, b = 7;
  grub_size_t i;

  for (i = 0; i < ARRAY_SIZE (vectors); i++)
    {
      test_all (vectors[i]);
    }
  for (i = 0; i < 40000; i++)
    {
      a = 17 * a + 13 * b;
      b = 23 * a + 29 * b;
      if (b == 0)
	b = 1;
      if (a == 0)
	a = 1;
      test_all (a);
      test_all (b);
    }
}

/* Register example_test method as a functional test.  */
GRUB_FUNCTIONAL_TEST (ctz_test, ctz_test);
