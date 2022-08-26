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

#include <grub/misc.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/disk.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <guid.h>
#include <misc.h>
#include <vfat.h>
#include <wimboot.h>

/** GUID used in vendor device path */
#if 0
static const grub_packed_guid_t WIMBOOT_GUID =
{ 0x1322d197, 0x15dc, 0x4a45,
  { 0xa6, 0xa4, 0xfa, 0x57, 0x05, 0x4e, 0xa6, 0x14 }
};
#endif

grub_efivdisk_t wimboot_disk, wimboot_part;

grub_err_t
grub_wimboot_install (void)
{
  grub_efi_boot_services_t *b;
  b = grub_efi_system_table->boot_services;
  grub_efi_status_t status;
  grub_efi_device_path_t *tmp_dp;
  /* guid */
  grub_efi_guid_t dp_guid = GRUB_EFI_DEVICE_PATH_GUID;
  grub_efi_guid_t blk_io_guid = GRUB_EFI_BLOCK_IO_GUID;
  /* Install virtual disk */
  wimboot_disk.addr = 0;
  wimboot_disk.handle = NULL;
  grub_memcpy (&wimboot_disk.block_io,
               &blockio_template, sizeof (block_io_protocol_t));
  tmp_dp = grub_efi_create_device_node (HARDWARE_DEVICE_PATH, HW_VENDOR_DP,
                                        sizeof(grub_efi_vendor_device_path_t));
  grub_guidgen (&((grub_efi_vendor_device_path_t *)tmp_dp)->vendor_guid);
  //grub_guidcpy (&((grub_efi_vendor_device_path_t *)tmp_dp)->vendor_guid, &WIMBOOT_GUID);
  wimboot_disk.dp = grub_efi_append_device_node (NULL, tmp_dp);
  if (tmp_dp)
    grub_free (tmp_dp);
  wimboot_disk.block_io.media = &wimboot_disk.media;
  wimboot_disk.media.media_id = VDISK_MBR_SIGNATURE;
  wimboot_disk.media.removable_media = FALSE;
  wimboot_disk.media.media_present = TRUE;
  wimboot_disk.media.logical_partition = FALSE;
  wimboot_disk.media.read_only = TRUE;
  wimboot_disk.media.write_caching = FALSE;
  wimboot_disk.media.io_align = 16;
  wimboot_disk.media.block_size = VDISK_SECTOR_SIZE;
  wimboot_disk.media.last_block = VDISK_COUNT - 1;
  /* Install virtual partition */
  wimboot_part.addr = VDISK_PARTITION_LBA;
  wimboot_part.handle = NULL;
  grub_memcpy (&wimboot_part.block_io,
               &blockio_template, sizeof (block_io_protocol_t));
  tmp_dp = grub_efi_create_device_node (MEDIA_DEVICE_PATH, MEDIA_HARDDRIVE_DP,
                                        sizeof (grub_efi_hard_drive_device_path_t));
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)
        ->partition_number = 1;
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)
        ->partition_start = VDISK_PARTITION_LBA;
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)
        ->partition_size = VDISK_PARTITION_COUNT;
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)
        ->partition_signature[0] = ((VDISK_MBR_SIGNATURE >> 0) & 0xff);
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)
        ->partition_signature[1] = ((VDISK_MBR_SIGNATURE >> 8) & 0xff);
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)
        ->partition_signature[2] = ((VDISK_MBR_SIGNATURE >> 16) & 0xff);
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)
        ->partition_signature[3] = ((VDISK_MBR_SIGNATURE >> 24) & 0xff);
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)
        ->partmap_type = MBR_TYPE_PCAT;
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)
        ->signature_type = SIGNATURE_TYPE_MBR;
  wimboot_part.dp = grub_efi_append_device_node (wimboot_disk.dp, tmp_dp);
  if (tmp_dp)
    grub_free (tmp_dp);
  wimboot_part.block_io.media = &wimboot_part.media;
  wimboot_part.media.media_id = VDISK_MBR_SIGNATURE;
  wimboot_part.media.removable_media = FALSE;
  wimboot_part.media.media_present = TRUE;
  wimboot_part.media.logical_partition = TRUE;
  wimboot_part.media.read_only = TRUE;
  wimboot_part.media.write_caching = FALSE;
  wimboot_part.media.io_align = 16;
  wimboot_part.media.block_size = VDISK_SECTOR_SIZE;
  wimboot_part.media.last_block = VDISK_PARTITION_COUNT - 1;

  grub_printf ("Installing block_io protocol for virtual disk ...\n");
  status = efi_call_6 (b->install_multiple_protocol_interfaces,
                       &wimboot_disk.handle, &dp_guid, wimboot_disk.dp,
                       &blk_io_guid, &wimboot_disk.block_io, NULL);
  if (status != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_BAD_OS, "failed to install virtual disk\n");

  grub_printf ("Installing block_io protocol for virtual partition ...\n");
  status = efi_call_6 (b->install_multiple_protocol_interfaces,
                       &wimboot_part.handle, &dp_guid, wimboot_part.dp,
                       &blk_io_guid, &wimboot_part.block_io, NULL);
  if (status != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_BAD_OS, "failed to install virtual partition\n");

  grub_efi_set_first_disk (wimboot_disk.handle);
  return GRUB_ERR_NONE;
}
