/* kern/i386/tsc.c - x86 TSC time source implementation
 * Requires Pentium or better x86 CPU that supports the RDTSC instruction.
 * This module uses the PIT to calibrate the TSC to
 * real time.
 *
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008  Free Software Foundation, Inc.
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

#include <grub/types.h>
#include <grub/time.h>
#include <grub/misc.h>
#include <grub/i386/tsc.h>
#include <grub/xen.h>

int
grub_tsc_calibrate_from_xen (void)
{
  grub_uint64_t t;
  t = grub_xen_shared_info->vcpu_info[0].time.tsc_to_system_mul;
  if (grub_xen_shared_info->vcpu_info[0].time.tsc_shift > 0)
    t <<= grub_xen_shared_info->vcpu_info[0].time.tsc_shift;
  else
    t >>= -grub_xen_shared_info->vcpu_info[0].time.tsc_shift;
  grub_tsc_rate = grub_divmod64 (t, 1000000, 0);
  return 1;
}
