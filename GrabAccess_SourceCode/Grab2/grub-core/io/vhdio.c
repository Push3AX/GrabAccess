/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019,2020  Free Software Foundation, Inc.
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

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/file.h>
#include <grub/mm.h>

#pragma GCC diagnostic ignored "-Wcast-align"

GRUB_MOD_LICENSE ("GPLv3+");

typedef struct
{
  grub_uint8_t cookie[8];//string conectix
  grub_uint32_t features;
  grub_uint32_t fileFormatVersion;
  grub_uint64_t dataOffset;
  grub_uint32_t timeStamp;
  grub_uint8_t creatorApplication[4];
  grub_uint32_t creatorVersion;
  grub_uint32_t creatorHostOS;
  grub_uint64_t originalSize;
  grub_uint64_t currentSize;
  struct
  {
    grub_uint16_t cylinder;
    grub_uint8_t heads;
    grub_uint8_t sectorsPerTrack;
  } diskGeometry;
  grub_uint32_t diskType;
  grub_uint32_t checksum;
  grub_uint8_t uniqueId[16];
  grub_uint8_t savedState;
  grub_uint8_t reserved[427];
} GRUB_PACKED VHDFooter;

#define VHD_FOOTER_COOKIE      0x78697463656E6F63ULL
#define VHD_DYNAMIC_COOKIE     0x6573726170737863ULL

#define VHD_DISKTYPE_FIXED      2
#define VHD_DISKTYPE_DYNAMIC    3
#define VHD_DISKTYPE_DIFFERENCE 4

typedef struct
{
  grub_uint8_t cookie[8];//string cxsparse
  grub_uint64_t dataOffset;
  grub_uint64_t tableOffset;
  grub_uint32_t headerVersion;
  grub_uint32_t maxTableEntries;
  grub_uint32_t blockSize;
  grub_uint32_t checksum;
  grub_uint8_t parentUniqueID[16];
  grub_uint32_t parentTimeStamp;
  grub_uint8_t reserved[4];
  grub_uint8_t parentUnicodeName[512];
  grub_uint8_t parentLocaterEntry[8][24];
  grub_uint8_t reserved2[256];
} GRUB_PACKED VHDDynamicDiskHeader;

typedef struct VHDFileControl VHDFileControl;

struct VHDFileControl
{
  grub_uint64_t cFileMax;
  grub_uint64_t volumeSize;
  grub_uint64_t tableOffset;
  grub_uint32_t diskType;
  grub_uint32_t blockSize;
  grub_uint32_t blockSizeLog2;
  grub_uint32_t batEntries;
  grub_uint32_t blockBitmapSize;
  grub_uint8_t *blockAllocationTable;
  grub_uint8_t *blockBitmapAndData;
  grub_uint8_t *blockData;
  grub_uint32_t currentBlockOffset;
  struct VHDFileControl *parentVHDFC;
};

static grub_uint32_t log2pot32 (grub_uint32_t x)
{
  // x must be power of two
  return ((x & 0xFFFF0000) ? 16 : 0) |
         ((x & 0xFF00FF00) ? 8 : 0) |
         ((x & 0xF0F0F0F0) ? 4 : 0) |
         ((x & 0xCCCCCCCC) ? 2 : 0) |
         ((x & 0xAAAAAAAA) ? 1 : 0);
}

static void vhd_footer_in (VHDFooter *footer)
{
  footer->dataOffset = grub_swap_bytes64 (footer->dataOffset);
  footer->currentSize = grub_swap_bytes64 (footer->currentSize);
  footer->diskType = grub_swap_bytes32 (footer->diskType);
}

static void vhd_header_in (VHDDynamicDiskHeader *header)
{
  header->tableOffset = grub_swap_bytes64 (header->tableOffset);
  header->maxTableEntries = grub_swap_bytes32 (header->maxTableEntries);
  header->blockSize = grub_swap_bytes32 (header->blockSize);
}

struct grub_vhdio
{
  grub_file_t file;
  VHDFileControl *vhdfc;
};
typedef struct grub_vhdio *grub_vhdio_t;

static struct grub_fs grub_vhdio_fs;

static grub_err_t
grub_vhdio_close (grub_file_t file)
{
  grub_vhdio_t vhdio = file->data;
  VHDFileControl *vhdfc = vhdio->vhdfc;

  if (vhdfc)
  {
    if (vhdfc->blockAllocationTable)
      grub_free (vhdfc->blockAllocationTable);
    if (vhdfc->blockBitmapAndData)
      grub_free (vhdfc->blockBitmapAndData);
    grub_free(vhdfc);
  }
  grub_file_close (vhdio->file);
  grub_free (vhdio);
  file->device = 0;
  file->name = 0;
  return grub_errno;
}


static grub_file_t
grub_vhdio_open (grub_file_t io, enum grub_file_type type)
{
  grub_file_t file;
  grub_vhdio_t vhdio;
  VHDFooter footer;
  VHDDynamicDiskHeader dynaheader;
  VHDFileControl *vhdfc = NULL;

  if (type & GRUB_FILE_TYPE_NO_DECOMPRESS)
    return io;
  if (io->size < 0x10000)
    return io;

  /* test header */
  grub_memset (&footer, 0, sizeof(footer));
  grub_memset (&dynaheader, 0, sizeof(dynaheader));
  grub_file_seek (io, 0);
  grub_file_read (io, &footer, sizeof (footer));
  grub_file_seek (io, 0);
  if (grub_memcmp (footer.cookie, "conectix", 8) != 0)
    return io;

  vhd_footer_in (&footer);
  if (footer.diskType != VHD_DISKTYPE_DYNAMIC)
    return io;
  if (footer.dataOffset + sizeof(dynaheader) > io->size)
    return io;

  file = (grub_file_t) grub_zalloc (sizeof (*file));
  if (!file)
    return 0;

  vhdio = grub_zalloc (sizeof (*vhdio));
  if (!vhdio)
  {
    grub_free (file);
    return 0;
  }
  vhdfc = grub_zalloc (sizeof(VHDFileControl));
  if (!vhdfc)
  {
    grub_free (file);
    grub_free (vhdio);
    return 0;
  }
  vhdio->vhdfc = vhdfc;
  vhdio->file = io;

  file->device = io->device;
  file->data = vhdio;
  file->fs = &grub_vhdio_fs;
  file->size = GRUB_FILE_SIZE_UNKNOWN;
  file->not_easily_seekable = io->not_easily_seekable;

  grub_file_seek (vhdio->file, footer.dataOffset);
  grub_file_read (vhdio->file, &dynaheader, sizeof (dynaheader));
  vhd_header_in (&dynaheader);
  vhdfc->cFileMax = io->size;
  vhdfc->volumeSize = footer.currentSize;
  vhdfc->diskType = footer.diskType;
  vhdfc->tableOffset = dynaheader.tableOffset;
  vhdfc->blockSize = dynaheader.blockSize;
  vhdfc->blockSizeLog2 = log2pot32 (vhdfc->blockSize);
  vhdfc->batEntries = dynaheader.maxTableEntries;

  grub_uint32_t batSize = (vhdfc->batEntries * 4 + 511)&(-512LL);
  vhdfc->blockAllocationTable = grub_malloc (batSize);
  vhdfc->blockBitmapSize = vhdfc->blockSize / (512 * 8);
  vhdfc->blockBitmapAndData = grub_malloc (vhdfc->blockBitmapSize
                              + vhdfc->blockSize);
  vhdfc->blockData = vhdfc->blockBitmapAndData + vhdfc->blockBitmapSize;

  vhdio->file->offset = vhdfc->tableOffset;
  grub_file_read(vhdio->file, vhdfc->blockAllocationTable, batSize);
    vhdfc->currentBlockOffset = -1LL;

  file->size = vhdfc->volumeSize;

  return file;
}

static grub_ssize_t
grub_vhdio_read (grub_file_t file, char *buf, grub_size_t len)
{
  grub_uint64_t ret = 0;
  grub_uint64_t rem = len;
  grub_vhdio_t vhdio = file->data;
  VHDFileControl *vhdfc = vhdio->vhdfc;
  if (file->offset + len > vhdfc->volumeSize)
    len = (file->offset <= vhdfc->volumeSize) ? vhdfc->volumeSize - file->offset : 0;
  while (rem)
  {
    grub_uint32_t blockNumber = file->offset >> vhdfc->blockSizeLog2;
    grub_uint64_t blockOffset = blockNumber << vhdfc->blockSizeLog2;
    grub_uint32_t offsetInBlock = (grub_uint32_t)(file->offset - blockOffset);
    grub_uint32_t txLen = (rem < vhdfc->blockSize - offsetInBlock) ?
                          rem : vhdfc->blockSize - offsetInBlock;
    grub_uint32_t blockLBA = *(grub_uint32_t*)
                             (vhdfc->blockAllocationTable + blockNumber * 4);
    blockLBA = grub_swap_bytes32 (blockLBA);
    if (blockLBA == 0xFFFFFFFF)
    {
      // unused block on dynamic VHD. read zero
      grub_memset (buf, 0, txLen);
    }
    else
    {
      if (blockOffset != vhdfc->currentBlockOffset)
      {
        vhdio->file->offset = blockLBA * 512;
        grub_uint64_t nread = grub_file_read (vhdio->file,
            vhdfc->blockBitmapAndData, vhdfc->blockBitmapSize + vhdfc->blockSize);
        if (nread < vhdfc->blockBitmapSize + vhdfc->blockSize)
          break;
        vhdfc->currentBlockOffset = blockOffset;
      }
      grub_memmove (buf, vhdfc->blockData + offsetInBlock, txLen);
    }
    buf += txLen;
    file->offset += txLen;
    rem -= txLen;
    ret += txLen;
  }

  return ret;
}

static struct grub_fs grub_vhdio_fs = {
  .name = "vhdio",
  .fs_dir = 0,
  .fs_open = 0,
  .fs_read = grub_vhdio_read,
  .fs_close = grub_vhdio_close,
  .fs_label = 0,
  .next = 0
};

GRUB_MOD_INIT(vhd)
{
  grub_file_filter_register (GRUB_FILE_FILTER_VHDIO, grub_vhdio_open);
}

GRUB_MOD_FINI(vhd)
{
  grub_file_filter_unregister (GRUB_FILE_FILTER_VHDIO);
}
