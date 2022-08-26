/* gptrepair.c - verify and restore GPT info from alternate location.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
 *  Copyright (C) 2014  CoreOS, Inc.
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

#include <grub/command.h>
#include <grub/device.h>
#include <grub/err.h>
#include <grub/gpt_partition.h>
#include <grub/i18n.h>
#include <grub/misc.h>

GRUB_MOD_LICENSE ("GPLv3+");

static char *
trim_dev_name (char *name)
{
  grub_size_t len = grub_strlen (name);
  if (len && name[0] == '(' && name[len - 1] == ')')
    {
      name[len - 1] = '\0';
      name = name + 1;
    }
  return name;
}

static grub_err_t
grub_cmd_gptrepair (grub_command_t cmd __attribute__ ((unused)),
		    int argc, char **args)
{
  grub_device_t dev = NULL;
  grub_gpt_t gpt = NULL;
  char *dev_name;

  if (argc != 1 || !grub_strlen(args[0]))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "device name required");

  dev_name = trim_dev_name (args[0]);
  dev = grub_device_open (dev_name);
  if (!dev)
    goto done;

  if (!dev->disk)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "not a disk");
      goto done;
    }

  gpt = grub_gpt_read (dev->disk);
  if (!gpt)
    goto done;

  if (grub_gpt_both_valid (gpt))
    {
      grub_printf_ (N_("GPT already valid, %s unmodified.\n"), dev_name);
      goto done;
    }

  if (!grub_gpt_primary_valid (gpt))
    grub_printf_ (N_("Found invalid primary GPT on %s\n"), dev_name);

  if (!grub_gpt_backup_valid (gpt))
    grub_printf_ (N_("Found invalid backup GPT on %s\n"), dev_name);

  if (grub_gpt_repair (dev->disk, gpt))
    goto done;

  if (grub_gpt_write (dev->disk, gpt))
    goto done;

  grub_printf_ (N_("Repaired GPT on %s\n"), dev_name);

done:
  if (gpt)
    grub_gpt_free (gpt);

  if (dev)
    grub_device_close (dev);

  return grub_errno;
}

static grub_command_t cmd;

GRUB_MOD_INIT(gptrepair)
{
  cmd = grub_register_command ("gptrepair", grub_cmd_gptrepair,
			       N_("DEVICE"),
			       N_("Verify and repair GPT on drive DEVICE."));
}

GRUB_MOD_FINI(gptrepair)
{
  grub_unregister_command (cmd);
}
