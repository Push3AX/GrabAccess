/* grub-probe.c - probe device information for a given path */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2006,2007,2008,2009,2010  Free Software Foundation, Inc.
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
#include <grub/types.h>
#include <grub/emu/misc.h>
#include <grub/util/misc.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/file.h>
#include <grub/fs.h>
#include <grub/partition.h>
#include <grub/msdos_partition.h>
#include <grub/gpt_partition.h>
#include <grub/i386/pc/boot.h>
#include <grub/emu/hostdisk.h>
#include <grub/emu/getroot.h>
#include <grub/term.h>
#include <grub/env.h>
#include <grub/diskfilter.h>
#include <grub/i18n.h>
#include <grub/emu/misc.h>
#include <grub/util/ofpath.h>
#include <grub/crypto.h>
#include <grub/cryptodisk.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define _GNU_SOURCE	1

#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#include <argp.h>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wmissing-declarations"

#include "progname.h"

enum {
  PRINT_FS,
  PRINT_FS_UUID,
  PRINT_FS_LABEL,
  PRINT_DRIVE,
  PRINT_DEVICE,
  PRINT_PARTMAP,
  PRINT_PARTUUID,
  PRINT_ABSTRACTION,
  PRINT_CRYPTODISK_UUID,
  PRINT_HINT_STR,
  PRINT_BIOS_HINT,
  PRINT_IEEE1275_HINT,
  PRINT_BAREMETAL_HINT,
  PRINT_EFI_HINT,
  PRINT_ARC_HINT,
  PRINT_COMPATIBILITY_HINT,
  PRINT_MSDOS_PARTTYPE,
  PRINT_GPT_PARTTYPE,
  PRINT_ZERO_CHECK,
  PRINT_DISK
};

static const char *targets[] =
  {
    [PRINT_FS]                 = "fs",
    [PRINT_FS_UUID]            = "fs_uuid",
    [PRINT_FS_LABEL]           = "fs_label",
    [PRINT_DRIVE]              = "drive",
    [PRINT_DEVICE]             = "device",
    [PRINT_PARTMAP]            = "partmap",
    [PRINT_PARTUUID]           = "partuuid",
    [PRINT_ABSTRACTION]        = "abstraction",
    [PRINT_CRYPTODISK_UUID]    = "cryptodisk_uuid",
    [PRINT_HINT_STR]           = "hints_string",
    [PRINT_BIOS_HINT]          = "bios_hints",
    [PRINT_IEEE1275_HINT]      = "ieee1275_hints",
    [PRINT_BAREMETAL_HINT]     = "baremetal_hints",
    [PRINT_EFI_HINT]           = "efi_hints",
    [PRINT_ARC_HINT]           = "arc_hints",
    [PRINT_COMPATIBILITY_HINT] = "compatibility_hint",
    [PRINT_MSDOS_PARTTYPE]     = "msdos_parttype",
    [PRINT_GPT_PARTTYPE]       = "gpt_parttype",
    [PRINT_ZERO_CHECK]         = "zero_check",
    [PRINT_DISK]               = "disk",
  };

static int print = PRINT_FS;
static unsigned int argument_is_device = 0;

static char *
get_targets_string (void)
{
  char **arr = xmalloc (sizeof (targets));
  int len = 0;
  char *str;
  char *ptr;
  unsigned i;

  memcpy (arr, targets, sizeof (targets));
  qsort (arr, ARRAY_SIZE (targets), sizeof (char *), grub_qsort_strcmp);
  for (i = 0; i < ARRAY_SIZE (targets); i++)
    len += grub_strlen (targets[i]) + 2;
  ptr = str = xmalloc (len);
  for (i = 0; i < ARRAY_SIZE (targets); i++)
    {
      ptr = grub_stpcpy (ptr, arr[i]);
      *ptr++ = ',';
      *ptr++ = ' ';
    }
  ptr[-2] = '\0';
  free (arr);

  return str;
}

static int
print_gpt_guid (grub_gpt_part_guid_t guid)
{
  guid.data1 = grub_le_to_cpu32 (guid.data1);
  guid.data2 = grub_le_to_cpu16 (guid.data2);
  guid.data3 = grub_le_to_cpu16 (guid.data3);

  return grub_printf ("%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		      guid.data1, guid.data2, guid.data3, guid.data4[0],
		      guid.data4[1], guid.data4[2], guid.data4[3],
		      guid.data4[4], guid.data4[5], guid.data4[6],
		      guid.data4[7]);
}

static void
do_print (const char *x, void *data)
{
  char delim = *(const char *) data;
  grub_printf ("%s%c", x, delim);
}

static void
probe_partmap (grub_disk_t disk, char delim)
{
  grub_partition_t part;
  grub_disk_memberlist_t list = NULL, tmp;

  if (disk->partition == NULL)
    {
      grub_util_info ("no partition map found for %s", disk->name);
    }

  for (part = disk->partition; part; part = part->parent)
    printf ("%s%c", part->partmap->name, delim);

  if (disk->dev->id == GRUB_DISK_DEVICE_DISKFILTER_ID)
    grub_diskfilter_get_partmap (disk, do_print, &delim);

  /* In case of LVM/RAID, check the member devices as well.  */
  if (disk->dev->disk_memberlist)
    {
      list = disk->dev->disk_memberlist (disk);
    }
  while (list)
    {
      probe_partmap (list->disk, delim);
      tmp = list->next;
      free (list);
      list = tmp;
    }
}

static void
probe_partuuid (grub_disk_t disk, char delim)
{
  grub_partition_t p = disk->partition;

  /*
   * Nested partitions not supported for now.
   * Non-nested partitions must have disk->partition->parent == NULL
   */
  if (p && p->parent == NULL)
    {
      disk->partition = p->parent;

      if (strcmp(p->partmap->name, "msdos") == 0)
	{
	    /*
	     * The partition GUID for MSDOS is the partition number (starting
	     * with 1) prepended with the NT disk signature.
	     */
	    grub_uint32_t nt_disk_sig;

	    if (grub_disk_read (disk, 0, GRUB_BOOT_MACHINE_WINDOWS_NT_MAGIC,
				sizeof(nt_disk_sig), &nt_disk_sig) == 0)
	      grub_printf ("%08x-%02x",
			   grub_le_to_cpu32(nt_disk_sig), 1 + p->number);
	}
      else if (strcmp(p->partmap->name, "gpt") == 0)
	{
	  struct grub_gpt_partentry gptdata;

	  if (grub_disk_read (disk, p->offset, p->index,
			      sizeof(gptdata), &gptdata) == 0)
	    print_gpt_guid(gptdata.guid);
	}

      disk->partition = p;
    }
}

static void
probe_cryptodisk_uuid (grub_disk_t disk, char delim)
{
  grub_disk_memberlist_t list = NULL, tmp;

  /* In case of LVM/RAID, check the member devices as well.  */
  if (disk->dev->disk_memberlist)
    {
      list = disk->dev->disk_memberlist (disk);
    }
  while (list)
    {
      probe_cryptodisk_uuid (list->disk, delim);
      tmp = list->next;
      free (list);
      list = tmp;
    }
  if (disk->dev->id == GRUB_DISK_DEVICE_CRYPTODISK_ID)
    {
      const char *uu = grub_util_cryptodisk_get_uuid (disk);
      grub_printf ("%s%c", uu, delim);
    }
}

static int
probe_raid_level (grub_disk_t disk)
{
  /* disk might be NULL in the case of a LVM physical volume with no LVM
     signature.  Ignore such cases here.  */
  if (!disk)
    return -1;

  if (disk->dev->id != GRUB_DISK_DEVICE_DISKFILTER_ID)
    return -1;

  if (disk->name[0] != 'm' || disk->name[1] != 'd')
    return -1;

  if (!((struct grub_diskfilter_lv *) disk->data)->segments)
    return -1;
  return ((struct grub_diskfilter_lv *) disk->data)->segments->type;
}

static void
probe_abstraction (grub_disk_t disk, char delim)
{
  grub_disk_memberlist_t list = NULL, tmp;
  int raid_level;

  if (disk->dev->disk_memberlist)
    list = disk->dev->disk_memberlist (disk);
  while (list)
    {
      probe_abstraction (list->disk, delim);

      tmp = list->next;
      free (list);
      list = tmp;
    }

  if (disk->dev->id == GRUB_DISK_DEVICE_DISKFILTER_ID
      && (grub_memcmp (disk->name, "lvm/", sizeof ("lvm/") - 1) == 0 ||
	  grub_memcmp (disk->name, "lvmid/", sizeof ("lvmid/") - 1) == 0))
    printf ("lvm%c", delim);

  if (disk->dev->id == GRUB_DISK_DEVICE_DISKFILTER_ID
      && grub_memcmp (disk->name, "ldm/", sizeof ("ldm/") - 1) == 0)
    printf ("ldm%c", delim);

  if (disk->dev->id == GRUB_DISK_DEVICE_CRYPTODISK_ID)
    grub_util_cryptodisk_get_abstraction (disk, do_print, &delim);

  raid_level = probe_raid_level (disk);
  if (raid_level >= 0)
    {
      printf ("diskfilter%c", delim);
      if (disk->dev->disk_raidname)
	printf ("%s%c", disk->dev->disk_raidname (disk), delim);
    }
  if (raid_level == 5)
    printf ("raid5rec%c", delim);
  if (raid_level == 6)
    printf ("raid6rec%c", delim);
}

static void
probe (const char *path, char **device_names, char delim)
{
  char **drives_names = NULL;
  char **curdev, **curdrive;
  char *grub_path = NULL;
  int ndev = 0;

  if (path != NULL)
    {
      grub_path = grub_canonicalize_file_name (path);
      if (! grub_path)
	grub_util_error (_("failed to get canonical path of `%s'"), path);
      device_names = grub_guess_root_devices (grub_path);
      free (grub_path);
    }

  if (! device_names)
    grub_util_error (_("cannot find a device for %s (is /dev mounted?)"), path);

  if (print == PRINT_DEVICE)
    {
      for (curdev = device_names; *curdev; curdev++)
	{
	  printf ("%s", *curdev);
	  putchar (delim);
	}
      goto free_device_names;
    }

  if (print == PRINT_DISK)
    {
      for (curdev = device_names; *curdev; curdev++)
	{
	  char *disk;
	  disk = grub_util_get_os_disk (*curdev);
	  if (!disk)
	    {
	      grub_print_error ();
	      continue;
	    }
	  printf ("%s", disk);
	  putchar (delim);
	  free (disk);
	}
      goto free_device_names;
    }

  for (curdev = device_names; *curdev; curdev++)
    {
      grub_util_pull_device (*curdev);
      ndev++;
    }

  drives_names = xcalloc (ndev + 1, sizeof (drives_names[0]));

  for (curdev = device_names, curdrive = drives_names; *curdev; curdev++,
       curdrive++)
    {
      *curdrive = grub_util_get_grub_dev (*curdev);
      if (! *curdrive)
	grub_util_error (_("cannot find a GRUB drive for %s.  Check your device.map"),
			 *curdev);
    }
  *curdrive = 0;

  if (print == PRINT_DRIVE)
    {
      for (curdrive = drives_names; *curdrive; curdrive++)
	{
	  printf ("(%s)", *curdrive);
	  putchar (delim);
	}
      goto end;
    }

  if (print == PRINT_ZERO_CHECK)
    {
      for (curdev = drives_names; *curdev; curdev++)
	{
	  grub_device_t dev = NULL;
	  grub_uint32_t buffer[32768];
	  grub_disk_addr_t addr;
	  grub_disk_addr_t dsize;

	  grub_util_info ("opening %s", *curdev);
	  dev = grub_device_open (*curdev);
	  if (! dev || !dev->disk)
	    grub_util_error ("%s", grub_errmsg);

	  dsize = grub_disk_native_sectors (dev->disk);
	  for (addr = 0; addr < dsize;
	       addr += sizeof (buffer) / GRUB_DISK_SECTOR_SIZE)
	    {
	      grub_size_t sz = sizeof (buffer);
	      grub_uint32_t *ptr;

	      if (sizeof (buffer) / GRUB_DISK_SECTOR_SIZE > dsize - addr)
		sz = (dsize - addr) * GRUB_DISK_SECTOR_SIZE;
	      grub_disk_read (dev->disk, addr, 0, sz, buffer);

	      for (ptr = buffer; ptr < buffer + sz / sizeof (*buffer); ptr++)
		if (*ptr)
		  {
		    grub_printf ("false\n");
		    grub_device_close (dev);
		    goto end;
		  }
	    }

	  grub_device_close (dev);
	}
      grub_printf ("true\n");
    }

  if (print == PRINT_FS || print == PRINT_FS_UUID
      || print == PRINT_FS_LABEL)
    {
      grub_device_t dev = NULL;
      grub_fs_t fs;

      grub_util_info ("opening %s", drives_names[0]);
      dev = grub_device_open (drives_names[0]);
      if (! dev)
	grub_util_error ("%s", grub_errmsg);
      
      fs = grub_fs_probe (dev);
      if (! fs)
	grub_util_error ("%s", grub_errmsg);

      if (print == PRINT_FS)
	{
	  printf ("%s", fs->name);
	  putchar (delim);
	}
      else if (print == PRINT_FS_UUID)
	{
	  char *uuid;
	  if (! fs->fs_uuid)
	    grub_util_error (_("%s does not support UUIDs"), fs->name);

	  if (fs->fs_uuid (dev, &uuid) != GRUB_ERR_NONE)
	    grub_util_error ("%s", grub_errmsg);

	  printf ("%s", uuid);
	  putchar (delim);
	}
      else if (print == PRINT_FS_LABEL)
	{
	  char *label;
	  if (! fs->fs_label)
	    grub_util_error (_("filesystem `%s' does not support labels"),
			     fs->name);

	  if (fs->fs_label (dev, &label) != GRUB_ERR_NONE)
	    grub_util_error ("%s", grub_errmsg);

	  printf ("%s", label);
	  putchar (delim);
	}
      grub_device_close (dev);
      goto end;
    }

  for (curdrive = drives_names, curdev = device_names; *curdrive;
       curdrive++, curdev++)
    {
      grub_device_t dev = NULL;

      grub_util_info ("opening %s", *curdrive);
      dev = grub_device_open (*curdrive);
      if (! dev)
	grub_util_error ("%s", grub_errmsg);

      if (print == PRINT_HINT_STR)
	{
	  const char *osdev = grub_util_biosdisk_get_osdev (dev->disk);
	  char *ofpath = osdev ? grub_util_devname_to_ofpath (osdev) : 0;
	  char *biosname, *bare, *efi;
	  const char *map;

	  if (ofpath)
	    {
	      char *tmp = xmalloc (strlen (ofpath) + sizeof ("ieee1275/"));
	      char *p;
	      p = grub_stpcpy (tmp, "ieee1275/");
	      strcpy (p, ofpath);
	      printf ("--hint-ieee1275='");
	      grub_util_fprint_full_disk_name (stdout, tmp, dev);
	      printf ("' ");
	      free (tmp);
	      free (ofpath);
	    }

	  biosname = grub_util_guess_bios_drive (*curdev);
	  if (biosname)
	    {
	      printf ("--hint-bios=");
	      grub_util_fprint_full_disk_name (stdout, biosname, dev);
	      printf (" ");
	    }
	  free (biosname);

	  efi = grub_util_guess_efi_drive (*curdev);
	  if (efi)
	    {
	      printf ("--hint-efi=");
	      grub_util_fprint_full_disk_name (stdout, efi, dev);
	      printf (" ");
	    }
	  free (efi);

	  bare = grub_util_guess_baremetal_drive (*curdev);
	  if (bare)
	    {
	      printf ("--hint-baremetal=");
	      grub_util_fprint_full_disk_name (stdout, bare, dev);
	      printf (" ");
	    }
	  free (bare);

	  /* FIXME: Add ARC hint.  */

	  map = grub_util_biosdisk_get_compatibility_hint (dev->disk);
	  if (map)
	    {
	      printf ("--hint='");
	      grub_util_fprint_full_disk_name (stdout, map, dev);
	      printf ("' ");
	    }
	  if (curdrive[1])
	    printf (" ");
	  else
	    printf ("\n");
	}
      
      else if ((print == PRINT_COMPATIBILITY_HINT || print == PRINT_BIOS_HINT
	   || print == PRINT_IEEE1275_HINT || print == PRINT_BAREMETAL_HINT
	   || print == PRINT_EFI_HINT || print == PRINT_ARC_HINT)
	  && dev->disk->dev->id != GRUB_DISK_DEVICE_HOSTDISK_ID)
	{
	  grub_util_fprint_full_disk_name (stdout, dev->disk->name, dev);
	  putchar (delim);
	}

      else if (print == PRINT_COMPATIBILITY_HINT)
	{
	  const char *map;
	  char *biosname;
	  map = grub_util_biosdisk_get_compatibility_hint (dev->disk);
	  if (map)
	    {
	      grub_util_fprint_full_disk_name (stdout, map, dev);
	      putchar (delim);
	      grub_device_close (dev);
	      /* Compatibility hint is one device only.  */
	      break;
	    }
	  biosname = grub_util_guess_bios_drive (*curdev);
	  if (biosname)
	    {
	      grub_util_fprint_full_disk_name (stdout, biosname, dev);
	      putchar (delim);
	      free (biosname);
	      /* Compatibility hint is one device only.  */
	      grub_device_close (dev);
	      break;
	    }
	}

      else if (print == PRINT_BIOS_HINT)
	{
	  char *biosname;
	  biosname = grub_util_guess_bios_drive (*curdev);
	  if (biosname)
	    {
	      grub_util_fprint_full_disk_name (stdout, biosname, dev);
	      putchar (delim);
	      free (biosname);
	    }
	}
      else if (print == PRINT_IEEE1275_HINT)
	{
	  const char *osdev = grub_util_biosdisk_get_osdev (dev->disk);
	  char *ofpath = grub_util_devname_to_ofpath (osdev);
	  const char *map;

	  map = grub_util_biosdisk_get_compatibility_hint (dev->disk);
	  if (map)
	    {
	      grub_util_fprint_full_disk_name (stdout, map, dev);
	      putchar (delim);
	    }

	  if (ofpath)
	    {
	      char *tmp = xmalloc (strlen (ofpath) + sizeof ("ieee1275/"));
	      char *p;
	      p = grub_stpcpy (tmp, "ieee1275/");
	      strcpy (p, ofpath);
	      grub_util_fprint_full_disk_name (stdout, tmp, dev);
	      free (tmp);
	      free (ofpath);
	      putchar (delim);
	    }
	}
      else if (print == PRINT_EFI_HINT)
	{
	  char *biosname;
	  const char *map;
	  biosname = grub_util_guess_efi_drive (*curdev);

	  map = grub_util_biosdisk_get_compatibility_hint (dev->disk);
	  if (map)
	    {
	      grub_util_fprint_full_disk_name (stdout, map, dev);
	      putchar (delim);
	    }
	  if (biosname)
	    {
	      grub_util_fprint_full_disk_name (stdout, biosname, dev);
	      putchar (delim);
	      free (biosname);
	    }
	}

      else if (print == PRINT_BAREMETAL_HINT)
	{
	  char *biosname;
	  const char *map;

	  biosname = grub_util_guess_baremetal_drive (*curdev);

	  map = grub_util_biosdisk_get_compatibility_hint (dev->disk);
	  if (map)
	    {
	      grub_util_fprint_full_disk_name (stdout, map, dev);
	      putchar (delim);
	    }
	  if (biosname)
	    {
	      grub_util_fprint_full_disk_name (stdout, biosname, dev);
	      putchar (delim);
	      free (biosname);
	    }
	}

      else if (print == PRINT_ARC_HINT)
	{
	  const char *map;

	  map = grub_util_biosdisk_get_compatibility_hint (dev->disk);
	  if (map)
	    {
	      grub_util_fprint_full_disk_name (stdout, map, dev);
	      putchar (delim);
	    }
	}

      else if (print == PRINT_ABSTRACTION)
	probe_abstraction (dev->disk, delim);

      else if (print == PRINT_CRYPTODISK_UUID)
	probe_cryptodisk_uuid (dev->disk, delim);

      else if (print == PRINT_PARTMAP)
	/* Check if dev->disk itself is contained in a partmap.  */
	probe_partmap (dev->disk, delim);

      else if (print == PRINT_PARTUUID)
	{
	  probe_partuuid (dev->disk, delim);
	  putchar (delim);
	}

      else if (print == PRINT_MSDOS_PARTTYPE)
	{
	  if (dev->disk->partition
	      && strcmp(dev->disk->partition->partmap->name, "msdos") == 0)
	    printf ("%02x", dev->disk->partition->msdostype);

	  putchar (delim);
	}

      else if (print == PRINT_GPT_PARTTYPE)
	{
          if (dev->disk->partition
	      && strcmp (dev->disk->partition->partmap->name, "gpt") == 0)
            {
              struct grub_gpt_partentry gptdata;
              grub_partition_t p = dev->disk->partition;
              dev->disk->partition = dev->disk->partition->parent;

              if (grub_disk_read (dev->disk, p->offset, p->index,
                                  sizeof (gptdata), &gptdata) == 0)
		print_gpt_guid(gptdata.type);
              dev->disk->partition = p;
            }
          putchar (delim);
        }

      grub_device_close (dev);
    }

 end:
  for (curdrive = drives_names; *curdrive; curdrive++)
    free (*curdrive);
  free (drives_names);

free_device_names:
  if (path != NULL)
    {
      for (curdev = device_names; *curdev; curdev++)
	free (*curdev);
      free (device_names);
    }
}

static struct argp_option options[] = {
  {"device",  'd', 0, 0,
   N_("given argument is a system device, not a path"), 0},
  {"device-map",  'm', N_("FILE"), 0,
   N_("use FILE as the device map [default=%s]"), 0},
  {"target",  't', N_("TARGET"), 0, 0, 0},
  {"verbose",     'v', 0,      0, N_("print verbose messages."), 0},
  {0, '0', 0, 0, N_("separate items in output using ASCII NUL characters"), 0},
  { 0, 0, 0, 0, 0, 0 }
};

#pragma GCC diagnostic ignored "-Wformat-nonliteral"

static char *
help_filter (int key, const char *text, void *input __attribute__ ((unused)))
{
  switch (key)
    {
      case 'm':
        return xasprintf (text, DEFAULT_DEVICE_MAP);

      case 't':
	{
	  char *ret, *t = get_targets_string (), *def;

	  def = xasprintf (_("[default=%s]"), targets[print]);

	  ret = xasprintf ("%s\n%s %s %s", _("print TARGET"),
			    _("available targets:"), t, def);
	  free (t);
	  free (def);
	  return ret;
	}

      default:
        return (char *) text;
    }
}

#pragma GCC diagnostic error "-Wformat-nonliteral"

struct arguments
{
  char **devices;
  size_t device_max;
  size_t ndevices;
  char *dev_map;
  int zero_delim;
};

static error_t
argp_parser (int key, char *arg, struct argp_state *state)
{
  /* Get the input argument from argp_parse, which we
     know is a pointer to our arguments structure. */
  struct arguments *arguments = state->input;

  switch (key)
    {
    case 'd':
      argument_is_device = 1;
      break;

    case 'm':
      if (arguments->dev_map)
	free (arguments->dev_map);

      arguments->dev_map = xstrdup (arg);
      break;

    case 't':
      {
	int i;

	for (i = PRINT_FS; i < ARRAY_SIZE (targets); i++)
	  if (strcmp (arg, targets[i]) == 0)
	    {
	      print = i;
	      break;
	    }
	if (i == ARRAY_SIZE (targets))
	  argp_usage (state);
      }
      break;

    case '0':
      arguments->zero_delim = 1;
      break;

    case 'v':
      verbosity++;
      break;

    case ARGP_KEY_NO_ARGS:
      fprintf (stderr, "%s", _("No path or device is specified.\n"));
      argp_usage (state);
      break;

    case ARGP_KEY_ARG:
      assert (arguments->ndevices < arguments->device_max);
      arguments->devices[arguments->ndevices++] = xstrdup(arg);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static struct argp argp = {
  options, argp_parser, N_("[OPTION]... [PATH|DEVICE]"),
  N_("\
Probe device information for a given path (or device, if the -d option is given)."),
  NULL, help_filter, NULL
};

int
main (int argc, char *argv[])
{
  char delim;
  struct arguments arguments;

  grub_util_host_init (&argc, &argv);

  memset (&arguments, 0, sizeof (struct arguments));
  arguments.device_max = argc + 1;
  arguments.devices = xmalloc ((arguments.device_max + 1)
			       * sizeof (arguments.devices[0]));
  memset (arguments.devices, 0, (arguments.device_max + 1)
	  * sizeof (arguments.devices[0]));

  /* Parse our arguments */
  if (argp_parse (&argp, argc, argv, 0, 0, &arguments) != 0)
    {
      fprintf (stderr, "%s", _("Error in parsing command line arguments\n"));
      exit(1);
    }

  if (verbosity > 1)
    grub_env_set ("debug", "all");

  /* Obtain ARGUMENT.  */
  if (arguments.ndevices != 1 && !argument_is_device)
    {
      char *program = xstrdup(program_name);
      fprintf (stderr, _("Unknown extra argument `%s'."), arguments.devices[1]);
      fprintf (stderr, "\n");
      argp_help (&argp, stderr, ARGP_HELP_STD_USAGE, program);
      free (program);
      exit(1);
    }

  /* Initialize the emulated biosdisk driver.  */
  grub_util_biosdisk_init (arguments.dev_map ? : DEFAULT_DEVICE_MAP);

  /* Initialize all modules. */
  grub_init_all ();
  grub_gcry_init_all ();

  grub_lvm_fini ();
  grub_mdraid09_fini ();
  grub_mdraid1x_fini ();
  grub_diskfilter_fini ();
  grub_diskfilter_init ();
  grub_mdraid09_init ();
  grub_mdraid1x_init ();
  grub_lvm_init ();

  if (print == PRINT_BIOS_HINT
      || print == PRINT_IEEE1275_HINT || print == PRINT_BAREMETAL_HINT
      || print == PRINT_EFI_HINT || print == PRINT_ARC_HINT)
    delim = ' ';
  else
    delim = '\n';

  if (arguments.zero_delim)
    delim = '\0';

  /* Do it.  */
  if (argument_is_device)
    probe (NULL, arguments.devices, delim);
  else
    probe (arguments.devices[0], NULL, delim);

  if (delim == ' ')
    putchar ('\n');

  /* Free resources.  */
  grub_gcry_fini_all ();
  grub_fini_all ();
  grub_util_biosdisk_fini ();

  {
    size_t i;
    for (i = 0; i < arguments.ndevices; i++)
      free (arguments.devices[i]);
  }
  free (arguments.devices);

  free (arguments.dev_map);

  return 0;
}
