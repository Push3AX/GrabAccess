/* efi.h - declare variables and functions for EFI support */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008,2009  Free Software Foundation, Inc.
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

#ifndef GRUB_EFI_EFI_HEADER
#define GRUB_EFI_EFI_HEADER	1

#include <grub/types.h>
#include <grub/dl.h>
#include <grub/disk.h>
#include <grub/efi/api.h>

#define TRUE  ((grub_efi_boolean_t)(1==1))

#define FALSE ((grub_efi_boolean_t)(0==1))

/* DevicePath */
#define HW_VENDOR_DP              0x04
#define MSG_ATAPI_DP              0x01
#define MEDIA_HARDDRIVE_DP        0x01
#define MEDIA_CDROM_DP            0x02
#define MEDIA_FILEPATH_DP         0x04

#define MEDIA_DEVICE_PATH         0x04
#define MESSAGING_DEVICE_PATH     0x03
#define HARDWARE_DEVICE_PATH      0x01

#define EFI_REMOVABLE_MEDIA_FILE_NAME_IA32    "/EFI/BOOT/BOOTIA32.EFI"
#define EFI_REMOVABLE_MEDIA_FILE_NAME_X64     "/EFI/BOOT/BOOTX64.EFI"
#define EFI_REMOVABLE_MEDIA_FILE_NAME_ARM     "/EFI/BOOT/BOOTARM.EFI"
#define EFI_REMOVABLE_MEDIA_FILE_NAME_AARCH64 "/EFI/BOOT/BOOTAA64.EFI"

#if defined (__i386__)
  #define EFI_REMOVABLE_MEDIA_FILE_NAME   EFI_REMOVABLE_MEDIA_FILE_NAME_IA32
#elif defined (__x86_64__)
  #define EFI_REMOVABLE_MEDIA_FILE_NAME   EFI_REMOVABLE_MEDIA_FILE_NAME_X64
#elif defined (__arm__)
  #define EFI_REMOVABLE_MEDIA_FILE_NAME   EFI_REMOVABLE_MEDIA_FILE_NAME_ARM
#elif defined (__aarch64__)
  #define EFI_REMOVABLE_MEDIA_FILE_NAME   EFI_REMOVABLE_MEDIA_FILE_NAME_AARCH64
#else
  #error Unknown Processor Type
#endif

#if defined (__x86_64__)
#if (defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)))||(defined(__clang__) && (__clang_major__ > 3 || (__clang_major__ == 3 && __clang_minor__ >= 2)))
  #define EFIAPI __attribute__((ms_abi))
#else
  #error Compiler is too old for GNU_EFI_USE_MS_ABI
#endif
#endif

#ifndef EFIAPI
  #define EFIAPI  // Substitute expresion to force C calling convention 
#endif

/* Functions.  */
void *EXPORT_FUNC(grub_efi_locate_protocol) (grub_efi_guid_t *protocol,
					     void *registration);
grub_efi_handle_t *
EXPORT_FUNC(grub_efi_locate_handle) (grub_efi_locate_search_type_t search_type,
				     grub_efi_guid_t *protocol,
				     void *search_key,
				     grub_efi_uintn_t *num_handles);
void *EXPORT_FUNC(grub_efi_open_protocol) (grub_efi_handle_t handle,
					   grub_efi_guid_t *protocol,
					   grub_efi_uint32_t attributes);
grub_efi_status_t
EXPORT_FUNC(grub_efi_close_protocol) (grub_efi_handle_t handle, grub_efi_guid_t *protocol);
int EXPORT_FUNC(grub_efi_set_text_mode) (int on);
void EXPORT_FUNC(grub_efi_stall) (grub_efi_uintn_t microseconds);
void *
EXPORT_FUNC(grub_efi_allocate_pages_real) (grub_efi_physical_address_t address,
				           grub_efi_uintn_t pages,
					   grub_efi_allocate_type_t alloctype,
					   grub_efi_memory_type_t memtype);
void *
EXPORT_FUNC(grub_efi_allocate_fixed) (grub_efi_physical_address_t address,
				      grub_efi_uintn_t pages);
void *
EXPORT_FUNC(grub_efi_allocate_any_pages) (grub_efi_uintn_t pages);
void *
EXPORT_FUNC(grub_efi_allocate_pages_max) (grub_efi_physical_address_t max,
					  grub_efi_uintn_t pages);
void EXPORT_FUNC(grub_efi_free_pages) (grub_efi_physical_address_t address,
				       grub_efi_uintn_t pages);
grub_efi_uintn_t EXPORT_FUNC(grub_efi_find_mmap_size) (void);
int
EXPORT_FUNC(grub_efi_get_memory_map) (grub_efi_uintn_t *memory_map_size,
				      grub_efi_memory_descriptor_t *memory_map,
				      grub_efi_uintn_t *map_key,
				      grub_efi_uintn_t *descriptor_size,
				      grub_efi_uint32_t *descriptor_version);
void grub_efi_memory_fini (void);
grub_efi_loaded_image_t *EXPORT_FUNC(grub_efi_get_loaded_image) (grub_efi_handle_t image_handle);
void EXPORT_FUNC(grub_efi_print_device_path) (grub_efi_device_path_t *dp);
char *EXPORT_FUNC(grub_efi_get_filename) (grub_efi_device_path_t *dp);
grub_efi_device_path_t *
EXPORT_FUNC(grub_efi_get_device_path) (grub_efi_handle_t handle);
grub_efi_device_path_t *
EXPORT_FUNC(grub_efi_find_last_device_path) (const grub_efi_device_path_t *dp);
grub_efi_device_path_t *
EXPORT_FUNC(grub_efi_duplicate_device_path) (const grub_efi_device_path_t *dp);
grub_err_t EXPORT_FUNC (grub_efi_finish_boot_services) (grub_efi_uintn_t *outbuf_size, void *outbuf,
							grub_efi_uintn_t *map_key,
							grub_efi_uintn_t *efi_desc_size,
							grub_efi_uint32_t *efi_desc_version);
grub_err_t EXPORT_FUNC (grub_efi_set_virtual_address_map) (grub_efi_uintn_t memory_map_size,
							   grub_efi_uintn_t descriptor_size,
							   grub_efi_uint32_t descriptor_version,
							   grub_efi_memory_descriptor_t *virtual_map);
grub_efi_status_t
EXPORT_FUNC(grub_efi_allocate_pool) (grub_efi_memory_type_t pool_type,
                                     grub_efi_uintn_t buffer_size, void **buffer);
grub_efi_status_t EXPORT_FUNC(grub_efi_free_pool) (void *buffer);
grub_efi_status_t EXPORT_FUNC (grub_efi_get_variable_with_attributes) (const char *variable,
								       const grub_efi_guid_t *guid,
								       grub_size_t *datasize_out,
								       void **data_out,
								       grub_efi_uint32_t *attributes);
grub_efi_status_t EXPORT_FUNC (grub_efi_get_variable) (const char *variable,
						       const grub_efi_guid_t *guid,
						       grub_size_t *datasize_out,
						       void **data_out);
grub_efi_status_t
EXPORT_FUNC (grub_efi_set_var_attr) (const char *var,
                    const grub_efi_guid_t *guid,
                    void *data, grub_size_t datasize, grub_efi_uint32_t attr);
grub_err_t
EXPORT_FUNC (grub_efi_set_variable) (const char *var,
				     const grub_efi_guid_t *guid,
				     void *data,
				     grub_size_t datasize);
int
EXPORT_FUNC (grub_efi_compare_device_paths) (const grub_efi_device_path_t *dp1,
					     const grub_efi_device_path_t *dp2);

extern void (*EXPORT_VAR(grub_efi_net_config)) (grub_efi_handle_t hnd, 
						char **device,
						char **path);

grub_efi_guid_t *
EXPORT_FUNC (grub_efi_copy_guid) (grub_efi_guid_t *dest,
                                  const grub_efi_guid_t *src);
grub_efi_boolean_t
EXPORT_FUNC (grub_efi_compare_guid) (const grub_efi_guid_t *g1,
                                     const grub_efi_guid_t *g2);

grub_efi_device_path_t*
EXPORT_FUNC (grub_efi_file_device_path)
             (grub_efi_device_path_t *dp, const char *filename);

grub_err_t
copy_file_path (grub_efi_file_path_device_path_t *fp, 
                const char *str, grub_efi_uint16_t len);

#if defined(__arm__) || defined(__aarch64__) || defined(__riscv)
void *EXPORT_FUNC(grub_efi_get_firmware_fdt)(void);
grub_err_t EXPORT_FUNC(grub_efi_get_ram_base)(grub_addr_t *);
#include <grub/cpu/linux.h>
grub_err_t grub_arch_efi_linux_check_image(struct linux_arch_kernel_header *lh);
grub_err_t grub_arch_efi_linux_boot_image(grub_addr_t addr, grub_size_t size,
                                           char *args);
#endif

grub_addr_t grub_efi_modules_addr (void);

void grub_efi_mm_init (void);
void grub_efi_mm_fini (void);
void grub_efi_init (void);
void grub_efi_fini (void);
void grub_efi_set_prefix (void);

/* Variables.  */
extern grub_efi_system_table_t *EXPORT_VAR(grub_efi_system_table);
extern grub_efi_handle_t EXPORT_VAR(grub_efi_image_handle);

extern grub_efi_uintn_t EXPORT_VAR(grub_efi_protocol_data_len);
extern void *EXPORT_VAR(grub_efi_protocol_data_addr);

extern int EXPORT_VAR(grub_efi_is_finished);

struct grub_net_card;

grub_efi_handle_t
grub_efinet_get_device_handle (struct grub_net_card *card);

/* dp */
char *EXPORT_FUNC (grub_efi_device_path_to_str) (grub_efi_device_path_t *dp);
grub_efi_uintn_t
EXPORT_FUNC (grub_efi_get_dp_size) (const grub_efi_device_path_protocol_t *dp);

grub_efi_device_path_protocol_t *EXPORT_FUNC (grub_efi_create_device_node)
    (grub_efi_uint8_t node_type, grub_efi_uintn_t node_subtype,
     grub_efi_uint16_t node_length);

grub_efi_device_path_protocol_t *EXPORT_FUNC (grub_efi_append_device_path)
    (const grub_efi_device_path_protocol_t *dp1,
     const grub_efi_device_path_protocol_t *dp2);

grub_efi_device_path_protocol_t *EXPORT_FUNC (grub_efi_append_device_node)
    (const grub_efi_device_path_protocol_t *device_path,
     const grub_efi_device_path_protocol_t *device_node);

int EXPORT_FUNC (grub_efi_is_child_dp)
    (const grub_efi_device_path_t *child, const grub_efi_device_path_t *parent);

grub_efi_handle_t
grub_efi_bootpart (grub_efi_device_path_t *dp, const char *filename);

grub_efi_handle_t
grub_efi_bootdisk (grub_efi_device_path_t *dp, const char *filename);

void
grub_efi_unlock_rom_area (void);

void
grub_efi_lock_rom_area (void);

void
grub_efi_fake_bios_data (int use_rom);

#endif /* ! GRUB_EFI_EFI_HEADER */
