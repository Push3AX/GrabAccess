/*
 *  GRUB  --  GRand Unified Bootloader
 *
 *  Copyright (C) 2012  Google Inc.
 *  Copyright (C) 2016  Free Software Foundation, Inc.
 *
 *  This is based on depthcharge code.
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

#include <grub/mm.h>
#include <grub/time.h>
#include <grub/misc.h>
#include <grub/arm/cros_ec.h>
#include <grub/fdtbus.h>

static const grub_uint64_t FRAMING_TIMEOUT_MS = 300;

static const grub_uint8_t EC_FRAMING_BYTE = 0xec;

#define EC_CMD_MKBP_STATE 0x60
#define EC_CMD_VERSION0 0xdc

static grub_uint64_t last_transfer;

static void
stop_bus (const struct grub_fdtbus_dev *spi)
{
  spi->driver->stop (spi);
  last_transfer = grub_get_time_ms ();
}

static int
wait_for_frame (const struct grub_fdtbus_dev *spi)
{
  grub_uint64_t start = grub_get_time_ms ();
  grub_uint8_t byte;
  do
    {
      if (spi->driver->receive (spi, &byte, 1))
	return -1;
      if (byte != EC_FRAMING_BYTE &&
	  grub_get_time_ms () - start > FRAMING_TIMEOUT_MS)
	{
	  grub_dprintf ("cros", "Timeout waiting for framing byte.\n");
	  return -1;
	}
    }
  while (byte != EC_FRAMING_BYTE);
  return 0;
}

/*
 * Calculate a simple 8-bit checksum of a data block
 *
 * @param data	Data block to checksum
 * @param size	Size of data block in bytes
 * @return checksum value (0 to 255)
 */
static grub_uint8_t
cros_ec_calc_checksum (const void *data, int size)
{
  grub_uint8_t csum;
  const grub_uint8_t *bytes = data;
  int i;

  for (i = csum = 0; i < size; i++)
    csum += bytes[i];
  return csum & 0xff;
}

enum
{
  /* response, arglen */
  CROS_EC_SPI_IN_HDR_SIZE = 2,
  /* version, cmd, arglen */
  CROS_EC_SPI_OUT_HDR_SIZE = 3
};

static grub_uint8_t busbuf[256];
#define MSG_BYTES ((int)sizeof (busbuf))

static int
ec_command (const struct grub_fdtbus_dev *dev, int cmd, int cmd_version,
	    const void *dout, int dout_len, void *din, int din_len)
{
  const struct grub_fdtbus_dev *spi = dev->parent;
  grub_uint8_t *bytes;

  /* Header + data + checksum. */
  grub_uint32_t out_bytes = CROS_EC_SPI_OUT_HDR_SIZE + dout_len + 1;
  grub_uint32_t in_bytes = CROS_EC_SPI_IN_HDR_SIZE + din_len + 1;

  /*
   * Sanity-check I/O sizes given transaction overhead in internal
   * buffers.
   */
  if (out_bytes > MSG_BYTES)
    {
      grub_dprintf ("cros", "Cannot send %d bytes\n", dout_len);
      return -1;
    }
  if (in_bytes > MSG_BYTES)
    {
      grub_dprintf ("cros", "Cannot receive %d bytes\n", din_len);
      return -1;
    }

  /* Prepare the output. */
  bytes = busbuf;
  *bytes++ = EC_CMD_VERSION0 + cmd_version;
  *bytes++ = cmd;
  *bytes++ = dout_len;
  grub_memcpy (bytes, dout, dout_len);
  bytes += dout_len;

  *bytes++ = cros_ec_calc_checksum (busbuf,
				    CROS_EC_SPI_OUT_HDR_SIZE + dout_len);

  /* Depthcharge uses 200 us here but GRUB timer resolution is only 1ms,
     decrease this when we increase timer resolution.  */
  while (grub_get_time_ms () - last_transfer < 1)
    ;

  if (spi->driver->start (spi))
    return -1;

  /* Allow EC to ramp up clock after being awoken. */
  /* Depthcharge only waits 100 us here but GRUB timer resolution is only 1ms,
     decrease this when we increase timer resolution.  */
  grub_millisleep (1);

  if (spi->driver->send (spi, busbuf, out_bytes))
    {
      stop_bus (spi);
      return -1;
    }

  /* Wait until the EC is ready. */
  if (wait_for_frame (spi))
    {
      stop_bus (spi);
      return -1;
    }

  /* Read the response code and the data length. */
  bytes = busbuf;
  if (spi->driver->receive (spi, bytes, 2))
    {
      stop_bus (spi);
      return -1;
    }
  grub_uint8_t result = *bytes++;
  grub_uint8_t length = *bytes++;

  /* Make sure there's enough room for the data. */
  if (CROS_EC_SPI_IN_HDR_SIZE + length + 1 > MSG_BYTES)
    {
      grub_dprintf ("cros", "Received length %#02x too large\n", length);
      stop_bus (spi);
      return -1;
    }

  /* Read the data and the checksum, and finish up. */
  if (spi->driver->receive (spi, bytes, length + 1))
    {
      stop_bus (spi);
      return -1;
    }
  bytes += length;
  int expected = *bytes++;
  stop_bus (spi);

  /* Check the integrity of the response. */
  if (result != 0)
    {
      grub_dprintf ("cros", "Received bad result code %d\n", result);
      return -result;
    }

  int csum = cros_ec_calc_checksum (busbuf,
				    CROS_EC_SPI_IN_HDR_SIZE + length);

  if (csum != expected)
    {
      grub_dprintf ("cros", "Invalid checksum rx %#02x, calced %#02x\n",
		    expected, csum);
      return -1;
    }

  /* If the caller wants the response, copy it out for them. */
  if (length < din_len)
    din_len = length;
  if (din)
    {
      grub_memcpy (din, (grub_uint8_t *) busbuf + CROS_EC_SPI_IN_HDR_SIZE, din_len);
    }

  return din_len;
}

int
grub_cros_ec_scan_keyboard (const struct grub_fdtbus_dev *dev, struct grub_cros_ec_keyscan *scan)
{
  if (ec_command (dev, EC_CMD_MKBP_STATE, 0, NULL, 0, scan,
		  sizeof (*scan)) < (int) sizeof (*scan))
    return -1;

  return 0;
}

int
grub_cros_ec_validate (const struct grub_fdtbus_dev *dev)
{
  if (!grub_fdtbus_is_compatible("google,cros-ec-spi", dev))
    return 0;
  if (!dev->parent)
    return 0;
  if (!dev->parent->driver)
    return 0;
  if (!dev->parent->driver->send
      || !dev->parent->driver->receive)
    return 0;
  return 1;
}

