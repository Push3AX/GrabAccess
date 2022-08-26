/* bonito.c - PCI bonito interface.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
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
#include <grub/misc.h>

static grub_uint32_t base_win[GRUB_MACHINE_PCI_NUM_WIN];
static const grub_size_t sizes_win[GRUB_MACHINE_PCI_NUM_WIN] = 
  {GRUB_MACHINE_PCI_WIN1_SIZE, GRUB_MACHINE_PCI_WIN_SIZE, 
   GRUB_MACHINE_PCI_WIN_SIZE};
/* Usage counters.  */
static int usage_win[GRUB_MACHINE_PCI_NUM_WIN];
static grub_addr_t addr_win[GRUB_MACHINE_PCI_NUM_WIN] = 
  {GRUB_MACHINE_PCI_WIN1_ADDR, GRUB_MACHINE_PCI_WIN2_ADDR,
   GRUB_MACHINE_PCI_WIN3_ADDR};

grub_bonito_type_t grub_bonito_type;

static volatile void *
config_addr (grub_pci_address_t addr)
{
  if (grub_bonito_type == GRUB_BONITO_2F)
    {
      GRUB_MACHINE_PCI_CONF_CTRL_REG_2F = 1 << ((addr >> 11) & 0xf);
      return (volatile void *) (GRUB_MACHINE_PCI_CONFSPACE_2F
				| (addr & 0x07ff));
    }
  else
    {

      if (addr >> 16)
	return (volatile void *) (GRUB_MACHINE_PCI_CONFSPACE_3A_EXT | addr);
      else
	return (volatile void *) (GRUB_MACHINE_PCI_CONFSPACE_3A | addr);
    }
}

grub_uint32_t
grub_pci_read (grub_pci_address_t addr)
{
  return *(volatile grub_uint32_t *) config_addr (addr);
}

grub_uint16_t
grub_pci_read_word (grub_pci_address_t addr)
{
  return *(volatile grub_uint16_t *) config_addr (addr);
}

grub_uint8_t
grub_pci_read_byte (grub_pci_address_t addr)
{
  return *(volatile grub_uint8_t *) config_addr (addr);
}

void
grub_pci_write (grub_pci_address_t addr, grub_uint32_t data)
{
  *(volatile grub_uint32_t *) config_addr (addr) = data;
}

void
grub_pci_write_word (grub_pci_address_t addr, grub_uint16_t data)
{
  *(volatile grub_uint16_t *) config_addr (addr) = data;
}

void
grub_pci_write_byte (grub_pci_address_t addr, grub_uint8_t data)
{
  *(volatile grub_uint8_t *) config_addr (addr) = data;
}


static inline void
write_bases_2f (void)
{
  int i;
  grub_uint32_t reg = 0;
  for (i = 0; i < GRUB_MACHINE_PCI_NUM_WIN; i++) 
    reg |= (((base_win[i] >> GRUB_MACHINE_PCI_WIN_SHIFT) 
	     & GRUB_MACHINE_PCI_WIN_MASK) 
	    << (i * GRUB_MACHINE_PCI_WIN_MASK_SIZE));
  GRUB_MACHINE_PCI_IO_CTRL_REG_2F = reg;
}

volatile void *
grub_pci_device_map_range (grub_pci_device_t dev __attribute__ ((unused)),
			   grub_addr_t base, grub_size_t size)
{
  if (grub_bonito_type == GRUB_BONITO_2F)
    {
      int i;
      grub_addr_t newbase;

      /* First try already used registers. */
      for (i = 0; i < GRUB_MACHINE_PCI_NUM_WIN; i++)
	if (usage_win[i] && base_win[i] <= base 
	    && base_win[i] + sizes_win[i] > base + size)
	  {
	    usage_win[i]++;
	    return (void *) 
	      (addr_win[i] | (base & GRUB_MACHINE_PCI_WIN_OFFSET_MASK));
	  }
      /* Map new register.  */
      newbase = base & ~GRUB_MACHINE_PCI_WIN_OFFSET_MASK;
      for (i = 0; i < GRUB_MACHINE_PCI_NUM_WIN; i++)
	if (!usage_win[i] && newbase <= base 
	    && newbase + sizes_win[i] > base + size)
	  {
	    usage_win[i]++;
	    base_win[i] = newbase;
	    write_bases_2f ();
	    return (void *) 
	      (addr_win[i] | (base & GRUB_MACHINE_PCI_WIN_OFFSET_MASK));
	  }
      grub_fatal ("Out of PCI windows.");
    }
  else
    {
      int region = 0;
      if (base >= 0x10000000
	  && base + size <= 0x18000000)
	region = 1;
      if (base >= 0x1c000000
	  && base + size <= 0x1f000000)
	region = 2;
      if (region == 0)
	grub_fatal ("Attempt to map out of regions");
      return (void *) (0xa0000000 | base);
    }
}

void *
grub_pci_device_map_range_cached (grub_pci_device_t dev,
				  grub_addr_t base, grub_size_t size)
{
  return (void *) (((grub_addr_t) grub_pci_device_map_range (dev, base, size))
		   & ~0x20000000);
}

void
grub_pci_device_unmap_range (grub_pci_device_t dev __attribute__ ((unused)),
			     volatile void *mem,
			     grub_size_t size __attribute__ ((unused)))
{
  if (grub_bonito_type == GRUB_BONITO_2F)
    {
      int i;
      for (i = 0; i < GRUB_MACHINE_PCI_NUM_WIN; i++)
	if (usage_win[i] && addr_win[i] 
	    == (((grub_addr_t) mem | 0x20000000)
		& ~GRUB_MACHINE_PCI_WIN_OFFSET_MASK))
	  {
	    usage_win[i]--;
	    return;
	  }
      grub_fatal ("Tried to unmap not mapped region");
    }
}
