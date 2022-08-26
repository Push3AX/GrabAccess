/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2009  Free Software Foundation, Inc.
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

#ifndef KERNEL_CPU_PMTIMER_HEADER
#define KERNEL_CPU_PMTIMER_HEADER   1

#include <grub/i386/tsc.h>
#include <grub/i386/io.h>

/*
  Preconditions:
  * Caller has ensured that both pmtimer and tsc are supported
  * 1 <= num_pm_ticks <= 3580
  Return:
  * Number of TSC ticks elapsed
  * 0 on failure.
*/
grub_uint64_t
EXPORT_FUNC(grub_pmtimer_wait_count_tsc) (grub_port_t pmtimer,
					  grub_uint16_t num_pm_ticks);

#endif
