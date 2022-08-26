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

/* We're testing shifts, don't replace access to this with a shift.  */
static const grub_uint8_t bitmask[] =
  { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

typedef union {
  grub_uint64_t v64;
  grub_uint8_t v8[8];
} grub_raw_u64_t;

static int
get_bit64 (grub_uint64_t v, int b)
{
  grub_raw_u64_t vr = { .v64 = v };
  grub_uint8_t *p = vr.v8;
  if (b >= 64)
    return 0;
#ifdef GRUB_CPU_WORDS_BIGENDIAN
  p += 7 - b / 8;
#else
  p += b / 8;
#endif
  return !!(*p & bitmask[b % 8]);
}

static grub_uint64_t
set_bit64 (grub_uint64_t v, int b)
{
  grub_raw_u64_t vr = { .v64 = v };
  grub_uint8_t *p = vr.v8;
  if (b >= 64)
    return v;
#ifdef GRUB_CPU_WORDS_BIGENDIAN
  p += 7 - b / 8;
#else
  p += b / 8;
#endif
  *p |= bitmask[b % 8];
  return vr.v64;
}

static grub_uint64_t
left_shift64 (grub_uint64_t v, int s)
{
  grub_uint64_t r = 0;
  int i;
  for (i = 0; i + s < 64; i++)
    if (get_bit64 (v, i))
      r = set_bit64 (r, i + s);
  return r;
}

static grub_uint64_t
right_shift64 (grub_uint64_t v, int s)
{
  grub_uint64_t r = 0;
  int i;
  for (i = s; i < 64; i++)
    if (get_bit64 (v, i))
      r = set_bit64 (r, i - s);
  return r;
}

static grub_uint64_t
arithmetic_right_shift64 (grub_uint64_t v, int s)
{
  grub_uint64_t r = 0;
  int i;
  for (i = s; i < 64; i++)
    if (get_bit64 (v, i))
      r = set_bit64 (r, i - s);
  if (get_bit64 (v, 63))
    for (i -= s; i < 64; i++)
	r = set_bit64 (r, i);
    
  return r;
}

static void
test64 (grub_uint64_t v)
{
  int i;
  for (i = 0; i < 64; i++)
    {
      grub_test_assert ((v << i) == left_shift64 (v, i),
			"lshift wrong: 0x%llx << %d: 0x%llx, 0x%llx",
			(long long) v, i,
			(long long) (v << i), (long long) left_shift64 (v, i));
      grub_test_assert ((v >> i) == right_shift64 (v, i),
			"rshift wrong: 0x%llx >> %d: 0x%llx, 0x%llx",
			(long long) v, i,
			(long long) (v >> i), (long long) right_shift64 (v, i));
      grub_test_assert ((((grub_int64_t) v) >> i) == (grub_int64_t) arithmetic_right_shift64 (v, i),
			"arithmetic rshift wrong: ((grub_int64_t) 0x%llx) >> %d: 0x%llx, 0x%llx",
			(long long) v, i,
			(long long) (((grub_int64_t) v) >> i), (long long) arithmetic_right_shift64 (v, i));
    }
}

static void
test_all(grub_uint64_t a)
{
  test64 (a);
}

static void
shift_test (void)
{
  grub_uint64_t a = 404, b = 7;
  grub_size_t i;

  for (i = 0; i < ARRAY_SIZE (vectors); i++)
    {
      test_all (vectors[i]);
    }
  for (i = 0; i < 4000; i++)
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
GRUB_FUNCTIONAL_TEST (shift_test, shift_test);
