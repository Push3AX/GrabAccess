/* gptprio.c - manage priority based partition selection.  */
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

#include <grub/device.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/gpt_partition.h>
#include <grub/i18n.h>
#include <grub/misc.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options_next[] = {
  {"set-device", 'd', 0,
   N_("Set a variable to the name of selected partition."),
   N_("VARNAME"), ARG_TYPE_STRING},
  {"set-uuid", 'u', 0,
   N_("Set a variable to the GPT UUID of selected partition."),
   N_("VARNAME"), ARG_TYPE_STRING},
  {0, 0, 0, 0, 0, 0}
};

enum options_next
{
  NEXT_SET_DEVICE,
  NEXT_SET_UUID,
};

static unsigned int
grub_gptprio_priority (struct grub_gpt_partentry *entry)
{
  return (unsigned int) grub_gpt_entry_attribute
    (entry, GRUB_GPT_PART_ATTR_OFFSET_GPTPRIO_PRIORITY, 4);
}

static unsigned int
grub_gptprio_tries_left (struct grub_gpt_partentry *entry)
{
  return (unsigned int) grub_gpt_entry_attribute
    (entry, GRUB_GPT_PART_ATTR_OFFSET_GPTPRIO_TRIES_LEFT, 4);
}

static void
grub_gptprio_set_tries_left (struct grub_gpt_partentry *entry,
			     unsigned int tries_left)
{
  grub_gpt_entry_set_attribute
    (entry, tries_left, GRUB_GPT_PART_ATTR_OFFSET_GPTPRIO_TRIES_LEFT, 4);
}

static unsigned int
grub_gptprio_successful (struct grub_gpt_partentry *entry)
{
  return (unsigned int) grub_gpt_entry_attribute
    (entry, GRUB_GPT_PART_ATTR_OFFSET_GPTPRIO_SUCCESSFUL, 1);
}

static grub_err_t
grub_find_next (const char *disk_name,
		const grub_gpt_part_guid_t *part_type,
		char **part_name, char **part_guid)
{
  struct grub_gpt_partentry *part, *part_found = NULL;
  grub_device_t dev = NULL;
  grub_gpt_t gpt = NULL;
  grub_uint32_t i, part_index;

  dev = grub_device_open (disk_name);
  if (!dev)
    goto done;

  gpt = grub_gpt_read (dev->disk);
  if (!gpt)
    goto done;

  if (grub_gpt_repair (dev->disk, gpt))
    goto done;

  for (i = 0; (part = grub_gpt_get_partentry (gpt, i)) != NULL; i++)
    {
      if (grub_memcmp (part_type, &part->type, sizeof (*part_type)) == 0)
	{
	  unsigned int priority, tries_left, successful, old_priority = 0;

	  priority = grub_gptprio_priority (part);
	  tries_left = grub_gptprio_tries_left (part);
	  successful = grub_gptprio_successful (part);

	  if (part_found)
	    old_priority = grub_gptprio_priority (part_found);

	  if ((tries_left || successful) && priority > old_priority)
	    {
	      part_index = i;
	      part_found = part;
	    }
	}
    }

  if (!part_found)
    {
      grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("no such partition"));
      goto done;
    }

  if (grub_gptprio_tries_left (part_found))
    {
      unsigned int tries_left = grub_gptprio_tries_left (part_found);

      grub_gptprio_set_tries_left (part_found, tries_left - 1);

      if (grub_gpt_update (gpt))
	goto done;

      if (grub_gpt_write (dev->disk, gpt))
	goto done;
    }

  *part_name = grub_xasprintf ("%s,gpt%u", disk_name, part_index + 1);
  if (!*part_name)
    goto done;

  *part_guid = grub_gpt_guid_to_str (&part_found->guid);
  if (!*part_guid)
    goto done;

  grub_errno = GRUB_ERR_NONE;

done:
  grub_gpt_free (gpt);

  if (dev)
    grub_device_close (dev);

  return grub_errno;
}



static grub_err_t
grub_cmd_next (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  char *p, *root = NULL, *part_name = NULL, *part_guid = NULL;

  /* TODO: Add a uuid parser and a command line flag for providing type.  */
  grub_gpt_part_guid_t part_type = GRUB_GPT_PARTITION_TYPE_USR_X86_64;

  if (!state[NEXT_SET_DEVICE].set || !state[NEXT_SET_UUID].set)
    {
      grub_error (GRUB_ERR_INVALID_COMMAND, N_("-d and -u are required"));
      goto done;
    }

  if (argc == 0)
    root = grub_strdup (grub_env_get ("root"));
  else if (argc == 1)
    root = grub_strdup (args[0]);
  else
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, N_("unexpected arguments"));
      goto done;
    }

  if (!root)
    goto done;

  /* To make using $root practical strip off the partition name.  */
  p = grub_strchr (root, ',');
  if (p)
    *p = '\0';

  if (grub_find_next (root, &part_type, &part_name, &part_guid))
    goto done;

  if (grub_env_set (state[NEXT_SET_DEVICE].arg, part_name))
    goto done;

  if (grub_env_set (state[NEXT_SET_UUID].arg, part_guid))
    goto done;

  grub_errno = GRUB_ERR_NONE;

done:
  grub_free (root);
  grub_free (part_name);
  grub_free (part_guid);

  return grub_errno;
}

static grub_extcmd_t cmd_next;

GRUB_MOD_INIT(gptprio)
{
  cmd_next = grub_register_extcmd ("gptprio.next", grub_cmd_next, 0,
				   N_("-d VARNAME -u VARNAME [DEVICE]"),
				   N_("Select next partition to boot."),
				   options_next);
}

GRUB_MOD_FINI(gptprio)
{
  grub_unregister_extcmd (cmd_next);
}
