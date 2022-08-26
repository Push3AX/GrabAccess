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
#include <grub/emu/hostdisk.h>
#include <grub/util/misc.h>
#include <grub/misc.h>
#include <grub/i18n.h>
#include <grub/emu/exec.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

static char *
get_ofpathname (const char *dev)
{
  size_t alloced = 4096;
  char *ret = xmalloc (alloced);
  size_t offset = 0;
  int fd;
  pid_t pid;

  pid = grub_util_exec_pipe ((const char * []){ "ofpathname", dev, NULL }, &fd);
  if (!pid)
    goto fail;

  FILE *fp = fdopen (fd, "r");
  if (!fp)
    goto fail;

  while (!feof (fp))
    {
      size_t r;
      if (alloced == offset)
       {
         alloced *= 2;
         ret = xrealloc (ret, alloced);
       }
      r = fread (ret + offset, 1, alloced - offset, fp);
      offset += r;
    }

  if (offset > 0 && ret[offset - 1] == '\n')
    offset--;
  if (offset > 0 && ret[offset - 1] == '\r')
    offset--;
  if (alloced == offset)
    {
      alloced++;
      ret = xrealloc (ret, alloced);
    }
  ret[offset] = '\0';

  fclose (fp);

  return ret;

 fail:
  grub_util_error (_("couldn't find IEEE1275 device path for %s.\nYou will have to set `boot-device' variable manually"),
		   dev);
}

static int
grub_install_remove_efi_entries_by_distributor (const char *efi_distributor)
{
  int fd;
  pid_t pid = grub_util_exec_pipe ((const char * []){ "efibootmgr", NULL }, &fd);
  char *line = NULL;
  size_t len = 0;
  int rc = 0;

  if (!pid)
    {
      grub_util_warn (_("Unable to open stream from %s: %s"),
		      "efibootmgr", strerror (errno));
      return errno;
    }

  FILE *fp = fdopen (fd, "r");
  if (!fp)
    {
      grub_util_warn (_("Unable to open stream from %s: %s"),
		      "efibootmgr", strerror (errno));
      return errno;
    }

  line = xmalloc (80);
  len = 80;
  while (1)
    {
      int ret;
      char *bootnum;
      ret = getline (&line, &len, fp);
      if (ret == -1)
	break;
      if (grub_memcmp (line, "Boot", sizeof ("Boot") - 1) != 0
	  || line[sizeof ("Boot") - 1] < '0'
	  || line[sizeof ("Boot") - 1] > '9')
	continue;
      if (!strcasestr (line, efi_distributor))
	continue;
      bootnum = line + sizeof ("Boot") - 1;
      bootnum[4] = '\0';
      if (!verbosity)
	rc = grub_util_exec ((const char * []){ "efibootmgr", "-q",
	      "-b", bootnum,  "-B", NULL });
      else
	rc = grub_util_exec ((const char * []){ "efibootmgr",
	      "-b", bootnum, "-B", NULL });
    }

  free (line);
  return rc;
}

int
grub_install_register_efi (grub_device_t efidir_grub_dev,
			   const char *efifile_path,
			   const char *efi_distributor)
{
  const char * efidir_disk;
  int efidir_part;
  int ret;
  efidir_disk = grub_util_biosdisk_get_osdev (efidir_grub_dev->disk);
  efidir_part = efidir_grub_dev->disk->partition ? efidir_grub_dev->disk->partition->number + 1 : 1;

  if (grub_util_exec_redirect_null ((const char * []){ "efibootmgr", "--version", NULL }))
    {
      /* TRANSLATORS: This message is shown when required executable `%s'
	 isn't found.  */
      grub_util_error (_("%s: not found"), "efibootmgr");
    }

  /* On Linux, we need the efivars kernel modules.  */
#ifdef __linux__
  grub_util_exec ((const char * []){ "modprobe", "-q", "efivars", NULL });
#endif
  /* Delete old entries from the same distributor.  */
  ret = grub_install_remove_efi_entries_by_distributor (efi_distributor);
  if (ret)
    return ret;

  char *efidir_part_str = xasprintf ("%d", efidir_part);

  if (!verbosity)
    ret = grub_util_exec ((const char * []){ "efibootmgr", "-q",
	  "-c", "-d", efidir_disk,
	  "-p", efidir_part_str, "-w",
	  "-L", efi_distributor, "-l", 
	  efifile_path, NULL });
  else
    ret = grub_util_exec ((const char * []){ "efibootmgr",
	  "-c", "-d", efidir_disk,
	  "-p", efidir_part_str, "-w",
	  "-L", efi_distributor, "-l", 
	  efifile_path, NULL });
  free (efidir_part_str);
  return ret;
}

void
grub_install_register_ieee1275 (int is_prep, const char *install_device,
				int partno, const char *relpath)
{
  char *boot_device;

  if (grub_util_exec_redirect_null ((const char * []){ "ofpathname", "--version", NULL }))
    {
      /* TRANSLATORS: This message is shown when required executable `%s'
	 isn't found.  */
      grub_util_error (_("%s: not found"), "ofpathname");
    }

  /* Get the Open Firmware device tree path translation.  */
  if (!is_prep)
    {
      char *ptr;
      char *ofpath;
      const char *iptr;

      ofpath = get_ofpathname (install_device);
      boot_device = xmalloc (strlen (ofpath) + 1
			     + sizeof ("XXXXXXXXXXXXXXXXXXXX")
			     + 1 + strlen (relpath) + 1);
      ptr = grub_stpcpy (boot_device, ofpath);
      *ptr++ = ':';
      grub_snprintf (ptr, sizeof ("XXXXXXXXXXXXXXXXXXXX"), "%d",
		     partno);
      ptr += strlen (ptr);
      *ptr++ = ',';
      for (iptr = relpath; *iptr; iptr++, ptr++)
	{
	  if (*iptr == '/')
	    *ptr = '\\';
	  else
	    *ptr = *iptr;
	}
      *ptr = '\0';
    }
  else
    boot_device = get_ofpathname (install_device);

  if (grub_util_exec ((const char * []){ "nvsetenv", "boot-device",
	  boot_device, NULL }))
    {
      char *cmd = xasprintf ("setenv boot-device %s", boot_device);
      grub_util_error (_("`nvsetenv' failed. \nYou will have to set `boot-device' variable manually.  At the IEEE1275 prompt, type:\n  %s\n"),
		       cmd);
      free (cmd);
    }

  free (boot_device);
}

void
grub_install_sgi_setup (const char *install_device,
			const char *imgfile, const char *destname)
{
  grub_util_exec ((const char * []){ "dvhtool", "-d",
	install_device, "--unix-to-vh", 
	imgfile, destname, NULL });
  grub_util_warn ("%s", _("You will have to set `SystemPartition' and `OSLoader' manually."));
}
