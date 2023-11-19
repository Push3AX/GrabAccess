#ifndef _GRABACCESSDXE_H_
#define _GRABACCESSDXE_H_

#pragma pack(1)

typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      BinarySize;
  UINT64                      BinaryLocation;
  UINT8                       Layout;
  UINT8                       Type;
  UINT16                      ArgLength;
} EFI_ACPI_5_0_PLATFORM_BINARY_TABLE;

typedef struct _EFI_ACPI_WPBT_PROTOCOL  EFI_ACPI_WPBT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PLATFORM_BINARY)
(
  IN EFI_ACPI_WPBT_PROTOCOL            *This,
  IN EFI_GUID                          *FileGuid,
  IN UINT16                            ArgLength,
  IN CHAR16                            *Arg
);

struct _EFI_ACPI_WPBT_PROTOCOL {
  EFI_LOCATE_PLATFORM_BINARY        LocatePlatformBinary;
};

#pragma pack()

EFI_ACPI_5_0_PLATFORM_BINARY_TABLE mPlatformBinaryTableTemplate = {
  {
    EFI_ACPI_5_0_PLATFORM_BINARY_TABLE_SIGNATURE,
    sizeof (EFI_ACPI_5_0_PLATFORM_BINARY_TABLE),
    1,                                              // Revision
    0x00,                                           // Checksum will be updated at runtime
    'G','R','A','B',' ',' ',                        // OEMID is a 6 bytes long field
    SIGNATURE_64('A','C','C','E','S','S',' ',' '),  // OEM table identification(8 bytes long)
    0x00000001,                                     // OEM revision number
    SIGNATURE_32('A','C','P','I'),                  // ASL compiler vendor ID
    0x00040000,                                     // ASL compiler revision number
  },
  0,        // BinarySize
  0,        // BinaryLocation
  0x01,     // Content Layout
  0x01,     // Content Type
  0         // ArgLength
};

EFI_GUID FileGuid = {0x2136252F, 0x5F7C, 0x486D, {0xB8, 0x9F, 0x54, 0x5E, 0xC4, 0x2A, 0xD4, 0x5C}};

#endif
