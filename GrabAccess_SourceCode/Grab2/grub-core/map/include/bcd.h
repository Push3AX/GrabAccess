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

#ifndef GRUB_BOOTBCD_H
#define GRUB_BOOTBCD_H

#include <grub/types.h>
#include <grub/misc.h>
#include <stdint.h>

#define BCD_DP_MAGIC "GNU GRUB2 NTBOOT"

#define GUID_OSENTRY L"{19260817-6666-8888-abcd-000000000000}"
#define GUID_REENTRY L"{19260817-6666-8888-abcd-000000000001}"

#define GUID_BOOTMGR L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}"
#define GUID_RAMDISK L"{ae5534e0-a924-466c-b836-758539a3ee3a}"
#define GUID_MEMDIAG L"{b2721d73-1db4-4c62-bf78-c548a880142d}"
#define GUID_OSNTLDR L"{466f5a88-0af2-4f76-9038-095b170dc21c}"

#define BCD_REG_ROOT L"Objects"
#define BCD_REG_HKEY L"Elements"
#define BCD_REG_HVAL L"Element"

#define BCDOPT_REPATH   L"12000002"
#define BCDOPT_REHIBR   L"22000002"

#define BCDOPT_WINLOAD  L"12000002"
#define BCDOPT_CMDLINE  L"12000030"
#define BCDOPT_TESTMODE L"16000049"
#define BCDOPT_HIGHEST  L"16000054"
#define BCDOPT_SYSROOT  L"22000002"
#define BCDOPT_TIMEOUT  L"25000004" // {bootmgr}
#define BCDOPT_NX       L"25000020"
#define BCDOPT_PAE      L"25000021"
#define BCDOPT_DETHAL   L"26000010"
#define BCDOPT_DISPLAY  L"26000020" // {bootmgr}
#define BCDOPT_WINPE    L"26000022"
#define BCDOPT_NOVESA   L"26000042"
#define BCDOPT_NOVGA    L"26000043"
#define BCDOPT_SOS      L"26000091"
#define BCDOPT_IMGOFS   L"35000001" // {ramdiskoptions}

#define NX_OPTIN     0x00
#define NX_OPTOUT    0x01
#define NX_ALWAYSOFF 0x02
#define NX_ALWAYSON  0x03

#define PAE_DEFAULT  0x00
#define PAE_ENABLE   0x01
#define PAE_DISABLE  0x02

#define BCD_DECOMPRESS_LEN 16384

#ifdef GRUB_MACHINE_EFI
#define BCD_SEARCH_EXT  L".exe"
#define BCD_REPLACE_EXT L".efi"
#else
#define BCD_SEARCH_EXT  L".efi"
#define BCD_REPLACE_EXT L".exe"
#endif

#define BCD_DEFAULT_CMDLINE "DDISABLE_INTEGRITY_CHECKS"

#ifdef GRUB_MACHINE_EFI
#define BCD_DEFAULT_WINLOAD "\\Windows\\System32\\boot\\winload.efi"
#define BCD_SHORT_WINLOAD "\\Windows\\System32\\winload.efi"
#define BCD_DEFAULT_WINRESUME "\\Windows\\System32\\winresume.efi"
#else
#define BCD_DEFAULT_WINLOAD "\\Windows\\System32\\boot\\winload.exe"
#define BCD_SHORT_WINLOAD "\\Windows\\System32\\winload.exe"
#define BCD_DEFAULT_WINRESUME "\\Windows\\System32\\winresume.exe"
#endif

#define BCD_DEFAULT_HIBERFIL "\\hiberfil.sys"

#define BCD_DEFAULT_SYSROOT "\\Windows"

enum bcd_type
{
  BOOT_RAW,
  BOOT_WIN,
  BOOT_WIM,
  BOOT_VHD,
};

struct bcd_dp
{
  uint8_t partid[16];
  uint32_t unknown;
  uint32_t partmap;
  uint8_t diskid[16];
} GRUB_PACKED;

struct bcd_patch_data
{
  enum bcd_type type;
  struct bcd_dp dp;
  const char *path;
  grub_file_t file;
  /* bcd options */
  const char *testmode;
  const char *highest;
  const char *nx;
  const char *pae;
  const char *detecthal;
  const char *winpe;
  const char *timeout;
  const char *novesa;
  const char *novga;
  const char *cmdline;
  const char *winload;
  const char *sysroot;
};

extern grub_uint8_t grub_bcd_data[];

grub_err_t grub_patch_bcd (struct bcd_patch_data *cmd);
void grub_load_bcd (void);
void grub_unload_bcd (void);

#endif
