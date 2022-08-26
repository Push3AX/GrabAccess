/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2000,2001,2002,2003,2004,2005,2007,2008,2009,2010  Free Software Foundation, Inc.
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

#include <grub/serial.h>
#include <grub/usbserial.h>
#include <grub/dl.h>

GRUB_MOD_LICENSE ("GPLv3+");

void
grub_usbserial_fini (struct grub_serial_port *port)
{
  port->usbdev->config[port->configno].interf[port->interfno].detach_hook = 0;
  port->usbdev->config[port->configno].interf[port->interfno].attached = 0;
}

void
grub_usbserial_detach (grub_usb_device_t usbdev, int configno, int interfno)
{
  static struct grub_serial_port *port;
  port = usbdev->config[configno].interf[interfno].detach_data;

  grub_serial_unregister (port);
}

static int usbnum = 0;

int
grub_usbserial_attach (grub_usb_device_t usbdev, int configno, int interfno,
		       struct grub_serial_driver *driver, int in_endp,
		       int out_endp)
{
  struct grub_serial_port *port;
  int j;
  struct grub_usb_desc_if *interf;
  grub_usb_err_t err = GRUB_USB_ERR_NONE;

  interf = usbdev->config[configno].interf[interfno].descif;

  port = grub_zalloc (sizeof (*port));
  if (!port)
    {
      grub_print_error ();
      return 0;
    }

  port->name = grub_xasprintf ("usb%d", usbnum++);
  if (!port->name)
    {
      grub_free (port);
      grub_print_error ();
      return 0;
    }

  port->usbdev = usbdev;
  port->driver = driver;
  for (j = 0; j < interf->endpointcnt; j++)
    {
      struct grub_usb_desc_endp *endp;
      endp = usbdev->config[0].interf[interfno].descendp[j];

      if ((endp->endp_addr & 128) && (endp->attrib & 3) == 2
	  && (in_endp == GRUB_USB_SERIAL_ENDPOINT_LAST_MATCHING
	      || in_endp == endp->endp_addr))
	{
	  /* Bulk IN endpoint.  */
	  port->in_endp = endp;
	}
      else if (!(endp->endp_addr & 128) && (endp->attrib & 3) == 2
	       && (out_endp == GRUB_USB_SERIAL_ENDPOINT_LAST_MATCHING
		   || out_endp == endp->endp_addr))
	{
	  /* Bulk OUT endpoint.  */
	  port->out_endp = endp;
	}
    }

  /* Configure device */
  if (port->out_endp && port->in_endp)
    err = grub_usb_set_configuration (usbdev, configno + 1);
  
  if (!port->out_endp || !port->in_endp || err)
    {
      grub_free (port->name);
      grub_free (port);
      return 0;
    }

  port->configno = configno;
  port->interfno = interfno;

  grub_serial_config_defaults (port);
  grub_serial_register (port);

  port->usbdev->config[port->configno].interf[port->interfno].detach_hook
    = grub_usbserial_detach;
  port->usbdev->config[port->configno].interf[port->interfno].detach_data
    = port;

  return 1;
}

int
grub_usbserial_fetch (struct grub_serial_port *port, grub_size_t header_size)
{
  grub_usb_err_t err;
  grub_size_t actual;

  if (port->bufstart < port->bufend)
    return port->buf[port->bufstart++];

  err = grub_usb_bulk_read_extended (port->usbdev, port->in_endp,
				     sizeof (port->buf), port->buf, 10,
				     &actual);
  if (err != GRUB_USB_ERR_NONE)
    return -1;

  port->bufstart = header_size;
  port->bufend = actual;
  if (port->bufstart >= port->bufend)
    return -1;

  return port->buf[port->bufstart++];
}
