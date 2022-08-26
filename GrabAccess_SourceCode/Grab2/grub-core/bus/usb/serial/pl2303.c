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
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/usb.h>
#include <grub/usbserial.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* Convert speed to divisor.  */
static grub_uint32_t
is_speed_supported (unsigned int speed)
{
  unsigned int i;
  unsigned int supported[] = { 2400, 4800, 9600, 19200, 38400, 57600, 115200};

  for (i = 0; i < ARRAY_SIZE (supported); i++)
    if (supported[i] == speed)
      return 1;
  return 0;
}

#define GRUB_PL2303_REQUEST_SET_CONFIG 0x20
#define GRUB_PL2303_STOP_BITS_1 0x0
#define GRUB_PL2303_STOP_BITS_1_5 0x1
#define GRUB_PL2303_STOP_BITS_2 0x2

#define GRUB_PL2303_PARITY_NONE 0
#define GRUB_PL2303_PARITY_ODD  1
#define GRUB_PL2303_PARITY_EVEN 2

struct grub_pl2303_config
{
  grub_uint32_t speed;
  grub_uint8_t stop_bits;
  grub_uint8_t parity;
  grub_uint8_t word_len;
} GRUB_PACKED;

static void
real_config (struct grub_serial_port *port)
{
  struct grub_pl2303_config config_pl2303;
  char xx;

  if (port->configured)
    return;

  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_IN,
			1, 0x8484, 0, 1, &xx);
  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_OUT,
			1, 0x0404, 0, 0, 0);

  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_IN,
			1, 0x8484, 0, 1, &xx);
  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_IN,
			1, 0x8383, 0, 1, &xx);
  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_IN,
			1, 0x8484, 0, 1, &xx);
  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_OUT,
			1, 0x0404, 1, 0, 0);

  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_IN,
			1, 0x8484, 0, 1, &xx);
  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_IN,
			1, 0x8383, 0, 1, &xx);

  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_OUT,
			1, 0, 1, 0, 0);
  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_OUT,
			1, 1, 0, 0, 0);
  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_OUT,
			1, 2, 0x44, 0, 0);
  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_OUT,
			1, 8, 0, 0, 0);
  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_OUT,
			1, 9, 0, 0, 0);

  if (port->config.stop_bits == GRUB_SERIAL_STOP_BITS_2)
    config_pl2303.stop_bits = GRUB_PL2303_STOP_BITS_2;
  else if (port->config.stop_bits == GRUB_SERIAL_STOP_BITS_1_5)
    config_pl2303.stop_bits = GRUB_PL2303_STOP_BITS_1_5;
  else
    config_pl2303.stop_bits = GRUB_PL2303_STOP_BITS_1;

  switch (port->config.parity)
    {
    case GRUB_SERIAL_PARITY_NONE:
      config_pl2303.parity = GRUB_PL2303_PARITY_NONE;
      break;
    case GRUB_SERIAL_PARITY_ODD:
      config_pl2303.parity = GRUB_PL2303_PARITY_ODD;
      break;
    case GRUB_SERIAL_PARITY_EVEN:
      config_pl2303.parity = GRUB_PL2303_PARITY_EVEN;
      break;
    }

  config_pl2303.word_len = port->config.word_len;
  config_pl2303.speed = port->config.speed;
  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_CLASS_INTERFACE_OUT,
			GRUB_PL2303_REQUEST_SET_CONFIG, 0, 0,
			sizeof (config_pl2303), (char *) &config_pl2303);
  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_CLASS_INTERFACE_OUT,
			0x22, 3, 0, 0, 0);

  grub_usb_control_msg (port->usbdev, GRUB_USB_REQTYPE_VENDOR_OUT,
			1, 0, port->config.rtscts ? 0x61 : 0, 0, 0);
  port->configured = 1;
}

/* Fetch a key.  */
static int
pl2303_hw_fetch (struct grub_serial_port *port)
{
  real_config (port);

  return grub_usbserial_fetch (port, 0);
}

/* Put a character.  */
static void
pl2303_hw_put (struct grub_serial_port *port, const int c)
{
  char cc = c;

  real_config (port);

  grub_usb_bulk_write (port->usbdev, port->out_endp, 1, &cc);
}

static grub_err_t
pl2303_hw_configure (struct grub_serial_port *port,
			struct grub_serial_config *config)
{
  if (!is_speed_supported (config->speed))
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("unsupported serial port speed"));

  if (config->parity != GRUB_SERIAL_PARITY_NONE
      && config->parity != GRUB_SERIAL_PARITY_ODD
      && config->parity != GRUB_SERIAL_PARITY_EVEN)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("unsupported serial port parity"));

  if (config->stop_bits != GRUB_SERIAL_STOP_BITS_1
      && config->stop_bits != GRUB_SERIAL_STOP_BITS_1_5
      && config->stop_bits != GRUB_SERIAL_STOP_BITS_2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("unsupported serial port stop bits number"));

  if (config->word_len < 5 || config->word_len > 8)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("unsupported serial port word length"));

  port->config = *config;
  port->configured = 0;

  return GRUB_ERR_NONE;
}

static struct grub_serial_driver grub_pl2303_driver =
  {
    .configure = pl2303_hw_configure,
    .fetch = pl2303_hw_fetch,
    .put = pl2303_hw_put,
    .fini = grub_usbserial_fini
  };

static const struct 
{
  grub_uint16_t vendor, product;
} products[] =
  {
    {0x067b, 0x2303}
  };

static int
grub_pl2303_attach (grub_usb_device_t usbdev, int configno, int interfno)
{
  unsigned j;

  for (j = 0; j < ARRAY_SIZE (products); j++)
    if (usbdev->descdev.vendorid == products[j].vendor
	&& usbdev->descdev.prodid == products[j].product)
      break;
  if (j == ARRAY_SIZE (products))
    return 0;

  return grub_usbserial_attach (usbdev, configno, interfno,
				&grub_pl2303_driver,
				GRUB_USB_SERIAL_ENDPOINT_LAST_MATCHING,
				GRUB_USB_SERIAL_ENDPOINT_LAST_MATCHING);
}

static struct grub_usb_attach_desc attach_hook =
{
  .class = 0xff,
  .hook = grub_pl2303_attach
};

GRUB_MOD_INIT(usbserial_pl2303)
{
  grub_usb_register_attach_hook_class (&attach_hook);
}

GRUB_MOD_FINI(usbserial_pl2303)
{
  grub_serial_unregister_driver (&grub_pl2303_driver);
  grub_usb_unregister_attach_hook_class (&attach_hook);
}
