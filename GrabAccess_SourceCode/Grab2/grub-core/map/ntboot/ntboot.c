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

#include <grub/dl.h>
#include <grub/device.h>
#include <grub/err.h>
#include <grub/env.h>
#include <grub/extcmd.h>
#include <grub/file.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/term.h>
#include <grub/partition.h>
#ifdef GRUB_MACHINE_MULTIBOOT
#include <grub/machine/kernel.h>
#endif

#include <xz.h>
#include <misc.h>
#include <wimboot.h>
#include <bcd.h>
#include <vfat.h>
#include <sdi.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options_ntboot[] =
{
  {"gui", 'g', 0, N_("Display graphical boot messages."), 0, 0},
  {"pause", 'p', 0, N_("Show info and wait for keypress."), 0, 0},
  {"vhd", 'v', 0, N_("Boot NT6+ VHD/VHDX."), 0, 0},
  {"wim", 'w', 0, N_("Boot NT6+ WIM."), 0, 0},
  {"win", 'n', 0, N_("Boot NT6+ Windows."), 0, 0},
  {"efi", 'e', 0, N_("Specify the bootmgfw.efi file."), N_("FILE"), ARG_TYPE_FILE},
  {"sdi", 's', 0, N_("Specify the boot.sdi file."), N_("FILE"), ARG_TYPE_FILE},
  {"dll", 'd', 0, N_("Specify the bootvhd.dll file."), N_("FILE"), ARG_TYPE_FILE},

  {"testmode", 0, 0, N_("Test Mode (testsigning)."), N_("yes|no"), ARG_TYPE_STRING},
  {"highest", 0, 0, N_("Force Highest Resolution."), N_("yes|no"), ARG_TYPE_STRING},
  {"nx", 0, 0, N_("Nx Policy."),
    N_("OptIn|OptOut|AlwaysOff|AlwaysOn"), ARG_TYPE_STRING},
  {"pae", 0, 0, N_("PAE Policy."), N_("Default|Enable|Disable"), ARG_TYPE_STRING},
  {"detecthal", 0, 0, N_("Detect HAL and kernel."), N_("yes|no"), ARG_TYPE_STRING},
  {"winpe", 0, 0, N_("Boot into WinPE."), N_("yes|no"), ARG_TYPE_STRING},
  {"timeout", 0, 0, N_("Set Timeout."), N_("n"), ARG_TYPE_INT},
  {"novesa", 0, 0, N_("Avoid VESA BIOS calls."), N_("yes|no"), ARG_TYPE_STRING},
  {"novga", 0, 0, N_("Disable VGA modes."), N_("yes|no"), ARG_TYPE_STRING},
  {"loadoptions", 0, 0, N_("Set LoadOptionsString."), N_("STRING"), ARG_TYPE_STRING},
  {"winload", 0, 0, N_("Set path of winload."), N_("WIN32_PATH"), ARG_TYPE_STRING},
  {"sysroot", 0, 0, N_("Set system root."), N_("WIN32_PATH"), ARG_TYPE_STRING},
  {0, 0, 0, 0, 0, 0}
};

enum options_ntboot
{
  NTBOOT_GUI,
  NTBOOT_PAUSE,
  NTBOOT_VHD,
  NTBOOT_WIM,
  NTBOOT_WIN,
  NTBOOT_EFI,
  NTBOOT_SDI,
  NTBOOT_DLL,
  /* bcd boot options */
  NTBOOT_TESTMODE, // bool
  NTBOOT_HIGHEST,  // bool
  NTBOOT_NX,       // uint64
  NTBOOT_PAE,      // uint64
  NTBOOT_DETHAL,   // bool
  NTBOOT_PE,       // bool
  NTBOOT_TIMEOUT,  // uint64
  NTBOOT_NOVESA,   // bool
  NTBOOT_NOVGA,    // bool
  NTBOOT_CMDLINE,  // string
  NTBOOT_WINLOAD,  // string
  NTBOOT_SYSROOT,  // string
};

static int check_disk (grub_disk_t disk)
{
  if (!disk || !disk->partition || !disk->dev)
    return 0;
  if (disk->dev->id == GRUB_DISK_DEVICE_EFIDISK_ID && disk->name[0] == 'h')
    return 1;
  if (disk->dev->id == GRUB_DISK_DEVICE_BIOSDISK_ID && disk->name[0] == 'h')
    return 1;
  if (disk->dev->id == GRUB_DISK_DEVICE_EFIVDISK_ID)
    return 1;
  return 0;
}

static grub_err_t
grub_cmd_ntboot (grub_extcmd_context_t ctxt,
                  int argc, char *argv[])
{
  struct grub_arg_list *state = ctxt->state;
  grub_file_t file = 0;
  char *path = NULL;
  enum bcd_type type;
  struct bcd_patch_data ntcmd;
  grub_memset (&ntcmd, 0, sizeof (ntcmd));

  if (argc != 1)
  {
    grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));
    goto fail;
  }

  if (argv[0][0] == 'h')
  {
    char str[32];
    grub_snprintf (str, 32, "(%s)", argv[0]);
    file = file_open (str, 0, 0, 0);
  }
  else
    file = file_open (argv[0], 0, 0, 0);

  if (!file)
  {
    grub_error (GRUB_ERR_FILE_READ_ERROR, N_("failed to open file"));
    goto fail;
  }
  if (!file->device || !check_disk (file->device->disk))
  {
    grub_error (GRUB_ERR_BAD_DEVICE,
                "this command is available only for disk devices");
    goto fail;
  }
  if (argv[0][0] == '(')
    path = grub_strchr (argv[0], '/');
  if (!path)
    path = argv[0];

  if (path[grub_strlen (path) - 1] == 'm' ||
      path[grub_strlen (path) - 1] == 'M') /* wim */
    type = BOOT_WIM;
  else if (path[grub_strlen (path) - 1] == ')')
    type = BOOT_WIN;
  else
    type = BOOT_VHD;
  if (state[NTBOOT_WIM].set)
    type = BOOT_WIM;
  if (state[NTBOOT_VHD].set)
    type = BOOT_VHD;
  if (state[NTBOOT_WIN].set)
    type = BOOT_WIN;

  /* fill ntboot_cmd */
  ntcmd.type = type;
  ntcmd.file = file;
  ntcmd.path = path;
  if (state[NTBOOT_TESTMODE].set)
    ntcmd.testmode = state[NTBOOT_TESTMODE].arg;
  if (state[NTBOOT_HIGHEST].set)
    ntcmd.highest = state[NTBOOT_HIGHEST].arg;
  if (state[NTBOOT_NX].set)
    ntcmd.nx = state[NTBOOT_NX].arg;
  if (state[NTBOOT_PAE].set)
    ntcmd.pae = state[NTBOOT_PAE].arg;
  if (state[NTBOOT_DETHAL].set)
    ntcmd.detecthal = state[NTBOOT_DETHAL].arg;
  if (state[NTBOOT_PE].set)
    ntcmd.winpe = state[NTBOOT_PE].arg;
  if (state[NTBOOT_TIMEOUT].set)
    ntcmd.timeout = state[NTBOOT_TIMEOUT].arg;
  if (state[NTBOOT_NOVESA].set)
    ntcmd.novesa = state[NTBOOT_NOVESA].arg;
  if (state[NTBOOT_NOVGA].set)
    ntcmd.novga = state[NTBOOT_NOVGA].arg;
  if (state[NTBOOT_CMDLINE].set)
    ntcmd.cmdline = state[NTBOOT_CMDLINE].arg;
  if (state[NTBOOT_WINLOAD].set)
    ntcmd.winload = state[NTBOOT_WINLOAD].arg;
  if (state[NTBOOT_SYSROOT].set)
    ntcmd.sysroot = state[NTBOOT_SYSROOT].arg;
  grub_patch_bcd (&ntcmd);

  struct wimboot_cmdline wimboot_cmd =
      { 0, 1, 1, 0, 0, L"\\Windows\\System32", NULL, NULL, NULL, NULL };
  grub_file_t bootmgr = 0;
  grub_file_t bootsdi = 0;
  grub_file_t bcd = 0;
  grub_file_t vhd_dll = 0;

  if (state[NTBOOT_GUI].set)
    wimboot_cmd.gui = 1;
  if (state[NTBOOT_PAUSE].set)
    wimboot_cmd.pause = 1;

  bcd = file_open ("(proc)/bcd", 0, 0, 0);
  vfat_add_file ("bcd", bcd, bcd->size, vfat_read_wrapper);

  if (state[NTBOOT_DLL].set)
  {
    vhd_dll = file_open (state[NTBOOT_DLL].arg, 0, 0, 0);
    vfat_add_file ("bootvhd.dll", vhd_dll, vhd_dll->size, vfat_read_wrapper);
  }

  if (state[NTBOOT_EFI].set)
    bootmgr = file_open (state[NTBOOT_EFI].arg, 0, 0, 0);
  else
    bootmgr = file_open ("/efi/microsoft/boot/bootmgfw.efi", 0, 0, 0);
  if (!bootmgr)
  {
    grub_error (GRUB_ERR_FILE_READ_ERROR, N_("failed to open bootmgfw.efi"));
    goto fail;
  }
  wimboot_cmd.bootmgfw = vfat_add_file ("bootmgfw.efi",
                                        bootmgr, bootmgr->size, vfat_read_wrapper);

  if (type == BOOT_WIM)
  {
    if (state[NTBOOT_SDI].set)
      bootsdi = file_open (state[NTBOOT_SDI].arg, 0, 0, 0);
    else
      bootsdi = file_open ("(proc)/boot.sdi", 0, 0, 0);
    if (!bootsdi)
    {
      grub_error (GRUB_ERR_FILE_READ_ERROR, N_("failed to open boot.sdi"));
      goto fail;
    }
    vfat_add_file ("boot.sdi", bootsdi, bootsdi->size, vfat_read_wrapper);
  }
  grub_wimboot_install ();
  if (wimboot_cmd.pause)
    grub_getkey ();
  grub_wimboot_boot (&wimboot_cmd);
  if (vhd_dll)
    grub_file_close (vhd_dll);
  if (bootmgr)
    grub_file_close (bootmgr);
  if (bootsdi)
    grub_file_close (bootsdi);

fail:
  if (file)
    grub_file_close (file);
  return grub_errno;
}

static grub_extcmd_t cmd_ntboot;

GRUB_MOD_INIT(ntboot)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (!grub_mb_check_bios_int (0x13))
    return;
#endif
  cmd_ntboot = grub_register_extcmd ("ntboot", grub_cmd_ntboot, 0,
                    N_("[-v|-w] [--efi=FILE] FILE"),
                    N_("Boot NT6+ VHD/VHDX/WIM"), options_ntboot);
}

GRUB_MOD_FINI(ntboot)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (!grub_mb_check_bios_int (0x13))
    return;
#endif
  grub_unregister_extcmd (cmd_ntboot);
}
