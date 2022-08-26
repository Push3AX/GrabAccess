/* usbms.c - USB Mass Storage Support.  */
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

#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/usb.h>
#include <grub/scsi.h>
#include <grub/scsicmd.h>
#include <grub/misc.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define GRUB_USBMS_DIRECTION_BIT	7

/* Length of CBI command should be always 12 bytes */
#define GRUB_USBMS_CBI_CMD_SIZE         12
/* CBI class-specific USB request ADSC - it sends CBI (scsi) command to
 * device in DATA stage */
#define GRUB_USBMS_CBI_ADSC_REQ         0x00

/* The USB Mass Storage Command Block Wrapper.  */
struct grub_usbms_cbw
{
  grub_uint32_t signature;
  grub_uint32_t tag;
  grub_uint32_t transfer_length;
  grub_uint8_t flags;
  grub_uint8_t lun;
  grub_uint8_t length;
  grub_uint8_t cbwcb[16];
} GRUB_PACKED;

struct grub_usbms_csw
{
  grub_uint32_t signature;
  grub_uint32_t tag;
  grub_uint32_t residue;
  grub_uint8_t status;
} GRUB_PACKED;

struct grub_usbms_dev
{
  struct grub_usb_device *dev;

  int luns;

  int config;
  int interface;
  struct grub_usb_desc_endp *in;
  struct grub_usb_desc_endp *out;

  int subclass;
  int protocol;
  struct grub_usb_desc_endp *intrpt;
};
typedef struct grub_usbms_dev *grub_usbms_dev_t;

/* FIXME: remove limit.  */
#define MAX_USBMS_DEVICES 128
static grub_usbms_dev_t grub_usbms_devices[MAX_USBMS_DEVICES];
static int first_available_slot = 0;

static grub_usb_err_t
grub_usbms_cbi_cmd (grub_usb_device_t dev, int interface,
                    grub_uint8_t *cbicb)
{
  return grub_usb_control_msg (dev,
                               GRUB_USB_REQTYPE_CLASS_INTERFACE_OUT,
                               GRUB_USBMS_CBI_ADSC_REQ, 0, interface,
                               GRUB_USBMS_CBI_CMD_SIZE, (char*)cbicb);
}

static grub_usb_err_t
grub_usbms_cbi_reset (grub_usb_device_t dev, int interface)
{
  /* Prepare array with Command Block Reset (=CBR) */
  /* CBI specific communication reset command should be send to device
   * via CBI USB class specific request ADCS */
  struct grub_cbi_reset
    {
      grub_uint8_t opcode; /* 0x1d = SEND DIAGNOSTIC */
      grub_uint8_t lun; /* 7-5 LUN, 4-0 flags - for CBR always = 0x04 */
      grub_uint8_t pad[10];
      /* XXX: There is collision between CBI and UFI specifications:
       *      CBI says 0xff, UFI says 0x00 ... probably it does
       *      not matter ... (?) */
    } cbicb = { 0x1d, 0x04,
                { 0xff, 0xff, 0xff, 0xff, 0xff,
                  0xff, 0xff, 0xff, 0xff, 0xff }
              };
  
  return grub_usbms_cbi_cmd (dev, interface, (grub_uint8_t *)&cbicb);
}

static grub_usb_err_t
grub_usbms_bo_reset (grub_usb_device_t dev, int interface)
{
  return grub_usb_control_msg (dev, 0x21, 255, 0, interface, 0, 0);
}

static grub_usb_err_t
grub_usbms_reset (grub_usbms_dev_t dev)
{
  if (dev->protocol == GRUB_USBMS_PROTOCOL_BULK)
    return grub_usbms_bo_reset (dev->dev, dev->interface);
  else
    return grub_usbms_cbi_reset (dev->dev, dev->interface);
}

static void
grub_usbms_detach (grub_usb_device_t usbdev, int config, int interface)
{
  unsigned i;
  for (i = 0; i < ARRAY_SIZE (grub_usbms_devices); i++)
    if (grub_usbms_devices[i] && grub_usbms_devices[i]->dev == usbdev
	&& grub_usbms_devices[i]->interface == interface
	&& grub_usbms_devices[i]->config == config)
      {
	grub_free (grub_usbms_devices[i]);
	grub_usbms_devices[i] = 0;
      }
}

static int
grub_usbms_attach (grub_usb_device_t usbdev, int configno, int interfno)
{
  struct grub_usb_desc_if *interf
    = usbdev->config[configno].interf[interfno].descif;
  int j;
  grub_uint8_t luns = 0;
  unsigned curnum;
  grub_usb_err_t err = GRUB_USB_ERR_NONE;

  grub_boot_time ("Attaching USB mass storage");

  if (first_available_slot == ARRAY_SIZE (grub_usbms_devices))
    return 0;

  curnum = first_available_slot;
  first_available_slot++;

  interf = usbdev->config[configno].interf[interfno].descif;

  if ((interf->subclass != GRUB_USBMS_SUBCLASS_BULK
       /* Experimental support of RBC, MMC-2, UFI, SFF-8070i devices */
       && interf->subclass != GRUB_USBMS_SUBCLASS_RBC
       && interf->subclass != GRUB_USBMS_SUBCLASS_MMC2
       && interf->subclass != GRUB_USBMS_SUBCLASS_UFI 
       && interf->subclass != GRUB_USBMS_SUBCLASS_SFF8070 )
      || (interf->protocol != GRUB_USBMS_PROTOCOL_BULK
          && interf->protocol != GRUB_USBMS_PROTOCOL_CBI
          && interf->protocol != GRUB_USBMS_PROTOCOL_CB))
    return 0;

  grub_usbms_devices[curnum] = grub_zalloc (sizeof (struct grub_usbms_dev));
  if (! grub_usbms_devices[curnum])
    return 0;

  grub_usbms_devices[curnum]->dev = usbdev;
  grub_usbms_devices[curnum]->interface = interfno;
  grub_usbms_devices[curnum]->subclass = interf->subclass;
  grub_usbms_devices[curnum]->protocol = interf->protocol;

  grub_dprintf ("usbms", "alive\n");

  /* Iterate over all endpoints of this interface, at least a
     IN and OUT bulk endpoint are required.  */
  for (j = 0; j < interf->endpointcnt; j++)
    {
      struct grub_usb_desc_endp *endp;
      endp = usbdev->config[0].interf[interfno].descendp[j];

      if ((endp->endp_addr & 128) && (endp->attrib & 3) == 2)
	/* Bulk IN endpoint.  */
	grub_usbms_devices[curnum]->in = endp;
      else if (!(endp->endp_addr & 128) && (endp->attrib & 3) == 2)
        /* Bulk OUT endpoint.  */
	grub_usbms_devices[curnum]->out = endp;
      else if ((endp->endp_addr & 128) && (endp->attrib & 3) == 3)
        /* Interrupt (IN) endpoint.  */
	grub_usbms_devices[curnum]->intrpt = endp;
    }

  if (!grub_usbms_devices[curnum]->in || !grub_usbms_devices[curnum]->out
      || ((grub_usbms_devices[curnum]->protocol == GRUB_USBMS_PROTOCOL_CBI)
          && !grub_usbms_devices[curnum]->intrpt))
    {
      grub_free (grub_usbms_devices[curnum]);
      grub_usbms_devices[curnum] = 0;
      return 0;
    }

  grub_dprintf ("usbms", "alive\n");

  /* XXX: Activate the first configuration.  */
  grub_usb_set_configuration (usbdev, 1);

  /* Query the amount of LUNs.  */
  if (grub_usbms_devices[curnum]->protocol == GRUB_USBMS_PROTOCOL_BULK)
    { /* Only Bulk only devices support Get Max LUN command */
      err = grub_usb_control_msg (usbdev, 0xA1, 254, 0, interfno, 1, (char *) &luns);
  		
      if (err)
        {
          /* In case of a stall, clear the stall.  */
          if (err == GRUB_USB_ERR_STALL)
	    {
	      grub_usb_clear_halt (usbdev, grub_usbms_devices[curnum]->in->endp_addr);
	      grub_usb_clear_halt (usbdev, grub_usbms_devices[curnum]->out->endp_addr);
	    }
          /* Just set the amount of LUNs to one.  */
          grub_errno = GRUB_ERR_NONE;
          grub_usbms_devices[curnum]->luns = 1;
        }
      else
        /* luns = 0 means one LUN with ID 0 present ! */
        /* We get from device not number of LUNs but highest
         * LUN number. LUNs are numbered from 0, 
         * i.e. number of LUNs is luns+1 ! */
        grub_usbms_devices[curnum]->luns = luns + 1;
    }
  else
    /* XXX: Does CBI devices support multiple LUNs ?
     * I.e., should we detect number of device's LUNs ? (How?) */
    grub_usbms_devices[curnum]->luns = 1;
    
  grub_dprintf ("usbms", "alive\n");

  usbdev->config[configno].interf[interfno].detach_hook = grub_usbms_detach;

  grub_boot_time ("Attached USB mass storage");

#if 0 /* All this part should be probably deleted.
       * This make trouble on some devices if they are not in
       * Phase Error state - and there they should be not in such state...
       * Bulk only mass storage reset procedure should be used only
       * on place and in time when it is really necessary. */
  /* Reset recovery procedure */
  /* Bulk-Only Mass Storage Reset, after the reset commands
     will be accepted.  */
  grub_usbms_reset (usbdev, i);
  grub_usb_clear_halt (usbdev, usbms->in->endp_addr);
  grub_usb_clear_halt (usbdev, usbms->out->endp_addr);
#endif

  return 1;
}



static int
grub_usbms_iterate (grub_scsi_dev_iterate_hook_t hook, void *hook_data,
		    grub_disk_pull_t pull)
{
  unsigned i;

  if (pull != GRUB_DISK_PULL_NONE)
    return 0;

  grub_usb_poll_devices (1);

  for (i = 0; i < ARRAY_SIZE (grub_usbms_devices); i++)
    if (grub_usbms_devices[i])
      {
	if (hook (GRUB_SCSI_SUBSYSTEM_USBMS, i, grub_usbms_devices[i]->luns,
		  hook_data))
	  return 1;
      }

  return 0;
}

static grub_err_t
grub_usbms_transfer_bo (struct grub_scsi *scsi, grub_size_t cmdsize, char *cmd,
		        grub_size_t size, char *buf, int read_write)
{
  struct grub_usbms_cbw cbw;
  grub_usbms_dev_t dev = (grub_usbms_dev_t) scsi->data;
  struct grub_usbms_csw status;
  static grub_uint32_t tag = 0;
  grub_usb_err_t err = GRUB_USB_ERR_NONE;
  grub_usb_err_t errCSW = GRUB_USB_ERR_NONE;
  int retrycnt = 3 + 1;
  
  tag++;

 retry:
  retrycnt--;
  if (retrycnt == 0)
    return grub_error (GRUB_ERR_IO, "USB Mass Storage stalled");

  /* Setup the request.  */
  grub_memset (&cbw, 0, sizeof (cbw));
  cbw.signature = grub_cpu_to_le32_compile_time (0x43425355);
  cbw.tag = tag;
  cbw.transfer_length = grub_cpu_to_le32 (size);
  cbw.flags = (!read_write) << GRUB_USBMS_DIRECTION_BIT;
  cbw.lun = scsi->lun; /* In USB MS CBW are LUN bits on another place than in SCSI CDB, both should be set correctly. */
  cbw.length = cmdsize;
  grub_memcpy (cbw.cbwcb, cmd, cmdsize);
  
  /* Debug print of CBW content. */
  grub_dprintf ("usb", "CBW: sign=0x%08x tag=0x%08x len=0x%08x\n",
  	cbw.signature, cbw.tag, cbw.transfer_length);
  grub_dprintf ("usb", "CBW: flags=0x%02x lun=0x%02x CB_len=0x%02x\n",
  	cbw.flags, cbw.lun, cbw.length);
  grub_dprintf ("usb", "CBW: cmd:\n %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
  	cbw.cbwcb[ 0], cbw.cbwcb[ 1], cbw.cbwcb[ 2], cbw.cbwcb[ 3],
  	cbw.cbwcb[ 4], cbw.cbwcb[ 5], cbw.cbwcb[ 6], cbw.cbwcb[ 7],
  	cbw.cbwcb[ 8], cbw.cbwcb[ 9], cbw.cbwcb[10], cbw.cbwcb[11],
  	cbw.cbwcb[12], cbw.cbwcb[13], cbw.cbwcb[14], cbw.cbwcb[15]);

  /* Write the request.
   * XXX: Error recovery is maybe still not fully correct. */
  err = grub_usb_bulk_write (dev->dev, dev->out,
			     sizeof (cbw), (char *) &cbw);
  if (err)
    {
      if (err == GRUB_USB_ERR_STALL)
	{
	  grub_usb_clear_halt (dev->dev, dev->out->endp_addr);
	  goto CheckCSW;
	}
      goto retry;
    }

  /* Read/write the data, (maybe) according to specification.  */
  if (size && (read_write == 0))
    {
      err = grub_usb_bulk_read (dev->dev, dev->in, size, buf);
      grub_dprintf ("usb", "read: %d %d\n", err, GRUB_USB_ERR_STALL); 
      if (err)
        {
          if (err == GRUB_USB_ERR_STALL)
	    grub_usb_clear_halt (dev->dev, dev->in->endp_addr);
          goto CheckCSW;
        }
      /* Debug print of received data. */
      grub_dprintf ("usb", "buf:\n");
      if (size <= 64)
	{
	  unsigned i;
	  for (i = 0; i < size; i++)
	    grub_dprintf ("usb", "0x%02x: 0x%02x\n", i, buf[i]);
	}
      else
          grub_dprintf ("usb", "Too much data for debug print...\n");
    }
  else if (size)
    {
      err = grub_usb_bulk_write (dev->dev, dev->out, size, buf);
      grub_dprintf ("usb", "write: %d %d\n", err, GRUB_USB_ERR_STALL);
      grub_dprintf ("usb", "First 16 bytes of sent data:\n %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
  	buf[ 0], buf[ 1], buf[ 2], buf[ 3],
  	buf[ 4], buf[ 5], buf[ 6], buf[ 7],
  	buf[ 8], buf[ 9], buf[10], buf[11],
  	buf[12], buf[13], buf[14], buf[15]);
      if (err)
        {
          if (err == GRUB_USB_ERR_STALL)
	    grub_usb_clear_halt (dev->dev, dev->out->endp_addr);
          goto CheckCSW;
        }
      /* Debug print of sent data. */
      if (size <= 256)
	{
	  unsigned i;
	  for (i=0; i<size; i++)
	    grub_dprintf ("usb", "0x%02x: 0x%02x\n", i, buf[i]);
	}
      else
          grub_dprintf ("usb", "Too much data for debug print...\n");
    }

  /* Read the status - (maybe) according to specification.  */
CheckCSW:
  errCSW = grub_usb_bulk_read (dev->dev, dev->in,
		    sizeof (status), (char *) &status);
  if (errCSW)
    {
      grub_usb_clear_halt (dev->dev, dev->in->endp_addr);
      errCSW = grub_usb_bulk_read (dev->dev, dev->in,
			        sizeof (status), (char *) &status);
      if (errCSW)
        { /* Bulk-only reset device. */
          grub_dprintf ("usb", "Bulk-only reset device - errCSW\n");
          grub_usbms_reset (dev);
          grub_usb_clear_halt (dev->dev, dev->in->endp_addr);
          grub_usb_clear_halt (dev->dev, dev->out->endp_addr);
	  goto retry;
        }
    }

  /* Debug print of CSW content. */
  grub_dprintf ("usb", "CSW: sign=0x%08x tag=0x%08x resid=0x%08x\n",
  	status.signature, status.tag, status.residue);
  grub_dprintf ("usb", "CSW: status=0x%02x\n", status.status);
  
  /* If phase error or not valid signature, do bulk-only reset device. */
  if ((status.status == 2) ||
      (status.signature != grub_cpu_to_le32_compile_time(0x53425355)))
    { /* Bulk-only reset device. */
      grub_dprintf ("usb", "Bulk-only reset device - bad status\n");
      grub_usbms_reset (dev);
      grub_usb_clear_halt (dev->dev, dev->in->endp_addr);
      grub_usb_clear_halt (dev->dev, dev->out->endp_addr);

      goto retry;
    }

  /* If "command failed" status or data transfer failed -> error */
  if ((status.status || err) && !read_write)
    return grub_error (GRUB_ERR_READ_ERROR,
		       "error communication with USB Mass Storage device");
  else if ((status.status || err) && read_write)
    return grub_error (GRUB_ERR_WRITE_ERROR,
		       "error communication with USB Mass Storage device");

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_usbms_transfer_cbi (struct grub_scsi *scsi, grub_size_t cmdsize, char *cmd,
		        grub_size_t size, char *buf, int read_write)
{
  grub_usbms_dev_t dev = (grub_usbms_dev_t) scsi->data;
  int retrycnt = 3 + 1;
  grub_usb_err_t err = GRUB_USB_ERR_NONE;
  grub_uint8_t cbicb[GRUB_USBMS_CBI_CMD_SIZE];
  grub_uint16_t status;
  
 retry:
  retrycnt--;
  if (retrycnt == 0)
    return grub_error (GRUB_ERR_IO, "USB Mass Storage CBI failed");

  /* Setup the request.  */
  grub_memset (cbicb, 0, sizeof (cbicb));
  grub_memcpy (cbicb, cmd,
               cmdsize >= GRUB_USBMS_CBI_CMD_SIZE
                 ? GRUB_USBMS_CBI_CMD_SIZE
                 : cmdsize);
  
  /* Debug print of CBIcb content. */
  grub_dprintf ("usb", "cbicb:\n %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
  	cbicb[ 0], cbicb[ 1], cbicb[ 2], cbicb[ 3],
  	cbicb[ 4], cbicb[ 5], cbicb[ 6], cbicb[ 7],
  	cbicb[ 8], cbicb[ 9], cbicb[10], cbicb[11]);

  /* Write the request.
   * XXX: Error recovery is maybe not correct. */
  err = grub_usbms_cbi_cmd (dev->dev, dev->interface, cbicb);
  if (err)
    {
      grub_dprintf ("usb", "CBI cmdcb setup err=%d\n", err);
      if (err == GRUB_USB_ERR_STALL)
	{
	  /* Stall in this place probably means bad or unsupported
	   * command, so we will not try it again. */
         return grub_error (GRUB_ERR_IO, "USB Mass Storage CBI request failed");
	}
      else if (dev->protocol == GRUB_USBMS_PROTOCOL_CBI)
        {
          /* Try to get status from interrupt pipe */
          err = grub_usb_bulk_read (dev->dev, dev->intrpt,
                                    2, (char*)&status);
          grub_dprintf ("usb", "CBI cmdcb setup status: err=%d, status=0x%x\n", err, status);
        }
        /* Any other error could be transport problem, try it again */
        goto retry;
    }

  /* Read/write the data, (maybe) according to specification.  */
  if (size && (read_write == 0))
    {
      err = grub_usb_bulk_read (dev->dev, dev->in, size, buf);
      grub_dprintf ("usb", "read: %d\n", err); 
      if (err)
        {
          if (err == GRUB_USB_ERR_STALL)
            grub_usb_clear_halt (dev->dev, dev->in->endp_addr);
          goto retry;
        }
    }
  else if (size)
    {
      err = grub_usb_bulk_write (dev->dev, dev->out, size, buf);
      grub_dprintf ("usb", "write: %d\n", err);
      if (err)
        {
          if (err == GRUB_USB_ERR_STALL)
	    grub_usb_clear_halt (dev->dev, dev->out->endp_addr);
          goto retry;
        }
    }

  /* XXX: It is not clear to me yet, how to check status of CBI
   * data transfer on devices without interrupt pipe.
   * AFAIK there is probably no status phase to indicate possibly
   * bad transported data.
   * Maybe we should do check on higher level, i.e. issue RequestSense
   * command (we do it already in scsi.c) and check returned values
   * (we do not it yet) - ? */
  if (dev->protocol == GRUB_USBMS_PROTOCOL_CBI)
    { /* Check status in interrupt pipe */
      err = grub_usb_bulk_read (dev->dev, dev->intrpt,
                                2, (char*)&status);
      grub_dprintf ("usb", "read status: %d\n", err);
      if (err)
        {
          /* Try to reset device, because it is probably not standard
           * situation */
          grub_usbms_reset (dev);
          grub_usb_clear_halt (dev->dev, dev->in->endp_addr);
          grub_usb_clear_halt (dev->dev, dev->out->endp_addr);
          grub_usb_clear_halt (dev->dev, dev->intrpt->endp_addr);
          goto retry;
        }
      if (dev->subclass == GRUB_USBMS_SUBCLASS_UFI)
        {
          /* These devices should return bASC and bASCQ */
          if (status != 0)
            /* Some error, currently we don't care what it is... */
            goto retry;
        }
      else if (dev->subclass == GRUB_USBMS_SUBCLASS_RBC)
        {
          /* XXX: I don't understand what returns RBC subclass devices,
           * so I don't check it - maybe somebody helps ? */
        }
      else
        {
          /* Any other device should return bType = 0 and some bValue */
          if (status & 0xff)
            return grub_error (GRUB_ERR_IO, "USB Mass Storage CBI status type != 0");
          status = (status & 0x0300) >> 8;
          switch (status)
            {
              case 0 : /* OK */
                break;
              case 1 : /* Fail */
                goto retry;
                break;
              case 2 : /* Phase error */
              case 3 : /* Persistent Failure */
                grub_dprintf ("usb", "CBI reset device - phase error or persistent failure\n");
                grub_usbms_reset (dev);
                grub_usb_clear_halt (dev->dev, dev->in->endp_addr);
                grub_usb_clear_halt (dev->dev, dev->out->endp_addr);
                grub_usb_clear_halt (dev->dev, dev->intrpt->endp_addr);
                goto retry;
                break;
            }
        }
    }

  if (err)
    return grub_error (GRUB_ERR_IO, "USB error %d", err);
    
  return GRUB_ERR_NONE;
}


static grub_err_t
grub_usbms_transfer (struct grub_scsi *scsi, grub_size_t cmdsize, char *cmd,
		        grub_size_t size, char *buf, int read_write)
{
  grub_usbms_dev_t dev = (grub_usbms_dev_t) scsi->data;

  if (dev->protocol == GRUB_USBMS_PROTOCOL_BULK)
    return grub_usbms_transfer_bo (scsi, cmdsize, cmd, size, buf,
                                   read_write);
  else
    return grub_usbms_transfer_cbi (scsi, cmdsize, cmd, size, buf,
                                    read_write);
}

static grub_err_t
grub_usbms_read (struct grub_scsi *scsi, grub_size_t cmdsize, char *cmd,
		 grub_size_t size, char *buf)
{
  return grub_usbms_transfer (scsi, cmdsize, cmd, size, buf, 0);
}

static grub_err_t
grub_usbms_write (struct grub_scsi *scsi, grub_size_t cmdsize, char *cmd,
		  grub_size_t size, const char *buf)
{
  return grub_usbms_transfer (scsi, cmdsize, cmd, size, (char *) buf, 1);
}

static grub_err_t
grub_usbms_open (int id, int devnum, struct grub_scsi *scsi)
{
  if (id != GRUB_SCSI_SUBSYSTEM_USBMS)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE,
		       "not USB Mass Storage device");

  if (!grub_usbms_devices[devnum])
    grub_usb_poll_devices (1);

  if (!grub_usbms_devices[devnum])
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE,
		       "unknown USB Mass Storage device");

  scsi->data = grub_usbms_devices[devnum];
  scsi->luns = grub_usbms_devices[devnum]->luns;

  return GRUB_ERR_NONE;
}

static struct grub_scsi_dev grub_usbms_dev =
  {
    .iterate = grub_usbms_iterate,
    .open = grub_usbms_open,
    .read = grub_usbms_read,
    .write = grub_usbms_write
  };

static struct grub_usb_attach_desc attach_hook =
{
  .class = GRUB_USB_CLASS_MASS_STORAGE,
  .hook = grub_usbms_attach
};

GRUB_MOD_INIT(usbms)
{
  grub_usb_register_attach_hook_class (&attach_hook);
  grub_scsi_dev_register (&grub_usbms_dev);
}

GRUB_MOD_FINI(usbms)
{
  unsigned i;
  for (i = 0; i < ARRAY_SIZE (grub_usbms_devices); i++)
    {
      grub_usbms_devices[i]->dev->config[grub_usbms_devices[i]->config]
	.interf[grub_usbms_devices[i]->interface].detach_hook = 0;
      grub_usbms_devices[i]->dev->config[grub_usbms_devices[i]->config]
	.interf[grub_usbms_devices[i]->interface].attached = 0;
    }
  grub_usb_unregister_attach_hook_class (&attach_hook);
  grub_scsi_dev_unregister (&grub_usbms_dev);
}
