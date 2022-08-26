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
#include <grub/fdtbus.h>
#include <grub/machine/kernel.h>

static grub_err_t
spi_send (const struct grub_fdtbus_dev *dev, const void *data, grub_size_t sz)
{
  const grub_uint8_t *ptr = data, *end = ptr + sz;
  volatile grub_uint32_t *spi = grub_fdtbus_map_reg (dev, 0, 0);
  spi[2] = 0;
  spi[1] = sz - 1;
  spi[0] = ((1 << 18) | spi[0]) & ~(1 << 19);
  spi[2] = 1;
  while (ptr < end)
    {
      while (spi[9] & 2);
      spi[256] = *ptr++;
    }
  while (spi[9] & 1);
  return GRUB_ERR_NONE;
}

static grub_err_t
spi_receive (const struct grub_fdtbus_dev *dev, void *data, grub_size_t sz)
{
  grub_uint8_t *ptr = data, *end = ptr + sz;
  volatile grub_uint32_t *spi = grub_fdtbus_map_reg (dev, 0, 0);
  spi[2] = 0;
  spi[1] = sz - 1;
  spi[0] = ((1 << 19) | spi[0]) & ~(1 << 18);
  spi[2] = 1;
  while (ptr < end)
    {
      while (spi[9] & 8);
      *ptr++ = spi[512];
    }
  while (spi[9] & 1);
  return GRUB_ERR_NONE;
}

static grub_err_t
spi_start (const struct grub_fdtbus_dev *dev)
{
  volatile grub_uint32_t *spi = grub_fdtbus_map_reg (dev, 0, 0);
  spi[3] = 1;
  return GRUB_ERR_NONE;
}

static void
spi_stop (const struct grub_fdtbus_dev *dev)
{
  volatile grub_uint32_t *spi = grub_fdtbus_map_reg (dev, 0, 0);
  spi[3] = 0;
}

static grub_err_t
spi_attach(const struct grub_fdtbus_dev *dev)
{
  if (!grub_fdtbus_is_mapping_valid (grub_fdtbus_map_reg (dev, 0, 0)))
    return GRUB_ERR_IO;

  return GRUB_ERR_NONE;
}

static struct grub_fdtbus_driver spi =
{
  .compatible = "rockchip,rk3288-spi",
  .attach = spi_attach,
  .send = spi_send,
  .receive = spi_receive,
  .start = spi_start,
  .stop = spi_stop,
};

void
grub_rk3288_spi_init (void)
{
  grub_fdtbus_register (&spi);
}
