 /*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019,2020  Free Software Foundation, Inc.
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
 *
 */

#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/device.h>
#include <grub/err.h>
#include <grub/env.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/term.h>

#include <vfat.h>
#include <misc.h>

struct grub_vfatdisk_file *vfat_file_list;

static int
grub_vfatdisk_iterate (grub_disk_dev_iterate_hook_t hook, void *hook_data,
                       grub_disk_pull_t pull __unused)
{
  if (pull != GRUB_DISK_PULL_NONE)
    return 0;
  return hook ("vfat", hook_data);
}

static grub_err_t
grub_vfatdisk_open (const char *name, grub_disk_t disk)
{
  if (grub_strcmp (name, "vfat"))
      return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "not a vfat disk");

  disk->total_sectors = VDISK_COUNT;
  disk->max_agglomerate = GRUB_DISK_MAX_MAX_AGGLOMERATE;
  disk->id = 0;

  return GRUB_ERR_NONE;
}

static void
grub_vfatdisk_close (grub_disk_t disk __attribute((unused)))
{
}

static grub_err_t
grub_vfatdisk_read (grub_disk_t disk __attribute((unused)), grub_disk_addr_t sector,
                    grub_size_t size, char *buf)
{
  vfat_read (sector, size, buf);
  return 0;
}

static grub_err_t
grub_vfatdisk_write (grub_disk_t disk __attribute ((unused)),
                     grub_disk_addr_t sector __attribute ((unused)),
                     grub_size_t size __attribute ((unused)),
                     const char *buf __attribute ((unused)))
{
  return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET, "vfat write is not supported");
}

static struct grub_disk_dev grub_vfatdisk_dev =
{
  .name = "vfat",
  .id = GRUB_DISK_DEVICE_VFAT_ID,
  .disk_iterate = grub_vfatdisk_iterate,
  .disk_open = grub_vfatdisk_open,
  .disk_close = grub_vfatdisk_close,
  .disk_read = grub_vfatdisk_read,
  .disk_write = grub_vfatdisk_write,
  .next = 0
};

void
vfat_help (void)
{
  grub_printf ("\nvfat -- Virtual FAT Disk\n");
  grub_printf ("vfat --create\n");
  grub_printf ("    mount virtual disk to (vfat)\n");
  grub_printf ("vfat [--mem] --add=XXX YYY\n");
  grub_printf ("    Add file \"YYY\" to disk, file name is \"XXX\"\n");
  grub_printf ("vfat --install\n");
  grub_printf ("    Install block_io protocol for virtual disk\n");
  grub_printf ("vfat --boot\n");
  grub_printf ("    Boot bootmgfw.efi from virtual disk\n");
  grub_printf ("vfat --ls\n");
  grub_printf ("    List all files in virtual disk\n");
  grub_printf ("vfat --patch=FILE --offset=n STRING\n");
  grub_printf ("vfat --patch=FILE --search=STRING [--count=n] STRING\n");
  grub_printf ("    Patch files in vdisk\n");
}

void
vfat_create (void)
{
  grub_disk_dev_t dev;
  for (dev = grub_disk_dev_list; dev; dev = dev->next)
  {
    if (grub_strcmp (dev->name, "vfat") == 0)
    {
      grub_printf ("vfat: already exist\n");
      return;
    }
  }
  grub_disk_dev_register (&grub_vfatdisk_dev);
}

void
vfat_ls (void)
{
  int i = 1;
  struct grub_vfatdisk_file *f = NULL;
  for (f = vfat_file_list; f; f = f->next, i++)
  {
    grub_printf ("[%d] %s %s", i, f->name, f->file->name);
  }
}

grub_size_t
vfat_replace_hex (char *addr, grub_size_t addr_len,
             const char *search, grub_size_t search_len,
             const char *replace, grub_size_t replace_len, int count)
{
  grub_size_t offset, last = 0;
  int cnt = 0;
  for (offset = 0; offset + search_len < addr_len; offset++)
  {
    if (grub_memcmp (addr + offset, search, search_len) == 0)
    {
      last = offset;
      cnt++;
      grub_memcpy (addr + offset, replace, replace_len);
      if (count && cnt == count)
        break;
    }
  }
  return last;
}

static unsigned int
to_digit (char c)
{
  if ('0' <= c && c <= '9')
    return c - '0';
  if ('a' <= c && c <= 'f')
    return c + 10 - 'a';
  if ('A' <= c && c <= 'F')
    return c + 10 - 'A';
  return 0;
}

static char *
hex_to_str (const char *hex) 
{
  unsigned int d1, d2;
  grub_size_t i;
  grub_size_t len = grub_strlen (hex) >> 1;
  char *str = NULL;
  if (!len)
    return NULL;
  str = grub_zalloc (len);
  if (!str)
    return NULL;
  for (i = 0; i < len; i++)
  {
    d1 = to_digit (hex[i << 1]) << 4;
    d2 = to_digit (hex[(i << 1) + 1]);
    str[i] = d1 + d2;
  }
  return str;
}

static char *
str_to_wcs (const char *str)
{
  grub_size_t i;
  grub_size_t len = grub_strlen (str) + 1;
  wchar_t *wcs = NULL;
  if (!len)
    return NULL;
  wcs = grub_zalloc (len << 1);
  if (!wcs)
    return NULL;
  for (i = 0; i < len; i++)
    wcs[i] = str[i];
  return (char *)wcs;
}

static void *
get_vfat_file (const char *file, grub_size_t *size)
{
  void *addr = NULL;
  struct grub_vfatdisk_file *f = NULL;
  for (f = vfat_file_list; f; f = f->next)
  {
    if (grub_ismemfile (f->file->name) && grub_strcmp (f->name, file) == 0)
    {
      addr = f->file->data;
      if (size)
        *size = f->file->size;
      break;
    }
    else
      continue;
  }
  return addr;
}

static char *
process_str (const char *in, grub_size_t *len)
{
  char *str = NULL;
  grub_size_t l = grub_strlen (in);
  if (l > 1 && in[0] == 's')
  {
    str = grub_strdup (&in[1]);
    l--;
  }
  else if (l > 1 && in[0] == 'w')
  {
    str = str_to_wcs (&in[1]);
    l--;
    l = l << 1;
  }
  else
  {
    str = hex_to_str (in);
    l = l >> 1;
  }
  *len = l;
  return str;
}

void
vfat_patch_offset (const char *file, grub_size_t offset, const char *replace)
{
  grub_size_t len;
  char *str = NULL;
  char *addr = get_vfat_file (file, NULL);
  if (!addr)
    return;
  str = process_str (replace, &len);
  if (!str)
    return;
  grub_memcpy (addr + offset, str, len);
  grub_free (str);
}

void
vfat_patch_search (const char *file, const char *search,
                   const char *replace, int count)
{
  grub_size_t search_len, replace_len;
  char *search_str = NULL;
  char *replace_str = NULL;
  grub_size_t size = 0;
  char *addr = get_vfat_file (file, &size);
  if (!addr)
    return;
  search_str = process_str (search, &search_len);
  replace_str = process_str (replace, &replace_len);
  vfat_replace_hex (addr, size, search_str, search_len,
               replace_str, replace_len, count);
  if (search_str)
    grub_free (search_str);
  if (replace_str)
    grub_free (replace_str);
}

void
vfat_append_list (grub_file_t file, const char *file_name)
{
  struct grub_vfatdisk_file *newfile = NULL;
  newfile = grub_malloc (sizeof (struct grub_vfatdisk_file));
  if (!newfile)
    goto err;

  grub_printf ("Add: %s -> %s\n", file->name, file_name);

  newfile->name = grub_strdup (file_name);
  if (!newfile->name)
    goto err;
  newfile->file = file;
  newfile->next = vfat_file_list;
  vfat_file_list = newfile;
  return;
err:
  if (newfile)
    grub_free (newfile);
}

void
vfat_read_wrapper (struct vfat_file *vfile, void *data, size_t offset, size_t len)
{
  file_read (vfile->opaque, data, len, offset);
}
