/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013 Free Software Foundation, Inc.
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

#include <grub/util/install.h>
#include <grub/emu/exec.h>
#include <grub/emu/misc.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

#include <sys/utsname.h>

static int
is_not_empty_directory (const char *dir)
{
  DIR *d;
  struct dirent *de;

  d = opendir (dir);
  if (!d)
    return 0;
  while ((de = readdir (d)))
    {
      if (strcmp (de->d_name, ".") == 0
	  || strcmp (de->d_name, "..") == 0)
	continue;
      closedir (d);
      return 1;
    }

  closedir (d);
  return 0;
}

static int
is_64_kernel (void)
{
  struct utsname un;

  if (uname (&un) < 0)
    return 0;

  return strcmp (un.machine, "x86_64") == 0;
}

static int
read_platform_size (void)
{
  FILE *fp;
  char *buf = NULL;
  size_t len = 0;
  int ret = 0;

  /* Newer kernels can tell us directly about the size of the
   * underlying firmware - let's see if that interface is there. */
  fp = grub_util_fopen ("/sys/firmware/efi/fw_platform_size", "r");
  if (fp != NULL)
  {
    if (getline (&buf, &len, fp) >= 3) /* 2 digits plus newline */
      {
	if (strncmp (buf, "32", 2) == 0)
	  ret = 32;
	else if (strncmp (buf, "64", 2) == 0)
	  ret = 64;
      }
    free (buf);
    fclose (fp);
  }

  if (ret == 0)
    {
      /* Unrecognised - fall back to matching the kernel size
       * instead */
      if (is_64_kernel ())
	ret = 64;
      else
	ret = 32;
    }

  return ret;
}

/* Are we running on an EFI-based system? */
static int
is_efi_system (void)
{
  /*
   * Linux uses efivarfs (mounted on /sys/firmware/efi/efivars) to access the
   * EFI variable store. Some legacy systems may still use the deprecated
   * efivars interface (accessed through /sys/firmware/efi/vars). Where both
   * are present, libefivar will use the former in preference, so attempting
   * to load efivars will not interfere with later operations.
   */
  grub_util_exec_redirect_all ((const char * []){ "modprobe", "efivars", NULL },
			       NULL, NULL, "/dev/null");

  grub_util_info ("Looking for /sys/firmware/efi ..");
  if (is_not_empty_directory ("/sys/firmware/efi"))
    {
      grub_util_info ("...found");
      return 1;
    }
  else
    {
      grub_util_info ("... not found");
      return 0;
    }
}

const char *
grub_install_get_default_arm_platform (void)
{
  if (is_efi_system())
    return "arm-efi";
  else
    return "arm-uboot";
}

const char *
grub_install_get_default_x86_platform (void)
{
  if (is_efi_system())
    {
      if (read_platform_size() == 64)
	return "x86_64-efi";
      else
	return "i386-efi";
    }

  grub_util_info ("Looking for /proc/device-tree ..");
  if (is_not_empty_directory ("/proc/device-tree"))
    {
      grub_util_info ("...found");
      return "i386-ieee1275";
    }

  grub_util_info ("... not found");
  return "i386-pc";
}
