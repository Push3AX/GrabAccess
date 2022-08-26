/* ehci.c - EHCI Support.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2011  Free Software Foundation, Inc.
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
#include <grub/cpu/pci.h>
#include <grub/cs5536.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/time.h>
#include <grub/usb.h>

#define GRUB_EHCI_PCI_SBRN_REG  0x60
#define GRUB_EHCI_ADDR_MEM_MASK	(~0xff)

/* USBLEGSUP bits and related OS OWNED byte offset */
enum
{
  GRUB_EHCI_BIOS_OWNED = (1 << 16),
  GRUB_EHCI_OS_OWNED = (1 << 24)
};

/* PCI iteration function... */
static int
grub_ehci_pci_iter (grub_pci_device_t dev, grub_pci_id_t pciid,
		    void *data __attribute__ ((unused)))
{
  volatile grub_uint32_t *regs;
  grub_uint32_t base, base_h;
  grub_uint32_t eecp_offset;
  grub_uint32_t usblegsup = 0;
  grub_uint64_t maxtime;
  grub_uint32_t interf;
  grub_uint32_t subclass;
  grub_uint32_t class;
  grub_uint8_t release;
  grub_uint32_t class_code;

  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: begin\n");

  if (pciid == GRUB_CS5536_PCIID)
    {
      grub_uint64_t basereg;

      basereg = grub_cs5536_read_msr (dev, GRUB_CS5536_MSR_USB_EHCI_BASE);
      if (!(basereg & GRUB_CS5536_MSR_USB_BASE_MEMORY_ENABLE))
	{
	  /* Shouldn't happen.  */
	  grub_dprintf ("ehci", "No EHCI address is assigned\n");
	  return 0;
	}
      base = (basereg & GRUB_CS5536_MSR_USB_BASE_ADDR_MASK);
      basereg |= GRUB_CS5536_MSR_USB_BASE_BUS_MASTER;
      basereg &= ~GRUB_CS5536_MSR_USB_BASE_PME_ENABLED;
      basereg &= ~GRUB_CS5536_MSR_USB_BASE_PME_STATUS;
      basereg &= ~GRUB_CS5536_MSR_USB_BASE_SMI_ENABLE;
      grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_USB_EHCI_BASE, basereg);
    }
  else
    {
      grub_pci_address_t addr;
      addr = grub_pci_make_address (dev, GRUB_PCI_REG_CLASS);
      class_code = grub_pci_read (addr) >> 8;
      interf = class_code & 0xFF;
      subclass = (class_code >> 8) & 0xFF;
      class = class_code >> 16;

      /* If this is not an EHCI controller, just return.  */
      if (class != 0x0c || subclass != 0x03 || interf != 0x20)
	return 0;

      grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: class OK\n");

      /* Check Serial Bus Release Number */
      addr = grub_pci_make_address (dev, GRUB_EHCI_PCI_SBRN_REG);
      release = grub_pci_read_byte (addr);
      if (release != 0x20)
	{
	  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: Wrong SBRN: %0x\n",
			release);
	  return 0;
	}
      grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: bus rev. num. OK\n");
  
      /* Determine EHCI EHCC registers base address.  */
      addr = grub_pci_make_address (dev, GRUB_PCI_REG_ADDRESS_REG0);
      base = grub_pci_read (addr);
      addr = grub_pci_make_address (dev, GRUB_PCI_REG_ADDRESS_REG1);
      base_h = grub_pci_read (addr);
      /* Stop if registers are mapped above 4G - GRUB does not currently
       * work with registers mapped above 4G */
      if (((base & GRUB_PCI_ADDR_MEM_TYPE_MASK) != GRUB_PCI_ADDR_MEM_TYPE_32)
	  && (base_h != 0))
	{
	  grub_dprintf ("ehci",
			"EHCI grub_ehci_pci_iter: registers above 4G are not supported\n");
	  return 0;
	}
      base &= GRUB_PCI_ADDR_MEM_MASK;
      if (!base)
	{
	  grub_dprintf ("ehci",
			"EHCI: EHCI is not mapped\n");
	  return 0;
	}

      /* Set bus master - needed for coreboot, VMware, broken BIOSes etc. */
      addr = grub_pci_make_address (dev, GRUB_PCI_REG_COMMAND);
      grub_pci_write_word(addr,
			  GRUB_PCI_COMMAND_MEM_ENABLED
			  | GRUB_PCI_COMMAND_BUS_MASTER
			  | grub_pci_read_word(addr));
      
      grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: 32-bit EHCI OK\n");
    }

  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: iobase of EHCC: %08x\n",
		(base & GRUB_EHCI_ADDR_MEM_MASK));

  regs = grub_pci_device_map_range (dev,
				    (base & GRUB_EHCI_ADDR_MEM_MASK),
				    0x100);

  /* Is there EECP ? */
  eecp_offset = (grub_le_to_cpu32 (regs[2]) >> 8) & 0xff;

    /* Determine and change ownership. */
  /* EECP offset valid in HCCPARAMS */
  /* Ownership can be changed via EECP only */
  if (pciid != GRUB_CS5536_PCIID && eecp_offset >= 0x40)	
    {
      grub_pci_address_t pciaddr_eecp;
      pciaddr_eecp = grub_pci_make_address (dev, eecp_offset);

      usblegsup = grub_pci_read (pciaddr_eecp);
      if (usblegsup & GRUB_EHCI_BIOS_OWNED)
	{
	  grub_boot_time ("Taking ownership of EHCI controller");
	  grub_dprintf ("ehci",
			"EHCI grub_ehci_pci_iter: EHCI owned by: BIOS\n");
	  /* Ownership change - set OS_OWNED bit */
	  grub_pci_write (pciaddr_eecp, usblegsup | GRUB_EHCI_OS_OWNED);
	  /* Ensure PCI register is written */
	  grub_pci_read (pciaddr_eecp);

	  /* Wait for finish of ownership change, EHCI specification
	   * doesn't say how long it can take... */
	  maxtime = grub_get_time_ms () + 1000;
	  while ((grub_pci_read (pciaddr_eecp) & GRUB_EHCI_BIOS_OWNED)
		 && (grub_get_time_ms () < maxtime));
	  if (grub_pci_read (pciaddr_eecp) & GRUB_EHCI_BIOS_OWNED)
	    {
	      grub_dprintf ("ehci",
			    "EHCI grub_ehci_pci_iter: EHCI change ownership timeout");
	      /* Change ownership in "hard way" - reset BIOS ownership */
	      grub_pci_write (pciaddr_eecp, GRUB_EHCI_OS_OWNED);
	      /* Ensure PCI register is written */
	      grub_pci_read (pciaddr_eecp);
	    }
	}
      else if (usblegsup & GRUB_EHCI_OS_OWNED)
	/* XXX: What to do in this case - nothing ? Can it happen ? */
	grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: EHCI owned by: OS\n");
      else
	{
	  grub_dprintf ("ehci",
			"EHCI grub_ehci_pci_iter: EHCI owned by: NONE\n");
	  /* XXX: What to do in this case ? Can it happen ?
	   * Is code below correct ? */
	  /* Ownership change - set OS_OWNED bit */
	  grub_pci_write (pciaddr_eecp, GRUB_EHCI_OS_OWNED);
	  /* Ensure PCI register is written */
	  grub_pci_read (pciaddr_eecp);
	}

      /* Disable SMI, just to be sure.  */
      pciaddr_eecp = grub_pci_make_address (dev, eecp_offset + 4);
      grub_pci_write (pciaddr_eecp, 0);
      /* Ensure PCI register is written */
      grub_pci_read (pciaddr_eecp);
    }

  grub_dprintf ("ehci", "inithw: EHCI grub_ehci_pci_iter: ownership OK\n");

  grub_ehci_init_device (regs);
  return 0;
}

void
grub_ehci_pci_scan (void)
{
  grub_pci_iterate (grub_ehci_pci_iter, NULL);
}
