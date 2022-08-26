/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007  Free Software Foundation, Inc.
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

#ifndef GRUB_EFI_DISK_HEADER
#define GRUB_EFI_DISK_HEADER	1

#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/symbol.h>
#include <grub/disk.h>
#include <grub/file.h>

struct grub_efidisk_data
{
  grub_efi_handle_t handle;
  grub_efi_device_path_t *device_path;
  grub_efi_device_path_t *last_device_path;
  grub_efi_block_io_t *block_io;
  struct grub_efidisk_data *next;
};

grub_efi_handle_t
EXPORT_FUNC(grub_efidisk_get_device_handle) (grub_disk_t disk);
char *EXPORT_FUNC(grub_efidisk_get_device_name) (grub_efi_handle_t *handle);
char *EXPORT_FUNC(grub_efidisk_get_device_name_from_dp) (grub_efi_device_path_t *dp);

void EXPORT_FUNC(grub_efidisk_init) (void);
void EXPORT_FUNC(grub_efidisk_fini) (void);

struct block_io_protocol
{
  grub_efi_uint64_t revision;
  grub_efi_block_io_media_t *media;
  grub_efi_status_t (EFIAPI *reset) (struct block_io_protocol *this,
                                     grub_efi_boolean_t extended_verification);
  grub_efi_status_t (EFIAPI *read_blocks) (struct block_io_protocol *this,
                                           grub_efi_uint32_t media_id,
                                           grub_efi_lba_t lba,
                                           grub_efi_uintn_t buffer_size,
                                           void *buffer);
  grub_efi_status_t (EFIAPI *write_blocks) (struct block_io_protocol *this,
                                            grub_efi_uint32_t media_id,
                                            grub_efi_lba_t lba,
                                            grub_efi_uintn_t buffer_size,
                                            void *buffer);
  grub_efi_status_t (EFIAPI *flush_blocks) (struct block_io_protocol *this);
};
typedef struct block_io_protocol block_io_protocol_t;

typedef struct
{
  /* efi data */
  grub_efi_uint64_t addr;
  grub_efi_uint64_t size;
  grub_efi_handle_t handle;
  grub_efi_device_path_t *dp;
  block_io_protocol_t block_io;
  grub_efi_block_io_media_t media;
  /* grub data */
  grub_file_t file;
} grub_efivdisk_t;

enum grub_efivdisk_type
{
  UNKNOWN,
  HD,
  CD,
  FD,
  MBR,
  GPT,
};

struct grub_efivdisk_data
{
  char devname[20];
  enum grub_efivdisk_type type;
  grub_packed_guid_t guid;
  grub_efivdisk_t vdisk;
  grub_efivdisk_t vpart;
  struct grub_efivdisk_data *next;
};

extern struct grub_efivdisk_data *EXPORT_VAR (grub_efivdisk_list);

#endif /* ! GRUB_EFI_DISK_HEADER */
