/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2006,2007,2008,2009,2010,2011,2012,2013  Free Software Foundation, Inc.
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

#include <config-util.h>

#include <grub/disk.h>
#include <grub/partition.h>
#include <grub/msdos_partition.h>
#include <grub/types.h>
#include <grub/err.h>
#include <grub/emu/misc.h>
#include <grub/emu/hostdisk.h>
#include <grub/emu/getroot.h>
#include <grub/misc.h>
#include <grub/i18n.h>
#include <grub/list.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include <hurd.h>
#include <hurd/lookup.h>
#include <hurd/fs.h>
#include <sys/mman.h>

int
grub_util_hurd_get_disk_info (const char *dev, grub_uint32_t *secsize, grub_disk_addr_t *offset,
			      grub_disk_addr_t *size, char **parent)
{
  file_t file;
  mach_port_t *ports;
  int *ints;
  loff_t *offsets;
  char *data;
  error_t err;
  mach_msg_type_number_t num_ports = 0, num_ints = 0, num_offsets = 0, data_len = 0;

  file = file_name_lookup (dev, 0, 0);
  if (file == MACH_PORT_NULL)
    return 0;

  err = file_get_storage_info (file,
			       &ports, &num_ports,
			       &ints, &num_ints,
			       &offsets, &num_offsets,
			       &data, &data_len);

  if (num_ints < 1)
    grub_util_error (_("Storage information for `%s' does not include type"), dev);
  if (ints[0] != STORAGE_DEVICE)
    grub_util_error (_("`%s' is not a local disk"), dev);

  if (num_offsets != 2)
    grub_util_error (_("Storage information for `%s' indicates neither a plain partition nor a plain disk"), dev);
  if (parent)
    {
      *parent = NULL;
      if (num_ints >= 5)
	{
	  size_t len = ints[4];
	  if (len > data_len)
	    len = data_len;
	  *parent = xmalloc (len+1);
	  memcpy (*parent, data, len);
	  (*parent)[len] = '\0';
	}
    }
  if (offset)
    *offset = offsets[0];
  if (size)
    *size = offsets[1];
  if (secsize)
    *secsize = ints[2];
  if (ports && num_ports > 0)
    {
      mach_msg_type_number_t i;
      for (i = 0; i < num_ports; i++)
        {
	  mach_port_t port = ports[i];
	  if (port != MACH_PORT_NULL)
	    mach_port_deallocate (mach_task_self(), port);
        }
      munmap ((caddr_t) ports, num_ports * sizeof (*ports));
    }

  if (ints && num_ints > 0)
    munmap ((caddr_t) ints, num_ints * sizeof (*ints));
  if (offsets && num_offsets > 0)
    munmap ((caddr_t) offsets, num_offsets * sizeof (*offsets));
  if (data && data_len > 0)
    munmap (data, data_len);
  mach_port_deallocate (mach_task_self (), file);

  return 1;
}

grub_int64_t
grub_util_get_fd_size_os (grub_util_fd_t fd, const char *name, unsigned *log_secsize)
{
  grub_uint32_t sector_size;
  grub_disk_addr_t size;
  unsigned log_sector_size;

  if (!grub_util_hurd_get_disk_info (name, &sector_size, NULL, &size, NULL))
    return -1;

  if (sector_size & (sector_size - 1) || !sector_size)
    return -1;
  for (log_sector_size = 0;
       (1 << log_sector_size) < sector_size;
       log_sector_size++);

  if (log_secsize)
    *log_secsize = log_sector_size;

  return size << log_sector_size;
}

void
grub_hostdisk_flush_initial_buffer (const char *os_dev __attribute__ ((unused)))
{
}
