/* cpio.c - cpio and tar filesystem.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007,2008,2009,2013 Free Software Foundation, Inc.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/dl.h>
#include <grub/types.h>
#include <grub/err.h>
#include <grub/linux.h>
#include <grub/misc.h>
#include <grub/file.h>
#include <grub/mm.h>
#include <grub/normal.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/disk.h>

#define ALIGN_CPIO(x) (ALIGN_UP ((x), 4))
#define MAGIC  "070701"
#define MAGIC2 "070702"
#define SIZE_1MB ((grub_uint32_t)(1 << 20))

struct head
{
  char magic[6];
  char ino[8];
  char mode[8];
  char uid[8];
  char gid[8];
  char nlink[8];
  char mtime[8];
  char filesize[8];
  char devmajor[8];
  char devminor[8];
  char rdevmajor[8];
  char rdevminor[8];
  char namesize[8];
  char check[8];
} GRUB_PACKED;

struct dir
{
  char *name;
  struct dir *next;
  struct dir *child;
};

static inline unsigned long long
read_number (const char *str, grub_size_t size)
{
  unsigned long long ret = 0;
  while (size-- && grub_isxdigit (*str))
  {
    char dig = *str++;
    if (dig >= '0' && dig <= '9')
      dig &= 0xf;
    else if (dig >= 'a' && dig <= 'f')
      dig -= 'a' - 10;
    else
      dig -= 'A' - 10;
    ret = (ret << 4) | (dig);
  }
  return ret;
}

static char
hex (grub_uint8_t val)
{
  if (val < 10)
    return '0' + val;
  return 'a' + val - 10;
}

static void
set_field (char *var, grub_uint32_t val)
{
  int i;
  char *ptr = var;
  for (i = 28; i >= 0; i -= 4)
    *ptr++ = hex((val >> i) & 0xf);
}

static grub_uint8_t *
make_header (grub_uint8_t *ptr,
             const char *name,
             grub_uint32_t mode,
             grub_uint32_t file_size)
{
  struct head *head = (struct head *) ptr;
  grub_uint8_t *optr;
  grub_size_t oh = 0;
  grub_uint32_t name_size = grub_strlen (name) + 1;
  static grub_uint32_t ino = 0xFFFFFFF0;
  grub_memcpy (head->magic, MAGIC, 6);
  set_field (head->ino, ino--);
  set_field (head->mode, mode);
  set_field (head->uid, 0);
  set_field (head->gid, 0);
  set_field (head->nlink, 1);
  set_field (head->mtime, 0);
  set_field (head->filesize, file_size);
  set_field (head->devmajor, 0);
  set_field (head->devminor, 0);
  set_field (head->rdevmajor, 0);
  set_field (head->rdevminor, 0);
  set_field (head->namesize, name_size);
  set_field (head->check, 0);
  optr = ptr;
  ptr += sizeof (struct head);
  grub_memcpy (ptr, name, name_size);
  ptr += name_size;
  oh = ALIGN_UP_OVERHEAD (ptr - optr, 4);
  grub_memset (ptr, 0, oh);
  ptr += oh;
  return ptr;
}

#define FSNAME "newc"

#include "cpio_common.c"

struct grub_initrd
{
  char *devname;
  grub_uint8_t *addr;
  grub_size_t cur_size;
  grub_size_t max_size;
  struct grub_initrd *next;
  unsigned long id;
};

static struct grub_initrd *initrd_list;
static unsigned long last_id = 0;

static int
grub_initrd_iterate (grub_disk_dev_iterate_hook_t hook, void *hook_data,
                     grub_disk_pull_t pull)
{
  struct grub_initrd *d;
  if (pull != GRUB_DISK_PULL_NONE)
    return 0;
  for (d = initrd_list; d; d = d->next)
  {
    if (hook (d->devname, hook_data))
      return 1;
  }
  return 0;
}

static grub_err_t
grub_initrd_open (const char *name, grub_disk_t disk)
{
  struct grub_initrd *dev;
  for (dev = initrd_list; dev; dev = dev->next)
    if (grub_strcmp (dev->devname, name) == 0)
      break;
  if (! dev)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "can't open device");
  disk->total_sectors = (dev->cur_size + GRUB_DISK_SECTOR_SIZE - 1)
                        >> GRUB_DISK_SECTOR_BITS;
  disk->max_agglomerate = GRUB_DISK_MAX_MAX_AGGLOMERATE;
  disk->id = dev->id;
  disk->data = dev;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_initrd_read (grub_disk_t disk, grub_disk_addr_t sector,
                  grub_size_t size, char *buf)
{
  grub_uint8_t *addr = ((struct grub_initrd *) disk->data)->addr;
  grub_memcpy (buf, addr + (sector << GRUB_DISK_SECTOR_BITS),
               size << GRUB_DISK_SECTOR_BITS);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_initrd_write (grub_disk_t disk, grub_disk_addr_t sector,
                   grub_size_t size, const char *buf)
{
  grub_uint8_t *addr = ((struct grub_initrd *) disk->data)->addr;
  grub_memcpy (addr + (sector << GRUB_DISK_SECTOR_BITS), buf,
               size << GRUB_DISK_SECTOR_BITS);
  return GRUB_ERR_NONE;
}

static struct grub_disk_dev grub_initrd_dev =
{
  .name = "initrd",
  .id = GRUB_DISK_DEVICE_INITRD_ID,
  .disk_iterate = grub_initrd_iterate,
  .disk_open = grub_initrd_open,
  .disk_read = grub_initrd_read,
  .disk_write = grub_initrd_write,
  .next = 0
};

static const struct grub_arg_option options[] =
{
  {"create", 'c', 0, N_("Create an initrd."), 0, 0},
  {"add", 'a', 0, N_("Copy a file to initrd."), 0, 0},
  {"ren", 'r', 0, N_("Rename file."), 0, 0},
  {"delete", 'd', 0, N_("Delete the specified initrd."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

enum options
{
  INITRD_CREATE,
  INITRD_ADD,
  INITRD_REN,
  INITRD_DELETE,
};

static grub_err_t
grub_initrd_delete (const char *name)
{
  struct grub_initrd *dev;
  struct grub_initrd **prev;

  /* Search for the device.  */
  for (dev = initrd_list, prev = &initrd_list; dev;
       prev = &dev->next, dev = dev->next)
    if (grub_strcmp (dev->devname, name) == 0)
      break;
  if (! dev)
    return GRUB_ERR_NONE;
  /* Remove the device from the list.  */
  *prev = dev->next;
  grub_free (dev->devname);
  if (dev->addr)
    grub_free (dev->addr);
  grub_free (dev);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_initrd_realloc (struct grub_initrd *dev, grub_size_t size)
{
  void *addr = NULL;
  grub_size_t new_size;
  if (! dev)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "can't open device");
  new_size = dev->cur_size + SIZE_1MB + size;
  if (size > GRUB_UINT_MAX || new_size >= GRUB_UINT_MAX)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "bad file size");
  addr = grub_realloc (dev->addr, new_size);
  if (!addr)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
  dev->addr = addr;
  dev->max_size = new_size;
  return GRUB_ERR_NONE;
}

static grub_uint8_t *
grub_initrd_find_end (grub_uint8_t *file_addr, grub_size_t file_size)
{
  grub_uint8_t *ptr;
  if (file_size < grub_strlen(MAGIC))
  {
    return (file_addr + file_size);
  }
  ptr = file_addr + file_size - grub_strlen(MAGIC);
  while (ptr > file_addr)
  {
    ptr--;
    if (grub_memcmp (ptr, MAGIC, grub_strlen (MAGIC)) == 0)
      return ptr;
  }
  grub_printf ("NEWC MAGIC NOT FOUND\n");
  return (file_addr + file_size);
}

static grub_err_t
grub_initrd_add (const char *dev_name, grub_file_t file, const char *name)
{
  grub_uint8_t *ptr;
  struct grub_initrd *dev;
  grub_size_t add_size = 0;
  if (!file || file->size >= GRUB_UINT_MAX)
    return grub_error (GRUB_ERR_BAD_FILE_TYPE, "bad file");
  for (dev = initrd_list; dev; dev = dev->next)
    if (grub_strcmp (dev->devname, dev_name) == 0)
      break;
  if (! dev)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "can't open device");
  /* re-calculate size */
  add_size = ALIGN_UP (sizeof (struct head) + grub_strlen (name) + 1, 4);
  add_size += file->size;
  add_size = ALIGN_UP (add_size, 4);
  if (add_size >= dev->max_size - dev->cur_size)
  {
    grub_initrd_realloc (dev, add_size);
    if (grub_errno)
      return grub_errno;
  }
  /* goto end */
  ptr = grub_initrd_find_end (dev->addr, dev->cur_size);
  dev->cur_size += add_size;
  /* fill data */
  ptr = make_header (ptr, name, 0100777, file->size);
  grub_file_read (file, ptr, file->size);
  ptr += file->size;
  /* newc end */
  grub_memset (ptr, 0, ALIGN_UP_OVERHEAD (file->size, 4));
  ptr += ALIGN_UP_OVERHEAD (file->size, 4);
  ptr = make_header (ptr, "TRAILER!!!", 0, 0);
  return GRUB_ERR_NONE;
}

static grub_uint8_t *
grub_initrd_find_file (struct grub_initrd *dev, const char *file_name)
{
  grub_uint8_t *ptr = dev->addr;
  while (ptr < dev->addr + dev->cur_size)
  {
    struct head *hd = (void *)ptr;
    unsigned long long namesize, filesize;
    char *name = NULL;
    if (grub_memcmp (hd->magic, MAGIC, sizeof (hd->magic)) != 0)
    {
      ptr++;
      continue;
    }
    namesize = read_number (hd->namesize, ARRAY_SIZE (hd->namesize));
    filesize = read_number (hd->filesize, ARRAY_SIZE (hd->filesize));
    if (namesize >= 0x80000000)
      return NULL;
    name = grub_zalloc (namesize + 1);
    if (!name)
      return NULL;
    grub_memcpy (name, ptr + sizeof (struct head), namesize);
    grub_printf ("file: %s, size=%llu\n", name, filesize);
    if (grub_strcmp (name, file_name) == 0)
    {
      grub_free (name);
      return ptr;
    }
    grub_free (name);
    ptr += sizeof (struct head) + namesize + filesize;
  }
  return NULL;
}

static grub_err_t
grub_initrd_ren (const char *dev_name, const char *name, const char *new_name)
{
  grub_uint8_t *ptr = NULL;
  struct grub_initrd *dev;
  for (dev = initrd_list; dev; dev = dev->next)
    if (grub_strcmp (dev->devname, dev_name) == 0)
      break;
  if (! dev)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "can't open device");
  if (grub_strlen (name) != grub_strlen (new_name))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "namesize mismatch");
  ptr = grub_initrd_find_file (dev, name);
  if (! ptr)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "file not found");
  grub_memcpy (ptr + sizeof (struct head), new_name, grub_strlen (name));
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_initrd_create (const char *dev_name, grub_file_t file)
{
  struct grub_initrd *newdev;
  grub_uint8_t *addr = 0;
  grub_size_t cur_size = 0;
  grub_size_t max_size = SIZE_1MB;
  if (file)
  {
    if (file->size >= GRUB_UINT_MAX)
      return grub_error (GRUB_ERR_OUT_OF_MEMORY, "bad file size");
    cur_size = file->size;
    max_size = file->size + SIZE_1MB;
  }
  addr = grub_malloc (max_size);
  if (!addr)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
  if (file)
    grub_file_read (file, addr, file->size);
  for (newdev = initrd_list; newdev; newdev = newdev->next)
    if (grub_strcmp (newdev->devname, dev_name) == 0)
      break;
  if (newdev)
  {
    if (newdev->addr)
      grub_free (newdev->addr);
    newdev->cur_size = cur_size;
    newdev->max_size = max_size;
    newdev->addr = addr;
    return GRUB_ERR_NONE;
  }
  /* Unable to replace it, make a new entry.  */
  newdev = grub_malloc (sizeof (struct grub_initrd));
  if (! newdev)
  {
    grub_free (addr);
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "failed to create device");
  }
  newdev->addr = addr;
  newdev->cur_size = cur_size;
  newdev->max_size = max_size;
  newdev->id = last_id++;
  newdev->devname = grub_strdup (dev_name);
  if (! newdev->devname)
  {
    grub_free (newdev->addr);
    grub_free (newdev);
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "failed to set device name");
  }
  /* Add the new entry to the list.  */
  newdev->next = initrd_list;
  initrd_list = newdev;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_mkinitrd (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  grub_file_t file = 0;
  if (state[INITRD_CREATE].set)
  {
    if (argc < 1)
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "Usage: --create DEV [FILE]");
    if (argc > 1)
    {
      file = grub_file_open (args[1], GRUB_FILE_TYPE_LOOPBACK);
      if (!file)
        return grub_errno;
    }
    grub_initrd_create (args[0], file);
  }
  else if (state[INITRD_ADD].set)
  {
    if (argc < 3)
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "Usage: --add DEV FILE NAME");
    file = grub_file_open (args[1], GRUB_FILE_TYPE_LOOPBACK);
    if (!file)
      return grub_errno;
    grub_initrd_add (args[0], file, args[2]);
  }
  else if (state[INITRD_REN].set)
  {
    if (argc < 3)
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "Usage: --ren DEV OLDNAME NEWNAME");
    grub_initrd_ren (args[0], args[1], args[2]);
  }
  else if (state[INITRD_DELETE].set)
  {
    if (argc < 1)
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "Usage: --delete DEV");
    grub_initrd_delete (args[0]);
  }
  if (file)
    grub_file_close (file);
  return grub_errno;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT (newc)
{
  grub_fs_register (&grub_cpio_fs);
  cmd = grub_register_extcmd ("mkinitrd", grub_cmd_mkinitrd, 0,
                              N_("OPTIONS"),
                              N_("Make a virtual drive from a file."), options);
  grub_disk_dev_register (&grub_initrd_dev);
}

GRUB_MOD_FINI (newc)
{
  grub_fs_unregister (&grub_cpio_fs);
  grub_unregister_extcmd (cmd);
  grub_disk_dev_unregister (&grub_initrd_dev);
}
