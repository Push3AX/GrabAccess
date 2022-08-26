/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2015 Free Software Foundation, Inc.
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

static grub_uint64_t vectors[] = {
  0xffffffffffffffffULL, 1, 2, 0, 0x0102030405060708ULL
};

static void
test16 (grub_uint16_t a)
{
  grub_uint16_t b, c;
  grub_uint8_t *ap, *bp;
  int i;
  b = grub_swap_bytes16 (a);
  c = grub_swap_bytes16 (b);
  grub_test_assert (a == c, "bswap not idempotent: 0x%llx, 0x%llx, 0x%llx",
		    (long long) a, (long long) b, (long long) c);
  ap = (grub_uint8_t *) &a;
  bp = (grub_uint8_t *) &b;
  for (i = 0; i < 2; i++)
    {
      grub_test_assert (ap[i] == bp[1 - i],
			"bswap bytes wrong: 0x%llx, 0x%llx",
			(long long) a, (long long) b);
    }
}

static void
test32 (grub_uint32_t a)
{
  grub_uint32_t b, c;
  grub_uint8_t *ap, *bp;
  int i;
  b = grub_swap_bytes32 (a);
  c = grub_swap_bytes32 (b);
  grub_test_assert (a == c, "bswap not idempotent: 0x%llx, 0x%llx, 0x%llx",
		    (long long) a, (long long) b, (long long) c);
  ap = (grub_uint8_t *) &a;
  bp = (grub_uint8_t *) &b;
  for (i = 0; i < 4; i++)
    {
      grub_test_assert (ap[i] == bp[3 - i],
			"bswap bytes wrong: 0x%llx, 0x%llx",
			(long long) a, (long long) b);
    }
}

static void
test64 (grub_uint64_t a)
{
  grub_uint64_t b, c;
  grub_uint8_t *ap, *bp;
  int i;
  b = grub_swap_bytes64 (a);
  c = grub_swap_bytes64 (b);
  grub_test_assert (a == c, "bswap not idempotent: 0x%llx, 0x%llx, 0x%llx",
		    (long long) a, (long long) b, (long long) c);
  ap = (grub_uint8_t *) &a;
  bp = (grub_uint8_t *) &b;
  for (i = 0; i < 4; i++)
    {
      grub_test_assert (ap[i] == bp[7 - i],
			"bswap bytes wrong: 0x%llx, 0x%llx",
			(long long) a, (long long) b);
    }
}

static void
test_all(grub_uint64_t a)
{
  test64 (a);
  test32 (a);
  test16 (a);
}

static void
bswap_test (void)
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
GRUB_FUNCTIONAL_TEST (bswap_test, bswap_test);
