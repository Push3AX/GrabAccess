/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
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

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/partition.h>
#include <grub/net.h>
#include <grub/fs.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/env.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/msdos_partition.h>
#include <grub/gpt_partition.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
  {
    {"set",             's', 0,
     N_("Set a variable to return value."), N_("VARNAME"), ARG_TYPE_STRING},
    /* TRANSLATORS: It's a driver that is currently in use to access
       the diven disk.  */
    {"driver",		'd', 0, N_("Determine driver."), 0, 0},
    {"partmap",		'p', 0, N_("Determine partition map type."), 0, 0},
    {"fs",		'f', 0, N_("Determine filesystem type."), 0, 0},
    {"fs-uuid",		'u', 0, N_("Determine filesystem UUID."), 0, 0},
    {"label",		'l', 0, N_("Determine filesystem label."), 0, 0},
    {"partuuid",       'g', 0, N_("Determine partition UUID."), 0, 0}, 
    {"bootable",	'b', 0, N_("Determine if bootable / active flag is set."), 0, 0},
    {"quiet",	'q', 0, N_("Don't print error."), 0, 0},
    {0, 0, 0, 0, 0, 0}
  };

enum options
{
    PROBE_SET,
    PROBE_DRIVER,
    PROBE_PARTMAP,
    PROBE_FS,
    PROBE_FSUUID,
    PROBE_LABEL,
    PROBE_PARTUUID,
    PROBE_BOOTABLE,
    PROBE_QUIET,
};

static grub_err_t
grub_cmd_probe (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  grub_device_t dev;
  grub_fs_t fs;
  char *ptr;
  grub_err_t err;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "device name required");

  ptr = args[0] + grub_strlen (args[0]) - 1;
  if (args[0][0] == '(' && *ptr == ')')
    {
      *ptr = 0;
      dev = grub_device_open (args[0] + 1);
      *ptr = ')';
    }
  else
    dev = grub_device_open (args[0]);
  if (! dev)
    return grub_errno;

  if (state[PROBE_DRIVER].set)
    {
      const char *val = "none";
      if (dev->net)
	val = dev->net->protocol->name;
      if (dev->disk)
	val = dev->disk->dev->name;
      if (state[PROBE_SET].set)
	grub_env_set (state[PROBE_SET].arg, val);
      else
	grub_printf ("%s", val);
      grub_device_close (dev);
      return GRUB_ERR_NONE;
    }
  if (state[PROBE_PARTMAP].set)
    {
      const char *val = "none";
      if (dev->disk && dev->disk->partition)
	val = dev->disk->partition->partmap->name;
      if (state[PROBE_SET].set)
	grub_env_set (state[PROBE_SET].arg, val);
      else
	grub_printf ("%s", val);
      grub_device_close (dev);
      return GRUB_ERR_NONE;
    }
  fs = grub_fs_probe (dev);
  if (! fs)
  {
    grub_device_close (dev);
    return grub_errno;
  }
  if (state[PROBE_FS].set)
    {
      if (state[PROBE_SET].set)
	grub_env_set (state[PROBE_SET].arg, fs->name);
      else
	grub_printf ("%s", fs->name);
      grub_device_close (dev);
      return GRUB_ERR_NONE;
    }
  if (state[PROBE_FSUUID].set)
    {
      char *uuid;
      if (! fs->fs_uuid)
        {
          if (state[PROBE_QUIET].set)
	        return GRUB_ERR_NONE;
	      else
            return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
			               N_("%s does not support UUIDs"), fs->name);
        }
      err = fs->fs_uuid (dev, &uuid);
      if (err)
      {
        grub_device_close (dev);
        return err;
      }
      if (! uuid)
      {
        grub_device_close (dev);
        if (state[PROBE_QUIET].set)
          return GRUB_ERR_NONE;
        else
          return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
                             N_("%s does not support UUIDs"), fs->name);
      }

      if (state[PROBE_SET].set)
        grub_env_set (state[PROBE_SET].arg, uuid);
      else
        grub_printf ("%s", uuid);
      grub_free (uuid);
      grub_device_close (dev);
      return GRUB_ERR_NONE;
    }
  if (state[PROBE_LABEL].set)
    {
      char *label;
      if (! fs->fs_label)
      {
        grub_device_close (dev);
        if (state[PROBE_QUIET].set)
          return GRUB_ERR_NONE;
        else
          return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
                             N_("filesystem `%s' does not support labels"),
                             fs->name);
      }
      err = fs->fs_label (dev, &label);
      if (err)
      {
        grub_device_close (dev);
        return err;
      }
      if (! label)
      {
        grub_device_close (dev);
        if (state[PROBE_QUIET].set)
          return GRUB_ERR_NONE;
        else
          return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
                             N_("filesystem `%s' does not support labels"),
                             fs->name);
      }

      if (state[PROBE_SET].set)
	grub_env_set (state[PROBE_SET].arg, label);
      else
	grub_printf ("%s", label);
      grub_free (label);
      grub_device_close (dev);
      return GRUB_ERR_NONE;
    }
  if (state[PROBE_PARTUUID].set)
    {
      char *partuuid = NULL; /* NULL to silence a spurious GCC warning */
      grub_uint8_t diskbuf[16];
      if (dev->disk && dev->disk->partition)
       {
         grub_partition_t p = dev->disk->partition;
         if (!grub_strcmp (p->partmap->name, "msdos"))
           {
             const int diskid_offset = 440; /* location in MBR */
             dev->disk->partition = p->parent;
             /* little-endian 4-byte NT disk signature */
             err = grub_disk_read (dev->disk, 0, diskid_offset, 4, diskbuf);
             dev->disk->partition = p;
             if (err)
             {
               grub_device_close (dev);
               return grub_errno;
             }
             partuuid = grub_xasprintf ("%02x%02x%02x%02x-%02x",
                                        diskbuf[3], diskbuf[2], diskbuf[1], diskbuf[0],
                                        p->number + 1); /* one based partition number */
           }
         else if (!grub_strcmp (p->partmap->name, "gpt"))
           {
             const int guid_offset = 16; /* location in entry */
             dev->disk->partition = p->parent;
             /* little-endian 16-byte EFI partition GUID */
             err = grub_disk_read (dev->disk, p->offset, p->index + guid_offset, 16, diskbuf);
             dev->disk->partition = p;
             if (err)
             {
               grub_device_close (dev);
               return grub_errno;
             }
             partuuid = grub_xasprintf ("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                                        diskbuf[3], diskbuf[2], diskbuf[1], diskbuf[0],
                                        diskbuf[5], diskbuf[4],
                                        diskbuf[7], diskbuf[6],
                                        diskbuf[8], diskbuf[9],
                                        diskbuf[10], diskbuf[11], diskbuf[12], diskbuf[13], diskbuf[14], diskbuf[15]);
           }
         else
           {
             grub_device_close (dev);
             return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
                        N_("partition map %s does not support partition UUIDs"),
                        dev->disk->partition->partmap->name);
           }
       }
      else
       partuuid = grub_strdup (""); /* a freeable empty string */

      if (state[PROBE_SET].set)
       grub_env_set (state[PROBE_SET].arg, partuuid);
      else
       grub_printf ("%s", partuuid);
      grub_free (partuuid);
      grub_device_close (dev);
      return GRUB_ERR_NONE;
    }
  if (state[PROBE_BOOTABLE].set)
    {
      const char *val = "none";
      if (dev->disk && dev->disk->partition &&
          dev->disk->partition->msdostype != GRUB_PC_PARTITION_TYPE_GPT_DISK &&
          grub_strcmp (dev->disk->partition->partmap->name, "msdos") == 0)
      {
        if (dev->disk->partition->flag & 0x80)
          val = "bootable";
      }
      else if (dev->disk && dev->disk->partition &&
               grub_strcmp (dev->disk->partition->partmap->name, "gpt") == 0)
      {
        grub_packed_guid_t EFI_GUID = GRUB_GPT_PARTITION_TYPE_EFI_SYSTEM;
        if (grub_memcmp (&dev->disk->partition->gpttype,
                         &EFI_GUID, sizeof (grub_packed_guid_t)) == 0)
          val = "bootable";
      }
      if (state[PROBE_SET].set)
        grub_env_set (state[PROBE_SET].arg, val);
      else
        grub_printf ("%s", val);
      grub_device_close (dev);
      return GRUB_ERR_NONE;
    }
  grub_device_close (dev);
  return grub_error (GRUB_ERR_BAD_ARGUMENT, "unrecognised target");
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT (probe)
{
  cmd = grub_register_extcmd ("probe", grub_cmd_probe, 0, N_("DEVICE"),
			      N_("Retrieve device info."), options);
}

GRUB_MOD_FINI (probe)
{
  grub_unregister_extcmd (cmd);
}
