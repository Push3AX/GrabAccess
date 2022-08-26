/* ata_pthru.c - ATA pass through for ata.mod.  */
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

#include <grub/ata.h>
#include <grub/scsi.h>
#include <grub/disk.h>
#include <grub/dl.h>
#include <grub/mm.h>
#ifndef GRUB_MACHINE_MIPS_QEMU_MIPS
#include <grub/pci.h>
#include <grub/cs5536.h>
#else
#define GRUB_MACHINE_PCI_IO_BASE  0xb4000000
#endif
#include <grub/time.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* At the moment, only two IDE ports are supported.  */
static const grub_port_t grub_pata_ioaddress[] = { GRUB_ATA_CH0_PORT1,
						   GRUB_ATA_CH1_PORT1 };

struct grub_pata_device
{
  /* IDE port to use.  */
  int port;

  /* IO addresses on which the registers for this device can be
     found.  */
  grub_port_t ioaddress;

  /* Two devices can be connected to a single cable.  Use this field
     to select device 0 (commonly known as "master") or device 1
     (commonly known as "slave").  */
  int device;

  int present;

  struct grub_pata_device *next;
};

static struct grub_pata_device *grub_pata_devices;

static inline void
grub_pata_regset (struct grub_pata_device *dev, int reg, int val)
{
  grub_outb (val, dev->ioaddress + reg);
}

static inline grub_uint8_t
grub_pata_regget (struct grub_pata_device *dev, int reg)
{
  return grub_inb (dev->ioaddress + reg);
}

/* Wait for !BSY.  */
static grub_err_t
grub_pata_wait_not_busy (struct grub_pata_device *dev, int milliseconds)
{
  /* ATA requires 400ns (after a write to CMD register) or
     1 PIO cycle (after a DRQ block transfer) before
     first check of BSY.  */
  grub_millisleep (1);

  int i = 1;
  grub_uint8_t sts;
  while ((sts = grub_pata_regget (dev, GRUB_ATA_REG_STATUS))
	 & GRUB_ATA_STATUS_BUSY)
    {
      if (i >= milliseconds)
        {
	  grub_dprintf ("pata", "timeout: %dms, status=0x%x\n",
			milliseconds, sts);
	  return grub_error (GRUB_ERR_TIMEOUT, "PATA timeout");
	}

      grub_millisleep (1);
      i++;
    }

  return GRUB_ERR_NONE;
}

static inline grub_err_t
grub_pata_check_ready (struct grub_pata_device *dev, int spinup)
{
  if (grub_pata_regget (dev, GRUB_ATA_REG_STATUS) & GRUB_ATA_STATUS_BUSY)
    return grub_pata_wait_not_busy (dev, spinup ? GRUB_ATA_TOUT_SPINUP
				    : GRUB_ATA_TOUT_STD);

  return GRUB_ERR_NONE;
}

static inline void
grub_pata_wait (void)
{
  grub_millisleep (50);
}

#ifdef GRUB_MACHINE_MIPS_QEMU_MIPS
#define grub_ata_to_cpu16(x) ((grub_uint16_t) (x))
#define grub_cpu_to_ata16(x) ((grub_uint16_t) (x))
#else
#define grub_ata_to_cpu16 grub_le_to_cpu16
#define grub_cpu_to_ata16 grub_cpu_to_le16
#endif

static void
grub_pata_pio_read (struct grub_pata_device *dev, char *buf, grub_size_t size)
{ 
  unsigned int i;

  /* Read in the data, word by word.  */
  for (i = 0; i < size / 2; i++)
    grub_set_unaligned16 (buf + 2 * i,
			  grub_ata_to_cpu16 (grub_inw(dev->ioaddress
						     + GRUB_ATA_REG_DATA)));
  if (size & 1)
    buf[size - 1] = (char) grub_ata_to_cpu16 (grub_inw (dev->ioaddress
						       + GRUB_ATA_REG_DATA));
}

static void
grub_pata_pio_write (struct grub_pata_device *dev, char *buf, grub_size_t size)
{
  unsigned int i;

  /* Write the data, word by word.  */
  for (i = 0; i < size / 2; i++)
    grub_outw(grub_cpu_to_ata16 (grub_get_unaligned16 (buf + 2 * i)), dev->ioaddress + GRUB_ATA_REG_DATA);
}

/* ATA pass through support, used by hdparm.mod.  */
static grub_err_t
grub_pata_readwrite (struct grub_ata *disk,
		     struct grub_disk_ata_pass_through_parms *parms,
		     int spinup)
{
  struct grub_pata_device *dev = (struct grub_pata_device *) disk->data;
  grub_size_t nread = 0;
  int i;

  if (! (parms->cmdsize == 0 || parms->cmdsize == 12))
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		       "ATAPI non-12 byte commands not supported");

  grub_dprintf ("pata", "pata_pass_through: cmd=0x%x, features=0x%x, sectors=0x%x\n",
		parms->taskfile.cmd,
		parms->taskfile.features,
		parms->taskfile.sectors);
  grub_dprintf ("pata", "lba_high=0x%x, lba_mid=0x%x, lba_low=0x%x, size=%"
		PRIuGRUB_SIZE "\n",
	        parms->taskfile.lba_high,
	        parms->taskfile.lba_mid,
	        parms->taskfile.lba_low, parms->size);

  /* Set registers.  */
  grub_pata_regset (dev, GRUB_ATA_REG_DISK, (dev->device << 4)
		    | (parms->taskfile.disk & 0xef));
  if (grub_pata_check_ready (dev, spinup))
    return grub_errno;

  for (i = GRUB_ATA_REG_SECTORS; i <= GRUB_ATA_REG_LBAHIGH; i++)
    grub_pata_regset (dev, i,
		     parms->taskfile.raw[7 + (i - GRUB_ATA_REG_SECTORS)]);
  for (i = GRUB_ATA_REG_FEATURES; i <= GRUB_ATA_REG_LBAHIGH; i++)
    grub_pata_regset (dev, i, parms->taskfile.raw[i - GRUB_ATA_REG_FEATURES]);

  /* Start command. */
  grub_pata_regset (dev, GRUB_ATA_REG_CMD, parms->taskfile.cmd);

  /* Wait for !BSY.  */
  if (grub_pata_wait_not_busy (dev, GRUB_ATA_TOUT_DATA))
    return grub_errno;

  /* Check status.  */
  grub_int8_t sts = grub_pata_regget (dev, GRUB_ATA_REG_STATUS);
  grub_dprintf ("pata", "status=0x%x\n", sts);

  if (parms->cmdsize)
    {
      grub_uint8_t irs;
      /* Wait for !BSY.  */
      if (grub_pata_wait_not_busy (dev, GRUB_ATA_TOUT_DATA))
	return grub_errno;

      irs = grub_pata_regget (dev, GRUB_ATAPI_REG_IREASON);
      /* OK if DRQ is asserted and interrupt reason is as expected.  */
      if (!((sts & GRUB_ATA_STATUS_DRQ)
	    && (irs & GRUB_ATAPI_IREASON_MASK) == GRUB_ATAPI_IREASON_CMD_OUT))
	return grub_error (GRUB_ERR_READ_ERROR, "ATAPI protocol error");
      /* Write the packet.  */
      grub_pata_pio_write (dev, parms->cmd, parms->cmdsize);
    }

  /* Transfer data.  */
  while (nread < parms->size
	 && (sts & (GRUB_ATA_STATUS_DRQ | GRUB_ATA_STATUS_ERR))
	 == GRUB_ATA_STATUS_DRQ)
    {
      unsigned cnt;

      /* Wait for !BSY.  */
      if (grub_pata_wait_not_busy (dev, GRUB_ATA_TOUT_DATA))
	return grub_errno;

      if (parms->cmdsize)
	{
	  if ((grub_pata_regget (dev, GRUB_ATAPI_REG_IREASON)
	       & GRUB_ATAPI_IREASON_MASK) != GRUB_ATAPI_IREASON_DATA_IN)
	    return grub_error (GRUB_ERR_READ_ERROR, "ATAPI protocol error");

	  cnt = grub_pata_regget (dev, GRUB_ATAPI_REG_CNTHIGH) << 8
	    | grub_pata_regget (dev, GRUB_ATAPI_REG_CNTLOW);
	  grub_dprintf("pata", "DRQ count=%u\n", cnt);

	  /* Count of last transfer may be uneven.  */
	  if (! (0 < cnt && cnt <= parms->size - nread
		 && (! (cnt & 1) || cnt == parms->size - nread)))
	    return grub_error (GRUB_ERR_READ_ERROR,
			       "invalid ATAPI transfer count");
	}
      else
	cnt = GRUB_DISK_SECTOR_SIZE;
      if (cnt > parms->size - nread)
	cnt = parms->size - nread;

      if (parms->write)
	grub_pata_pio_write (dev, (char *) parms->buffer + nread, cnt);
      else
	grub_pata_pio_read (dev, (char *) parms->buffer + nread, cnt);

      nread += cnt;
    }
  if (parms->write)
    {
      /* Check for write error.  */
      if (grub_pata_wait_not_busy (dev, GRUB_ATA_TOUT_DATA))
	return grub_errno;

      if (grub_pata_regget (dev, GRUB_ATA_REG_STATUS)
	  & (GRUB_ATA_STATUS_DRQ | GRUB_ATA_STATUS_ERR))
	return grub_error (GRUB_ERR_WRITE_ERROR, "ATA write error");
    }
  parms->size = nread;

  /* Wait for !BSY.  */
  if (grub_pata_wait_not_busy (dev, GRUB_ATA_TOUT_DATA))
    return grub_errno;

  /* Return registers.  */
  for (i = GRUB_ATA_REG_ERROR; i <= GRUB_ATA_REG_STATUS; i++)
    parms->taskfile.raw[i - GRUB_ATA_REG_FEATURES] = grub_pata_regget (dev, i);

  grub_dprintf ("pata", "status=0x%x, error=0x%x, sectors=0x%x\n",
	        parms->taskfile.status,
	        parms->taskfile.error,
		parms->taskfile.sectors);

  if (parms->taskfile.status
      & (GRUB_ATA_STATUS_DRQ | GRUB_ATA_STATUS_ERR))
    return grub_error (GRUB_ERR_READ_ERROR, "PATA passthrough failed");

  return GRUB_ERR_NONE;
}

static grub_err_t
check_device (struct grub_pata_device *dev)
{
  grub_pata_regset (dev, GRUB_ATA_REG_DISK, dev->device << 4);
  grub_pata_wait ();

  /* Try to detect if the port is in use by writing to it,
     waiting for a while and reading it again.  If the value
     was preserved, there is a device connected.  */
  grub_pata_regset (dev, GRUB_ATA_REG_SECTORS, 0x5A);
  grub_pata_wait ();
  grub_uint8_t sec = grub_pata_regget (dev, GRUB_ATA_REG_SECTORS);
  grub_dprintf ("ata", "sectors=0x%x\n", sec);
  if (sec != 0x5A)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "no device connected");

  /* The above test may detect a second (slave) device
     connected to a SATA controller which supports only one
     (master) device.  It is not safe to use the status register
     READY bit to check for controller channel existence.  Some
     ATAPI commands (RESET, DIAGNOSTIC) may clear this bit.  */

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_pata_device_initialize (int port, int device, int addr)
{
  struct grub_pata_device *dev;
  struct grub_pata_device **devp;
  grub_err_t err;

  grub_dprintf ("pata", "detecting device %d,%d (0x%x)\n",
		port, device, addr);

  dev = grub_malloc (sizeof(*dev));
  if (! dev)
    return grub_errno;

  /* Setup the device information.  */
  dev->port = port;
  dev->device = device;
  dev->ioaddress = addr + GRUB_MACHINE_PCI_IO_BASE;
  dev->present = 1;
  dev->next = NULL;

  /* Register the device.  */
  for (devp = &grub_pata_devices; *devp; devp = &(*devp)->next);
  *devp = dev;

  err = check_device (dev);
  if (err == GRUB_ERR_UNKNOWN_DEVICE)
    {
      grub_dprintf ("pata", "%s\n", grub_errmsg);
      grub_error_pop ();
    }

  if (err)
    grub_print_error ();

  return 0;
}

#ifndef GRUB_MACHINE_MIPS_QEMU_MIPS
static int
grub_pata_pciinit (grub_pci_device_t dev,
		   grub_pci_id_t pciid,
		   void *data __attribute__ ((unused)))
{
  static int compat_use[2] = { 0 };
  grub_pci_address_t addr;
  grub_uint32_t class;
  grub_uint32_t bar1;
  grub_uint32_t bar2;
  int rega;
  int i;
  static int controller = 0;
  int cs5536 = 0;
  int nports = 2;

  /* Read class.  */
  addr = grub_pci_make_address (dev, GRUB_PCI_REG_CLASS);
  class = grub_pci_read (addr);

  /* AMD CS5536 Southbridge.  */
  if (pciid == GRUB_CS5536_PCIID)
    {
      cs5536 = 1;
      nports = 1;
    }

  /* Check if this class ID matches that of a PCI IDE Controller.  */
  if (!cs5536 && (class >> 16 != 0x0101))
    return 0;

  for (i = 0; i < nports; i++)
    {
      /* Set to 0 when the channel operated in compatibility mode.  */
      int compat;

      /* We don't support non-compatibility mode for CS5536.  */
      if (cs5536)
	compat = 0;
      else
	compat = (class >> (8 + 2 * i)) & 1;

      rega = 0;

      /* If the channel is in compatibility mode, just assign the
	 default registers.  */
      if (compat == 0 && !compat_use[i])
	{
	  rega = grub_pata_ioaddress[i];
	  compat_use[i] = 1;
	}
      else if (compat)
	{
	  /* Read the BARs, which either contain a mmapped IO address
	     or the IO port address.  */
	  addr = grub_pci_make_address (dev, GRUB_PCI_REG_ADDRESSES
					+ sizeof (grub_uint64_t) * i);
	  bar1 = grub_pci_read (addr);
	  addr = grub_pci_make_address (dev, GRUB_PCI_REG_ADDRESSES
					+ sizeof (grub_uint64_t) * i
					+ sizeof (grub_uint32_t));
	  bar2 = grub_pci_read (addr);

	  /* Check if the BARs describe an IO region.  */
	  if ((bar1 & 1) && (bar2 & 1) && (bar1 & ~3))
	    {
	      rega = bar1 & ~3;
	      addr = grub_pci_make_address (dev, GRUB_PCI_REG_COMMAND);
	      grub_pci_write_word (addr, grub_pci_read_word (addr)
				   | GRUB_PCI_COMMAND_IO_ENABLED
				   | GRUB_PCI_COMMAND_MEM_ENABLED
				   | GRUB_PCI_COMMAND_BUS_MASTER);

	    }
	}

      grub_dprintf ("pata",
		    "PCI dev (%d,%d,%d) compat=%d rega=0x%x\n",
		    grub_pci_get_bus (dev), grub_pci_get_device (dev),
		    grub_pci_get_function (dev), compat, rega);

      if (rega)
	{
	  grub_errno = GRUB_ERR_NONE;
	  grub_pata_device_initialize (controller * 2 + i, 0, rega);

	  /* Most errors raised by grub_ata_device_initialize() are harmless.
	     They just indicate this particular drive is not responding, most
	     likely because it doesn't exist.  We might want to ignore specific
	     error types here, instead of printing them.  */
	  if (grub_errno)
	    {
	      grub_print_error ();
	      grub_errno = GRUB_ERR_NONE;
	    }

	  grub_pata_device_initialize (controller * 2 + i, 1, rega);

	  /* Likewise.  */
	  if (grub_errno)
	    {
	      grub_print_error ();
	      grub_errno = GRUB_ERR_NONE;
	    }
	}
    }

  controller++;

  return 0;
}

static grub_err_t
grub_pata_initialize (void)
{
  grub_pci_iterate (grub_pata_pciinit, NULL);
  return 0;
}
#else
static grub_err_t
grub_pata_initialize (void)
{
  int i;
  for (i = 0; i < 2; i++)
    {
      grub_pata_device_initialize (i, 0, grub_pata_ioaddress[i]);
      grub_pata_device_initialize (i, 1, grub_pata_ioaddress[i]);
    }
  return 0;
}
#endif

static grub_err_t
grub_pata_open (int id, int devnum, struct grub_ata *ata)
{
  struct grub_pata_device *dev;
  struct grub_pata_device *devfnd = 0;
  grub_err_t err;

  if (id != GRUB_SCSI_SUBSYSTEM_PATA)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "not a PATA device");

  for (dev = grub_pata_devices; dev; dev = dev->next)
    {
      if (dev->port * 2 + dev->device == devnum)
	{
	  devfnd = dev;
	  break;
	}
    }

  grub_dprintf ("pata", "opening PATA dev `ata%d'\n", devnum);

  if (! devfnd)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "no such PATA device");

  err = check_device (devfnd);
  if (err)
    return err;

  ata->data = devfnd;
  ata->dma = 0;
  ata->maxbuffer = 256 * 512;
  ata->present = &devfnd->present;

  return GRUB_ERR_NONE;
}

static int
grub_pata_iterate (grub_ata_dev_iterate_hook_t hook, void *hook_data,
		   grub_disk_pull_t pull)
{
  struct grub_pata_device *dev;

  if (pull != GRUB_DISK_PULL_NONE)
    return 0;

  for (dev = grub_pata_devices; dev; dev = dev->next)
    if (hook (GRUB_SCSI_SUBSYSTEM_PATA, dev->port * 2 + dev->device,
	      hook_data))
      return 1;

  return 0;
}


static struct grub_ata_dev grub_pata_dev =
  {
    .iterate = grub_pata_iterate,
    .open = grub_pata_open,
    .readwrite = grub_pata_readwrite,
  };




GRUB_MOD_INIT(ata_pthru)
{
  grub_stop_disk_firmware ();

  /* ATA initialization.  */
  grub_pata_initialize ();

  grub_ata_dev_register (&grub_pata_dev);
}

GRUB_MOD_FINI(ata_pthru)
{
  grub_ata_dev_unregister (&grub_pata_dev);
}
