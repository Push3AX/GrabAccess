/* CrScreenshotDxe.c

Copyright (c) 2016, Nikolaj Schlej, All rights reserved.

Redistribution and use in source and binary forms, 
with or without modification, are permitted provided that the following conditions are met:
- Redistributions of source code must retain the above copyright notice, 
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice, 
  this list of conditions and the following disclaimer in the documentation 
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "uefi_wrapper.h"
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/sfs.h>
#include <grub/efi/graphics_output.h>

#include <stdio.h>
#include <stdlib.h>

#include "lodepng.h" //PNG encoding library
#include "AppleEventMin.h" // Mac-specific keyboard input

GRUB_MOD_LICENSE ("GPLv3+");
GRUB_MOD_DUAL_LICENSE("BSD 2-Clause");

static EFI_GUID mAppleEventProtocolGuid = APPLE_EVENT_PROTOCOL_GUID;
static EFI_GUID gEfiSimpleFileSystemProtocolGuid =
                    GRUB_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID gEfiGraphicsOutputProtocolGuid = GRUB_EFI_GOP_GUID;
static EFI_GUID gEfiSimpleTextInputExProtocolGuid =
                    GRUB_EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;

static grub_efi_boot_services_t *b;
static grub_efi_runtime_services_t *r;

static CHAR16 utf16_fat_file_name[13];

static void
utf8_to_utf16 (const char *str)
{
  int i;
  for (i = 0; i < 12; i++)
  {
    utf16_fat_file_name[i] = str[i];
    if (str[i] == '\0')
      break;
  }
  utf16_fat_file_name[12] = 0;
}

static EFI_STATUS
FindWritableFs (EFI_FILE_PROTOCOL **WritableFs)
{
    EFI_HANDLE *HandleBuffer = NULL;
    UINTN      HandleCount;
    UINTN      i;

    // Locate all the simple file system devices in the system
    EFI_STATUS Status = efi_call_5 (b->locate_handle_buffer,
                                    GRUB_EFI_BY_PROTOCOL,
                                    &gEfiSimpleFileSystemProtocolGuid, NULL,
                                    &HandleCount, &HandleBuffer);
    if (!EFI_ERROR (Status)) {
        EFI_FILE_PROTOCOL *Fs = NULL;
        // For each located volume
        for (i = 0; i < HandleCount; i++) {
            EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SimpleFs = NULL;
            EFI_FILE_PROTOCOL *File = NULL;

            // Get protocol pointer for current volume
            Status = efi_call_3 (b->handle_protocol,
                                 HandleBuffer[i],
                                 &gEfiSimpleFileSystemProtocolGuid,
                                 (VOID **) &SimpleFs);
            if (EFI_ERROR (Status)) {
                grub_dprintf ("crscreenshot",
                    "FindWritableFs: gBS->HandleProtocol returned err\n");
                continue;
            }

            // Open the volume
            Status = efi_call_2 (SimpleFs->open_volume, SimpleFs, &Fs);
            if (EFI_ERROR (Status)) {
                grub_dprintf ("crscreenshot",
                          "FindWritableFs: SimpleFs->OpenVolume returned err\n");
                continue;
            }

            // Try opening a file for writing
            utf8_to_utf16 ("crsdtest.fil");
            Status = efi_call_5 (Fs->file_open,
                                 Fs, &File, utf16_fat_file_name,
                                 GRUB_EFI_FILE_MODE_CREATE |
                                 GRUB_EFI_FILE_MODE_READ |
                                 GRUB_EFI_FILE_MODE_WRITE, 0);
            if (EFI_ERROR (Status)) {
                grub_dprintf ("crscreenshot",
                              "FindWritableFs: Fs->Open returned err\n");
                continue;
            }

            // Writable FS found
            efi_call_1 (Fs->file_delete, File);
            *WritableFs = Fs;
            Status = GRUB_EFI_SUCCESS;
            break;
        }
    }

    // Free memory
    if (HandleBuffer) {
        efi_call_1 (b->free_pool, HandleBuffer);
    }

    return Status;
}

static EFI_STATUS
ShowStatus (UINT8 Red, UINT8 Green, UINT8 Blue)
{
    // Determines the size of status square
    #define STATUS_SQUARE_SIDE 5

    UINTN        HandleCount;
    EFI_HANDLE   *HandleBuffer = NULL;
    EFI_GRAPHICS_OUTPUT_PROTOCOL  *GraphicsOutput = NULL;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Square[STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE];
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Backup[STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE];
    UINTN i;

    // Locate all instances of GOP
    EFI_STATUS Status = efi_call_5 (b->locate_handle_buffer,
                                    GRUB_EFI_BY_PROTOCOL,
                                    &gEfiGraphicsOutputProtocolGuid, NULL,
                                    &HandleCount, &HandleBuffer);
    if (EFI_ERROR (Status)) {
        grub_dprintf ("crscreenshot",
                      "ShowStatus: Graphics output protocol not found\n");
        return GRUB_EFI_UNSUPPORTED;
    }

    // Set square color
    for (i = 0 ; i < STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE; i++) {
        Square[i].blue = Blue;
        Square[i].green = Green;
        Square[i].red = Red;
        Square[i].reserved = 0x00;
    }

    // For each GOP instance
    for (i = 0; i < HandleCount; i ++) {
        // Handle protocol
        Status = efi_call_3 (b->handle_protocol, HandleBuffer[i],
                             &gEfiGraphicsOutputProtocolGuid,
                             (VOID **) &GraphicsOutput);
        if (EFI_ERROR (Status)) {
            grub_dprintf ("crscreenshot",
                          "ShowStatus: gBS->HandleProtocol returned err\n");
            continue;
        }

        // Backup current image
        efi_call_10 (GraphicsOutput->blt, GraphicsOutput, Backup,
                     GRUB_EFI_BLT_VIDEO_TO_BLT_BUFFER, 0, 0, 0, 0,
                     STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);

        // Draw the status square
        efi_call_10 (GraphicsOutput->blt, GraphicsOutput, Square,
                     GRUB_EFI_BLT_BUFFER_TO_VIDEO, 0, 0, 0, 0,
                     STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);

        // Wait 500ms
        efi_call_1 (b->stall, 500*1000);

        // Restore the backup
        efi_call_10 (GraphicsOutput->blt, GraphicsOutput, Backup,
                     GRUB_EFI_BLT_BUFFER_TO_VIDEO, 0, 0, 0, 0,
                     STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
    }

    return GRUB_EFI_SUCCESS;
}


static EFI_STATUS EFIAPI
TakeScreenshot (EFI_KEY_DATA *KeyData)
{
    EFI_FILE_PROTOCOL *Fs = NULL;
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_GRAPHICS_OUTPUT_PROTOCOL  *GraphicsOutput = NULL;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Image = NULL;
    UINTN      ImageSize;         // Size in pixels
    UINT8      *PngFile = NULL;
    UINTN      PngFileSize;       // Size in bytes
    EFI_STATUS Status;
    UINTN      HandleCount;
    EFI_HANDLE *HandleBuffer = NULL;
    UINT32     ScreenWidth;
    UINT32     ScreenHeight;
    EFI_TIME   Time;
    UINTN      i, j;
    (VOID)KeyData;

    // Find writable FS
    Status = FindWritableFs(&Fs);
    if (EFI_ERROR (Status)) {
        grub_dprintf ("crscreenshot", "TakeScreenshot: Can't find writable FS\n");
        ShowStatus(0xFF, 0xFF, 0x00); //Yellow
        return GRUB_EFI_SUCCESS;
    }

    // Locate all instances of GOP
    Status = efi_call_5 (b->locate_handle_buffer, GRUB_EFI_BY_PROTOCOL,
                         &gEfiGraphicsOutputProtocolGuid,
                         NULL, &HandleCount, &HandleBuffer);
    if (EFI_ERROR (Status)) {
        grub_dprintf ("crscreenshot",
                      "ShowStatus: Graphics output protocol not found\n");
        return GRUB_EFI_SUCCESS;
    }

    // For each GOP instance
    for (i = 0; i < HandleCount; i++) {
        do { // Break from do used instead of "goto error"
            // Handle protocol
            Status = efi_call_3 (b->handle_protocol, HandleBuffer[i],
                                 &gEfiGraphicsOutputProtocolGuid,
                                 (VOID **) &GraphicsOutput);
            if (EFI_ERROR (Status)) {
                grub_dprintf ("crscreenshot",
                              "ShowStatus: gBS->HandleProtocol returned err\n");
                break;
            }

            // Set screen width, height and image size in pixels
            ScreenWidth  = GraphicsOutput->mode->info->width;
            ScreenHeight = GraphicsOutput->mode->info->height;
            ImageSize = ScreenWidth * ScreenHeight;

            // Get current time
            Status = efi_call_2 (r->get_time, &Time, NULL);
            if (!EFI_ERROR(Status)) {
                // Set file name to current day and time
              char name[13];
              grub_snprintf (name, 13, "%02d%02d%02d%02d.png",
                             Time.day, Time.hour, Time.minute, Time.second);
              utf8_to_utf16 (name);
            }
            else {
                // Set file name to scrnshot.png
              utf8_to_utf16 ("scrnshot.png");
            }

            // Allocate memory for screenshot
            Status = efi_call_3 (b->allocate_pool,
                                 GRUB_EFI_BOOT_SERVICES_DATA,
                                 ImageSize * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL),
                                 (VOID **)&Image);
            if (EFI_ERROR(Status)) {
                grub_dprintf ("crscreenshot",
                              "TakeScreenshot: gBS->AllocatePool returned err\n");
                break;
            }

            // Take screenshot
            Status = efi_call_10 (GraphicsOutput->blt, GraphicsOutput, Image,
                                  GRUB_EFI_BLT_VIDEO_TO_BLT_BUFFER, 0, 0, 0, 0,
                                  ScreenWidth, ScreenHeight, 0);
            if (EFI_ERROR(Status)) {
                grub_dprintf ("crscreenshot",
                          "TakeScreenshot: GraphicsOutput->Blt returned err\n");
                break;
            }

            // Check for pitch black image (it means we are using a wrong GOP)
            for (j = 0; j < ImageSize; j++) {
                if (Image[j].red != 0x00 ||
                    Image[j].green != 0x00 ||
                    Image[j].blue != 0x00)
                    break;
            }
            if (j == ImageSize) {
                grub_dprintf ("crscreenshot", "TakeScreenshot: GraphicsOutput->Blt returned pitch black image, skipped\n");
                ShowStatus(0x00, 0x00, 0xFF); //Blue
                break;
            }

            // Open or create output file
            Status = efi_call_5 (Fs->file_open, Fs, &File, utf16_fat_file_name,
                                 GRUB_EFI_FILE_MODE_CREATE |
                                 GRUB_EFI_FILE_MODE_READ |
                                 GRUB_EFI_FILE_MODE_WRITE, 0);
            if (EFI_ERROR (Status)) {
                grub_dprintf ("crscreenshot",
                      "TakeScreenshot: Fs->Open returned err\n");
                break;
            }

            // Convert BGR to RGBA with Alpha set to 0xFF
            for (j = 0; j < ImageSize; j++) {
                UINT8 Temp = Image[j].blue;
                Image[j].blue = Image[j].red;
                Image[j].red = Temp;
                Image[j].reserved = 0xFF;
            }

            // Encode raw RGB image to PNG format
            j = lodepng_encode32(&PngFile, &PngFileSize, (const UINT8*)Image, ScreenWidth, ScreenHeight);
            if (j) {
                grub_dprintf ("crscreenshot",
                              "TakeScreenshot: lodepng_encode32 returned err\n");
                break;
            }

            // Write PNG image into the file and close it
            Status = efi_call_3(File->file_write, File, &PngFileSize, PngFile);
            efi_call_1 (File->file_close, File);
            if (EFI_ERROR(Status)) {
                grub_dprintf ("crscreenshot",
                              "TakeScreenshot: File->Write returned err\n");
                break;
            }

            // Show success
            ShowStatus(0x00, 0xFF, 0x00); //Green
        } while(0);

        // Free memory
        if (Image)
            efi_call_1 (b->free_pool, Image);
        if (PngFile)
            free(PngFile);
        Image = NULL;
        PngFile = NULL;
    }

    // Show error
    if (EFI_ERROR(Status))
        ShowStatus(0xFF, 0x00, 0x00); //Red

    return GRUB_EFI_SUCCESS;
}

static VOID EFIAPI
AppleEventKeyHandler (APPLE_EVENT_INFORMATION *Information, VOID *NotifyContext)
{
    // Mark the context argument as used
    (VOID) NotifyContext;

    // Ignore invalid information if it happened to arrive
    if (Information == NULL || (Information->EventType & APPLE_EVENT_TYPE_KEY_UP) == 0) {
        return;
    }

    // Apple calls ALT key by the name of OPTION key
    if (Information->KeyData->InputKey.scan_code == SCAN_F12 &&
        Information->Modifiers == (APPLE_MODIFIER_LEFT_CONTROL|
                                   APPLE_MODIFIER_LEFT_OPTION)) {
        // Take a screenshot
        TakeScreenshot (NULL);
    }
}

static EFI_STATUS
CrScreenshotDxeEntry (VOID)
{
    EFI_STATUS                        Status;
    UINTN                             HandleCount = 0;
    EFI_HANDLE                        *HandleBuffer = NULL;
    UINTN                             Index;
    EFI_KEY_DATA                      SimpleTextInExKeyStroke;
    EFI_HANDLE                        SimpleTextInExHandle;
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *SimpleTextInEx;
    APPLE_EVENT_HANDLE                AppleEventHandle;
    APPLE_EVENT_PROTOCOL              *AppleEvent;
    BOOLEAN                           Installed = FALSE;

    // Set keystroke to be LCtrl+LAlt+F12
    SimpleTextInExKeyStroke.key.scan_code = SCAN_F12;
    SimpleTextInExKeyStroke.key.unicode_char = 0;
    SimpleTextInExKeyStroke.key_state.key_shift_state =
        EFI_SHIFT_STATE_VALID | EFI_LEFT_CONTROL_PRESSED | EFI_LEFT_ALT_PRESSED;
    SimpleTextInExKeyStroke.key_state.key_toggle_state = 0;

    // Locate compatible protocols, firstly try SimpleTextInEx, otherwise use AppleEvent
    Status = efi_call_5 (b->locate_handle_buffer, GRUB_EFI_BY_PROTOCOL,
                         &gEfiSimpleTextInputExProtocolGuid,
                         NULL, &HandleCount, &HandleBuffer);
    if (!EFI_ERROR (Status)) {
        // For each instance
        for (Index = 0; Index < HandleCount; Index++) {
            Status = efi_call_3 (b->handle_protocol, HandleBuffer[Index],
                                 &gEfiSimpleTextInputExProtocolGuid,
                                 (VOID **) &SimpleTextInEx);

            // Get protocol handle
            if (EFI_ERROR (Status)) {
               grub_dprintf ("crscreenshot", "CrScreenshotDxeEntry: gBS->HandleProtocol SimpleTextInputEx returned err\n");
               continue;
            }

            // Register key notification function
            Status = efi_call_4 (SimpleTextInEx->register_key_notify,
                                 SimpleTextInEx,
                                 &SimpleTextInExKeyStroke,
                                 TakeScreenshot,
                                 &SimpleTextInExHandle);
            if (!EFI_ERROR (Status)) {
                Installed = TRUE;
            } else {
                grub_dprintf ("crscreenshot",
        "CrScreenshotDxeEntry: SimpleTextInEx->RegisterKeyNotify returned err\n");
            }
        }
    } else {
        grub_dprintf ("crscreenshot", "CrScreenshotDxeEntry: gBS->LocateHandleBuffer SimpleTextInputEx returned err\n");
        HandleBuffer = NULL;
        Status = efi_call_5 (b->locate_handle_buffer, GRUB_EFI_BY_PROTOCOL,
                             &mAppleEventProtocolGuid,
                             NULL, &HandleCount, &HandleBuffer);
        if (EFI_ERROR (Status)) {
            grub_dprintf ("crscreenshot", "CrScreenshotDxeEntry: gBS->LocateHandleBuffer AppleEvent returned err\n");
            return GRUB_EFI_UNSUPPORTED;
        }

        // Traverse AppleEvent handles similarly to SimpleTextInputEx
        for (Index = 0; Index < HandleCount; Index++) {
            Status = efi_call_3 (b->handle_protocol, HandleBuffer[Index],
                                 &mAppleEventProtocolGuid, (VOID **) &AppleEvent);

            // Get protocol handle
            if (EFI_ERROR (Status)) {
               continue;
            }

            // Check protocol interface compatibility
            if (AppleEvent->Revision < APPLE_EVENT_PROTOCOL_REVISION) {
                continue;
            }

            // Register key handler, which will later determine LCtrl+LAlt+F12 combination
            Status = efi_call_4 (AppleEvent->RegisterHandler,
                                 APPLE_EVENT_TYPE_KEY_UP,
                                 AppleEventKeyHandler,
                                 &AppleEventHandle, NULL);
            if (!EFI_ERROR (Status)) {
                Installed = TRUE;
            } else {
                grub_dprintf ("crscreenshot",
                              "CrScreenshotDxeEntry: AppleEvent->RegisterHandler returned err\n");
            }
        }
    }

    // Free memory used for handle buffer
    if (HandleBuffer) {
        efi_call_1(b->free_pool, HandleBuffer);
    }

    // Show success only when we found at least one working implementation
    if (Installed) {
        ShowStatus(0xFF, 0xFF, 0xFF); //White
    }

    return GRUB_EFI_SUCCESS;
}

GRUB_MOD_INIT(crscreenshot)
{
  b = grub_efi_system_table->boot_services;
  r = grub_efi_system_table->runtime_services;
  CrScreenshotDxeEntry ();
}

GRUB_MOD_FINI(crscreenshot)
{
}
