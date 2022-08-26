/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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
#include <grub/time.h>
#include <grub/i18n.h>
#include <grub/arc/arc.h>


static void
do_real_config (struct grub_serial_port *port)
{
  char *name;
  if (port->configured)
    return;

  name = grub_arc_alt_name_to_norm (port->name, "");

  if (GRUB_ARC_FIRMWARE_VECTOR->open (name,GRUB_ARC_FILE_ACCESS_OPEN_RW,
				      &port->handle))
    port->handle_valid = 0;
  else
    port->handle_valid = 1;

  port->configured = 1;
}

/* Fetch a key.  */
static int
serial_hw_fetch (struct grub_serial_port *port)
{
  unsigned long actual;
  char c;

  do_real_config (port);

  if (!port->handle_valid)
    return -1;
  if (GRUB_ARC_FIRMWARE_VECTOR->read (port->handle, &c,
				      1, &actual) || actual <= 0)
    return -1;
  return c;
}

/* Put a character.  */
static void
serial_hw_put (struct grub_serial_port *port, const int c)
{
  unsigned long actual;
  char c0 = c;

  do_real_config (port);

  if (!port->handle_valid)
    return;

  GRUB_ARC_FIRMWARE_VECTOR->write (port->handle, &c0,
				   1, &actual);
}

/* Initialize a serial device. PORT is the port number for a serial device.
   SPEED is a DTE-DTE speed which must be one of these: 2400, 4800, 9600,
   19200, 38400, 57600 and 115200. WORD_LEN is the word length to be used
   for the device. Likewise, PARITY is the type of the parity and
   STOP_BIT_LEN is the length of the stop bit. The possible values for
   WORD_LEN, PARITY and STOP_BIT_LEN are defined in the header file as
   macros.  */
static grub_err_t
serial_hw_configure (struct grub_serial_port *port __attribute__ ((unused)),
		     struct grub_serial_config *config __attribute__ ((unused)))
{
  /*  FIXME: no ARC serial config available.  */

  return GRUB_ERR_NONE;
}

struct grub_serial_driver grub_arcserial_driver =
  {
    .configure = serial_hw_configure,
    .fetch = serial_hw_fetch,
    .put = serial_hw_put
  };

const char *
grub_arcserial_add_port (const char *path)
{
  struct grub_serial_port *port;
  grub_err_t err;

  port = grub_zalloc (sizeof (*port));
  if (!port)
    return NULL;
  port->name = grub_strdup (path);
  if (!port->name)
    return NULL;

  port->driver = &grub_arcserial_driver;
  err = grub_serial_config_defaults (port);
  if (err)
    grub_print_error ();

  grub_serial_register (port);

  return port->name;
}

static int
dev_iterate (const char *name,
	     const struct grub_arc_component *comp __attribute__ ((unused)),
	     void *data __attribute__ ((unused)))
{
  /* We should check consolein/consoleout flags as
     well but some implementations are buggy.  */
  if ((comp->flags & (GRUB_ARC_COMPONENT_FLAG_IN | GRUB_ARC_COMPONENT_FLAG_OUT))
      != (GRUB_ARC_COMPONENT_FLAG_IN | GRUB_ARC_COMPONENT_FLAG_OUT))
    return 0;
  if (!grub_arc_is_device_serial (name, 1))
    return 0;
  grub_arcserial_add_port (name);
  return 0;
}

void
grub_arcserial_init (void)
{
  grub_arc_iterate_devs (dev_iterate, 0, 1);
}

