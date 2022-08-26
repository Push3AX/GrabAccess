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
#include <grub/efi/efi.h>
#include <grub/efi/api.h>

int
grub_tsc_calibrate_from_efi (void)
{
  grub_uint64_t start_tsc, end_tsc;
  /* Use EFI Time Service to calibrate TSC */
  start_tsc = grub_get_tsc ();
  efi_call_1 (grub_efi_system_table->boot_services->stall, 1000);
  end_tsc = grub_get_tsc ();
  grub_tsc_rate = grub_divmod64 ((1ULL << 32), end_tsc - start_tsc, 0);
  return 1;
}
