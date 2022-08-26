 /*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019,2020  Free Software Foundation, Inc.
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
 *
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/time.h>
#include <grub/uuid.h>

#include <guid.h>

#pragma GCC diagnostic ignored "-Wcast-align"

void
grub_guidgen (grub_packed_guid_t *guid)
{
  grub_srand (grub_get_time_ms());
  int i;
  grub_uint32_t r;
  for (i = 0; i < 4; i++)
  {
    r = grub_rand ();
    grub_memcpy ((grub_uint32_t *)guid + i, &r, sizeof (grub_uint32_t));
  }
}

int
grub_guidcmp (const grub_packed_guid_t *g1, const grub_packed_guid_t *g2)
{
  grub_uint64_t g1_low, g2_low;
  grub_uint64_t g1_high, g2_high;
  g1_low = grub_get_unaligned64 ((const grub_uint64_t *)g1);
  g2_low = grub_get_unaligned64 ((const grub_uint64_t *)g2);
  g1_high = grub_get_unaligned64 ((const grub_uint64_t *)g1 + 1);
  g2_high = grub_get_unaligned64 ((const grub_uint64_t *)g2 + 1);
  return (int) (g1_low == g2_low && g1_high == g2_high);
}

grub_packed_guid_t *
grub_guidcpy (grub_packed_guid_t *dst, const grub_packed_guid_t *src)
{
  grub_set_unaligned64 ((grub_uint64_t *)dst,
                        grub_get_unaligned64 ((const grub_uint64_t *)src));
  grub_set_unaligned64 ((grub_uint64_t *)dst + 1,
                        grub_get_unaligned64 ((const grub_uint64_t*)src + 1));
  return dst;
}
