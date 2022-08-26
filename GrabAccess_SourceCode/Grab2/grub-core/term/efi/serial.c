/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008,2012  Free Software Foundation, Inc.
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

#include <grub/disk.h>
#include <grub/partition.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/err.h>
#include <grub/term.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/disk.h>
#include <grub/serial.h>
#include <grub/types.h>
#include <grub/i18n.h>

/* GUID.  */
static grub_efi_guid_t serial_io_guid = GRUB_EFI_SERIAL_IO_GUID;

static void
do_real_config (struct grub_serial_port *port)
{
  grub_efi_status_t status = GRUB_EFI_SUCCESS;
  const grub_efi_parity_type_t parities[] = {
    [GRUB_SERIAL_PARITY_NONE] = GRUB_EFI_SERIAL_NO_PARITY,
    [GRUB_SERIAL_PARITY_ODD] = GRUB_EFI_SERIAL_ODD_PARITY,
    [GRUB_SERIAL_PARITY_EVEN] = GRUB_EFI_SERIAL_EVEN_PARITY
  };
  const grub_efi_stop_bits_t stop_bits[] = {
    [GRUB_SERIAL_STOP_BITS_1] = GRUB_EFI_SERIAL_1_STOP_BIT,
    [GRUB_SERIAL_STOP_BITS_1_5] = GRUB_EFI_SERIAL_1_5_STOP_BITS,
    [GRUB_SERIAL_STOP_BITS_2] = GRUB_EFI_SERIAL_2_STOP_BITS,
  };

  if (port->configured)
    return;

  status = efi_call_7 (port->interface->set_attributes, port->interface,
		       port->config.speed,
		       0, 0, parities[port->config.parity],
		       port->config.word_len,
		       stop_bits[port->config.stop_bits]);
  if (status != GRUB_EFI_SUCCESS)
    port->broken = 1;

  status = efi_call_2 (port->interface->set_control_bits, port->interface,
		       port->config.rtscts ? 0x4002 : 0x2);

  port->configured = 1;
}

/* Fetch a key.  */
static int
serial_hw_fetch (struct grub_serial_port *port)
{
  grub_efi_uintn_t bufsize = 1;
  char c;
  grub_efi_status_t status = GRUB_EFI_SUCCESS;
  do_real_config (port);
  if (port->broken)
    return -1;

  status = efi_call_3 (port->interface->read, port->interface, &bufsize, &c);
  if (status != GRUB_EFI_SUCCESS || bufsize == 0)
    return -1;

  return c;
}

/* Put a character.  */
static void
serial_hw_put (struct grub_serial_port *port, const int c)
{
  grub_efi_uintn_t bufsize = 1;
  char c0 = c;

  do_real_config (port);

  if (port->broken)
    return;

  efi_call_3 (port->interface->write, port->interface, &bufsize, &c0);
}

/* Initialize a serial device. PORT is the port number for a serial device.
   SPEED is a DTE-DTE speed which must be one of these: 2400, 4800, 9600,
   19200, 38400, 57600 and 115200. WORD_LEN is the word length to be used
   for the device. Likewise, PARITY is the type of the parity and
   STOP_BIT_LEN is the length of the stop bit. The possible values for
   WORD_LEN, PARITY and STOP_BIT_LEN are defined in the header file as
   macros.  */
static grub_err_t
serial_hw_configure (struct grub_serial_port *port,
		     struct grub_serial_config *config)
{
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

  /*  FIXME: should check if the serial terminal was found.  */

  return GRUB_ERR_NONE;
}

struct grub_serial_driver grub_efiserial_driver =
  {
    .configure = serial_hw_configure,
    .fetch = serial_hw_fetch,
    .put = serial_hw_put
  };

void
grub_efiserial_init (void)
{
  grub_efi_uintn_t num_handles;
  grub_efi_handle_t *handles;
  grub_efi_handle_t *handle;
  int num_serial = 0;
  grub_err_t err;

  /* Find handles which support the disk io interface.  */
  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &serial_io_guid,
				    0, &num_handles);
  if (! handles)
    return;

  /* Make a linked list of devices.  */
  for (handle = handles; num_handles--; handle++)
    {
      struct grub_serial_port *port;
      struct grub_efi_serial_io_interface *sio;

      sio = grub_efi_open_protocol (*handle, &serial_io_guid,
				    GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
      if (! sio)
	/* This should not happen... Why?  */
	continue;

      port = grub_zalloc (sizeof (*port));
      if (!port)
	return;

      port->name = grub_malloc (sizeof ("efiXXXXXXXXXXXXXXXXXXXX"));
      if (!port->name)
	{
	  grub_free (port);
	  return;
	}
      grub_snprintf (port->name, sizeof ("efiXXXXXXXXXXXXXXXXXXXX"),
		     "efi%d", num_serial++);

      port->driver = &grub_efiserial_driver;
      port->interface = sio;
      err = grub_serial_config_defaults (port);
      if (err)
	grub_print_error ();

      grub_serial_register (port);
    }

  grub_free (handles);

  return;
}
