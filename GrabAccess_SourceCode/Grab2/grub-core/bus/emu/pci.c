/* pci.c - Generic PCI interfaces.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007,2009  Free Software Foundation, Inc.
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

#include <grub/pci.h>
#include <grub/dl.h>
#include <grub/emu/misc.h>
#include <grub/util/misc.h>

grub_pci_address_t
grub_pci_make_address (grub_pci_device_t dev, int reg)
{
  grub_pci_address_t ret;
  ret.dev = dev;
  ret.pos = reg;
  return ret;
}

void
grub_pci_iterate (grub_pci_iteratefunc_t hook, void *hook_data)
{
  struct pci_device_iterator *iter;
  struct pci_slot_match slot;
  struct pci_device *dev;
  slot.domain = PCI_MATCH_ANY;
  slot.bus = PCI_MATCH_ANY;
  slot.dev = PCI_MATCH_ANY;
  slot.func = PCI_MATCH_ANY;
  iter = pci_slot_match_iterator_create (&slot);
  while ((dev = pci_device_next (iter)))
    hook (dev, dev->vendor_id | (dev->device_id << 16), hook_data);
  pci_iterator_destroy (iter);
}

void *
grub_pci_device_map_range (grub_pci_device_t dev, grub_addr_t base,
			   grub_size_t size)
{
  void *addr;
  int err;
  err = pci_device_map_range (dev, base, size, PCI_DEV_MAP_FLAG_WRITABLE, &addr);
  if (err)
    grub_util_error ("mapping 0x%llx failed (error %d)",
		     (unsigned long long) base, err);
  return addr;
}

void
grub_pci_device_unmap_range (grub_pci_device_t dev, void *mem,
			     grub_size_t size)
{
  pci_device_unmap_range (dev, mem, size);
}

GRUB_MOD_INIT (emupci)
{
  pci_system_init ();
}

GRUB_MOD_FINI (emupci)
{
  pci_system_cleanup ();
}
