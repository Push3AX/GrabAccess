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

#include <sys/socket.h>
#include <sys/types.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>

#include <grub/emu/net.h>

static int fd;

grub_ssize_t
grub_emunet_send (const void *packet, grub_size_t sz)
{
  return write (fd, packet, sz);
}

grub_ssize_t
grub_emunet_receive (void *packet, grub_size_t sz)
{
  return read (fd, packet, sz);
}

int
grub_emunet_create (grub_size_t *mtu)
{
  struct ifreq ifr;
  *mtu = 1500;
  fd = open ("/dev/net/tun", O_RDWR | O_NONBLOCK);
  if (fd < 0)
    return -1;
  memset (&ifr, 0, sizeof (ifr));
  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
  if (ioctl (fd, TUNSETIFF, &ifr) < 0)
    {
      close (fd);
      fd = -1;
      return -1;
    }
  return 0;
}

void
grub_emunet_close (void)
{
  if (fd < 0)
    return;

  close (fd);
  fd = -1;
}
