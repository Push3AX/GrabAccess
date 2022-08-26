/* host.c - Dummy disk driver to provide access to the hosts filesystem  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007  Free Software Foundation, Inc.
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

/* When using the disk, make a reference to this module.  Otherwise
   the user will end up with a useless module :-).  */

#include <config.h>
#include <config-util.h>

#include <grub/dl.h>
#include <grub/disk.h>
#include <grub/misc.h>
#include <grub/emu/hostdisk.h>

int grub_disk_host_i_want_a_reference;

static int
grub_host_iterate (grub_disk_dev_iterate_hook_t hook, void *hook_data,
		   grub_disk_pull_t pull)
{
  if (pull != GRUB_DISK_PULL_NONE)
    return 0;

  if (hook ("host", hook_data))
    return 1;
  return 0;
}

static grub_err_t
grub_host_open (const char *name, grub_disk_t disk)
{
  if (grub_strcmp (name, "host"))
      return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "not a host disk");

  disk->total_sectors = 0;
  disk->id = 0;

  disk->data = 0;

  return GRUB_ERR_NONE;
}

static void
grub_host_close (grub_disk_t disk __attribute((unused)))
{
}

static grub_err_t
grub_host_read (grub_disk_t disk __attribute((unused)),
		grub_disk_addr_t sector __attribute((unused)),
		grub_size_t size __attribute((unused)),
		char *buf __attribute((unused)))
{
  return GRUB_ERR_OUT_OF_RANGE;
}

static grub_err_t
grub_host_write (grub_disk_t disk __attribute ((unused)),
		     grub_disk_addr_t sector __attribute ((unused)),
		     grub_size_t size __attribute ((unused)),
		     const char *buf __attribute ((unused)))
{
  return GRUB_ERR_OUT_OF_RANGE;
}

static struct grub_disk_dev grub_host_dev =
  {
    /* The only important line in this file :-) */
    .name = "host",
    .id = GRUB_DISK_DEVICE_HOST_ID,
    .disk_iterate = grub_host_iterate,
    .disk_open = grub_host_open,
    .disk_close = grub_host_close,
    .disk_read = grub_host_read,
    .disk_write = grub_host_write,
    .next = 0
  };

GRUB_MOD_INIT(host)
{
  grub_disk_dev_register (&grub_host_dev);
}

GRUB_MOD_FINI(host)
{
  grub_disk_dev_unregister (&grub_host_dev);
}
