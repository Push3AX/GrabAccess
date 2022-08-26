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
 *
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/file.h>
#include <grub/err.h>
#include <grub/script_sh.h>

#include <misc.h>
#include <vfat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <xz.h>
#include <wimboot.h>

#include "wimboot.c"

#define WIMBOOT_LEN 13602

#pragma GCC diagnostic ignored "-Wcast-align"

static char wimboot_script[5120];
static char initrd[4096];

void grub_wimboot_boot (struct wimboot_cmdline *cmd)
{
  unsigned int i;
  struct vfat_file *file;
  void *wimboot = grub_malloc (WIMBOOT_LEN);
  grub_xz_decompress (wimboot_bin, wimboot_bin_len, wimboot, WIMBOOT_LEN);
  for (i = 0 ;i < VDISK_MAX_FILES; i++)
  {
    file = &vfat_files[i];
    if (!file->opaque)
      break;
    if (strcasecmp (file->name, "bootmgfw.efi") == 0)
    {
      grub_printf ("...rename %s to bootmgr.exe.\n", file->name);
      grub_sprintf (initrd + strlen (initrd),
                    " newc:bootmgr.exe:(vfat,1)/%s", file->name);
    }
    else
    {
      grub_sprintf (initrd + strlen (initrd), " newc:%s:(vfat,1)/%s",
                    file->name, file->name);
      grub_printf ("...add newc:%s\n", file->name);
    }
  }
  grub_snprintf (wimboot_script, 5120, "set enable_progress_indicator=1\n"
                 "linux16 mem:%p:size:%u %s %s\n"
                 "initrd16 %s\n"
                 "set gfxmode=1920x1080,1366x768,1024x768,800x600,auto\n"
                 "terminal_output gfxterm\nboot\n", wimboot, WIMBOOT_LEN,
                 cmd->gui? "gui": "", cmd->pause? "pause": "", initrd);
  grub_printf ("cmd:\n%s\n", wimboot_script);
  if (cmd->pause)
    grub_getkey ();
  grub_script_execute_sourcecode (wimboot_script);
}
