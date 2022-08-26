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

#include <misc.h>
#include <vfat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <wimboot.h>
#include <wimpatch.h>
#include <wimfile.h>

#pragma GCC diagnostic ignored "-Wcast-align"

#if defined (__i386__)
  #define BOOT_FILE_NAME   "BOOTIA32.EFI"
#elif defined (__x86_64__)
  #define BOOT_FILE_NAME   "BOOTX64.EFI"
#elif defined (__arm__)
  #define BOOT_FILE_NAME   "BOOTARM.EFI"
#elif defined (__aarch64__)
  #define BOOT_FILE_NAME   "BOOTAA64.EFI"
#else
  #error Unknown Processor Type
#endif

#ifdef GRUB_MACHINE_EFI
#define SEARCH_EXT  L".exe"
#define REPLACE_EXT L".efi"
#else
#define SEARCH_EXT  L".efi"
#define REPLACE_EXT L".exe"
#endif

/** bootmgfw.efi path within WIM */
#ifdef GRUB_MACHINE_EFI
#define WIM_BOOTMGR_PATH L"\\Windows\\Boot\\EFI\\bootmgfw.efi"
#define WIM_BOOTMGR_NAME L"" BOOT_FILE_NAME
#else
#define WIM_BOOTMGR_PATH L"\\Windows\\Boot\\PXE\\bootmgr.exe"
#define WIM_BOOTMGR_NAME L"bootmgr.exe"
#endif

static void
vfat_patch_bcd (struct vfat_file *file __unused,
           void *data, size_t offset __unused, size_t len)
{
  static const wchar_t search[] = SEARCH_EXT;
  static const wchar_t replace[] = REPLACE_EXT;
  size_t i;

  /* Patch any occurrences of ".exe" to ".efi".  In the common
   * simple cases, this allows the same BCD file to be used for
   * both BIOS and UEFI systems.
   */
  for (i = 0 ;(i + sizeof (search)) < len; i++)
  {
    if (wcscasecmp ((wchar_t *)((char *)data + i), search) == 0)
    {
      memcpy (((char *)data + i), replace, sizeof (replace));
    }
  }
}

static int
isbootmgfw (const char *name)
{
  if (strcasecmp(name, "bootmgfw.efi") == 0)
    return 1;
#ifdef GRUB_MACHINE_EFI
  if (strcasecmp (name, BOOT_FILE_NAME) == 0)
    return 1;
#else
  if (strcasecmp(name, "bootmgr.exe") == 0)
    return 1;
  if (strcasecmp (name, "bootmgr") == 0)
    return 1;
#endif
  return 0;
}

static int
iswim (const char *name)
{
  uint32_t len = strlen (name);
  return (len > 4 && strcasecmp (name + len - 4, ".wim") == 0);
}

#define WIM_MAX_PATH (256 + VDISK_NAME_LEN + 1)

static void
add_orig (struct vfat_file *wimfile, struct wimboot_cmdline *cmd)
{
  unsigned int i, cnt;
  struct vfat_file *file;
  char inject_path[256];
  char path[WIM_MAX_PATH];
  wchar_t wpath[WIM_MAX_PATH];
  char name[VDISK_NAME_LEN + 1];
  wchar_t wname[VDISK_NAME_LEN + 1];
  wcstombs (inject_path, cmd->inject, 256);

  for (cnt = 0 ;cnt < VDISK_MAX_FILES; cnt++ )
  {
    file = &vfat_files[cnt];
    if (!file->opaque)
      break;
  }

  for (i = 0 ;i < cnt; i++ )
  {
    file = &vfat_files[i];
    grub_snprintf (path, WIM_MAX_PATH, "%s\\%s", inject_path, file->name);
    mbstowcs (wpath, path, WIM_MAX_PATH);
    grub_snprintf (name, VDISK_NAME_LEN + 1, "orig_%s", file->name);
    mbstowcs (wname, name, VDISK_NAME_LEN + 1);
    grub_printf ("looking up %s -> %s ...\n", path, name);
    wim_add_file (wimfile, cmd->index, wpath, wname);
  }
}

static int
file_add (const char *name, grub_file_t data, struct wimboot_cmdline *cmd)
{
  struct vfat_file *vfile;
  vfile = vfat_add_file (name, data, data->size, vfat_read_wrapper);

  /* Check for special-case files */
  if (isbootmgfw (name) )
  {
    printf ("...found bootmgr file %s\n", name);
    cmd->bootmgfw = vfile;
  }
  else if (strcasecmp (name, "BCD") == 0)
  {
    printf ("...found BCD\n");
    if (!cmd->rawbcd)
      vfat_patch_file (vfile, vfat_patch_bcd);
    cmd->bcd = vfile;
  }
  else if (strcasecmp (name, "boot.sdi") == 0)
  {
    printf ("...found boot.sdi\n");
    cmd->bootsdi = vfile;
  }
  else if (iswim (name))
  {
    printf ("...found WIM file %s\n", name);
    cmd->wim = name;
    if (!cmd->rawwim)
    {
      add_orig (vfile, cmd);
      vfat_patch_file (vfile, patch_wim);
    }
    if (!cmd->bootmgfw)
    {
      cmd->bootmgfw = wim_add_file (vfile, cmd->index,
                                    WIM_BOOTMGR_PATH, WIM_BOOTMGR_NAME);
      if (cmd->bootmgfw)
        grub_printf ("...extract bootmgr from %s\n", name);
    }
  }
  return 0;
}

void
grub_wimboot_extract (struct wimboot_cmdline *cmd)
{
  struct grub_vfatdisk_file *f = NULL;
  struct grub_vfatdisk_file *wim = NULL;
  for (f = vfat_file_list; f; f = f->next)
  {
    if (iswim (f->name) && !wim)
      wim = f;
    else
      file_add (f->name, f->file, cmd);
  }
  if (wim)
    file_add (wim->name, wim->file, cmd);
  /* Check that we have a boot file */
  if (! cmd->bootmgfw)
    grub_pause_fatal ("FATAL: bootmgr not found\n");
}

void
grub_wimboot_init (int argc, char *argv[])
{
  int i;

  for (i = 0; i < argc; i++)
  {
    const char *fname = argv[i];
    char *file_name = NULL;
    grub_file_t file = 0;
    if (grub_memcmp (argv[i], "@:", 2) == 0 ||
        grub_memcmp (argv[i], "m:", 2) == 0 ||
        grub_memcmp (argv[i], "b:", 2) == 0 ||
        grub_memcmp (argv[i], "f:", 2) == 0)
    {
      const char *ptr, *eptr;
      ptr = argv[i] + 2;
      eptr = grub_strchr (ptr, ':');
      if (eptr)
      {
        file_name = grub_strndup (ptr, eptr - ptr);
        if (!file_name)
          grub_pause_fatal ("file name error.\n");
        fname = eptr + 1;
      }
    }
    int mem = 0;
    int bl = 0;
    if (argv[i][0] == 'm' || argv[i][0] == 'f')
      mem = 1;
    if (argv[i][0] == 'b' || argv[i][0] == 'f')
      bl = 1;
    file = file_open (fname, mem, bl, 0);
    if (!file)
      grub_pause_fatal ("fatal: bad file %s.\n", fname);
    if (!file_name)
      file_name = grub_strdup (file->name);
    vfat_append_list (file, file_name);
    grub_free (file_name);
  }
}
