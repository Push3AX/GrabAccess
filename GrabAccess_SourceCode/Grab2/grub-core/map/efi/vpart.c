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

#include <grub/efi/efi.h>
#include <grub/efi/api.h>
#include <grub/efi/sfs.h>
#include <grub/msdos_partition.h>
#include <grub/gpt_partition.h>

#include <guid.h>
#include <misc.h>
#include <stddef.h>
#include <iso.h>

#pragma GCC diagnostic ignored "-Wcast-align"

#define EFI_PARTITION   0xef

static grub_efi_device_path_t *
fill_mbr_dp (grub_efivdisk_t *vpart, grub_off_t *size)
{
  struct grub_msdos_partition_mbr mbr;
  grub_uint32_t unique_mbr_signature;
  grub_off_t  part_addr;
  grub_off_t  part_size;
  grub_efi_device_path_t *tmp_dp;
  grub_uint32_t i;
  grub_uint32_t part_num = 0;

  file_read (vpart->file, &mbr, sizeof (mbr), 0);

  for(i = 0; i < 4; i++)
  {
    if(mbr.entries[i].flag == 0x80)
    {
      part_addr = mbr.entries[i].start;
      part_size = mbr.entries[i].length;
      part_num = i + 1;
      break;
    }
  }
  if(!part_num)
    return NULL;

  vpart->addr = part_addr << FD_SHIFT;
  unique_mbr_signature = *(grub_efi_uint32_t*)(mbr.unique_signature);

  tmp_dp = grub_efi_create_device_node (MEDIA_DEVICE_PATH, MEDIA_HARDDRIVE_DP,
                                sizeof (grub_efi_hard_drive_device_path_t));
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)->partition_number = part_num;
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)->partition_start  = part_addr;
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)->partition_size   = part_size;
  *(grub_efi_uint32_t*)((grub_efi_hard_drive_device_path_t*)tmp_dp)
        ->partition_signature = unique_mbr_signature;
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)->partmap_type = 1;
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)->signature_type = 1;

  *size = part_size << FD_SHIFT;
  return tmp_dp;
}

static grub_efi_device_path_t *
fill_gpt_dp (grub_efivdisk_t *vpart, grub_off_t *size)
{
  struct grub_gpt_header gpt;
  struct grub_gpt_partentry *gpt_entry;
  grub_size_t gpt_entry_size;
  grub_uint64_t gpt_entry_pos;
  grub_uint64_t part_addr;
  grub_uint64_t part_size;
  grub_gpt_part_guid_t gpt_part_signature;
  grub_uint32_t part_num = 0;
  grub_efi_device_path_t *tmp_dp;
  grub_uint32_t i;

  /* "EFI PART" */
  grub_uint8_t GPT_HDR_MAGIC[8] = GRUB_GPT_HEADER_MAGIC;
  grub_packed_guid_t GPT_EFI_SYSTEM_PART_GUID = GRUB_GPT_PARTITION_TYPE_EFI_SYSTEM;

  file_read (vpart->file, &gpt, sizeof (gpt),
             PRIMARY_PART_HEADER_LBA * FD_BLOCK_SIZE);

  for (i = 0; i < 8; i++)
  {
    if (gpt.magic[i] != GPT_HDR_MAGIC[i])
      return NULL;
  }

  gpt_entry_pos = gpt.partitions << FD_SHIFT;
  gpt_entry_size = gpt.partentry_size;
  gpt_entry = grub_zalloc (gpt.partentry_size * gpt.maxpart);
  if(!gpt_entry)
    return NULL;

  for (i = 0; i < gpt.maxpart; i++)
  {
    file_read (vpart->file, gpt_entry, gpt_entry_size,
               gpt_entry_pos + i * gpt_entry_size);
    if (grub_guidcmp (&gpt_entry->type, &GPT_EFI_SYSTEM_PART_GUID))
    {
      part_addr = gpt_entry->start;
      part_size = gpt_entry->end - gpt_entry->start;
      grub_guidcpy (&gpt_part_signature, &gpt_entry->guid); 
      part_num = i + 1;
      break;
    }
  }
  if(!part_num)
  {
    grub_free (gpt_entry);
    return NULL;
  }
  vpart->addr = part_addr << FD_SHIFT;

  tmp_dp = grub_efi_create_device_node (MEDIA_DEVICE_PATH, MEDIA_HARDDRIVE_DP,
                                sizeof (grub_efi_hard_drive_device_path_t));
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)->partition_number = part_num;
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)->partition_start = part_addr;
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)->partition_size = part_size;
  grub_guidcpy ((grub_packed_guid_t *)
        &(((grub_efi_hard_drive_device_path_t*)tmp_dp)->partition_signature[0]),
        &gpt_part_signature);
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)->partmap_type = 2;
  ((grub_efi_hard_drive_device_path_t*)tmp_dp)->signature_type = 2;
  grub_free (gpt_entry);

  *size = part_size << FD_SHIFT;
  return tmp_dp;
}

static grub_efi_device_path_t *
fill_iso_dp (grub_efivdisk_t *vpart, grub_off_t *size)
{
  grub_efi_device_path_t *tmp_dp;
  grub_uint64_t part_size = 0;

  if (!grub_iso_get_eltorito (vpart->file, &vpart->addr, &part_size))
    return NULL;

  tmp_dp = grub_efi_create_device_node (MEDIA_DEVICE_PATH, MEDIA_CDROM_DP,
                                        sizeof (grub_efi_cdrom_device_path_t));
  ((grub_efi_cdrom_device_path_t *)tmp_dp)->boot_entry = 1;
  ((grub_efi_cdrom_device_path_t *)tmp_dp)->partition_start =
            vpart->addr >> CD_SHIFT;
  ((grub_efi_cdrom_device_path_t *)tmp_dp)->partition_size =
            part_size >> CD_SHIFT;

  *size = part_size;
  return tmp_dp;
}

grub_err_t
grub_efivpart_install (struct grub_efivdisk_data *disk,
                       struct grub_arg_list *state)
{
  grub_efi_status_t status;
  grub_efi_boot_services_t *b = grub_efi_system_table->boot_services;
  grub_efi_device_path_t *tmp_dp = NULL;
  grub_off_t part_size = 0;
  grub_efi_uint32_t bs = disk->vdisk.media.block_size;
  /* guid */
  grub_efi_guid_t dp_guid = GRUB_EFI_DEVICE_PATH_GUID;
  grub_efi_guid_t blk_io_guid = GRUB_EFI_BLOCK_IO_GUID;
  grub_efi_guid_t sfs_guid = GRUB_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

  switch (disk->type)
  {
    case CD:
      tmp_dp = fill_iso_dp (&disk->vpart, &part_size);
      break;
    case MBR:
      tmp_dp = fill_mbr_dp (&disk->vpart, &part_size);
      break;
    case GPT:
      tmp_dp = fill_gpt_dp (&disk->vpart, &part_size);
      break;
    default:
      break;
  }

  if (!tmp_dp)
  {
    grub_dprintf ("map", "BOOTABLE PARTITION NOT FOUND\n");
    return GRUB_ERR_FILE_NOT_FOUND;
  }

  disk->vpart.size = part_size;
  disk->vpart.handle = NULL;
  disk->vpart.dp = grub_efi_append_device_node (disk->vdisk.dp, tmp_dp);
  grub_free (tmp_dp);
  /* block io */
  grub_memcpy (&disk->vpart.block_io, &blockio_template,
               sizeof (block_io_protocol_t));
  /* media */
  disk->vpart.block_io.media = &disk->vpart.media;
  disk->vpart.media.media_id = VDISK_MEDIA_ID;
  disk->vpart.media.removable_media = FALSE;
  disk->vpart.media.media_present = TRUE;
  disk->vpart.media.logical_partition = TRUE;
  disk->vpart.media.read_only = state[MAP_RO].set;
  disk->vpart.media.write_caching = FALSE;
  disk->vpart.media.io_align = 16;
  disk->vpart.media.block_size = bs;
  disk->vpart.media.last_block =
              grub_divmod64 (part_size + bs - 1, bs, 0) - 1;
  /* info */
  grub_dprintf ("map", "VPART addr=%ld size=%lld\n",
                (unsigned long)disk->vpart.addr, (unsigned long long)part_size);
  grub_dprintf ("map", "VPART blksize=%d lastblk=%lld\n",
                disk->vpart.media.block_size,
                (unsigned long long)disk->vpart.media.last_block);
  grub_efi_dprintf_dp (disk->vpart.dp);
  status = efi_call_6 (b->install_multiple_protocol_interfaces,
                       &disk->vpart.handle, &dp_guid, disk->vpart.dp,
                       &blk_io_guid, &disk->vpart.block_io, NULL);
  if(status != GRUB_EFI_SUCCESS)
  {
    grub_dprintf ("map", "failed to install virtual partition\n");
    return GRUB_ERR_FILE_NOT_FOUND;
  }
  efi_call_4 (b->connect_controller, disk->vpart.handle, NULL, NULL, TRUE);

  if (disk->type != CD)
    return GRUB_ERR_NONE;
  /* for DUET UEFI firmware */
  grub_efi_simple_fs_protocol_t *sfs_protocol = NULL;
  status = efi_call_3 (b->handle_protocol, disk->vpart.handle,
                       &sfs_guid, (void **)&sfs_protocol);
  if(status == GRUB_EFI_SUCCESS)
    return GRUB_ERR_NONE;
  return grub_efivdisk_connect_driver (disk->vpart.handle,
                                       L"FAT File System Driver");
}
