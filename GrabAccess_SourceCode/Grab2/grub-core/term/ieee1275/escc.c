/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2000,2001,2002,2003,2004,2005,2006,2007,2008,2009,2010,2012  Free Software Foundation, Inc.
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

GRUB_MOD_LICENSE ("GPLv3+");

struct grub_escc_descriptor
{
  volatile grub_uint8_t *escc_ctrl;
  volatile grub_uint8_t *escc_data;
};

static void
do_real_config (struct grub_serial_port *port)
{
  grub_uint8_t bitsspec;
  grub_uint8_t parity_stop_spec;
  if (port->configured)
    return;

  /* Make sure the port is waiting for address now.  */
  (void) *port->escc_desc->escc_ctrl;
  switch (port->config.speed)
    {
    case 57600:
      *port->escc_desc->escc_ctrl = 13;
      *port->escc_desc->escc_ctrl = 0;
      *port->escc_desc->escc_ctrl = 12;
      *port->escc_desc->escc_ctrl = 0;
      *port->escc_desc->escc_ctrl = 14;
      *port->escc_desc->escc_ctrl = 1;
      *port->escc_desc->escc_ctrl = 11;
      *port->escc_desc->escc_ctrl = 0x50;
      break;
    case 38400:
      *port->escc_desc->escc_ctrl = 13;
      *port->escc_desc->escc_ctrl = 0;
      *port->escc_desc->escc_ctrl = 12;
      *port->escc_desc->escc_ctrl = 1;
      *port->escc_desc->escc_ctrl = 14;
      *port->escc_desc->escc_ctrl = 1;
      *port->escc_desc->escc_ctrl = 11;
      *port->escc_desc->escc_ctrl = 0x50;
      break;
    }

  parity_stop_spec = 0;
  switch (port->config.parity)
    {
    case GRUB_SERIAL_PARITY_NONE:
      parity_stop_spec |= 0;
      break;
    case GRUB_SERIAL_PARITY_ODD:
      parity_stop_spec |= 1;
      break;
    case GRUB_SERIAL_PARITY_EVEN:
      parity_stop_spec |= 3;
      break;
    }

  switch (port->config.stop_bits)
    {
    case GRUB_SERIAL_STOP_BITS_1:
      parity_stop_spec |= 0x4;
      break;
    case GRUB_SERIAL_STOP_BITS_1_5:
      parity_stop_spec |= 0x8;
      break;
    case GRUB_SERIAL_STOP_BITS_2:
      parity_stop_spec |= 0xc;
      break;      
    }

  *port->escc_desc->escc_ctrl = 4;
  *port->escc_desc->escc_ctrl = 0x40 | parity_stop_spec;

  bitsspec = port->config.word_len - 5;
  bitsspec = ((bitsspec >> 1) | (bitsspec << 1)) & 3;

  *port->escc_desc->escc_ctrl = 3;
  *port->escc_desc->escc_ctrl = (bitsspec << 6) | 0x1;

  port->configured = 1;

  return;
}

/* Fetch a key.  */
static int
serial_hw_fetch (struct grub_serial_port *port)
{
  do_real_config (port);

  *port->escc_desc->escc_ctrl = 0;
  if (*port->escc_desc->escc_ctrl & 1)
    return *port->escc_desc->escc_data;
  return -1;
}

/* Put a character.  */
static void
serial_hw_put (struct grub_serial_port *port, const int c)
{
  grub_uint64_t endtime;

  do_real_config (port);

  if (port->broken > 5)
    endtime = grub_get_time_ms ();
  else if (port->broken > 1)
    endtime = grub_get_time_ms () + 50;
  else
    endtime = grub_get_time_ms () + 200;
  /* Wait until the transmitter holding register is empty.  */
  while (1)
    {
      *port->escc_desc->escc_ctrl = 0;
      if (*port->escc_desc->escc_ctrl & 4)
	break;
      if (grub_get_time_ms () > endtime)
	{
	  port->broken++;
	  /* There is something wrong. But what can I do?  */
	  return;
	}
    }

  if (port->broken)
    port->broken--;

  *port->escc_desc->escc_data = c;
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
  if (config->speed != 38400 && config->speed != 57600)
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

  /*  FIXME: should check if the serial terminal was found.  */

  return GRUB_ERR_NONE;
}

struct grub_serial_driver grub_escc_driver =
  {
    .configure = serial_hw_configure,
    .fetch = serial_hw_fetch,
    .put = serial_hw_put
  };

static struct grub_escc_descriptor escc_descs[2];
static char *macio = 0;

static void
add_device (grub_addr_t addr, int channel)
{
  struct grub_serial_port *port;
  grub_err_t err;
  struct grub_serial_config config =
    {
      .speed = 38400,
      .word_len = 8,
      .parity = GRUB_SERIAL_PARITY_NONE,
      .stop_bits = GRUB_SERIAL_STOP_BITS_1
    };

  escc_descs[channel].escc_ctrl
    = (volatile grub_uint8_t *) (grub_addr_t) addr;
  escc_descs[channel].escc_data = escc_descs[channel].escc_ctrl + 16;

  port = grub_zalloc (sizeof (*port));
  if (!port)
    {
      grub_errno = 0;
      return;
    }

  port->name = grub_xasprintf ("escc-ch-%c", channel + 'a');
  if (!port->name)
    {
      grub_errno = 0;
      return;
    }

  port->escc_desc = &escc_descs[channel];

  port->driver = &grub_escc_driver;

  err = port->driver->configure (port, &config);
  if (err)
    grub_print_error ();

  grub_serial_register (port);
}

static int
find_macio (struct grub_ieee1275_devalias *alias)
{
  if (grub_strcmp (alias->type, "mac-io") != 0)
    return 0;
  macio = grub_strdup (alias->path);
  return 1;
}

GRUB_MOD_INIT (escc)
{
  grub_uint32_t macio_addr[4];
  grub_uint32_t escc_addr[2];
  grub_ieee1275_phandle_t dev;
  struct grub_ieee1275_devalias alias;
  char *escc = 0;

  grub_ieee1275_devices_iterate (find_macio);
  if (!macio)
    return;

  FOR_IEEE1275_DEVCHILDREN(macio, alias)
    if (grub_strcmp (alias.type, "escc") == 0)
      {
	escc = grub_strdup (alias.path);
	break;
      }
  grub_ieee1275_devalias_free (&alias);
  if (!escc)
    {
      grub_free (macio);
      return;
    }

  if (grub_ieee1275_finddevice (macio, &dev))
    {
      grub_free (macio);
      grub_free (escc);
      return;
    }
  if (grub_ieee1275_get_integer_property (dev, "assigned-addresses",
					  macio_addr, sizeof (macio_addr), 0))
    {
      grub_free (macio);
      grub_free (escc);
      return;
    }

  if (grub_ieee1275_finddevice (escc, &dev))
    {
      grub_free (macio);
      grub_free (escc);
      return;
    }

  if (grub_ieee1275_get_integer_property (dev, "reg",
					  escc_addr, sizeof (escc_addr), 0))
    {
      grub_free (macio);
      grub_free (escc);
      return;
    }

  add_device (macio_addr[2] + escc_addr[0] + 32, 0);
  add_device (macio_addr[2] + escc_addr[0], 1);

  grub_free (macio);
  grub_free (escc);
}

GRUB_MOD_FINI (escc)
{
}
