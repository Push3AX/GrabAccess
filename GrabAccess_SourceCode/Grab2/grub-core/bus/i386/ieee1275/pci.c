/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/pci.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/ieee1275/ieee1275.h>

volatile void *
grub_pci_device_map_range (grub_pci_device_t dev __attribute__ ((unused)),
			   grub_addr_t base,
			   grub_size_t size)
{
  if (grub_ieee1275_test_flag (GRUB_IEEE1275_FLAG_REAL_MODE))
    return (volatile void *) base;
  if (grub_ieee1275_map (base, base, size, 7))
    grub_fatal ("couldn't map 0x%lx", base);
  return (volatile void *) base;
}

void
grub_pci_device_unmap_range (grub_pci_device_t dev __attribute__ ((unused)),
			     volatile void *mem __attribute__ ((unused)),
			     grub_size_t size __attribute__ ((unused)))
{
}
