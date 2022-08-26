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
 */

#ifndef GRUB_EFI_DEVICE_PATH_HEADER
#define GRUB_EFI_DEVICE_PATH_HEADER	1

#include <grub/efi/api.h>
#include <grub/symbol.h>

struct grub_efi_device_path_to_text_protocol
{
  grub_efi_char16_t* (*device_node_to_text) (const grub_efi_device_path_protocol_t *device_node,
                      grub_efi_boolean_t display_only,
                      grub_efi_boolean_t allow_shortcuts);
  grub_efi_char16_t* (*device_path_to_text) (const grub_efi_device_path_protocol_t *device_path,
                      grub_efi_boolean_t display_only,
                      grub_efi_boolean_t allow_shortcuts);
};
typedef struct grub_efi_device_path_to_text_protocol grub_efi_device_path_to_text_protocol_t;

struct grub_efi_device_path_from_text_protocol
{
  grub_efi_device_path_protocol_t* (*text_to_device_node) (grub_efi_char16_t *text_device_node);
  grub_efi_device_path_protocol_t* (*text_to_device_path) (grub_efi_char16_t *text_device_path);
};
typedef struct grub_efi_device_path_from_text_protocol grub_efi_device_path_from_text_protocol_t;

struct grub_efi_device_path_utilities_protocol
{
  grub_efi_uintn_t (*get_device_path_size) (const grub_efi_device_path_protocol_t *device_path);
  grub_efi_device_path_protocol_t* (*duplicate_device_path) (const grub_efi_device_path_protocol_t *device_path);
  grub_efi_device_path_protocol_t* (*append_device_path) (const grub_efi_device_path_protocol_t *dp1,
                                    const grub_efi_device_path_protocol_t *dp2);
  grub_efi_device_path_protocol_t* (*append_device_node) (const grub_efi_device_path_protocol_t *device_path,
                                    const grub_efi_device_path_protocol_t *device_node);
  grub_efi_device_path_protocol_t* (*append_device_path_instance) (const grub_efi_device_path_protocol_t *device_path,
                                    const grub_efi_device_path_protocol_t *device_path_instance);
  grub_efi_device_path_protocol_t* (*get_next_device_path_instance) (grub_efi_device_path_protocol_t *device_path_instance,
                                    grub_efi_uintn_t *device_path_instance_size);
  grub_efi_device_path_protocol_t* (*create_device_node) (grub_efi_uint8_t node_type,
                                    grub_efi_uintn_t node_subtype,
                                    grub_efi_uint16_t node_length);
  grub_efi_boolean_t (*is_device_path_multi_instance) (const grub_efi_device_path_protocol_t *device_path);
};
typedef struct grub_efi_device_path_utilities_protocol grub_efi_device_path_utilities_protocol_t;

#endif /* ! GRUB_EFI_DEVICE_PATH_HEADER */