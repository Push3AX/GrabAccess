/*
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

#include <grub/cpu/io.h>
#include <grub/misc.h>
#include <grub/acpi.h>
#include <grub/i18n.h>
#include <grub/pci.h>
#include <grub/mm.h>

#ifdef GRUB_MACHINE_MULTIBOOT
#include <grub/machine/kernel.h>
#endif

#if !defined (GRUB_MACHINE_COREBOOT) && !defined (GRUB_MACHINE_QEMU)
#include <grub/machine/int.h>
#endif

const char bochs_shutdown[] = "Shutdown";

/*
 *  This call is special...  it never returns...  in fact it should simply
 *  hang at this point!
 */
static inline void  __attribute__ ((noreturn))
stop (void)
{
  asm volatile ("cli");
  while (1)
  {
    asm volatile ("hlt");
  }
}

static int
grub_shutdown_pci_iter (grub_pci_device_t dev, grub_pci_id_t pciid,
			void *data __attribute__ ((unused)))
{
  /* QEMU.  */
  if (pciid == 0x71138086)
  {
    grub_pci_address_t addr;
    addr = grub_pci_make_address (dev, 0x40);
    grub_pci_write (addr, 0x7001);
    addr = grub_pci_make_address (dev, 0x80);
    grub_pci_write (addr, grub_pci_read (addr) | 1);
    grub_outw (0x2000, 0x7004);
  }
  return 0;
}

void __attribute__ ((noreturn))
grub_halt (int no_apm)
{
  unsigned int i;

#if defined (GRUB_MACHINE_COREBOOT) || defined (GRUB_MACHINE_MULTIBOOT) \
    || defined (GRUB_MACHINE_PCBIOS)
  grub_acpi_halt ();
#endif

  /* Disable interrupts.  */
  asm volatile ("cli");

  /* Bochs, QEMU, etc. Removed in newer QEMU releases.  */
  for (i = 0; i < sizeof (bochs_shutdown) - 1; i++)
    grub_outb (bochs_shutdown[i], 0x8900);

  grub_pci_iterate (grub_shutdown_pci_iter, NULL);

  /* In order to return we'd have to check what the previous status of IF
     flag was.  But user most likely doesn't want to return anyway ...  */
#if defined (GRUB_MACHINE_COREBOOT) || defined (GRUB_MACHINE_QEMU)
  no_apm = 1;
#endif

#ifdef GRUB_MACHINE_MULTIBOOT
  if (! grub_mb_check_bios_int (0x15))
    no_apm = 1;
#endif

  if (no_apm)
    stop ();

#if !defined (GRUB_MACHINE_COREBOOT) && !defined (GRUB_MACHINE_QEMU)
  struct grub_bios_int_registers regs;
  /* detect APM */
  regs.eax = 0x5300;
  regs.ebx = 0;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x15, &regs);

  if (regs.flags & GRUB_CPU_INT_FLAGS_CARRY)
    stop ();

  /* disconnect APM first */
  regs.eax = 0x5304;
  regs.ebx = 0;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x15, &regs);

  /* connect APM */
  regs.eax = 0x5301;
  regs.ebx = 0;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x15, &regs);
  if (regs.flags & GRUB_CPU_INT_FLAGS_CARRY)
    stop ();

  /* set APM protocol level - 1.1 or bust. (this covers APM 1.2 also) */
  regs.eax = 0x530E;
  regs.ebx = 0;
  regs.ecx = 0x0101;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x15, &regs);
  if (regs.flags & GRUB_CPU_INT_FLAGS_CARRY)
    stop ();

  /* set the power state to off */
  regs.eax = 0x5307;
  regs.ebx = 1;
  regs.ecx = 3;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x15, &regs);
#endif
  /* shouldn't reach here */
  stop ();
}
