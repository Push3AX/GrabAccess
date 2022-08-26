/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2000,2001,2002,2003,2004,2005,2007,2008,2009,2010,2013  Free Software Foundation, Inc.
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
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/usb.h>
#include <grub/usbserial.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");


/* Fetch a key.  */
static int
usbdebug_late_hw_fetch (struct grub_serial_port *port)
{
  return grub_usbserial_fetch (port, 0);
}

/* Put a character.  */
static void
usbdebug_late_hw_put (struct grub_serial_port *port, const int c)
{
  char cc = c;

  grub_usb_bulk_write (port->usbdev, port->out_endp, 1, &cc);
}

static grub_err_t
usbdebug_late_hw_configure (struct grub_serial_port *port __attribute__ ((unused)),
			    struct grub_serial_config *config __attribute__ ((unused)))
{
  return GRUB_ERR_NONE;
}

static struct grub_serial_driver grub_usbdebug_late_driver =
  {
    .configure = usbdebug_late_hw_configure,
    .fetch = usbdebug_late_hw_fetch,
    .put = usbdebug_late_hw_put,
    .fini = grub_usbserial_fini
  };

static int
grub_usbdebug_late_attach (grub_usb_device_t usbdev, int configno, int interfno)
{
  grub_usb_err_t err;
  struct grub_usb_desc_debug debugdesc;

  err = grub_usb_get_descriptor (usbdev, GRUB_USB_DESCRIPTOR_DEBUG, configno,
				 sizeof (debugdesc), (char *) &debugdesc);
  if (err)
    return 0;

  return grub_usbserial_attach (usbdev, configno, interfno,
				&grub_usbdebug_late_driver,
				debugdesc.in_endp, debugdesc.out_endp);
}

static struct grub_usb_attach_desc attach_hook =
{
  .class = 0xff,
  .hook = grub_usbdebug_late_attach
};

GRUB_MOD_INIT(usbserial_usbdebug_late)
{
  grub_usb_register_attach_hook_class (&attach_hook);
}

GRUB_MOD_FINI(usbserial_usbdebug_late)
{
  grub_serial_unregister_driver (&grub_usbdebug_late_driver);
  grub_usb_unregister_attach_hook_class (&attach_hook);
}
