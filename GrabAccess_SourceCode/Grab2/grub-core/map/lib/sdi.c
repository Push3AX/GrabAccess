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

#include <grub/dl.h>
#include <grub/err.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/procfs.h>
#include <xz.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <misc.h>
#include <sdi.h>

#include "raw/ntfs.c"

static grub_uint8_t *ntsdi = NULL;

static void
load_sdi (void)
{
  struct grub_sdi_toc_record part, wim;
  ntsdi = grub_zalloc (GRUB_SDI_LEN);
  if (!ntsdi)
    return;
  grub_xz_decompress (ntfs_img, ntfs_img_len,
                      ntsdi + GRUB_SDI_NTFS_OFS, GRUB_SDI_NTFS_LEN);
  memcpy (ntsdi, GRUB_SDI_MAGIC, 8);
  ntsdi[GRUB_SDI_ALIGN_OFS] = GRUB_SDI_ALIGN;
  ntsdi[GRUB_SDI_CHKSUM_OFS] = GRUB_SDI_CHKSUM;
  /* NTFS PART record */
  memset (&part, 0, sizeof (part));
  memcpy (part.blob_type, "PART", 4);
  part.offset = GRUB_SDI_PART_OFS;
  part.size = GRUB_SDI_PART_LEN;
  part.base_addr = GRUB_SDI_PART_ID;
  memcpy (ntsdi + GRUB_SDI_TOC_OFS, &part, GRUB_SDI_TOC_SIZE);
  /* WIM record */
  memset (&wim, 0, sizeof (wim));
  memcpy (wim.blob_type, "WIM", 3);
  wim.offset = GRUB_SDI_WIM_OFS;
  memcpy (ntsdi + GRUB_SDI_TOC_OFS + GRUB_SDI_TOC_SIZE, &wim, GRUB_SDI_TOC_SIZE);
}

static char *
get_sdi (grub_size_t *sz)
{
  *sz = 0;
  char *ret = NULL;
  *sz = GRUB_SDI_LEN;
  if (!*sz)
    return ret;
  ret = grub_malloc (*sz);
  if (!ret)
    return ret;
  grub_memcpy (ret, ntsdi, *sz);
  return ret;
}

static struct grub_procfs_entry proc_sdi =
{
  .name = "boot.sdi",
  .get_contents = get_sdi,
};

void
grub_load_bootsdi (void)
{
  load_sdi ();
  grub_procfs_register ("boot.sdi", &proc_sdi);
}

void
grub_unload_bootsdi (void)
{
  grub_procfs_unregister (&proc_sdi);
  if (ntsdi)
    grub_free (ntsdi);
}
