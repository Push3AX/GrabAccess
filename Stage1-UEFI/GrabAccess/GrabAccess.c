#include "GrabAccess.h"

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiApplicationEntryPoint.h>

#include <Protocol/AcpiTable.h>
#include <IndustryStandard/Acpi50.h>

#include <Guid/EventGroup.h>

#include <Guid/FileInfo.h>
#include <Library/DevicePathLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>


UINTN     mPlatformBinaryResourceTableKey = 0;
EFI_GUID  mFileGuid;
UINT16    mArgLength;
BOOLEAN   mAcpiWpbtInstalled = FALSE;
EFI_ACPI_5_0_PLATFORM_BINARY_TABLE *mPlatformBinaryTable;

EFI_STATUS LoadWindowsBootloader() {
  EFI_STATUS Status;
  UINTN HandleCount;
  EFI_HANDLE *HandleBuffer;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL *Root;
  EFI_FILE_PROTOCOL *Bootloader;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;
  EFI_DEVICE_PATH_PROTOCOL *BootloaderPath;
  EFI_HANDLE ImageHandle;

  //
  // Get all file system handle
  //
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &HandleCount, &HandleBuffer);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  for (UINTN Index = 0; Index < HandleCount; Index++) {
    //
    // Open file system
    //
    Status = gBS->HandleProtocol(HandleBuffer[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FileSystem);
    if (EFI_ERROR(Status)) {
      continue;
    }
    
    Status = FileSystem->OpenVolume(FileSystem, &Root);
    if (EFI_ERROR(Status)) {
      continue;
    }
    
    //
    // Open and run Windows bootloader
    //
    Status = Root->Open(Root, &Bootloader, WINDOWS_BOOTLOADER_PATH, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(Status)) {
      Status = gBS->HandleProtocol(HandleBuffer[Index], &gEfiDevicePathProtocolGuid, (VOID **)&DevicePath);
      if (!EFI_ERROR(Status)) {
        // Physical path of bootloader
        BootloaderPath = FileDevicePath(HandleBuffer[Index], WINDOWS_BOOTLOADER_PATH);
        if (BootloaderPath != NULL) {
          Print(L"BootloaderPath: %s\n", ConvertDevicePathToText(BootloaderPath,TRUE,FALSE));
          Status = gBS->LoadImage(FALSE, gImageHandle, BootloaderPath, NULL, 0, &ImageHandle);
          gBS->FreePool(BootloaderPath);
          if (!EFI_ERROR(Status)) {
            gBS->Stall(1000000); //delay 1 sec
            Status = gBS->StartImage(ImageHandle, NULL, NULL);
          }else{
            ErrorPrint(L"StartImage failed: %r\n", Status);
            return Status;
          }
        }
      }
      Bootloader->Close(Bootloader);
      break;
    }
    Root->Close(Root);
  }

  if (HandleBuffer != NULL) {
    gBS->FreePool(HandleBuffer);
  }

  return Status;
}

EFI_STATUS InstallWpbt ( CONST CHAR16* FilePath, IN UINT16 ArgLength, IN CHAR16 *Arg )
{
  EFI_STATUS  Status;
  EFI_ACPI_TABLE_PROTOCOL *AcpiTableProtocol;

  UINT8   *LoadBuffer;
  UINTN   Size;
  CHAR16  *PtrArg;
  
  EFI_LOADED_IMAGE_PROTOCOL*        loadedImageInfo;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL*  simpleFileSystem;
  EFI_FILE_PROTOCOL*  rootDir;
  EFI_FILE_PROTOCOL*  file;
  EFI_FILE_INFO*      fileInfo;
  UINT8 fileInfoBuffer[SIZE_OF_EFI_FILE_INFO + 100];
  UINTN bufferSize;

  //
  // ArgLength should be multiples of sizeof (CHAR16)
  //
  if ((ArgLength % (sizeof (CHAR16))) != 0) {
    ErrorPrint(L"Invalid ArgLength: (%d) \n", ArgLength);
    return EFI_INVALID_PARAMETER;
  }
  mArgLength = ArgLength;


  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **) &AcpiTableProtocol);
  if (EFI_ERROR (Status)) {
    ErrorPrint(L"Locate AcpiTableProtocol failed: (%r) \n", Status);
    return Status;
  }
  
  //
  // If WPBT already exists, disable it
  //
  EFI_ACPI_5_0_PLATFORM_BINARY_TABLE *ExistingTable = (EFI_ACPI_5_0_PLATFORM_BINARY_TABLE*)EfiLocateFirstAcpiTable(EFI_ACPI_5_0_PLATFORM_BINARY_TABLE_SIGNATURE);
	if (ExistingTable != NULL){
    Print(L"WPBT already exists, disable it\n");
    ExistingTable->Header.OemRevision = 0;
    ExistingTable->Header.Checksum = 0;
  }


  //
  // allocate real PlatformBinaryTable
  //
  gBS->AllocatePool ( EfiRuntimeServicesData, (sizeof (EFI_ACPI_5_0_PLATFORM_BINARY_TABLE) + mArgLength), (VOID **)&mPlatformBinaryTable );

  CopyMem (mPlatformBinaryTable, &mPlatformBinaryTableTemplate, sizeof (EFI_ACPI_5_0_PLATFORM_BINARY_TABLE));

  //
  // update Header->Length
  //
  mPlatformBinaryTable->Header.Length = (UINT32)(sizeof (EFI_ACPI_5_0_PLATFORM_BINARY_TABLE) + mArgLength);

  if (mArgLength > 0) {
    PtrArg = (CHAR16 *)((UINT8 *)(mPlatformBinaryTable) + sizeof (EFI_ACPI_5_0_PLATFORM_BINARY_TABLE));
    CopyMem (PtrArg, Arg, mArgLength);
  }  //end if (>0)
  
  //
  // Get EFI_SIMPLE_FILE_SYSTEM_PROTOCOL.
  //
  Status = gBS->OpenProtocol(gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&loadedImageInfo, gImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (EFI_ERROR(Status))
  {
      ErrorPrint(L"OpenProtocol(EFI_LOADED_IMAGE_PROTOCOL) failed: %r\n", Status);
      return Status;
  }
  
  Status = gBS->OpenProtocol(loadedImageInfo->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&simpleFileSystem, loadedImageInfo->DeviceHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (EFI_ERROR(Status))
  {
      ErrorPrint(L"OpenProtocol(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL) failed: %r\n", Status);
      return Status;
  }
  
  //
  // Open the given file.
  //
  Status = simpleFileSystem->OpenVolume(simpleFileSystem, &rootDir);
  if (EFI_ERROR(Status))
  {
      ErrorPrint(L"Open volume failed: %r\n", Status);
      return Status;
  }
  
  Status = rootDir->Open(rootDir, &file, (CHAR16*)FilePath, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(Status))
  {
      ErrorPrint(L"Open file failed: %r\n", Status);
      return Status;
  }
  
  Status = rootDir->Close(rootDir);
  ASSERT_EFI_ERROR(Status);
  
  //
  // Get the size of the file, allocate buffer and read contents onto it.
  //
  bufferSize = sizeof(fileInfoBuffer);
  Status = file->GetInfo(file, &gEfiFileInfoGuid, &bufferSize, fileInfoBuffer);
  if (EFI_ERROR(Status))
  {
      ErrorPrint(L"Get file info failed: %r\n", Status);
      return Status;
  }
  
  fileInfo = (EFI_FILE_INFO*)fileInfoBuffer;
  if (fileInfo->FileSize > MAX_UINT32)
  {
      ErrorPrint(L"File size too large: %llu bytes\n", fileInfo->FileSize);
      return Status;
  }
  Size = fileInfo->FileSize;
  
  Status = gBS->AllocatePool(EfiACPIReclaimMemory, Size, (VOID **)&LoadBuffer);
  if (EFI_ERROR(Status))
  {
      ErrorPrint(L"Memory allocation failed: %llu bytes\n", fileInfo->FileSize);
      return Status;
  }
  
  Status = file->Read(file, &Size, LoadBuffer);
  if (EFI_ERROR(Status))
  {
      ErrorPrint(L"Read file failed: %r\n", Status);
      return Status;
  }
  
  Status = file->Close(file);
  ASSERT_EFI_ERROR(Status);


  //
  // initialize WPBT members
  //
  mPlatformBinaryTable->BinarySize = (UINT32)Size;
  mPlatformBinaryTable->BinaryLocation = (UINT64)(UINTN)LoadBuffer;
  mPlatformBinaryTable->Layout = 0x01;
  mPlatformBinaryTable->Type = 0x01;
  mPlatformBinaryTable->ArgLength = mArgLength;

  //
  // Update Checksum.
  //
  UINTN ChecksumOffset;
  ChecksumOffset = OFFSET_OF (EFI_ACPI_DESCRIPTION_HEADER, Checksum);
  ((UINT8 *)mPlatformBinaryTable)[ChecksumOffset] = 0;
  ((UINT8 *)mPlatformBinaryTable)[ChecksumOffset] = CalculateCheckSum8 ((UINT8 *)mPlatformBinaryTable, (sizeof (EFI_ACPI_5_0_PLATFORM_BINARY_TABLE) + mPlatformBinaryTable->ArgLength));

  //
  // Publish Windows Platform Binary Table.
  //
  Status = AcpiTableProtocol->InstallAcpiTable ( AcpiTableProtocol, mPlatformBinaryTable, (sizeof (EFI_ACPI_5_0_PLATFORM_BINARY_TABLE) + mPlatformBinaryTable->ArgLength), &mPlatformBinaryResourceTableKey );
  if (EFI_ERROR (Status)) {
    ErrorPrint(L"InstallAcpiTable failed: (%r) \n", Status);
    return Status;
  }

  Print(L"WPBT Installed:0x%p\n", mPlatformBinaryTable);
  Print(L"WPBT BinarySize:0x%x\n", mPlatformBinaryTable->BinarySize);
  Print(L"WPBT ArgLength:0x%x\n", mPlatformBinaryTable->ArgLength);

  gBS->FreePool (mPlatformBinaryTable);
  mPlatformBinaryTable = NULL;

  return Status;
}


EFI_STATUS EFIAPI UefiMain ( IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable )
{
  EFI_STATUS  Status;
  UINTN       ArgLen;
  CHAR16*     FilePath;
  CHAR16*     InputArg;

  FilePath = L"native.exe";
  InputArg = L"";

  ArgLen = StrSize(InputArg);
  Status = InstallWpbt (FilePath, (UINT16)ArgLen, InputArg);

  if (EFI_ERROR(Status)){
    ErrorPrint(L"WPBT Install failed: %r\n", Status);
    return Status;
  }
  else{
    Print(L"Now load Windows");
    LoadWindowsBootloader();
  }

  return EFI_SUCCESS;
}

