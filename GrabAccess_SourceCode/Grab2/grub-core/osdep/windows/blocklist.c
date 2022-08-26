/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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

#include <config.h>

#include <grub/disk.h>
#include <grub/partition.h>
#include <grub/fs.h>
#include <grub/ntfs.h>
#include <grub/fat.h>
#include <grub/exfat.h>
#include <grub/udf.h>
#include <grub/util/misc.h>
#include <grub/util/install.h>
#include <grub/emu/getroot.h>
#include <grub/emu/hostfile.h>

#include <windows.h>
#include <winioctl.h>

void
grub_install_get_blocklist (grub_device_t root_dev,
			    const char *core_path,
			    const char *core_img __attribute__ ((unused)),
			    size_t core_size,
			    void (*callback) (grub_disk_addr_t sector,
					      unsigned offset,
					      unsigned length,
					      void *data),
			    void *hook_data)
{
  grub_disk_addr_t first_lcn = 0;
  HANDLE filehd;
  DWORD rets;
  RETRIEVAL_POINTERS_BUFFER *extbuf;
  size_t extbuf_size;
  DWORD i;
  grub_uint64_t sec_per_lcn;
  grub_uint64_t curvcn = 0;
  STARTING_VCN_INPUT_BUFFER start_vcn;
  grub_fs_t fs;
  grub_err_t err;

  fs = grub_fs_probe (root_dev);
  if (!fs)
    grub_util_error ("%s", grub_errmsg);

  /* This is ugly but windows doesn't give all needed data. Or does anyone
     have a pointer how to retrieve it?
   */
  if (grub_strcmp (fs->name, "ntfs") == 0)
    {
      struct grub_ntfs_bpb bpb;
      err = grub_disk_read (root_dev->disk, 0, 0, sizeof (bpb), &bpb);
      if (err)
	grub_util_error ("%s", grub_errmsg);
      sec_per_lcn = ((grub_uint32_t) bpb.sectors_per_cluster
		     * (grub_uint32_t) grub_le_to_cpu16 (bpb.bytes_per_sector))
	>> 9;
      first_lcn = 0;
    }
  else if (grub_strcmp (fs->name, "exfat") == 0)
    first_lcn = grub_exfat_get_cluster_sector (root_dev->disk, &sec_per_lcn);
  else if (grub_strcmp (fs->name, "fat") == 0)
    first_lcn = grub_fat_get_cluster_sector (root_dev->disk, &sec_per_lcn);
  else if (grub_strcmp (fs->name, "udf") == 0)
    first_lcn = grub_udf_get_cluster_sector (root_dev->disk, &sec_per_lcn);
  else
    grub_util_error ("unsupported fs for blocklist on windows: %s",
		     fs->name);

  grub_util_info ("sec_per_lcn = %"  GRUB_HOST_PRIuLONG_LONG
		  ", first_lcn=%" GRUB_HOST_PRIuLONG_LONG,
		  (unsigned long long) sec_per_lcn,
		  (unsigned long long) first_lcn);

  first_lcn += grub_partition_get_start (root_dev->disk->partition);

  start_vcn.StartingVcn.QuadPart = 0;

  filehd = grub_util_fd_open (core_path, GRUB_UTIL_FD_O_RDONLY);
  if (!GRUB_UTIL_FD_IS_VALID (filehd))
    grub_util_error (_("cannot open `%s': %s"), core_path,
		     grub_util_fd_strerror ());

  extbuf_size = sizeof (*extbuf) + sizeof (extbuf->Extents[0])
    * ((core_size + 511) / 512);
  extbuf = xmalloc (extbuf_size);

  if (!DeviceIoControl(filehd, FSCTL_GET_RETRIEVAL_POINTERS,
		       &start_vcn, sizeof (start_vcn),
		       extbuf, extbuf_size, &rets, NULL))
    grub_util_error ("FSCTL_GET_RETRIEVAL_POINTERS fails: %s",
		     grub_util_fd_strerror ());

  CloseHandle (filehd);

  for (i = 0; i < extbuf->ExtentCount; i++)
    callback (extbuf->Extents[i].Lcn.QuadPart
	      * sec_per_lcn + first_lcn,
	      0, 512 * sec_per_lcn * (extbuf->Extents[i].NextVcn.QuadPart - curvcn), hook_data);
  free (extbuf);
}
