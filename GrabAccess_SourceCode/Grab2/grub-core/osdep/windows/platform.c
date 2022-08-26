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

#include <config-util.h>

#include <windows.h>
#include <grub/util/install.h>
#include <grub/util/misc.h>
#include <grub/efi/api.h>
#include <grub/charset.h>
#include <grub/gpt_partition.h>

#if __GNUC__ >= 9
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif

#define GRUB_EFI_GLOBAL_VARIABLE_GUID_WINDOWS_STR L"{8be4df61-93ca-11d2-aa0d-00e098032b8c}"

static enum { PLAT_UNK, PLAT_BIOS, PLAT_EFI } platform;
static DWORD (WINAPI * func_GetFirmwareEnvironmentVariableW) (LPCWSTR lpName,
							      LPCWSTR lpGuid,
							      PVOID pBuffer,
							      DWORD nSize);
static BOOL (WINAPI * func_SetFirmwareEnvironmentVariableW) (LPCWSTR lpName,
							     LPCWSTR lpGuid,
							     PVOID pBuffer,
							     DWORD nSize);
static void (WINAPI * func_GetNativeSystemInfo) (LPSYSTEM_INFO lpSystemInfo);

static int
get_efi_privilegies (void)
{
  int ret = 1;
  HANDLE hSelf;
  TOKEN_PRIVILEGES tkp;

  if (!OpenProcessToken (GetCurrentProcess(),
			 TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hSelf))
    return 0;

  LookupPrivilegeValue (NULL, SE_SYSTEM_ENVIRONMENT_NAME,
		        &tkp.Privileges[0].Luid);
  tkp.PrivilegeCount = 1;
  tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  if (!AdjustTokenPrivileges (hSelf, FALSE, &tkp, 0, NULL, 0))
    ret = 0;
  if (GetLastError () != ERROR_SUCCESS)
    ret = 0;
  CloseHandle (hSelf);
  return 1;
}

static void
get_platform (void)
{
  HMODULE kernel32;
  char buffer[256];

  if (platform != PLAT_UNK)
    return;

  kernel32 = GetModuleHandle(TEXT("kernel32.dll"));
  if (!kernel32)
    {
      platform = PLAT_BIOS;
      return;
    }

  func_GetFirmwareEnvironmentVariableW = (void *)
    GetProcAddress (kernel32, "GetFirmwareEnvironmentVariableW");
  func_SetFirmwareEnvironmentVariableW = (void *)
    GetProcAddress (kernel32, "SetFirmwareEnvironmentVariableW");
  func_GetNativeSystemInfo = (void *)
    GetProcAddress (kernel32, "GetNativeSystemInfo");
  if (!func_GetNativeSystemInfo)
    func_GetNativeSystemInfo = GetSystemInfo;
  if (!func_GetFirmwareEnvironmentVariableW
      || !func_SetFirmwareEnvironmentVariableW)
    {
      platform = PLAT_BIOS;
      return;
    }

  if (!get_efi_privilegies ())
    {
      grub_util_warn (_("Insufficient privileges to access firmware, assuming BIOS"));
      platform = PLAT_BIOS;
    }

  if (!func_GetFirmwareEnvironmentVariableW (L"BootOrder", GRUB_EFI_GLOBAL_VARIABLE_GUID_WINDOWS_STR,
					     buffer, sizeof (buffer))
      && GetLastError () == ERROR_INVALID_FUNCTION)
    {
      platform = PLAT_BIOS;
      return;
    }    
  platform = PLAT_EFI;
  return;
}

const char *
grub_install_get_default_x86_platform (void)
{ 
  SYSTEM_INFO si;

  get_platform ();
  if (platform != PLAT_EFI)
    return "i386-pc";

  /* EFI */
  /* Assume 64-bit in case of failure.  */
  si.wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
  func_GetNativeSystemInfo (&si);
  if (si.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_INTEL)
    return "x86_64-efi";
  else
    return "i386-efi";
}

static void *
get_efi_variable (const wchar_t *varname, ssize_t *len)
{
  void *ret = NULL;
  size_t alloc_size = 256, read_size;
  get_platform ();
  while (1)
    {
      DWORD err;
      ret = xmalloc (alloc_size);
      read_size = func_GetFirmwareEnvironmentVariableW (varname, GRUB_EFI_GLOBAL_VARIABLE_GUID_WINDOWS_STR,
							ret, alloc_size);
      err = GetLastError ();
      if (read_size)
	{
	  *len = read_size;
	  return ret;
	}
      if (err == ERROR_INSUFFICIENT_BUFFER
	  && alloc_size * 2 != 0)
	{
	  alloc_size *= 2;
	  free (ret);
	  continue;
	}
      if (err == ERROR_ENVVAR_NOT_FOUND)
	{
	  *len = -1;
	  return NULL;
	}
      *len = -2;
      return NULL;
    }
}

static void
set_efi_variable (const wchar_t *varname, void *in, grub_size_t len)
{
  get_platform ();
  func_SetFirmwareEnvironmentVariableW (varname, GRUB_EFI_GLOBAL_VARIABLE_GUID_WINDOWS_STR,
					in, len);
}

static char
bin2hex (int v)
{
  if (v < 10)
    return '0' + v;
  return 'A' + v - 10;
}

static void *
get_efi_variable_bootn (grub_uint16_t n, ssize_t *len)
{
  wchar_t varname[20] = L"Boot0000";
  varname[7] = bin2hex (n & 0xf);
  varname[6] = bin2hex ((n >> 4) & 0xf);
  varname[5] = bin2hex ((n >> 8) & 0xf);
  varname[4] = bin2hex ((n >> 12) & 0xf);
  return get_efi_variable (varname, len);
}

static void
set_efi_variable_bootn (grub_uint16_t n, void *in, grub_size_t len)
{
  wchar_t varname[20] = L"Boot0000";
  varname[7] = bin2hex (n & 0xf);
  varname[6] = bin2hex ((n >> 4) & 0xf);
  varname[5] = bin2hex ((n >> 8) & 0xf);
  varname[4] = bin2hex ((n >> 12) & 0xf);
  set_efi_variable (varname, in, len);
}

int
grub_install_register_efi (grub_device_t efidir_grub_dev,
			   const char *efifile_path,
			   const char *efi_distributor)
{
  grub_uint16_t *boot_order, *new_boot_order;
  grub_uint16_t *distributor16;
  grub_uint8_t *entry;
  grub_size_t distrib8_len, distrib16_len, path16_len, path8_len;
  ssize_t boot_order_len, new_boot_order_len;
  grub_uint16_t order_num = 0;
  int have_order_num = 0;
  grub_size_t max_path_length;
  grub_uint8_t *path;
  void *pathptr;
  struct grub_efi_hard_drive_device_path *hddp;
  struct grub_efi_file_path_device_path *filep;
  struct grub_efi_device_path *endp;

  get_platform ();
  if (platform != PLAT_EFI)
    grub_util_error ("%s", _("no EFI routines are available when running in BIOS mode"));

  distrib8_len = grub_strlen (efi_distributor);
  distributor16 = xcalloc (distrib8_len + 1,
			   GRUB_MAX_UTF16_PER_UTF8 * sizeof (grub_uint16_t));
  distrib16_len = grub_utf8_to_utf16 (distributor16, distrib8_len * GRUB_MAX_UTF16_PER_UTF8,
				      (const grub_uint8_t *) efi_distributor,
				      distrib8_len, 0);
  distributor16[distrib16_len] = 0;

  /* Windows doesn't allow to list variables so first look for bootorder to
     find if there is an entry from the same distributor. If not try sequentially
     until we find same distributor or empty spot.  */
  boot_order = get_efi_variable (L"BootOrder", &boot_order_len);
  if (boot_order_len < -1)
    grub_util_error ("%s", _("unexpected EFI error"));
  if (boot_order_len > 0)
    {
      size_t i;
      for (i = 0; i < boot_order_len / 2; i++)
	{
	  void *current = NULL;
	  ssize_t current_len;
	  current = get_efi_variable_bootn (i, &current_len);
	  if (current_len < 0)
	    continue; /* FIXME Should we abort on error? */
	  if (current_len < (distrib16_len + 1) * sizeof (grub_uint16_t)
	      + 6)
	    {
	      grub_free (current);
	      continue;
	    }
	  if (grub_memcmp ((grub_uint16_t *) current + 3,
			   distributor16,
			   (distrib16_len + 1) * sizeof (grub_uint16_t)) != 0)
	    {
	      grub_free (current);
	      continue;
	    }
	  order_num = i;
	  have_order_num = 1;
	  grub_util_info ("Found matching distributor at Boot%04x",
			  order_num);
	  grub_free (current);
	  break;
	}
    }
  if (!have_order_num)
    {
      size_t i;
      for (i = 0; i < 0x10000; i++)
	{
	  void *current = NULL;
	  ssize_t current_len;
	  current = get_efi_variable_bootn (i, &current_len);
	  if (current_len < -1)
	    continue; /* FIXME Should we abort on error? */
	  if (current_len == -1)
	    {
	      if (!have_order_num)
		{
		  order_num = i;
		  have_order_num = 1;
		  grub_util_info ("Creating new entry at Boot%04x",
				  order_num);
		}
	      continue;
	    }
	  if (current_len < (distrib16_len + 1) * sizeof (grub_uint16_t)
	      + 6)
	    {
	      grub_free (current);
	      continue;
	    }
	  if (grub_memcmp ((grub_uint16_t *) current + 3,
			   distributor16,
			   (distrib16_len + 1) * sizeof (grub_uint16_t)) != 0)
	    {
	      grub_free (current);
	      continue;
	    }
	  order_num = i;
	  have_order_num = 1;
	  grub_util_info ("Found matching distributor at Boot%04x",
			  order_num);
	  grub_free (current);
	  break;
	}
    }
  if (!have_order_num)
    grub_util_error ("%s", _("Couldn't find a free BootNNNN slot"));
  path8_len = grub_strlen (efifile_path);
  max_path_length = sizeof (*hddp) + sizeof (*filep) + (path8_len * GRUB_MAX_UTF16_PER_UTF8 + 1) * sizeof (grub_uint16_t) + sizeof (*endp);
  entry = xmalloc (6 + (distrib16_len + 1) * sizeof (grub_uint16_t) + max_path_length);
  /* attributes: active.  */
  entry[0] = 1;
  entry[1] = 0;
  entry[2] = 0;
  entry[3] = 0;
  grub_memcpy (entry + 6,
	       distributor16,
	       (distrib16_len + 1) * sizeof (grub_uint16_t));

  path = entry + 6 + (distrib16_len + 1) * sizeof (grub_uint16_t);
  pathptr = path;

  hddp = pathptr;
  grub_memset (hddp, 0, sizeof (*hddp));
  hddp->header.type = GRUB_EFI_MEDIA_DEVICE_PATH_TYPE;
  hddp->header.subtype = GRUB_EFI_HARD_DRIVE_DEVICE_PATH_SUBTYPE;
  hddp->header.length = sizeof (*hddp);
  hddp->partition_number = efidir_grub_dev->disk->partition ? efidir_grub_dev->disk->partition->number + 1 : 1;
  if (efidir_grub_dev->disk->partition
      && grub_strcmp (efidir_grub_dev->disk->partition->partmap->name, "msdos") == 0)
    {
      grub_partition_t p;

      p = efidir_grub_dev->disk->partition;
      efidir_grub_dev->disk->partition = p->parent;
      if (grub_disk_read (efidir_grub_dev->disk, 0, 440,
			  4, hddp->partition_signature))
	grub_util_error ("%s", grub_errmsg);
      efidir_grub_dev->disk->partition = p;

      hddp->partmap_type = 1;
      hddp->signature_type = 1;
    }
  else if (efidir_grub_dev->disk->partition
	   && grub_strcmp (efidir_grub_dev->disk->partition->partmap->name, "gpt") == 0)
    {
      struct grub_gpt_partentry gptdata;
      grub_partition_t p;

      p = efidir_grub_dev->disk->partition;
      efidir_grub_dev->disk->partition = p->parent;
      if (grub_disk_read (efidir_grub_dev->disk,
			  p->offset, p->index,
			  sizeof (gptdata), &gptdata))
	grub_util_error ("%s", grub_errmsg);
      efidir_grub_dev->disk->partition = p;
      grub_memcpy (hddp->partition_signature,
		   &gptdata.guid, 16);

      hddp->partmap_type = 2;
      hddp->signature_type = 2;
    }

  hddp->partition_start = grub_partition_get_start (efidir_grub_dev->disk->partition)
    >> (efidir_grub_dev->disk->log_sector_size - GRUB_DISK_SECTOR_BITS);
  hddp->partition_size = grub_disk_native_sectors (efidir_grub_dev->disk)
    >> (efidir_grub_dev->disk->log_sector_size - GRUB_DISK_SECTOR_BITS);

  pathptr = hddp + 1;
  filep = pathptr;
  filep->header.type = GRUB_EFI_MEDIA_DEVICE_PATH_TYPE;
  filep->header.subtype = GRUB_EFI_FILE_PATH_DEVICE_PATH_SUBTYPE;

  path16_len = grub_utf8_to_utf16 (filep->path_name,
				   path8_len * GRUB_MAX_UTF16_PER_UTF8,
				   (const grub_uint8_t *) efifile_path,
				   path8_len, 0);
  filep->path_name[path16_len] = 0;
  filep->header.length = sizeof (*filep) + (path16_len + 1) * sizeof (grub_uint16_t);
  pathptr = &filep->path_name[path16_len + 1];
  endp = pathptr;
  endp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
  endp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
  endp->length = sizeof (*endp);
  pathptr = endp + 1;

  entry[4] = (grub_uint8_t *) pathptr - path;
  entry[5] = ((grub_uint8_t *) pathptr - path) >> 8;

  new_boot_order = xmalloc ((boot_order_len > 0 ? boot_order_len : 0) + 2);
  new_boot_order[0] = order_num;
  new_boot_order_len = 1;
  {
    ssize_t i;
    for (i = 0; i < boot_order_len / 2; i++)
      if (boot_order[i] != order_num)
	new_boot_order[new_boot_order_len++] = boot_order[i];
  }

  set_efi_variable_bootn (order_num, entry, (grub_uint8_t *) pathptr - entry);
  set_efi_variable (L"BootOrder", new_boot_order, new_boot_order_len * sizeof (grub_uint16_t));

  return 0;
}

void
grub_install_register_ieee1275 (int is_prep, const char *install_device,
				int partno, const char *relpath)
{
  grub_util_error ("%s", _("no IEEE1275 routines are available for your platform"));
}

void
grub_install_sgi_setup (const char *install_device,
			const char *imgfile, const char *destname)
{
  grub_util_error ("%s", _("no SGI routines are available for your platform"));
}

#if __GNUC__ >= 9
#pragma GCC diagnostic pop
#endif
