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
test32 (grub_uint32_t a, grub_uint32_t b)
{
  grub_uint64_t q, r;
  if (b == 0)
    return;
  q = grub_divmod64 (a, b, &r);
  grub_test_assert (r < b, "remainder is larger than dividend: 0x%llx %% 0x%llx = 0x%llx",
		    (long long) a, (long long) b, (long long) r);
  grub_test_assert (q * b + r == a, "division doesn't satisfy base property: 0x%llx * 0x%llx + 0x%llx != 0x%llx", (long long) q, (long long) b, (long long) r,
		    (long long) a);
  /* Overflow check.  */
  grub_test_assert ((q >> 32) == 0,
		    "division overflow in 0x%llx, 0x%llx", (long long) a, (long long) b);
  grub_test_assert ((r >> 32) == 0,
		    "division overflow in 0x%llx, 0x%llx", (long long) a, (long long) b);
  /* q * b + r is at most:
     0xffffffff * 0xffffffff + 0xffffffff = 0xffffffff00000000
     so no overflow
   */
  grub_test_assert (q == (a / b),
		    "C compiler division failure in 0x%llx, 0x%llx", (long long) a, (long long) b);
  grub_test_assert (r == (a % b),
		    "C compiler modulo failure in 0x%llx, 0x%llx", (long long) a, (long long) b);
}

static void
test64 (grub_uint64_t a, grub_uint64_t b)
{
  grub_uint64_t q, r;
  grub_uint64_t x1, x2;
  q = grub_divmod64 (a, b, &r);
  grub_test_assert (r < b, "remainder is larger than dividend: 0x%llx %% 0x%llx = 0x%llx",
		    (long long) a, (long long) b, (long long) r);
  grub_test_assert (q * b + r == a, "division doesn't satisfy base property: 0x%llx * 0x%llx + 0x%llx != 0x%llx", (long long) q, (long long) b, (long long) r,
		    (long long) a);
  /* Overflow checks.  */
  grub_test_assert ((q >> 32) * (b >> 32) == 0,
		    "division overflow in 0x%llx, 0x%llx", (long long) a, (long long) b);
  x1 = (q >> 32) * (b & 0xffffffff);
  grub_test_assert (x1 < (1LL << 32),
		    "division overflow in 0x%llx, 0x%llx", (long long) a, (long long) b);
  x1 <<= 32;
  x2 = (b >> 32) * (q & 0xffffffff);
  grub_test_assert (x2 < (1LL << 32),
		    "division overflow in 0x%llx, 0x%llx", (long long) a, (long long) b);
  x2 <<= 32;
  grub_test_assert (x1 <= ~x2,
		    "division overflow in 0x%llx, 0x%llx", (long long) a, (long long) b);
  x1 += x2;
  x2 = (q & 0xffffffff) * (b & 0xffffffff);
  grub_test_assert (x1 <= ~x2,
		    "division overflow in 0x%llx, 0x%llx", (long long) a, (long long) b);
  x1 += x2;
  grub_test_assert (x1 <= ~r,
		    "division overflow in 0x%llx, 0x%llx", (long long) a, (long long) b);
  x1 += r;
  grub_test_assert (a == x1,
		    "division overflow test failure in 0x%llx, 0x%llx", (long long) a, (long long) b);
#if GRUB_TARGET_SIZEOF_VOID_P == 8
  grub_test_assert (q == (a / b),
		    "C compiler division failure in 0x%llx, 0x%llx", (long long) a, (long long) b);
  grub_test_assert (r == (a % b),
		    "C compiler modulo failure in 0x%llx, 0x%llx", (long long) a, (long long) b);
#endif
}

static grub_int64_t
abs64(grub_int64_t a)
{
  return a > 0 ? a : -a;
}

static void
test32s (grub_int32_t a, grub_int32_t b)
{
  grub_int64_t q, r;
  if (b == 0)
    return;

  q = grub_divmod64s (a, b, &r);
  grub_test_assert (a > 0 ? r >= 0 : r <= 0, "remainder sign mismatch: %lld %% %lld = %lld",
		    (long long) a, (long long) b, (long long) r);
  grub_test_assert (((a > 0) == (b > 0)) ? q >= 0 : q <= 0, "quotient sign mismatch: %lld / %lld = %lld",
		    (long long) a, (long long) b, (long long) q);
  grub_test_assert (abs64(r) < abs64(b), "remainder is larger than dividend: %lld %% %lld = %lld",
		    (long long) a, (long long) b, (long long) r);
  grub_test_assert (q * b + r == a, "division doesn't satisfy base property: %lld * %lld + %lld != %lld", (long long) q, (long long) b, (long long) r,
		    (long long) a);
  if (0) {  grub_test_assert (q == (a / b),
		    "C compiler division failure in 0x%llx, 0x%llx", (long long) a, (long long) b);
  grub_test_assert (r == (a % b),
		    "C compiler modulo failure in 0x%llx, 0x%llx", (long long) a, (long long) b);
  }
}

static void
test64s (grub_int64_t a, grub_int64_t b)
{
  grub_int64_t q, r;
  q = grub_divmod64s (a, b, &r);

  grub_test_assert (a > 0 ? r >= 0 : r <= 0, "remainder sign mismatch: %lld %% %lld = %lld",
		    (long long) a, (long long) b, (long long) r);
  grub_test_assert (((a > 0) == (b > 0)) ? q >= 0 : q <= 0, "quotient sign mismatch: %lld / %lld = %lld",
		    (long long) a, (long long) b, (long long) q);
  grub_test_assert (abs64(r) < abs64(b), "remainder is larger than dividend: %lld %% %lld = %lld",
		    (long long) a, (long long) b, (long long) r);
  grub_test_assert (q * b + r == a, "division doesn't satisfy base property: 0x%llx * 0x%llx + 0x%llx != 0x%llx", (long long) q, (long long) b, (long long) r,
		    (long long) a);
#if GRUB_TARGET_SIZEOF_VOID_P == 8
  grub_test_assert (q == (a / b),
		    "C compiler division failure in 0x%llx, 0x%llx", (long long) a, (long long) b);
  grub_test_assert (r == (a % b),
		    "C compiler modulo failure in 0x%llx, 0x%llx", (long long) a, (long long) b);
#endif
}

static void
test_all(grub_uint64_t a, grub_uint64_t b)
{
  test64 (a, b);
  test32 (a, b);
  test64s (a, b);
  test32s (a, b);
  test64s (a, -b);
  test32s (a, -b);
  test64s (-a, b);
  test32s (-a, b);
  test64s (-a, -b);
  test32s (-a, -b);
}

static void
div_test (void)
{
  grub_uint64_t a = 404, b = 7;
  grub_size_t i;

  for (i = 0; i < ARRAY_SIZE (vectors); i++)
    {
      test_all (vectors[i][0], vectors[i][1]);
    }
  for (i = 0; i < 40000; i++)
    {
      a = 17 * a + 13 * b;
      b = 23 * a + 29 * b;
      if (b == 0)
	b = 1;
      if (a == 0)
	a = 1;
      test_all (a, b);
    }
}

/* Register example_test method as a functional test.  */
GRUB_FUNCTIONAL_TEST (div_test, div_test);
