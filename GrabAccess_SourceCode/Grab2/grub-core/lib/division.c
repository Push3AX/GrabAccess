/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2015  Free Software Foundation, Inc.
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

#include <grub/misc.h>
#include <grub/dl.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_uint64_t
abs64(grub_int64_t a)
{
  return a > 0 ? a : -a;
}

grub_int64_t
grub_divmod64s (grub_int64_t n,
		grub_int64_t d,
		grub_int64_t *ro)
{
  grub_uint64_t ru;
  grub_int64_t q, r;
  q = grub_divmod64 (abs64(n), abs64(d), &ru);
  r = ru;
  /* Now: |n| = |d| * q + r  */
  if (n < 0)
    {
      /* -|n| = |d| * (-q) + (-r)  */
      q = -q;
      r = -r;
    }
  /* Now: n = |d| * q + r  */
  if (d < 0)
    {
      /* n = (-|d|) * (-q) + r  */
      q = -q;
    }
  /* Now: n = d * q + r  */
  if (ro)
    *ro = r;
  return q;
}

grub_uint32_t
grub_divmod32 (grub_uint32_t n, grub_uint32_t d, grub_uint32_t *ro)
{
  grub_uint64_t q, r;
  q = grub_divmod64 (n, d, &r);
  *ro = r;
  return q;
}

grub_int32_t
grub_divmod32s (grub_int32_t n, grub_int32_t d, grub_int32_t *ro)
{
  grub_int64_t q, r;
  q = grub_divmod64s (n, d, &r);
  *ro = r;
  return q;
}
