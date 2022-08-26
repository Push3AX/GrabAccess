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

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/disk.h>
#include <grub/device.h>
#include <grub/term.h>
#include <grub/partition.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/ventoy.h>
#include <grub/acpi.h>
#include <grub/script_sh.h>
#ifdef GRUB_MACHINE_EFI
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#else
#include <grub/relocator.h>
#endif

ventoy_os_param *
grub_ventoy_get_osparam (void)
{
  void *data = NULL;
#ifdef GRUB_MACHINE_EFI
  grub_efi_guid_t ventoy_guid = VENTOY_GUID;
  grub_size_t datasize = 0;
  grub_efi_get_variable("VentoyOsParam", &ventoy_guid, &datasize, (void **) &data);
  if (!data || !datasize || datasize != sizeof (ventoy_os_param))
  {
    if (data)
      grub_free (data);
    return NULL;
  }
#else
  grub_addr_t addr = 0x80000;
  grub_packed_guid_t ventoy_guid = VENTOY_GUID;
  while (addr < 0xA0000)
  {
    if (grub_memcmp (&ventoy_guid, (void *)addr, sizeof (grub_packed_guid_t)) == 0)
    {
      data = (void *)addr;
      break;
    }
    addr++;
  }
  if (!data)
    return NULL;
#endif
  grub_printf ("VentoyOsParam found.\n");
  return data;
}

static int
ventoy_get_fs_type (const char *fs)
{
  if (!fs)
    return ventoy_fs_max;
  if (grub_strncmp(fs, "exfat", 5) == 0)
    return ventoy_fs_exfat;
  if (grub_strncmp(fs, "ntfs", 4) == 0)
    return ventoy_fs_ntfs;
  if (grub_strncmp(fs, "ext", 3) == 0)
    return ventoy_fs_ext;
  if (grub_strncmp(fs, "xfs", 3) == 0)
    return ventoy_fs_xfs;
  if (grub_strncmp(fs, "udf", 3) == 0)
    return ventoy_fs_udf;
  if (grub_strncmp(fs, "fat", 3) == 0)
    return ventoy_fs_fat;

  return ventoy_fs_max;
}

static int
ventoy_get_disk_guid (const char *filename, grub_uint8_t *guid)
{
  grub_disk_t disk;
  char *device_name;
  char *pos;
  char *pos2;

  device_name = grub_file_get_device_name(filename);
  if (!device_name)
    return 1;

  pos = device_name;
  if (pos[0] == '(')
    pos++;

  pos2 = grub_strstr(pos, ",");
  if (!pos2)
    pos2 = grub_strstr(pos, ")");

  if (pos2)
    *pos2 = 0;

  disk = grub_disk_open(pos);
  if (disk)
  {
    grub_disk_read(disk, 0, 0x180, 16, guid);
    grub_disk_close(disk);
  }
  else
  {
    return 1;
  }
  grub_free(device_name);
  return 0;
}

void
grub_ventoy_fill_osparam (grub_file_t file, ventoy_os_param *param)
{
  char *pos;
  grub_uint32_t i;
  grub_uint8_t chksum = 0;
  grub_disk_t disk;
  const grub_packed_guid_t vtguid = VENTOY_GUID;

  disk = file->device->disk;
  grub_memcpy(&param->guid, &vtguid, sizeof(grub_packed_guid_t));

  param->vtoy_disk_size = disk->total_sectors * (1 << disk->log_sector_size);
  param->vtoy_disk_part_id = disk->partition->number + 1;
  param->vtoy_disk_part_type = ventoy_get_fs_type(file->fs->name);

  pos = grub_strstr (file->name, "/");
  if (!pos)
    pos = file->name;

  grub_snprintf (param->vtoy_img_path, sizeof(param->vtoy_img_path), "%s", pos);

  ventoy_get_disk_guid(file->name, param->vtoy_disk_guid);

  param->vtoy_img_size = file->size;

  param->vtoy_reserved[0] = 0;
  param->vtoy_reserved[1] = 0;

  /* calculate checksum */
  for (i = 0; i < sizeof(ventoy_os_param); i++)
  {
    chksum += *((grub_uint8_t *)param + i);
  }
  param->chksum = (grub_uint8_t)(0x100 - chksum);

  return;
}

void
grub_ventoy_set_acpi_osparam (const char *filename)
{
  ventoy_os_param param, *osparam;
  grub_uint32_t max_chunk, i;
  struct grub_fs_block *p;
  grub_uint64_t part_start, offset = 0;
  grub_file_t file = 0;
  ventoy_image_location *location;
  ventoy_image_disk_region *region;
  struct grub_acpi_table_header *acpi = 0;
  grub_uint64_t buflen, loclen;
  char cmd[64];

  file = grub_file_open (filename, GRUB_FILE_TYPE_GET_SIZE);
  if (!file || !file->device || !file->device->disk)
    return;
  grub_ventoy_fill_osparam (file, &param);
  part_start = grub_partition_get_start (file->device->disk->partition);
  max_chunk = grub_blocklist_convert (file);
  if (!max_chunk)
    return;
  /* calculate acpi table length */
  loclen = sizeof (ventoy_image_location) +
           max_chunk * sizeof(ventoy_image_disk_region);
  buflen = sizeof (struct grub_acpi_table_header) +
           sizeof (ventoy_os_param) + loclen;
  acpi = grub_zalloc(buflen);
  if (!acpi)
    return;
  /* Step1: Fill acpi table header */
  grub_memcpy (acpi->signature, "VTOY", 4);
  acpi->length = buflen;
  acpi->revision = 1;
  grub_memcpy (acpi->oemid, "VENTOY", 6);
  grub_memcpy (acpi->oemtable, "OSPARAMS", 8);
  acpi->oemrev = 1;
  acpi->creator_id[0] = 1;
  acpi->creator_rev = 1;
  /* Step2: Fill data */
  osparam = (ventoy_os_param *)(acpi + 1);
  grub_memcpy (osparam, &param, sizeof(ventoy_os_param));
  osparam->vtoy_img_location_addr = 0;
  osparam->vtoy_img_location_len  = loclen;
  osparam->chksum = 0;
  osparam->chksum = 0x100 - grub_byte_checksum (osparam, sizeof (ventoy_os_param));

  location = (ventoy_image_location *)(osparam + 1);
  grub_memcpy (&location->guid, &osparam->guid, sizeof (grub_packed_guid_t));
  location->image_sector_size = 512;
  location->disk_sector_size  = 512;
  location->region_count = max_chunk;
  p = file->data;
  region = location->regions;
  for (i = 0; i < max_chunk; i++, region++, p++)
  {
    region->image_sector_count = p->length >> GRUB_DISK_SECTOR_BITS;
    region->image_start_sector = offset;
    region->disk_start_sector = (p->offset >> GRUB_DISK_SECTOR_BITS) + part_start;
    offset += region->image_sector_count;
    grub_printf ("add region: LBA=%llu IMG %llu+%llu\n",
                 (unsigned long long) region->disk_start_sector,
                 (unsigned long long) region->image_start_sector,
                 (unsigned long long) region->image_sector_count);
  }
  /* Step3: Fill acpi checksum */
  acpi->checksum = 0;
  acpi->checksum = 0x100 - grub_byte_checksum (acpi, acpi->length);

  /* load acpi table */
  grub_snprintf (cmd, sizeof(cmd), "acpi mem:%p:size:%u", acpi, acpi->length);
  grub_printf ("%s\n", cmd);
  grub_script_execute_sourcecode (cmd);
  grub_free(acpi);
  grub_file_close (file);
#ifdef GRUB_MACHINE_EFI
  /* unset uefi var VentoyOsParam */
  grub_efi_guid_t vtguid = VENTOY_GUID;
  grub_efi_set_var_attr ("VentoyOsParam", &vtguid, NULL, 0,
        GRUB_EFI_VARIABLE_BOOTSERVICE_ACCESS | GRUB_EFI_VARIABLE_RUNTIME_ACCESS);
#else
  grub_addr_t addr = 0x80000;
  grub_packed_guid_t vtguid = VENTOY_GUID;
  while (addr < 0xA0000)
  {
    if (grub_memcmp (&vtguid, (void *)addr, sizeof (vtguid)) == 0)
    {
      grub_memset((void *)addr, 0, sizeof (vtguid));
      break;
    }
    addr++;
  }
#endif
}

void
grub_ventoy_set_osparam (const char *filename)
{
  ventoy_os_param param;
  grub_file_t file = 0;
  file = grub_file_open (filename, GRUB_FILE_TYPE_GET_SIZE);
  if (!file)
    goto fail;
  grub_ventoy_fill_osparam (file, &param);
#ifdef GRUB_MACHINE_EFI
  grub_efi_guid_t vtguid = VENTOY_GUID;
  grub_efi_set_var_attr("VentoyOsParam", &vtguid, &param, sizeof (param),
        GRUB_EFI_VARIABLE_BOOTSERVICE_ACCESS | GRUB_EFI_VARIABLE_RUNTIME_ACCESS);
#else
  void *data = NULL;
  grub_relocator_chunk_t ch;
  struct grub_relocator *relocator = NULL;
  data = grub_ventoy_get_osparam ();
  if (!data)
  {
    relocator = grub_relocator_new ();
    if (!relocator)
      goto fail;
    if (grub_relocator_alloc_chunk_align (relocator, &ch, 0x80000, 0xA0000,
                sizeof (param), 1, GRUB_RELOCATOR_PREFERENCE_LOW, 0))
    {
      grub_relocator_unload (relocator);
      goto fail;
    }
    data = get_virtual_current_address(ch);
    grub_relocator_unload (relocator);
  }
  grub_memcpy (data, &param, sizeof (param));
#endif
  grub_printf ("VentoyOsParam created.\n");
fail:
  if (file)
    grub_file_close (file);
}
