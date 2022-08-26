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
#include <grub/wimtools.h>
#include <grub/lua.h>
#ifdef GRUB_MACHINE_MULTIBOOT
#include <grub/machine/kernel.h>
#endif

#include <misc.h>
#include <wimboot.h>
#include <wimpatch.h>
#include <vfat.h>
#include <string.h>
#include <bcd.h>
#include <sdi.h>

#ifdef GRUB_MACHINE_EFI
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/disk.h>
#endif

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options_wimboot[] =
{
  {"gui", 'g', 0, N_("Display graphical boot messages."), 0, 0},
  {"rawbcd", 'b', 0, N_("Disable rewriting .exe to .efi in the BCD file."), 0, 0},
  {"rawwim", 'w', 0, N_("Disable patching the wim file."), 0, 0},
  {"index", 'i', 0, N_("Use WIM image index n."), N_("n"), ARG_TYPE_INT},
  {"pause", 'p', 0, N_("Show info and wait for keypress."), 0, 0},
  {"inject", 'j', 0, N_("Set inject dir."), N_("PATH"), ARG_TYPE_STRING},

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

enum options_wimboot
{
  WIMBOOT_GUI,
  WIMBOOT_RAWBCD,
  WIMBOOT_RAWWIM,
  WIMBOOT_INDEX,
  WIMBOOT_PAUSE,
  WIMBOOT_INJECT,

  WIMBOOT_TESTMODE, // bool
  WIMBOOT_HIGHEST,  // bool
  WIMBOOT_NX,       // uint64
  WIMBOOT_PAE,      // uint64
  WIMBOOT_DETHAL,   // bool
  WIMBOOT_PE,       // bool
  WIMBOOT_TIMEOUT,  // uint64
  WIMBOOT_NOVESA,   // bool
  WIMBOOT_NOVGA,    // bool
  WIMBOOT_CMDLINE,  // string
  WIMBOOT_WINLOAD,  // string
  WIMBOOT_SYSROOT,  // string
};

static grub_err_t
grub_cmd_wimboot (grub_extcmd_context_t ctxt, int argc, char *argv[])
{
  struct grub_arg_list *state = ctxt->state;
  struct wimboot_cmdline wimboot_cmd =
      { 0, 0, 0, 0, 0, L"\\Windows\\System32", NULL, NULL, NULL, NULL };

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  if (state[WIMBOOT_GUI].set)
    wimboot_cmd.gui = 1;
  if (state[WIMBOOT_RAWBCD].set)
    wimboot_cmd.rawbcd = 1;
  if (state[WIMBOOT_RAWWIM].set)
    wimboot_cmd.rawwim = 1;
  if (state[WIMBOOT_PAUSE].set)
    wimboot_cmd.pause = 1;
  if (state[WIMBOOT_INDEX].set)
    wimboot_cmd.index = grub_strtoul (state[WIMBOOT_INDEX].arg, NULL, 0);
  if (state[WIMBOOT_INJECT].set)
    mbstowcs (wimboot_cmd.inject, state[WIMBOOT_INJECT].arg, 256);

  set_wim_patch (&wimboot_cmd);

  grub_wimboot_init (argc, argv);

  grub_wimboot_extract (&wimboot_cmd);
  if (! wimboot_cmd.bcd)
  {
    struct bcd_patch_data data;
    grub_memset (&data, 0, sizeof (data));
    data.type = BOOT_RAW;
    data.path = wimboot_cmd.wim? wimboot_cmd.wim: "boot.wim";
    if (state[WIMBOOT_TESTMODE].set)
      data.testmode = state[WIMBOOT_TESTMODE].arg;
    if (state[WIMBOOT_HIGHEST].set)
      data.highest = state[WIMBOOT_HIGHEST].arg;
    if (state[WIMBOOT_NX].set)
      data.nx = state[WIMBOOT_NX].arg;
    if (state[WIMBOOT_PAE].set)
      data.pae = state[WIMBOOT_PAE].arg;
    if (state[WIMBOOT_DETHAL].set)
      data.detecthal = state[WIMBOOT_DETHAL].arg;
    if (state[WIMBOOT_PE].set)
      data.winpe = state[WIMBOOT_PE].arg;
    if (state[WIMBOOT_TIMEOUT].set)
      data.timeout = state[WIMBOOT_TIMEOUT].arg;
    if (state[WIMBOOT_NOVESA].set)
      data.novesa = state[WIMBOOT_NOVESA].arg;
    if (state[WIMBOOT_NOVGA].set)
      data.novga = state[WIMBOOT_NOVGA].arg;
    if (state[WIMBOOT_CMDLINE].set)
      data.cmdline = state[WIMBOOT_CMDLINE].arg;
    if (state[WIMBOOT_WINLOAD].set)
      data.winload = state[WIMBOOT_WINLOAD].arg;
    if (state[WIMBOOT_SYSROOT].set)
      data.sysroot = state[WIMBOOT_SYSROOT].arg;
    grub_patch_bcd (&data);
    grub_file_t bcd = file_open ("(proc)/bcd", 0, 0, 0);
    vfat_add_file ("bcd", bcd, bcd->size, vfat_read_wrapper);
  }
  if (! wimboot_cmd.bootsdi)
  {
    grub_file_t bootsdi = file_open ("(proc)/boot.sdi", 0, 0, 0);
    vfat_add_file ("boot.sdi", bootsdi, bootsdi->size, vfat_read_wrapper);
  }
  grub_wimboot_install ();
  grub_wimboot_boot (&wimboot_cmd);

  grub_pause_fatal ("failed to boot.\n");
  return grub_errno;
}

static grub_extcmd_t cmd_wimboot;

static const struct grub_arg_option options_wimtools[] = {
  {"index", 'i', 0, N_("Use WIM image index n."), N_("n"), ARG_TYPE_INT},
  {"exist", 'e', 0, N_("Check file exists or not."), 0, 0},
  {"is64", 'a', 0, N_("Check winload.exe is 64 bit or not."), 0, 0},
  {"boot_index", 'b', 0, N_("Get boot index."), N_("VAR"), ARG_TYPE_STRING},
  {"image_count", 'c', 0, N_("Get number of images."), N_("VAR"), ARG_TYPE_STRING},
  {0, 0, 0, 0, 0, 0}
};

enum options_wimtools
{
  WIMTOOLS_INDEX,
  WIMTOOLS_EXIST,
  WIMTOOLS_IS64,
  WIMTOOLS_BOOT,
  WIMTOOLS_COUNT,
};

static grub_err_t
grub_cmd_wimtools (grub_extcmd_context_t ctxt, int argc, char *argv[])
{
  struct grub_arg_list *state = ctxt->state;
  char str[10];
  unsigned int index = 0;
  grub_file_t file = 0;
  grub_err_t err = GRUB_ERR_NONE;
  if (argc < 1 || (state[WIMTOOLS_EXIST].set && argc < 2))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  if (state[WIMTOOLS_INDEX].set)
    index = grub_strtoul (state[WIMTOOLS_INDEX].arg, NULL, 0);
  file = grub_file_open (argv[0], GRUB_FILE_TYPE_LOOPBACK);
  if (!file)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("failed to open file"));

  if (state[WIMTOOLS_EXIST].set)
  {
    if (grub_wim_file_exist (file, index, argv[1]))
      err = GRUB_ERR_NONE;
    else
      err = GRUB_ERR_TEST_FAILURE;
  }
  else if (state[WIMTOOLS_IS64].set)
  {
    if (grub_wim_is64 (file, index))
      err = GRUB_ERR_NONE;
    else
      err = GRUB_ERR_TEST_FAILURE;
  }
  else if (state[WIMTOOLS_BOOT].set)
  {
    index = grub_wim_boot_index (file);
    grub_snprintf (str, 10, "%u", index);
    grub_env_set (state[WIMTOOLS_BOOT].arg, str);
    err = GRUB_ERR_NONE;
  }
  else if (state[WIMTOOLS_COUNT].set)
  {
    index = grub_wim_image_count (file);
    grub_snprintf (str, 10, "%u", index);
    grub_env_set (state[WIMTOOLS_COUNT].arg, str);
    err = GRUB_ERR_NONE;
  }
  grub_file_close (file);
  return err;
}

static grub_extcmd_t cmd_wimtools;

static int
wim_file_exist (lua_State *state)
{
  const char *wim;
  const char *path;
  unsigned int index = 0;
  grub_file_t file = 0;

  wim = luaL_checkstring (state, 1);
  path = luaL_checkstring (state, 2);
  if (lua_gettop (state) > 2)
    index = luaL_checkinteger (state, 3);

  file = grub_file_open (wim, GRUB_FILE_TYPE_LOOPBACK);
  if (file)
  {
    lua_pushboolean (state, grub_wim_file_exist (file, index, path));
    grub_file_close (file);
  }
  else
    lua_pushboolean (state, 0);
  return 1;
}

static int
wim_is64 (lua_State *state)
{
  const char *wim;
  unsigned int index = 0;
  grub_file_t file = 0;

  wim = luaL_checkstring (state, 1);
  if (lua_gettop (state) > 1)
    index = luaL_checkinteger (state, 2);

  file = grub_file_open (wim, GRUB_FILE_TYPE_LOOPBACK);
  if (file)
  {
    lua_pushboolean (state, grub_wim_is64 (file, index));
    grub_file_close (file);
  }
  else
    lua_pushboolean (state, 0);
  return 1;
}

static int
wim_image_count (lua_State *state)
{
  const char *wim;
  grub_file_t file = 0;

  wim = luaL_checkstring (state, 1);

  file = grub_file_open (wim, GRUB_FILE_TYPE_LOOPBACK);
  if (file)
  {
    lua_pushinteger (state, grub_wim_image_count (file));
    grub_file_close (file);
  }
  else
    lua_pushinteger (state, 0);
  return 1;
}

static int
wim_boot_index (lua_State *state)
{
  const char *wim;
  grub_file_t file = 0;

  wim = luaL_checkstring (state, 1);

  file = grub_file_open (wim, GRUB_FILE_TYPE_LOOPBACK);
  if (file)
  {
    lua_pushinteger (state, grub_wim_boot_index (file));
    grub_file_close (file);
  }
  else
    lua_pushinteger (state, 0);
  return 1;
}

static luaL_Reg wimlib[] =
{
  {"file_exist", wim_file_exist},
  {"is64", wim_is64},
  {"image_count", wim_image_count},
  {"boot_index", wim_boot_index},
  {0, 0}
};

GRUB_MOD_INIT(wimboot)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (!grub_mb_check_bios_int (0x13))
    return;
#endif
  grub_load_bcd ();
  grub_load_bootsdi ();
  cmd_wimboot = grub_register_extcmd ("wimboot", grub_cmd_wimboot, 0,
                    N_("[--rawbcd] [--index=n] [--pause] @:NAME:PATH"),
                    N_("Windows Imaging Format bootloader"), options_wimboot);
  cmd_wimtools = grub_register_extcmd ("wimtools", grub_cmd_wimtools, 0,
                    N_("[--index=n] [OPTIONS] FILE [PATH]"),
                    N_("WIM Tools"), options_wimtools);
  if (grub_lua_global_state)
  {
    lua_gc (grub_lua_global_state, LUA_GCSTOP, 0);
    luaL_register (grub_lua_global_state, "wim", wimlib);
    lua_gc (grub_lua_global_state, LUA_GCRESTART, 0);
  }
}

GRUB_MOD_FINI(wimboot)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (!grub_mb_check_bios_int (0x13))
    return;
#endif
  grub_unload_bcd ();
  grub_unload_bootsdi ();
  grub_unregister_extcmd (cmd_wimboot);
  grub_unregister_extcmd (cmd_wimtools);
}
