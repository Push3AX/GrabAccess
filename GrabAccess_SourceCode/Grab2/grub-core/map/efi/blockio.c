 /*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
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

#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/disk.h>

#include <vfat.h>
#include <misc.h>
#include <stddef.h>

#define VDISK_BLOCKIO_TO_PARENT(a) CR(a, grub_efivdisk_t, block_io)

static grub_efi_status_t EFIAPI
blockio_reset (block_io_protocol_t *this __unused,
               grub_efi_boolean_t extended __unused)
{
  return GRUB_EFI_SUCCESS;
}

static grub_efi_status_t EFIAPI
blockio_read (block_io_protocol_t *this, grub_efi_uint32_t media_id,
              grub_efi_lba_t lba, grub_efi_uintn_t len, void *buf)
{
  grub_efivdisk_t *data;
  grub_efi_uintn_t block_num;

  if (!buf)
    return GRUB_EFI_INVALID_PARAMETER;

  if (!len)
    return GRUB_EFI_SUCCESS;

  data = VDISK_BLOCKIO_TO_PARENT(this);

  /* wimboot */
  if (data->media.media_id == VDISK_MBR_SIGNATURE)
  {
    vfat_read ((lba + data->addr), (len / VDISK_SECTOR_SIZE), buf);
    return GRUB_EFI_SUCCESS;
  }

  if (media_id != data->media.media_id)
    return GRUB_EFI_MEDIA_CHANGED;

  if ((len % data->media.block_size) != 0)
    return GRUB_EFI_BAD_BUFFER_SIZE;

  if (lba > data->media.last_block)
    return GRUB_EFI_INVALID_PARAMETER;

  block_num = len / data->media.block_size;

  if ((lba + block_num - 1) > data->media.last_block)
    return GRUB_EFI_INVALID_PARAMETER;

  file_read (data->file, buf, len, data->addr + lba * data->media.block_size);

  return GRUB_EFI_SUCCESS;
}

static grub_efi_status_t EFIAPI
blockio_write (block_io_protocol_t *this,
               grub_efi_uint32_t media_id,
               grub_efi_lba_t lba,
               grub_efi_uintn_t len, void *buf)
{
  grub_efivdisk_t *data;
  grub_efi_uintn_t block_num;

  if (!buf)
    return GRUB_EFI_INVALID_PARAMETER;

  if (!len)
    return GRUB_EFI_SUCCESS;

  data = VDISK_BLOCKIO_TO_PARENT(this);

  /* wimboot */
  if (data->media.media_id == VDISK_MBR_SIGNATURE || data->media.read_only)
    return GRUB_EFI_WRITE_PROTECTED;

  if (media_id != data->media.media_id)
    return GRUB_EFI_MEDIA_CHANGED;

  if ((len % data->media.block_size) != 0)
    return GRUB_EFI_BAD_BUFFER_SIZE;

  if (lba > data->media.last_block)
    return GRUB_EFI_INVALID_PARAMETER;

  block_num = len / data->media.block_size;

  if ((lba + block_num - 1) > data->media.last_block)
    return GRUB_EFI_INVALID_PARAMETER;

  file_write (data->file, buf, len, data->addr + lba * data->media.block_size);

  return GRUB_EFI_SUCCESS;
}

static grub_efi_status_t EFIAPI
blockio_flush (block_io_protocol_t *this __unused)
{
  return GRUB_EFI_SUCCESS;
}

block_io_protocol_t blockio_template =
{
  EFI_BLOCK_IO_PROTOCOL_REVISION,
  (grub_efi_block_io_media_t *) 0,
  blockio_reset,
  blockio_read,
  blockio_write,
  blockio_flush
};
