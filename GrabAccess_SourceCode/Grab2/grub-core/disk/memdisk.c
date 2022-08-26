/* memdisk.c - Access embedded memory disk.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007,2008  Free Software Foundation, Inc.
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
#include <grub/dl.h>
#include <grub/kernel.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/file.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_uint8_t *memdisk_orig_addr = NULL;
static unsigned long long memdisk_orig_size = 0;
static grub_uint8_t *memdisk_addr = NULL;
static unsigned long long memdisk_size = 0;

static int
grub_memdisk_iterate (grub_disk_dev_iterate_hook_t hook, void *hook_data,
                      grub_disk_pull_t pull)
{
  if (pull != GRUB_DISK_PULL_NONE)
    return 0;

  return hook ("memdisk", hook_data);
}

static grub_err_t
grub_memdisk_open (const char *name, grub_disk_t disk)
{
  char str[64];
  grub_file_t file = 0;
  if (grub_strcmp (name, "memdisk"))
      return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "not a memdisk");

  if (!memdisk_addr)
  {
    grub_snprintf (str, 64, "(mem)[%p]+[0x%llx]",
                  memdisk_orig_addr, memdisk_orig_size);
    file = grub_file_open (str, GRUB_FILE_TYPE_LOOPBACK);
    if (!file)
      return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "bad memdisk");

    memdisk_addr = grub_malloc (file->size);
    if (!memdisk_addr)
    {
      grub_file_close (file);
      return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
    }
    memdisk_size = file->size + GRUB_DISK_SECTOR_SIZE;

    grub_dprintf ("memdisk", "Extracting memdisk image to %p+0x%llx\n",
                  memdisk_addr, memdisk_size);
    grub_file_read (file, memdisk_addr, memdisk_size);
    grub_file_close (file);
  }

  disk->total_sectors = memdisk_size >> GRUB_DISK_SECTOR_BITS;
  disk->max_agglomerate = GRUB_DISK_MAX_MAX_AGGLOMERATE;
  disk->id = 0;

  return GRUB_ERR_NONE;
}

static void
grub_memdisk_close (grub_disk_t disk __attribute((unused)))
{
}

static grub_err_t
grub_memdisk_read (grub_disk_t disk __attribute((unused)), grub_disk_addr_t sector,
                   grub_size_t size, char *buf)
{
  grub_memcpy (buf, memdisk_addr + (sector << GRUB_DISK_SECTOR_BITS),
               size << GRUB_DISK_SECTOR_BITS);
  return 0;
}

static grub_err_t
grub_memdisk_write (grub_disk_t disk __attribute((unused)), grub_disk_addr_t sector,
                    grub_size_t size, const char *buf)
{
  grub_memcpy (memdisk_addr + (sector << GRUB_DISK_SECTOR_BITS), buf,
               size << GRUB_DISK_SECTOR_BITS);
  return 0;
}

static struct grub_disk_dev grub_memdisk_dev =
{
  .name = "memdisk",
  .id = GRUB_DISK_DEVICE_MEMDISK_ID,
  .disk_iterate = grub_memdisk_iterate,
  .disk_open = grub_memdisk_open,
  .disk_close = grub_memdisk_close,
  .disk_read = grub_memdisk_read,
  .disk_write = grub_memdisk_write,
  .next = 0
};

GRUB_MOD_INIT(memdisk)
{
  struct grub_module_header *header;
  const grub_uint8_t xz_hdr[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };
  const grub_uint8_t xz_end[2] = { 'Y', 'Z' };
  FOR_MODULES (header)
    if (header->type == OBJ_TYPE_MEMDISK)
    {
      memdisk_orig_addr = (grub_uint8_t *) header +
                          sizeof (struct grub_module_header);
      memdisk_orig_size = header->size - sizeof (struct grub_module_header);
      grub_dprintf ("memdisk", "Found memdisk image at %p+0x%llx\n",
                    memdisk_orig_addr, memdisk_orig_size);

      /* check XZ magic */
      if (grub_memcmp (memdisk_orig_addr, xz_hdr, 6) == 0)
      {
        grub_dprintf ("memdisk", "Found XZ header.\n");
        do
        {
          memdisk_orig_size -= sizeof (xz_end);
          if (grub_memcmp (memdisk_orig_addr + memdisk_orig_size,
                           xz_end, sizeof (xz_end)) == 0)
            break;
        }
        while (memdisk_orig_size > sizeof (xz_end));
        memdisk_orig_size += sizeof (xz_end);
        grub_dprintf ("memdisk", "Adjust memdisk size to 0x%llx\n",
                      memdisk_orig_size);
      }

      grub_disk_dev_register (&grub_memdisk_dev);
      break;
    }
}

GRUB_MOD_FINI(memdisk)
{
  if (memdisk_addr)
    grub_free (memdisk_addr);
  if (memdisk_orig_addr)
    grub_disk_dev_unregister (&grub_memdisk_dev);
}
