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
 *
 */

#include <grub/misc.h>
#include <grub/file.h>
#include <grub/fat.h>
#include <grub/eltorito.h>

#include <guid.h>
#include <misc.h>
#include <stddef.h>
#include <iso.h>

#pragma GCC diagnostic ignored "-Wcast-align"

#define EFI_PARTITION   0xef

static grub_off_t
fat_bpb_get_size (grub_file_t iso, grub_off_t offset)
{
  grub_off_t ret = 0;
  struct grub_fat_bpb bpb;
  file_read (iso, &bpb, sizeof (bpb), offset);

  if ((grub_memcmp ((const char *) bpb.version_specific.fat12_or_fat16.fstype,
                   "FAT12", 5) == 0) ||
      (grub_memcmp ((const char *) bpb.version_specific.fat12_or_fat16.fstype,
                   "FAT16", 5) == 0) ||
      (grub_memcmp ((const char *) bpb.version_specific.fat32.fstype,
                   "FAT32", 5) == 0))
  {
    if (bpb.num_total_sectors_32)
      ret = bpb.bytes_per_sector * (grub_uint64_t) bpb.num_total_sectors_32;
    else
      ret = bpb.bytes_per_sector * bpb.num_total_sectors_16;
  }
  return ret;
}

int
grub_iso_get_eltorito (grub_file_t iso, grub_off_t *offset, grub_off_t *len)
{
  cdrom_volume_descriptor_t *vol = NULL;
  eltorito_catalog_t *catalog = NULL;
  grub_size_t dbr_img_size = sizeof (grub_uint16_t);
  grub_uint16_t dbr_img_buf;
  int boot_entry = 0;
  grub_size_t i;
  grub_off_t fat;

  vol = grub_zalloc (CD_BLOCK_SIZE);
  if (!vol)
    goto fail;

  file_read (iso, vol, CD_BLOCK_SIZE, CD_BOOT_SECTOR * CD_BLOCK_SIZE);

  if (vol->unknown.type != CDVOL_TYPE_STANDARD ||
      grub_memcmp (vol->boot_record_volume.system_id, CDVOL_ELTORITO_ID,
                   sizeof (CDVOL_ELTORITO_ID) - 1) != 0)
    goto fail;

  catalog = (eltorito_catalog_t *) vol;
  file_read (iso, catalog, CD_BLOCK_SIZE,
      *((grub_uint32_t*) vol->boot_record_volume.elt_catalog) * CD_BLOCK_SIZE);
  if (catalog[0].catalog.indicator != ELTORITO_ID_CATALOG)
    goto fail;

  for (i = 0; i < 64; i++)
  {
    if (catalog[i].section.indicator == ELTORITO_ID_SECTION_HEADER_FINAL &&
        catalog[i].section.platform_id == EFI_PARTITION &&
        catalog[i+1].boot.indicator == ELTORITO_ID_SECTION_BOOTABLE)
    {
      boot_entry = 1;
      *offset = catalog[i+1].boot.lba << CD_SHIFT;
      *len = catalog[i+1].boot.sector_count << FD_SHIFT;

      file_read (iso, &dbr_img_buf, dbr_img_size, *offset + 0x13);
      dbr_img_size = dbr_img_buf << FD_SHIFT;
      if (*len < dbr_img_size)
        *len = dbr_img_size;

      if (*len < BLOCK_OF_1_44MB * FD_BLOCK_SIZE)
        *len = BLOCK_OF_1_44MB * FD_BLOCK_SIZE;
      break;
    }
  }

  fat = fat_bpb_get_size (iso, *offset);
  if (fat > *len)
  {
    grub_printf ("FAT fs size: %"PRIuGRUB_UINT64_T"\n", fat);
    *len = fat;
  }
  if (*len + *offset > iso->size)
    *len = iso->size - *offset;
fail:
  if (vol)
    grub_free (vol);
  return boot_entry;
}

int
grub_iso_check_vt (grub_file_t iso)
{
  int compat = 0, i;
  char *buf = NULL;
  const char vt[] = "VENTOY COMPATIBLE";

  buf = grub_zalloc (1024);
  if (!buf)
    return 0;

  file_read (iso, buf, 1024, (CD_BOOT_SECTOR - 1) * CD_BLOCK_SIZE);
  buf[703] = 0;
  for (i = 319; i < 703; i++)
  {
    if (buf[i] == 'V' && (grub_strcmp (buf + i, vt) == 0))
    {
      compat = 1;
      break;
    }
  }
  grub_free (buf);
  return compat;
}
