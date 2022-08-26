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

#ifndef GRUB_EFI_SHELL_FUNC_HEADER
#define GRUB_EFI_SHELL_FUNC_HEADER

#include <grub/types.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/misc.h>

#define GRUB_EFI_SHELL_PROTOCOL_GUID \
 { 0x6302d008, 0x7f9b, 0x4f30, \
      { 0x87, 0xac, 0x60, 0xc9, 0xfe, 0xf5, 0xda, 0x4e } \
 }

#define GRUB_EFI_SHELL_PARAMETERS_PROTOCOL_GUID \
 { 0x752f3136, 0x4e16, 0x4fdc, \
      { 0xa2, 0x2a, 0xe5, 0xf4, 0x68, 0x12, 0xf4, 0xca } \
 }

#define GRUB_EFI_SHELL_DYNAMIC_COMMAND_PROTOCOL_GUID \
 { 0x3c7200e9, 0x5f, 0x4ea4, \
      { 0x87, 0xde, 0xa3, 0xdf, 0xac, 0x8a, 0x27, 0xc3 } \
 }

#define EFI_FILE_MODE_READ    0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE   0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE  0x8000000000000000ULL

#define EFI_FILE_READ_ONLY  0x0000000000000001ULL
#define EFI_FILE_HIDDEN     0x0000000000000002ULL
#define EFI_FILE_SYSTEM     0x0000000000000004ULL
#define EFI_FILE_RESERVED   0x0000000000000008ULL
#define EFI_FILE_DIRECTORY  0x0000000000000010ULL
#define EFI_FILE_ARCHIVE    0x0000000000000020ULL
#define EFI_FILE_VALID_ATTR 0x0000000000000037ULL

typedef grub_efi_uint32_t shell_device_name_flags_t;
#define EFI_DEVICE_NAME_USE_COMPONENT_NAME  0x00000001
#define EFI_DEVICE_NAME_USE_DEVICE_PATH     0x00000002

typedef void* shell_file_handle_t;

typedef enum
{
  SHELL_SUCCESS               = 0,
  SHELL_LOAD_ERROR            = 1,
  SHELL_INVALID_PARAMETER     = 2,
  SHELL_UNSUPPORTED           = 3,
  SHELL_BAD_BUFFER_SIZE       = 4,
  SHELL_BUFFER_TOO_SMALL      = 5,
  SHELL_NOT_READY             = 6,
  SHELL_DEVICE_ERROR          = 7,
  SHELL_WRITE_PROTECTED       = 8,
  SHELL_OUT_OF_RESOURCES      = 9,
  SHELL_VOLUME_CORRUPTED      = 10,
  SHELL_VOLUME_FULL           = 11,
  SHELL_NO_MEDIA              = 12,
  SHELL_MEDIA_CHANGED         = 13,
  SHELL_NOT_FOUND             = 14,
  SHELL_ACCESS_DENIED         = 15,
  // note the skipping of 16 and 17
  SHELL_TIMEOUT               = 18,
  SHELL_NOT_STARTED           = 19,
  SHELL_ALREADY_STARTED       = 20,
  SHELL_ABORTED               = 21,
  // note the skipping of 22, 23, and 24
  SHELL_INCOMPATIBLE_VERSION  = 25,
  SHELL_SECURITY_VIOLATION    = 26,
  SHELL_NOT_EQUAL             = 27
} shell_status_t;

typedef struct
{
  grub_efi_uint64_t size;
  grub_efi_uint64_t file_size;
  grub_efi_uint64_t physical_size;
  grub_efi_time_t create_time;
  grub_efi_time_t last_access_time;
  grub_efi_time_t modification_time;
  grub_efi_uint64_t attribute;
  grub_efi_char16_t file_name[1];
} grub_efi_file_info_t;

typedef struct
{
  grub_efi_list_entry_t link;
  grub_efi_status_t status;
  const grub_efi_char16_t *full_name;
  const grub_efi_char16_t *file_name;
  shell_file_handle_t handle;
  grub_efi_file_info_t *info;
} shell_file_info_t;

struct grub_efi_shell_protocol
{
  grub_efi_status_t (*execute) (grub_efi_handle_t *parent,
                     grub_efi_char16_t command_line,
                     grub_efi_char16_t **environment,
                     grub_efi_status_t *status_code);
  const grub_efi_char16_t* (*get_env) (const grub_efi_char16_t *name);
  grub_efi_status_t (*set_env) (const grub_efi_char16_t *name,
                     const grub_efi_char16_t *value,
                     grub_efi_boolean_t is_volatile);
  const grub_efi_char16_t* (*get_alias) (const grub_efi_char16_t *alias,
                            grub_efi_boolean_t *is_volatile);
  grub_efi_status_t (*set_alias) (const grub_efi_char16_t *command,
                     const grub_efi_char16_t *alias,
                     grub_efi_boolean_t replace,
                     grub_efi_boolean_t is_volatile);
  grub_efi_status_t (*get_help_text) (const grub_efi_char16_t *command,
                     const grub_efi_char16_t *sections,
                     grub_efi_char16_t **help_text);
  const grub_efi_device_path_protocol_t* (*get_dp_from_map) (const grub_efi_char16_t *mapping);
  const grub_efi_char16_t* (*get_map_from_dp) (grub_efi_device_path_protocol_t **dp);
  grub_efi_device_path_protocol_t* (*get_dp_from_file_path) (const grub_efi_char16_t *path);
  grub_efi_char16_t* (*get_file_path_from_dp) (const grub_efi_device_path_protocol_t *path);
  grub_efi_status_t (*set_map) (const grub_efi_device_path_protocol_t *dp,
                     const grub_efi_char16_t *mapping);
  const grub_efi_char16_t* (*get_cur_dir) (const grub_efi_char16_t *file_system_mapping);
  grub_efi_status_t (*set_cur_dir) (const grub_efi_char16_t *file_system,
                     const grub_efi_char16_t *dir);
  grub_efi_status_t (*open_file_list) (grub_efi_char16_t *path,
                     grub_efi_uint64_t open_mode,
                     shell_file_info_t **file_list);
  grub_efi_status_t (*free_file_list) (shell_file_info_t **file_list);
  grub_efi_status_t (*remove_dup_in_file_list) (shell_file_info_t **file_list);
  grub_efi_boolean_t (*batch_is_active) (void);
  grub_efi_boolean_t (*is_root_shell) (void);
  void (*enable_page_break) (void);
  void (*disable_page_break) (void);
  grub_efi_boolean_t (*get_page_break) (void);
  grub_efi_status_t (*get_device_name) (grub_efi_handle_t *device_handle,
                                        shell_device_name_flags_t flags,
                                        grub_efi_char8_t *language,
                                        grub_efi_char16_t **best_device_name);
  grub_efi_file_info_t* (*get_file_info) (shell_file_handle_t file_handle);
  grub_efi_status_t (*set_file_info) (shell_file_handle_t file_handle,
                     const grub_efi_file_info_t *file_info);
  grub_efi_status_t (*open_file_by_name) (const grub_efi_char16_t *file_name,
                     shell_file_handle_t *file_handle,
                     grub_efi_uint64_t open_mode);
  grub_efi_status_t (*close_file) (shell_file_handle_t file_handle);
  grub_efi_status_t (*create_file) (const grub_efi_char16_t *file_name,
                     grub_efi_uint64_t file_attribs,
                     shell_file_handle_t *file_handle);
  grub_efi_status_t (*read_file) (shell_file_handle_t file_handle,
                     grub_efi_uintn_t *read_size,
                     void *buffer);
  grub_efi_status_t (*write_file) (shell_file_handle_t file_handle,
                     grub_efi_uintn_t *buffer_size,
                     void *buffer);
  grub_efi_status_t (*delete_file) (shell_file_handle_t file_handle);
  grub_efi_status_t (*delete_file_by_name) (const grub_efi_char16_t *file_name);
  grub_efi_status_t (*get_file_position) (shell_file_handle_t file_handle,
                     grub_efi_uint64_t *position);
  grub_efi_status_t (*set_file_position) (shell_file_handle_t file_handle,
                     grub_efi_uint64_t position);
  grub_efi_status_t (*flush_file) (shell_file_handle_t file_handle);
  grub_efi_status_t (*find_files) (const grub_efi_char16_t *file_pattern,
                     shell_file_info_t **file_list);
  grub_efi_status_t (*find_files_in_dir) (shell_file_handle_t file_dir_handle,
                     shell_file_info_t **file_list);
  grub_efi_status_t (*get_file_size) (shell_file_handle_t file_handle,
                     grub_efi_uint64_t *size);
  grub_efi_status_t (*open_root) (grub_efi_device_path_protocol_t *dp,
                     shell_file_handle_t *file_handle);
  grub_efi_status_t (*open_root_by_handle) (grub_efi_handle_t device_handle,
                     shell_file_handle_t *file_handle);
  grub_efi_event_t execution_break;
  grub_efi_uint32_t major_version;
  grub_efi_uint32_t minor_version;
  grub_efi_status_t (*register_guid_name) (const grub_efi_guid_t *guid,
                     const grub_efi_char16_t *guid_name);
  grub_efi_status_t (*get_guid_name) (grub_efi_guid_t *guid,
                     const grub_efi_char16_t **guid_name);
  grub_efi_status_t (*get_guid_from_name) (const grub_efi_char16_t *guid_name,
                     grub_efi_guid_t *guid);
  const grub_efi_char16_t* (*get_env_ex) (const grub_efi_char16_t *name,
                            grub_efi_uint32_t *attributes);
};
typedef struct grub_efi_shell_protocol grub_efi_shell_protocol_t;

struct grub_efi_shell_parameters_protocol
{
  grub_efi_char16_t **argv;
  grub_efi_uintn_t argc;
  shell_file_handle_t stdin;
  shell_file_handle_t stdout;
  shell_file_handle_t stderr;
};
typedef struct grub_efi_shell_parameters_protocol grub_efi_shell_parameters_protocol_t;

struct grub_efi_shell_dynamic_command_protocol
{
  const grub_efi_char16_t *command_name;
  grub_efi_status_t (*handler) (struct grub_efi_shell_dynamic_command_protocol *this,
                     grub_efi_system_table_t *system_table,
                     grub_efi_shell_parameters_protocol_t *shell_parameters,
                     grub_efi_shell_protocol_t *shell);
  grub_efi_char16_t* (*get_help) (struct grub_efi_shell_dynamic_command_protocol *this,
                      const grub_efi_char8_t *language);
};
typedef struct grub_efi_shell_dynamic_command_protocol grub_efi_shell_dynamic_command_protocol_t;

grub_err_t grub_efi_shell_chain (int argc, char *argv[], grub_efi_device_path_t *dp);

#endif
