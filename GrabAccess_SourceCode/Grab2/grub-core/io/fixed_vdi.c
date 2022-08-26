/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2020  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/file.h>
#include <grub/mm.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define VDI_IMAGE_FILE_INFO   "<<< Oracle VM VirtualBox Disk Image >>>\n"

#define VDI_OFFSET (2 * 1048576)

/** Image signature. */
#define VDI_IMAGE_SIGNATURE   (0xbeda107f)

typedef struct VDIPREHEADER
{
  /** Just text info about image type, for eyes only. */
  char            szFileInfo[64];
  /** The image signature (VDI_IMAGE_SIGNATURE). */
  grub_uint32_t   u32Signature;
  /** The image version (VDI_IMAGE_VERSION). */
  grub_uint32_t   u32Version;
} VDIPREHEADER, *PVDIPREHEADER;

struct grub_fixed_vdiio
{
  grub_file_t file;
};
typedef struct grub_fixed_vdiio *grub_fixed_vdiio_t;

static struct grub_fs grub_fixed_vdiio_fs;

static grub_err_t
grub_fixed_vdiio_close (grub_file_t file)
{
  grub_fixed_vdiio_t fixed_vdiio = file->data;
  grub_file_close (fixed_vdiio->file);
  grub_free (fixed_vdiio);
  file->device = 0;
  file->name = 0;
  return grub_errno;
}

static grub_file_t
grub_fixed_vdiio_open (grub_file_t io, enum grub_file_type type)
{
  grub_file_t file;
  grub_fixed_vdiio_t fixed_vdiio;
  VDIPREHEADER vdihdr;
  grub_uint8_t mbr[2];

  if (type & GRUB_FILE_TYPE_NO_DECOMPRESS)
    return io;
  if (io->size < VDI_OFFSET + 0x200)
    return io;

  /* test header */
  grub_memset (&vdihdr, 0, sizeof(vdihdr));
  grub_file_seek (io, 0);
  grub_file_read (io, &vdihdr, sizeof (vdihdr));
  grub_file_seek (io, 0);
  if (vdihdr.u32Signature != VDI_IMAGE_SIGNATURE ||
      grub_strncmp (vdihdr.szFileInfo, VDI_IMAGE_FILE_INFO,
                    grub_strlen(VDI_IMAGE_FILE_INFO)) != 0)
    return io;
  /* test mbr */
  grub_file_seek (io, VDI_OFFSET + 0x1fe);
  grub_file_read (io, mbr, sizeof (mbr));
  grub_file_seek (io, 0);
  if (mbr[0] != 0x55 || mbr[1] != 0xaa)
    return io;

  file = (grub_file_t) grub_zalloc (sizeof (*file));
  if (!file)
    return 0;

  fixed_vdiio = grub_zalloc (sizeof (*fixed_vdiio));
  if (!fixed_vdiio)
  {
    grub_free (file);
    return 0;
  }
  fixed_vdiio->file = io;

  file->device = io->device;
  file->data = fixed_vdiio;
  file->fs = &grub_fixed_vdiio_fs;
  file->size = io->size - VDI_OFFSET;
  file->not_easily_seekable = io->not_easily_seekable;

  return file;
}

static grub_ssize_t
grub_fixed_vdiio_read (grub_file_t file, char *buf, grub_size_t len)
{
  grub_fixed_vdiio_t fixed_vdiio = file->data;
  grub_ssize_t ret;
  grub_file_seek (fixed_vdiio->file, file->offset + VDI_OFFSET);
  ret = grub_file_read (fixed_vdiio->file, buf, len);
  file->offset += ret;
  return ret;
}

static struct grub_fs grub_fixed_vdiio_fs = {
  .name = "fixed_vdiio",
  .fs_dir = 0,
  .fs_open = 0,
  .fs_read = grub_fixed_vdiio_read,
  .fs_close = grub_fixed_vdiio_close,
  .fs_label = 0,
  .next = 0
};

GRUB_MOD_INIT(fixed_vdi)
{
  grub_file_filter_register (GRUB_FILE_FILTER_FIXED_VDIIO, grub_fixed_vdiio_open);
}

GRUB_MOD_FINI(fixed_vdi)
{
  grub_file_filter_unregister (GRUB_FILE_FILTER_FIXED_VDIIO);
}
