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

#ifndef CRSCREENSHOT_EFI_WRAPPER_HEADER
#define CRSCREENSHOT_EFI_WRAPPER_HEADER 1

#include <grub/types.h>
#include <grub/symbol.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/sfs.h>
#include <grub/efi/graphics_output.h>

typedef grub_efi_boolean_t      BOOLEAN;
typedef grub_efi_uintn_t        UINTN;
typedef grub_efi_intn_t         INTN;
typedef grub_efi_uint8_t        UINT8;
typedef grub_efi_int8_t         INT8;
typedef grub_efi_uint16_t       UINT16;
typedef grub_efi_int16_t        INT16;
typedef grub_efi_uint32_t       UINT32;
typedef grub_efi_int32_t        INT32;
typedef grub_efi_uint64_t       UINT64;
typedef grub_efi_int64_t        INT64;
typedef grub_efi_char8_t        CHAR8;
typedef grub_efi_char16_t       CHAR16;
typedef grub_efi_status_t       EFI_STATUS;
typedef void                    VOID;
typedef grub_efi_handle_t       EFI_HANDLE;
typedef grub_efi_event_t        EFI_EVENT;
typedef grub_efi_guid_t         EFI_GUID;
typedef grub_efi_input_key_t    EFI_INPUT_KEY;
typedef grub_efi_time_t         EFI_TIME;
typedef grub_efi_key_data_t     EFI_KEY_DATA;

#define IN
#define OUT

#define  BIT0     0x00000001
#define  BIT1     0x00000002
#define  BIT2     0x00000004
#define  BIT3     0x00000008
#define  BIT4     0x00000010
#define  BIT5     0x00000020
#define  BIT6     0x00000040
#define  BIT7     0x00000080
#define  BIT8     0x00000100
#define  BIT9     0x00000200

//
// Any Shift or Toggle State that is valid should have
// high order bit set.
//
// Shift state
//
#define EFI_SHIFT_STATE_VALID     0x80000000
#define EFI_RIGHT_SHIFT_PRESSED   0x00000001
#define EFI_LEFT_SHIFT_PRESSED    0x00000002
#define EFI_RIGHT_CONTROL_PRESSED 0x00000004
#define EFI_LEFT_CONTROL_PRESSED  0x00000008
#define EFI_RIGHT_ALT_PRESSED     0x00000010
#define EFI_LEFT_ALT_PRESSED      0x00000020
#define EFI_RIGHT_LOGO_PRESSED    0x00000040
#define EFI_LEFT_LOGO_PRESSED     0x00000080
#define EFI_MENU_KEY_PRESSED      0x00000100
#define EFI_SYS_REQ_PRESSED       0x00000200

//
// Toggle state
//
#define EFI_TOGGLE_STATE_VALID    0x80
#define EFI_KEY_STATE_EXPOSED     0x40
#define EFI_SCROLL_LOCK_ACTIVE    0x01
#define EFI_NUM_LOCK_ACTIVE       0x02
#define EFI_CAPS_LOCK_ACTIVE      0x04

//
// EFI Scan codes
//
#define SCAN_F11                  0x0015
#define SCAN_F12                  0x0016
#define SCAN_PAUSE                0x0048
#define SCAN_F13                  0x0068
#define SCAN_F14                  0x0069
#define SCAN_F15                  0x006A
#define SCAN_F16                  0x006B
#define SCAN_F17                  0x006C
#define SCAN_F18                  0x006D
#define SCAN_F19                  0x006E
#define SCAN_F20                  0x006F
#define SCAN_F21                  0x0070
#define SCAN_F22                  0x0071
#define SCAN_F23                  0x0072
#define SCAN_F24                  0x0073
#define SCAN_MUTE                 0x007F
#define SCAN_VOLUME_UP            0x0080
#define SCAN_VOLUME_DOWN          0x0081
#define SCAN_BRIGHTNESS_UP        0x0100
#define SCAN_BRIGHTNESS_DOWN      0x0101
#define SCAN_SUSPEND              0x0102
#define SCAN_HIBERNATE            0x0103
#define SCAN_TOGGLE_DISPLAY       0x0104
#define SCAN_RECOVERY             0x0105
#define SCAN_EJECT                0x0106

#define EFI_ERROR(x) ((x) != GRUB_EFI_SUCCESS)

typedef grub_efi_simple_text_input_ex_interface_t EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL;

/* SimpleFileSystem */
typedef grub_efi_file_protocol_t EFI_FILE_PROTOCOL;
typedef grub_efi_simple_fs_protocol_t EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/* GOP */
typedef struct grub_efi_gop EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef struct grub_efi_gop_blt_pixel EFI_GRAPHICS_OUTPUT_BLT_PIXEL;

#endif
