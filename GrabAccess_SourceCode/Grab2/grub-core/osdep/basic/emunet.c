/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2011,2012,2013  Free Software Foundation, Inc.
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

#include <config.h>
#include <config-util.h>

#include <grub/i18n.h>
#include <grub/emu/net.h>

grub_ssize_t
grub_emunet_send (const void *packet __attribute__ ((unused)),
		  grub_size_t sz __attribute__ ((unused)))
{
  return -1;
}

grub_ssize_t
grub_emunet_receive (void *packet __attribute__ ((unused)),
		     grub_size_t sz __attribute__ ((unused)))
{
  return -1;
}

int
grub_emunet_create (grub_size_t *mtu)
{
  *mtu = 1500;
  return -1;
}

void
grub_emunet_close (void)
{
  return;
}
