/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
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

#ifndef GRUB_EFI_SIMPLEFILESYSTEM_HEADER
#define GRUB_EFI_SIMPLEFILESYSTEM_HEADER

#include <grub/types.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/misc.h>

#define GRUB_EFI_FILE_PROTOCOL_REVISION  0x00010000
#define GRUB_EFI_FILE_PROTOCOL_REVISION2 0x00020000
#define GRUB_EFI_FILE_PROTOCOL_LATEST_REVISION GRUB_EFI_FILE_PROTOCOL_REVISION2

#define GRUB_EFI_FILE_REVISION GRUB_EFI_FILE_PROTOCOL_REVISION

typedef struct
{
  grub_efi_event_t event;
  grub_efi_status_t status;
  grub_efi_uintn_t buffer_size;
  void *buffer;
} grub_efi_file_io_token_t;

// Open modes
#define GRUB_EFI_FILE_MODE_READ   0x0000000000000001ULL
#define GRUB_EFI_FILE_MODE_WRITE  0x0000000000000002ULL
#define GRUB_EFI_FILE_MODE_CREATE 0x8000000000000000ULL

// File attributes
#define GRUB_EFI_FILE_READ_ONLY  0x0000000000000001ULL
#define GRUB_EFI_FILE_HIDDEN     0x0000000000000002ULL
#define GRUB_EFI_FILE_SYSTEM     0x0000000000000004ULL
#define GRUB_EFI_FILE_RESERVED   0x0000000000000008ULL
#define GRUB_EFI_FILE_DIRECTORY  0x0000000000000010ULL
#define GRUB_EFI_FILE_ARCHIVE    0x0000000000000020ULL
#define GRUB_EFI_FILE_VALID_ATTR 0x0000000000000037ULL

struct grub_efi_file_protocol
{
  grub_efi_uint64_t revision;
  grub_efi_status_t (*file_open) (struct grub_efi_file_protocol *this,
                                  struct grub_efi_file_protocol **new_handle,
                                  grub_efi_char16_t *file_name,
                                  grub_efi_uint64_t open_mode,
                                  grub_efi_uint64_t attributes);
  grub_efi_status_t (*file_close) (struct grub_efi_file_protocol *this);
  grub_efi_status_t (*file_delete) (struct grub_efi_file_protocol *this);
  grub_efi_status_t (*file_read) (struct grub_efi_file_protocol *this,
                                  grub_efi_uintn_t *buffer_size,
                                  void *buffer);
  grub_efi_status_t (*file_write) (struct grub_efi_file_protocol *this,
                                   grub_efi_uintn_t *buffer_size,
                                   void *buffer);
  grub_efi_status_t (*get_pos) (struct grub_efi_file_protocol *this,
                                grub_efi_uint64_t *pos);
  grub_efi_status_t (*set_pos) (struct grub_efi_file_protocol *this,
                                grub_efi_uint64_t pos);
  grub_efi_status_t (*get_info) (struct grub_efi_file_protocol *this,
                                 grub_efi_guid_t *information_type,
                                 grub_efi_uintn_t *buffer_size,
                                 void *buffer);
  grub_efi_status_t (*set_info) (struct grub_efi_file_protocol *this,
                                 grub_efi_guid_t *information_type,
                                 grub_efi_uintn_t buffer_size,
                                 void *buffer);
  grub_efi_status_t (*flush) (struct grub_efi_file_protocol *this);
  grub_efi_status_t (*open_ex) (struct grub_efi_file_protocol *this,
                                struct grub_efi_file_protocol **new_handle,
                                grub_efi_char16_t *file_name,
                                grub_efi_uint64_t open_mode,
                                grub_efi_uint64_t attributes,
                                grub_efi_file_io_token_t *token);
  grub_efi_status_t (*read_ex) (struct grub_efi_file_protocol *this,
                                grub_efi_file_io_token_t *token);
  grub_efi_status_t (*write_ex) (struct grub_efi_file_protocol *this,
                                 grub_efi_file_io_token_t *token);
  grub_efi_status_t (*flush_ex) (struct grub_efi_file_protocol *this,
                                 grub_efi_file_io_token_t *token);
};
typedef struct grub_efi_file_protocol grub_efi_file_protocol_t;
typedef grub_efi_file_protocol_t *grub_efi_file_handle_t;
typedef grub_efi_file_protocol_t  grub_efi_file_t;

#define GRUB_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION 0x00010000
#define GRUB_EFI_FILE_IO_REVISION  GRUB_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION

struct grub_efi_simple_fs_protocol
{
  grub_efi_uint64_t revision;
  grub_efi_status_t (*open_volume) (struct grub_efi_simple_fs_protocol *this,
                                    grub_efi_file_protocol_t **root);
};
typedef struct grub_efi_simple_fs_protocol grub_efi_simple_fs_protocol_t;

#define GRUB_EFI_FILE_INFO_GUID \
  { 0x09576e92, 0x6d3f, 0x11d2, \
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } \
  }

#define GRUB_EFI_FILE_SYSTEM_INFO_GUID \
  { 0x09576e93, 0x6d3f, 0x11d2, \
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } \
  }

#define GRUB_EFI_FILE_SYSTEM_VOLUME_LABEL_GUID \
  { 0xdb47d7d3, 0xfe81, 0x11d3, \
    { 0x9a, 0x35, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } \
  }

typedef struct grub_efi_file_info
{
  /* size of file_info structure */
  grub_efi_uint64_t size;
  grub_efi_uint64_t file_size;
  grub_efi_uint64_t physical_size;
  grub_efi_time_t create_time;
  grub_efi_time_t last_access_time;
  grub_efi_time_t modification_time;
  grub_efi_uint64_t attribute;
  grub_efi_char16_t file_name[1];
} grub_efi_file_info_t;

typedef struct grub_efi_file_system_info
{
  grub_efi_uint64_t size;
  grub_efi_boolean_t read_only;
  grub_efi_uint64_t volume_size;
  grub_efi_uint64_t free_space;
  grub_efi_uint32_t block_size;
  grub_efi_char16_t volume_label[1];
} grub_efi_fs_info_t;

typedef struct grub_efi_file_system_volume_label
{
  grub_efi_char16_t volume_label[1];
} grub_efi_fs_label_t;

#endif
