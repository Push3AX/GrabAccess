/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010  Free Software Foundation, Inc.
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
#include <grub/cs5536.h>
#include <grub/pci.h>
#include <grub/time.h>
#include <grub/ata.h>
#ifdef GRUB_MACHINE_MIPS_LOONGSON
#include <grub/machine/kernel.h>
#endif

#include <grub/dl.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* Context for grub_cs5536_find.  */
struct grub_cs5536_find_ctx
{
  grub_pci_device_t *devp;
  int found;
};

/* Helper for grub_cs5536_find.  */
static int
grub_cs5536_find_iter (grub_pci_device_t dev, grub_pci_id_t pciid, void *data)
{
  struct grub_cs5536_find_ctx *ctx = data;

  if (pciid == GRUB_CS5536_PCIID)
    {
      *ctx->devp = dev;
      ctx->found = 1;
      return 1;
    }
  return 0;
}

int
grub_cs5536_find (grub_pci_device_t *devp)
{
  struct grub_cs5536_find_ctx ctx = {
    .devp = devp,
    .found = 0
  };

  grub_pci_iterate (grub_cs5536_find_iter, &ctx);

  return ctx.found;
}

grub_uint64_t
grub_cs5536_read_msr (grub_pci_device_t dev, grub_uint32_t addr)
{
  grub_uint64_t ret = 0;
  grub_pci_write (grub_pci_make_address (dev, GRUB_CS5536_MSR_MAILBOX_ADDR),
		  addr);
  ret = (grub_uint64_t)
    grub_pci_read (grub_pci_make_address (dev, GRUB_CS5536_MSR_MAILBOX_DATA0));
  ret |= (((grub_uint64_t) 
	  grub_pci_read (grub_pci_make_address (dev,
						GRUB_CS5536_MSR_MAILBOX_DATA1)))
	  << 32);
  return ret;
}

void
grub_cs5536_write_msr (grub_pci_device_t dev, grub_uint32_t addr,
		       grub_uint64_t val)
{
  grub_pci_write (grub_pci_make_address (dev, GRUB_CS5536_MSR_MAILBOX_ADDR),
		  addr);
  grub_pci_write (grub_pci_make_address (dev, GRUB_CS5536_MSR_MAILBOX_DATA0),
		  val & 0xffffffff);
  grub_pci_write (grub_pci_make_address (dev, GRUB_CS5536_MSR_MAILBOX_DATA1),
		  val >> 32);
}

grub_err_t
grub_cs5536_smbus_wait (grub_port_t smbbase)
{
  grub_uint64_t start = grub_get_time_ms ();
  while (1)
    {
      grub_uint8_t status;
      status = grub_inb (smbbase + GRUB_CS5536_SMB_REG_STATUS);
      if (status & GRUB_CS5536_SMB_REG_STATUS_SDAST)
	return GRUB_ERR_NONE;	
      if (status & GRUB_CS5536_SMB_REG_STATUS_BER)
	return grub_error (GRUB_ERR_IO, "SM bus error");
      if (status & GRUB_CS5536_SMB_REG_STATUS_NACK)
	return grub_error (GRUB_ERR_IO, "NACK received");
      if (grub_get_time_ms () > start + 40)
	return grub_error (GRUB_ERR_IO, "SM stalled");
    }
}

grub_err_t
grub_cs5536_read_spd_byte (grub_port_t smbbase, grub_uint8_t dev,
			   grub_uint8_t addr, grub_uint8_t *res)
{
  grub_err_t err;

  /* Send START.  */
  grub_outb (grub_inb (smbbase + GRUB_CS5536_SMB_REG_CTRL1)
	     | GRUB_CS5536_SMB_REG_CTRL1_START,
	     smbbase + GRUB_CS5536_SMB_REG_CTRL1);

  /* Send device address.  */
  err = grub_cs5536_smbus_wait (smbbase); 
  if (err) 
    return err;
  grub_outb (dev << 1, smbbase + GRUB_CS5536_SMB_REG_DATA);

  /* Send ACK.  */
  err = grub_cs5536_smbus_wait (smbbase);
  if (err)
    return err;
  grub_outb (grub_inb (smbbase + GRUB_CS5536_SMB_REG_CTRL1)
	     | GRUB_CS5536_SMB_REG_CTRL1_ACK,
	     smbbase + GRUB_CS5536_SMB_REG_CTRL1);

  /* Send byte address.  */
  grub_outb (addr, smbbase + GRUB_CS5536_SMB_REG_DATA);

  /* Send START.  */
  err = grub_cs5536_smbus_wait (smbbase); 
  if (err) 
    return err;
  grub_outb (grub_inb (smbbase + GRUB_CS5536_SMB_REG_CTRL1)
	     | GRUB_CS5536_SMB_REG_CTRL1_START,
	     smbbase + GRUB_CS5536_SMB_REG_CTRL1);

  /* Send device address.  */
  err = grub_cs5536_smbus_wait (smbbase);
  if (err)
    return err;
  grub_outb ((dev << 1) | 1, smbbase + GRUB_CS5536_SMB_REG_DATA);

  /* Send STOP.  */
  err = grub_cs5536_smbus_wait (smbbase);
  if (err)
    return err;
  grub_outb (grub_inb (smbbase + GRUB_CS5536_SMB_REG_CTRL1)
	     | GRUB_CS5536_SMB_REG_CTRL1_STOP,
	     smbbase + GRUB_CS5536_SMB_REG_CTRL1);

  err = grub_cs5536_smbus_wait (smbbase);
  if (err) 
    return err;
  *res = grub_inb (smbbase + GRUB_CS5536_SMB_REG_DATA);

  return GRUB_ERR_NONE;
}

grub_err_t
grub_cs5536_init_smbus (grub_pci_device_t dev, grub_uint16_t divisor,
			grub_port_t *smbbase)
{
  grub_uint64_t smbbar;

  smbbar = grub_cs5536_read_msr (dev, GRUB_CS5536_MSR_SMB_BAR);

  /* FIXME  */
  if (!(smbbar & GRUB_CS5536_LBAR_ENABLE))
    return grub_error(GRUB_ERR_IO, "SMB controller not enabled\n");
  *smbbase = (smbbar & GRUB_CS5536_LBAR_ADDR_MASK) + GRUB_MACHINE_PCI_IO_BASE;

  if (divisor < 8)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid divisor");

  /* Disable SMB.  */
  grub_outb (0, *smbbase + GRUB_CS5536_SMB_REG_CTRL2);

  /* Disable interrupts.  */
  grub_outb (0, *smbbase + GRUB_CS5536_SMB_REG_CTRL1);

  /* Set as master.  */
  grub_outb (GRUB_CS5536_SMB_REG_ADDR_MASTER,
  	     *smbbase + GRUB_CS5536_SMB_REG_ADDR);

  /* Launch.  */
  grub_outb (((divisor >> 7) & 0xff), *smbbase + GRUB_CS5536_SMB_REG_CTRL3);
  grub_outb (((divisor << 1) & 0xfe) | GRUB_CS5536_SMB_REG_CTRL2_ENABLE,
	     *smbbase + GRUB_CS5536_SMB_REG_CTRL2);
  
  return GRUB_ERR_NONE; 
}

grub_err_t
grub_cs5536_read_spd (grub_port_t smbbase, grub_uint8_t dev,
		      struct grub_smbus_spd *res)
{
  grub_err_t err;
  grub_size_t size;
  grub_uint8_t b;
  grub_size_t ptr;

  err = grub_cs5536_read_spd_byte (smbbase, dev, 0, &b);
  if (err)
    return err;
  if (b == 0)
    return grub_error (GRUB_ERR_IO, "no SPD found");
  size = b;
  
  ((grub_uint8_t *) res)[0] = b;
  for (ptr = 1; ptr < size; ptr++)
    {
      err = grub_cs5536_read_spd_byte (smbbase, dev, ptr,
				       &((grub_uint8_t *) res)[ptr]);
      if (err)
	return err;
    }
  return GRUB_ERR_NONE;
}

static inline void
set_io_space (grub_pci_device_t dev, int num, grub_uint16_t start,
	      grub_uint16_t len)
{
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_GL_REGIONS_START + num,
			 ((((grub_uint64_t) start + len - 4)
			   << GRUB_CS5536_MSR_GL_REGION_IO_TOP_SHIFT)
			  & GRUB_CS5536_MSR_GL_REGION_TOP_MASK)
			 | (((grub_uint64_t) start
			     << GRUB_CS5536_MSR_GL_REGION_IO_BASE_SHIFT)
			  & GRUB_CS5536_MSR_GL_REGION_BASE_MASK)
			 | GRUB_CS5536_MSR_GL_REGION_IO
			 | GRUB_CS5536_MSR_GL_REGION_ENABLE);
}

static inline void
set_iod (grub_pci_device_t dev, int num, int dest, int start, int mask)
{
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_GL_IOD_START + num,
			 ((grub_uint64_t) dest << GRUB_CS5536_IOD_DEST_SHIFT)
			 | (((grub_uint64_t) start & GRUB_CS5536_IOD_ADDR_MASK)
			    << GRUB_CS5536_IOD_BASE_SHIFT)
			 | ((mask & GRUB_CS5536_IOD_ADDR_MASK)
			    << GRUB_CS5536_IOD_MASK_SHIFT));
}

static inline void
set_p2d (grub_pci_device_t dev, int num, int dest, grub_uint32_t start)
{
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_GL_P2D_START + num,
			 (((grub_uint64_t) dest) << GRUB_CS5536_P2D_DEST_SHIFT)
			 | ((grub_uint64_t) (start >> GRUB_CS5536_P2D_LOG_ALIGN)
			    << GRUB_CS5536_P2D_BASE_SHIFT)
			 | (((1 << (32 - GRUB_CS5536_P2D_LOG_ALIGN)) - 1)
			    << GRUB_CS5536_P2D_MASK_SHIFT));
}

void
grub_cs5536_init_geode (grub_pci_device_t dev)
{
  /* Enable more BARs.  */
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_IRQ_MAP_BAR,
			 GRUB_CS5536_LBAR_TURN_ON | GRUB_CS5536_LBAR_IRQ_MAP);
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_MFGPT_BAR,
			 GRUB_CS5536_LBAR_TURN_ON | GRUB_CS5536_LBAR_MFGPT);
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_ACPI_BAR,
			 GRUB_CS5536_LBAR_TURN_ON | GRUB_CS5536_LBAR_ACPI);
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_PM_BAR,
			 GRUB_CS5536_LBAR_TURN_ON | GRUB_CS5536_LBAR_PM);

  /* Setup DIVIL.  */
#ifdef GRUB_MACHINE_MIPS_LOONGSON
  switch (grub_arch_machine)
    {
    case GRUB_ARCH_MACHINE_YEELOONG:
      grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_DIVIL_LEG_IO,
			     GRUB_CS5536_MSR_DIVIL_LEG_IO_MODE_X86
			     | GRUB_CS5536_MSR_DIVIL_LEG_IO_F_REMAP
			     | GRUB_CS5536_MSR_DIVIL_LEG_IO_RTC_ENABLE0
			     | GRUB_CS5536_MSR_DIVIL_LEG_IO_RTC_ENABLE1);
      break;
    case GRUB_ARCH_MACHINE_FULOONG2F:
      grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_DIVIL_LEG_IO,
			     GRUB_CS5536_MSR_DIVIL_LEG_IO_UART2_COM3
			     | GRUB_CS5536_MSR_DIVIL_LEG_IO_UART1_COM1
			     | GRUB_CS5536_MSR_DIVIL_LEG_IO_MODE_X86
			     | GRUB_CS5536_MSR_DIVIL_LEG_IO_F_REMAP
			     | GRUB_CS5536_MSR_DIVIL_LEG_IO_RTC_ENABLE0
			     | GRUB_CS5536_MSR_DIVIL_LEG_IO_RTC_ENABLE1);
      break;
    }
#endif
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_DIVIL_IRQ_MAPPER_PRIMARY_MASK,
			 (~GRUB_CS5536_DIVIL_LPC_INTERRUPTS) & 0xffff);
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_DIVIL_IRQ_MAPPER_LPC_MASK,
			 GRUB_CS5536_DIVIL_LPC_INTERRUPTS);
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_DIVIL_LPC_SERIAL_IRQ_CONTROL,
			 GRUB_CS5536_MSR_DIVIL_LPC_SERIAL_IRQ_CONTROL_ENABLE);

  /* Initialise USB controller.  */
  /* FIXME: assign adresses dynamically.  */
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_USB_OHCI_BASE, 
			 GRUB_CS5536_MSR_USB_BASE_BUS_MASTER
			 | GRUB_CS5536_MSR_USB_BASE_MEMORY_ENABLE
			 | 0x05024000);
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_USB_EHCI_BASE,
			 GRUB_CS5536_MSR_USB_BASE_BUS_MASTER
			 | GRUB_CS5536_MSR_USB_BASE_MEMORY_ENABLE
			 | (0x20ULL << GRUB_CS5536_MSR_USB_EHCI_BASE_FLDJ_SHIFT)
			 | 0x05023000);
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_USB_CONTROLLER_BASE,
			 GRUB_CS5536_MSR_USB_BASE_BUS_MASTER
			 | GRUB_CS5536_MSR_USB_BASE_MEMORY_ENABLE | 0x05020000);
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_USB_OPTION_CONTROLLER_BASE,
			 GRUB_CS5536_MSR_USB_BASE_MEMORY_ENABLE | 0x05022000);
  set_p2d (dev, 0, GRUB_CS5536_DESTINATION_USB, 0x05020000);
  set_p2d (dev, 1, GRUB_CS5536_DESTINATION_USB, 0x05022000);
  set_p2d (dev, 5, GRUB_CS5536_DESTINATION_USB, 0x05024000);
  set_p2d (dev, 6, GRUB_CS5536_DESTINATION_USB, 0x05023000);

  {
    volatile grub_uint32_t *oc;
    oc = grub_pci_device_map_range (dev, 0x05022000,
				    GRUB_CS5536_USB_OPTION_REGS_SIZE);

    oc[GRUB_CS5536_USB_OPTION_REG_UOCMUX] =
      (oc[GRUB_CS5536_USB_OPTION_REG_UOCMUX]
       & ~GRUB_CS5536_USB_OPTION_REG_UOCMUX_PMUX_MASK)
      | GRUB_CS5536_USB_OPTION_REG_UOCMUX_PMUX_HC;
    grub_pci_device_unmap_range (dev, oc, GRUB_CS5536_USB_OPTION_REGS_SIZE);
  }

  /* Setup IDE controller.  */
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_IDE_IO_BAR,
			 GRUB_CS5536_LBAR_IDE
			 | GRUB_CS5536_MSR_IDE_IO_BAR_UNITS);
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_IDE_CFG,
			 GRUB_CS5536_MSR_IDE_CFG_CHANNEL_ENABLE);
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_IDE_TIMING,
			 (GRUB_CS5536_MSR_IDE_TIMING_PIO0
			  << GRUB_CS5536_MSR_IDE_TIMING_DRIVE0_SHIFT)
			 | (GRUB_CS5536_MSR_IDE_TIMING_PIO0
			    << GRUB_CS5536_MSR_IDE_TIMING_DRIVE1_SHIFT));
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_IDE_CAS_TIMING,
			 (GRUB_CS5536_MSR_IDE_CAS_TIMING_CMD_PIO0
			  << GRUB_CS5536_MSR_IDE_CAS_TIMING_CMD_SHIFT)
			 | (GRUB_CS5536_MSR_IDE_CAS_TIMING_PIO0
			    << GRUB_CS5536_MSR_IDE_CAS_TIMING_DRIVE0_SHIFT)
			 | (GRUB_CS5536_MSR_IDE_CAS_TIMING_PIO0
			    << GRUB_CS5536_MSR_IDE_CAS_TIMING_DRIVE1_SHIFT));

  /* Setup Geodelink PCI.  */
  grub_cs5536_write_msr (dev, GRUB_CS5536_MSR_GL_PCI_CTRL,
			 (4ULL << GRUB_CS5536_MSR_GL_PCI_CTRL_OUT_THR_SHIFT)
			 | (4ULL << GRUB_CS5536_MSR_GL_PCI_CTRL_IN_THR_SHIFT)
			 | (8ULL << GRUB_CS5536_MSR_GL_PCI_CTRL_LATENCY_SHIFT)
			 | GRUB_CS5536_MSR_GL_PCI_CTRL_IO_ENABLE
			 | GRUB_CS5536_MSR_GL_PCI_CTRL_MEMORY_ENABLE);

  /* Setup windows.  */
  set_io_space (dev, 0, GRUB_CS5536_LBAR_SMBUS, GRUB_CS5536_SMBUS_REGS_SIZE);
  set_io_space (dev, 1, GRUB_CS5536_LBAR_GPIO, GRUB_CS5536_GPIO_REGS_SIZE);
  set_io_space (dev, 2, GRUB_CS5536_LBAR_MFGPT, GRUB_CS5536_MFGPT_REGS_SIZE);
  set_io_space (dev, 3, GRUB_CS5536_LBAR_IRQ_MAP, GRUB_CS5536_IRQ_MAP_REGS_SIZE);
  set_io_space (dev, 4, GRUB_CS5536_LBAR_PM, GRUB_CS5536_PM_REGS_SIZE);
  set_io_space (dev, 5, GRUB_CS5536_LBAR_ACPI, GRUB_CS5536_ACPI_REGS_SIZE);
  set_iod (dev, 0, GRUB_CS5536_DESTINATION_IDE, GRUB_ATA_CH0_PORT1, 0xffff8);
  set_iod (dev, 1, GRUB_CS5536_DESTINATION_ACC, GRUB_CS5536_LBAR_ACC, 0xfff80);
  set_iod (dev, 2, GRUB_CS5536_DESTINATION_IDE, GRUB_CS5536_LBAR_IDE, 0xffff0);
}
