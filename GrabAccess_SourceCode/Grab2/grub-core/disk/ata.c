/* ata.c - ATA disk access.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007, 2008, 2009  Free Software Foundation, Inc.
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
#include <grub/dl.h>
#include <grub/disk.h>
#include <grub/mm.h>
#include <grub/scsi.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_ata_dev_t grub_ata_dev_list;

/* Byteorder has to be changed before strings can be read.  */
static void
grub_ata_strncpy (grub_uint16_t *dst16, grub_uint16_t *src16, grub_size_t len)
{
  unsigned int i;

  for (i = 0; i < len / 2; i++)
    *(dst16++) = grub_swap_bytes16 (*(src16++));
  *dst16 = 0;
}

static void
grub_ata_dumpinfo (struct grub_ata *dev, grub_uint16_t *info)
{
  grub_uint16_t text[21];

  /* The device information was read, dump it for debugging.  */
  grub_ata_strncpy (text, info + 10, 20);
  grub_dprintf ("ata", "Serial: %s\n", (char *) text);
  grub_ata_strncpy (text, info + 23, 8);
  grub_dprintf ("ata", "Firmware: %s\n", (char *) text);
  grub_ata_strncpy (text, info + 27, 40);
  grub_dprintf ("ata", "Model: %s\n", (char *) text);

  if (! dev->atapi)
    {
      grub_dprintf ("ata", "Addressing: %d\n", dev->addr);
      grub_dprintf ("ata", "Sectors: %lld\n", (unsigned long long) dev->size);
      grub_dprintf ("ata", "Sector size: %u\n", 1U << dev->log_sector_size);
    }
}

static grub_err_t
grub_atapi_identify (struct grub_ata *dev)
{
  struct grub_disk_ata_pass_through_parms parms;
  grub_uint16_t *info;
  grub_err_t err;

  info = grub_malloc (GRUB_DISK_SECTOR_SIZE);
  if (! info)
    return grub_errno;

  grub_memset (&parms, 0, sizeof (parms));
  parms.taskfile.disk = 0xE0;
  parms.taskfile.cmd = GRUB_ATA_CMD_IDENTIFY_PACKET_DEVICE;
  parms.size = GRUB_DISK_SECTOR_SIZE;
  parms.buffer = info;

  err = dev->dev->readwrite (dev, &parms, *dev->present);
  if (err)
    {
      *dev->present = 0;
      return err;
    }

  if (parms.size != GRUB_DISK_SECTOR_SIZE)
    {
      *dev->present = 0;
      return grub_error (GRUB_ERR_UNKNOWN_DEVICE,
			 "device cannot be identified");
    }

  dev->atapi = 1;

  grub_ata_dumpinfo (dev, info);

  grub_free (info);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_ata_identify (struct grub_ata *dev)
{
  struct grub_disk_ata_pass_through_parms parms;
  grub_uint64_t *info64;
  grub_uint32_t *info32;
  grub_uint16_t *info16;
  grub_err_t err;

  if (dev->atapi)
    return grub_atapi_identify (dev);

  info64 = grub_malloc (GRUB_DISK_SECTOR_SIZE);
  info32 = (grub_uint32_t *) info64;
  info16 = (grub_uint16_t *) info64;
  if (! info16)
    return grub_errno;

  grub_memset (&parms, 0, sizeof (parms));
  parms.buffer = info16;
  parms.size = GRUB_DISK_SECTOR_SIZE;
  parms.taskfile.disk = 0xE0;

  parms.taskfile.cmd = GRUB_ATA_CMD_IDENTIFY_DEVICE;

  err = dev->dev->readwrite (dev, &parms, *dev->present);

  if (err || parms.size != GRUB_DISK_SECTOR_SIZE)
    {
      grub_uint8_t sts = parms.taskfile.status;
      grub_free (info16);
      grub_errno = GRUB_ERR_NONE;
      if ((sts & (GRUB_ATA_STATUS_BUSY | GRUB_ATA_STATUS_DRQ
		   | GRUB_ATA_STATUS_ERR)) == GRUB_ATA_STATUS_ERR
	  && (parms.taskfile.error & 0x04 /* ABRT */))
	/* Device without ATA IDENTIFY, try ATAPI.  */
	return grub_atapi_identify (dev);

      else if (sts == 0x00)
	{
	  *dev->present = 0;
	  /* No device, return error but don't print message.  */
	  return GRUB_ERR_UNKNOWN_DEVICE;
	}
      else
	{
	  *dev->present = 0;
	  /* Other Error.  */
	  return grub_error (GRUB_ERR_UNKNOWN_DEVICE,
			     "device cannot be identified");
	}
    }

  /* Now it is certain that this is not an ATAPI device.  */
  dev->atapi = 0;

  /* CHS is always supported.  */
  dev->addr = GRUB_ATA_CHS;

  /* Check if LBA is supported.  */
  if (info16[49] & grub_cpu_to_le16_compile_time ((1 << 9)))
    {
      /* Check if LBA48 is supported.  */
      if (info16[83] & grub_cpu_to_le16_compile_time ((1 << 10)))
	dev->addr = GRUB_ATA_LBA48;
      else
	dev->addr = GRUB_ATA_LBA;
    }

  /* Determine the amount of sectors.  */
  if (dev->addr != GRUB_ATA_LBA48)
    dev->size = grub_le_to_cpu32 (info32[30]);
  else
    dev->size = grub_le_to_cpu64 (info64[25]);

  if (info16[106] & grub_cpu_to_le16_compile_time ((1 << 12)))
    {
      grub_uint32_t secsize;
      secsize = grub_le_to_cpu32 (grub_get_unaligned32 (&info16[117]));
      if (secsize & (secsize - 1) || !secsize
	  || secsize > 1048576)
	secsize = 256;
      for (dev->log_sector_size = 0;
	   (1U << dev->log_sector_size) < secsize;
	   dev->log_sector_size++);
      dev->log_sector_size++;
    }
  else
    dev->log_sector_size = 9;

  /* Read CHS information.  */
  dev->cylinders = grub_le_to_cpu16 (info16[1]);
  dev->heads = grub_le_to_cpu16 (info16[3]);
  dev->sectors_per_track = grub_le_to_cpu16 (info16[6]);

  grub_ata_dumpinfo (dev, info16);

  grub_free (info16);

  return 0;
}

static grub_err_t
grub_ata_setaddress (struct grub_ata *dev,
		     struct grub_disk_ata_pass_through_parms *parms,
		     grub_disk_addr_t sector,
		     grub_size_t size,
		     grub_ata_addressing_t addressing)
{
  switch (addressing)
    {
    case GRUB_ATA_CHS:
      {
	unsigned int cylinder;
	unsigned int head;
	unsigned int sect;

	if (dev->sectors_per_track == 0
	    || dev->heads == 0)
	  return grub_error (GRUB_ERR_OUT_OF_RANGE,
			     "sector %" PRIxGRUB_UINT64_T " cannot be addressed "
			     "using CHS addressing", sector);

	/* Calculate the sector, cylinder and head to use.  */
	sect = ((grub_uint32_t) sector % dev->sectors_per_track) + 1;
	cylinder = (((grub_uint32_t) sector / dev->sectors_per_track)
		    / dev->heads);
	head = ((grub_uint32_t) sector / dev->sectors_per_track) % dev->heads;

	if (sect > dev->sectors_per_track
	    || cylinder > dev->cylinders
	    || head > dev->heads)
	  return grub_error (GRUB_ERR_OUT_OF_RANGE,
			     "sector %" PRIxGRUB_UINT64_T " cannot be addressed "
			     "using CHS addressing", sector);
	
	parms->taskfile.disk = 0xE0 | head;
	parms->taskfile.sectnum = sect;
	parms->taskfile.cyllsb = cylinder & 0xFF;
	parms->taskfile.cylmsb = cylinder >> 8;

	break;
      }

    case GRUB_ATA_LBA:
      if (size == 256)
	size = 0;
      parms->taskfile.disk = 0xE0 | ((sector >> 24) & 0x0F);

      parms->taskfile.sectors = size;
      parms->taskfile.lba_low = sector & 0xFF;
      parms->taskfile.lba_mid = (sector >> 8) & 0xFF;
      parms->taskfile.lba_high = (sector >> 16) & 0xFF;
      break;

    case GRUB_ATA_LBA48:
      if (size == 65536)
	size = 0;

      parms->taskfile.disk = 0xE0;

      /* Set "Previous".  */
      parms->taskfile.sectors = size & 0xFF;
      parms->taskfile.lba_low = sector & 0xFF;
      parms->taskfile.lba_mid = (sector >> 8) & 0xFF;
      parms->taskfile.lba_high = (sector >> 16) & 0xFF;

      /* Set "Current".  */
      parms->taskfile.sectors48 = (size >> 8) & 0xFF;
      parms->taskfile.lba48_low = (sector >> 24) & 0xFF;
      parms->taskfile.lba48_mid = (sector >> 32) & 0xFF;
      parms->taskfile.lba48_high = (sector >> 40) & 0xFF;

      break;
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_ata_readwrite (grub_disk_t disk, grub_disk_addr_t sector,
		    grub_size_t size, char *buf, int rw)
{
  struct grub_ata *ata = disk->data;

  grub_ata_addressing_t addressing = ata->addr;
  grub_size_t batch;
  int cmd, cmd_write;
  grub_size_t nsectors = 0;

  grub_dprintf("ata", "grub_ata_readwrite (size=%llu, rw=%d)\n",
	       (unsigned long long) size, rw);

  if (addressing == GRUB_ATA_LBA48 && ((sector + size) >> 28) != 0)
    {
      if (ata->dma)
	{
	  cmd = GRUB_ATA_CMD_READ_SECTORS_DMA_EXT;
	  cmd_write = GRUB_ATA_CMD_WRITE_SECTORS_DMA_EXT;
	}
      else
	{
	  cmd = GRUB_ATA_CMD_READ_SECTORS_EXT;
	  cmd_write = GRUB_ATA_CMD_WRITE_SECTORS_EXT;
	}
    }
  else
    {
      if (addressing == GRUB_ATA_LBA48)
	addressing = GRUB_ATA_LBA;
      if (ata->dma)
	{
	  cmd = GRUB_ATA_CMD_READ_SECTORS_DMA;
	  cmd_write = GRUB_ATA_CMD_WRITE_SECTORS_DMA;
	}
      else
	{
	  cmd = GRUB_ATA_CMD_READ_SECTORS;
	  cmd_write = GRUB_ATA_CMD_WRITE_SECTORS;
	}
    }

  if (addressing != GRUB_ATA_CHS)
    batch = 256;
  else
    batch = 1;

  while (nsectors < size)
    {
      struct grub_disk_ata_pass_through_parms parms;
      grub_err_t err;

      if (size - nsectors < batch)
	batch = size - nsectors;

      grub_dprintf("ata", "rw=%d, sector=%llu, batch=%llu\n", rw, (unsigned long long) sector, (unsigned long long) batch);
      grub_memset (&parms, 0, sizeof (parms));
      grub_ata_setaddress (ata, &parms, sector, batch, addressing);
      parms.taskfile.cmd = (! rw ? cmd : cmd_write);
      parms.buffer = buf;
      parms.size = batch << ata->log_sector_size;
      parms.write = rw;
      if (ata->dma)
	parms.dma = 1;
  
      err = ata->dev->readwrite (ata, &parms, 0);
      if (err)
	return err;
      if (parms.size != batch << ata->log_sector_size)
	return grub_error (GRUB_ERR_READ_ERROR, "incomplete read");
      buf += batch << ata->log_sector_size;
      sector += batch;
      nsectors += batch;
    }

  return GRUB_ERR_NONE;
}



static inline void
grub_ata_real_close (struct grub_ata *ata)
{
  if (ata->dev->close)
    ata->dev->close (ata);
}

static struct grub_ata *
grub_ata_real_open (int id, int bus)
{
  struct grub_ata *ata;
  grub_ata_dev_t p;

  ata = grub_zalloc (sizeof (*ata));
  if (!ata)
    return NULL;
  for (p = grub_ata_dev_list; p; p = p->next)
    {
      grub_err_t err;
      if (p->open (id, bus, ata))
	{
	  grub_errno = GRUB_ERR_NONE;
	  continue;
	}
      ata->dev = p;
      /* Use the IDENTIFY DEVICE command to query the device.  */
      err = grub_ata_identify (ata);
      if (err)
	{
	  if (!grub_errno)
	    grub_error (GRUB_ERR_UNKNOWN_DEVICE, "no such ATA device");
	  grub_free (ata);
	  return NULL;
	}
      return ata;
    }
  grub_free (ata);
  grub_error (GRUB_ERR_UNKNOWN_DEVICE, "no such ATA device");
  return NULL;
}

/* Context for grub_ata_iterate.  */
struct grub_ata_iterate_ctx
{
  grub_disk_dev_iterate_hook_t hook;
  void *hook_data;
};

/* Helper for grub_ata_iterate.  */
static int
grub_ata_iterate_iter (int id, int bus, void *data)
{
  struct grub_ata_iterate_ctx *ctx = data;
  struct grub_ata *ata;
  int ret;
  char devname[40];

  ata = grub_ata_real_open (id, bus);

  if (!ata)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }
  if (ata->atapi)
    {
      grub_ata_real_close (ata);
      return 0;
    }
  grub_snprintf (devname, sizeof (devname), 
		 "%s%d", grub_scsi_names[id], bus);
  ret = ctx->hook (devname, ctx->hook_data);
  grub_ata_real_close (ata);
  return ret;
}

static int
grub_ata_iterate (grub_disk_dev_iterate_hook_t hook, void *hook_data,
		  grub_disk_pull_t pull)
{
  struct grub_ata_iterate_ctx ctx = { hook, hook_data };
  grub_ata_dev_t p;
  
  for (p = grub_ata_dev_list; p; p = p->next)
    if (p->iterate && p->iterate (grub_ata_iterate_iter, &ctx, pull))
      return 1;
  return 0;
}

static grub_err_t
grub_ata_open (const char *name, grub_disk_t disk)
{
  unsigned id, bus;
  struct grub_ata *ata;

  for (id = 0; id < GRUB_SCSI_NUM_SUBSYSTEMS; id++)
    if (grub_strncmp (grub_scsi_names[id], name,
		      grub_strlen (grub_scsi_names[id])) == 0
	&& grub_isdigit (name[grub_strlen (grub_scsi_names[id])]))
      break;
  if (id == GRUB_SCSI_NUM_SUBSYSTEMS)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "not an ATA harddisk");
  bus = grub_strtoul (name + grub_strlen (grub_scsi_names[id]), 0, 0);
  ata = grub_ata_real_open (id, bus);
  if (!ata)
    return grub_errno;

  if (ata->atapi)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "not an ATA harddisk");

  disk->total_sectors = ata->size;
  disk->max_agglomerate = (ata->maxbuffer >> (GRUB_DISK_CACHE_BITS + GRUB_DISK_SECTOR_BITS));
  if (disk->max_agglomerate > (256U >> (GRUB_DISK_CACHE_BITS + GRUB_DISK_SECTOR_BITS - ata->log_sector_size)))
    disk->max_agglomerate = (256U >> (GRUB_DISK_CACHE_BITS + GRUB_DISK_SECTOR_BITS - ata->log_sector_size));

  disk->log_sector_size = ata->log_sector_size;

  disk->id = grub_make_scsi_id (id, bus, 0);

  disk->data = ata;

  return 0;
}

static void
grub_ata_close (grub_disk_t disk)
{
  struct grub_ata *ata = disk->data;
  grub_ata_real_close (ata);
}

static grub_err_t
grub_ata_read (grub_disk_t disk, grub_disk_addr_t sector,
	       grub_size_t size, char *buf)
{
  return grub_ata_readwrite (disk, sector, size, buf, 0);
}

static grub_err_t
grub_ata_write (grub_disk_t disk,
		grub_disk_addr_t sector,
		grub_size_t size,
		const char *buf)
{
  return grub_ata_readwrite (disk, sector, size, (char *) buf, 1);
}

static struct grub_disk_dev grub_atadisk_dev =
  {
    .name = "ATA",
    .id = GRUB_DISK_DEVICE_ATA_ID,
    .disk_iterate = grub_ata_iterate,
    .disk_open = grub_ata_open,
    .disk_close = grub_ata_close,
    .disk_read = grub_ata_read,
    .disk_write = grub_ata_write,
    .next = 0
  };



/* ATAPI code.  */

static grub_err_t
grub_atapi_read (struct grub_scsi *scsi, grub_size_t cmdsize, char *cmd,
		 grub_size_t size, char *buf)
{
  struct grub_ata *dev = scsi->data;
  struct grub_disk_ata_pass_through_parms parms;
  grub_err_t err;

  grub_dprintf("ata", "grub_atapi_read (size=%llu)\n", (unsigned long long) size);
  grub_memset (&parms, 0, sizeof (parms));

  parms.taskfile.disk = 0;
  parms.taskfile.features = 0;
  parms.taskfile.atapi_ireason = 0;
  parms.taskfile.atapi_cnthigh = size >> 8;
  parms.taskfile.atapi_cntlow = size & 0xff;
  parms.taskfile.cmd = GRUB_ATA_CMD_PACKET;
  parms.cmd = cmd;
  parms.cmdsize = cmdsize;

  parms.size = size;
  parms.buffer = buf;
  
  err = dev->dev->readwrite (dev, &parms, 0);
  if (err)
    return err;

  if (parms.size != size)
    return grub_error (GRUB_ERR_READ_ERROR, "incomplete ATAPI read");
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_atapi_write (struct grub_scsi *scsi __attribute__((unused)),
		  grub_size_t cmdsize __attribute__((unused)),
		  char *cmd __attribute__((unused)),
		  grub_size_t size __attribute__((unused)),
		  const char *buf __attribute__((unused)))
{
  // XXX: scsi.mod does not use write yet.
  return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET, "ATAPI write not implemented");
}

static grub_err_t
grub_atapi_open (int id, int bus, struct grub_scsi *scsi)
{
  struct grub_ata *ata;

  ata = grub_ata_real_open (id, bus);
  if (!ata)
    return grub_errno;
    
  if (! ata->atapi)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "no such ATAPI device");

  scsi->data = ata;
  scsi->luns = 1;

  return GRUB_ERR_NONE;
}

/* Context for grub_atapi_iterate.  */
struct grub_atapi_iterate_ctx
{
  grub_scsi_dev_iterate_hook_t hook;
  void *hook_data;
};

/* Helper for grub_atapi_iterate.  */
static int
grub_atapi_iterate_iter (int id, int bus, void *data)
{
  struct grub_atapi_iterate_ctx *ctx = data;
  struct grub_ata *ata;
  int ret;

  ata = grub_ata_real_open (id, bus);

  if (!ata)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }
  if (!ata->atapi)
    {
      grub_ata_real_close (ata);
      return 0;
    }
  ret = ctx->hook (id, bus, 1, ctx->hook_data);
  grub_ata_real_close (ata);
  return ret;
}

static int
grub_atapi_iterate (grub_scsi_dev_iterate_hook_t hook, void *hook_data,
		    grub_disk_pull_t pull)
{
  struct grub_atapi_iterate_ctx ctx = { hook, hook_data };
  grub_ata_dev_t p;
  
  for (p = grub_ata_dev_list; p; p = p->next)
    if (p->iterate && p->iterate (grub_atapi_iterate_iter, &ctx, pull))
      return 1;
  return 0;
}

static void
grub_atapi_close (grub_scsi_t disk)
{
  struct grub_ata *ata = disk->data;
  grub_ata_real_close (ata);
}


void
grub_ata_dev_register (grub_ata_dev_t dev)
{
  dev->next = grub_ata_dev_list;
  grub_ata_dev_list = dev;
}

void
grub_ata_dev_unregister (grub_ata_dev_t dev)
{
  grub_ata_dev_t *p, q;

  for (p = &grub_ata_dev_list, q = *p; q; p = &(q->next), q = q->next)
    if (q == dev)
      {
        *p = q->next;
	break;
      }
}

static struct grub_scsi_dev grub_atapi_dev =
  {
    .iterate = grub_atapi_iterate,
    .open = grub_atapi_open,
    .close = grub_atapi_close,
    .read = grub_atapi_read,
    .write = grub_atapi_write,
    .next = 0
  };



GRUB_MOD_INIT(ata)
{
  grub_disk_dev_register (&grub_atadisk_dev);

  /* ATAPI devices are handled by scsi.mod.  */
  grub_scsi_dev_register (&grub_atapi_dev);
}

GRUB_MOD_FINI(ata)
{
  grub_scsi_dev_unregister (&grub_atapi_dev);
  grub_disk_dev_unregister (&grub_atadisk_dev);
}
