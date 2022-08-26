/* search.c - search devices based on a file or a filesystem label */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2007,2008,2009  Free Software Foundation, Inc.
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
#include <grub/env.h>
#include <grub/extcmd.h>
#include <grub/search.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
  {
    {"file",		'f', 0, N_("Search devices by a file."), 0, 0},
    {"label",		'l', 0, N_("Search devices by a filesystem label."),
     0, 0},
    {"fs-uuid",		'u', 0, N_("Search devices by a filesystem UUID."),
     0, 0},
    {"part-label",	'L', 0, N_("Search devices by a partition label."),
     0, 0},
    {"part-uuid",	'g', 0, N_("Search devices by a partition UUID."),
     0, 0},
    {"disk-uuid",	'U', 0, N_("Search devices by a disk UUID."),
     0, 0},
    {"set",		's', GRUB_ARG_OPTION_OPTIONAL,
     N_("Set a variable to the first device found."), N_("VARNAME"),
     ARG_TYPE_STRING},
    {"no-floppy",	'n', 0, N_("Do not probe any floppy drive."), 0, 0},
    {"quiet",		'q', 0, N_("Don't print error if no match."), 0, 0},
    {"hint",	        'h', GRUB_ARG_OPTION_REPEATABLE,
     N_("First try the device HINT. If HINT ends in comma, "
	"also try subpartitions"), N_("HINT"), ARG_TYPE_STRING},
    {"hint-ieee1275",   0, GRUB_ARG_OPTION_REPEATABLE,
     N_("First try the device HINT if currently running on IEEE1275. "
	"If HINT ends in comma, also try subpartitions"),
     N_("HINT"), ARG_TYPE_STRING},
    {"hint-bios",   0, GRUB_ARG_OPTION_REPEATABLE,
     N_("First try the device HINT if currently running on BIOS. "
	"If HINT ends in comma, also try subpartitions"),
     N_("HINT"), ARG_TYPE_STRING},
    {"hint-baremetal",   0, GRUB_ARG_OPTION_REPEATABLE,
     N_("First try the device HINT if direct hardware access is supported. "
	"If HINT ends in comma, also try subpartitions"),
     N_("HINT"), ARG_TYPE_STRING},
    {"hint-efi",   0, GRUB_ARG_OPTION_REPEATABLE,
     N_("First try the device HINT if currently running on EFI. "
	"If HINT ends in comma, also try subpartitions"),
     N_("HINT"), ARG_TYPE_STRING},
    {"hint-arc",   0, GRUB_ARG_OPTION_REPEATABLE,
     N_("First try the device HINT if currently running on ARC."
	" If HINT ends in comma, also try subpartitions"),
     N_("HINT"), ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
  };

enum options
  {
    SEARCH_FILE,
    SEARCH_LABEL,
    SEARCH_FS_UUID,
    SEARCH_PART_LABEL,
    SEARCH_PART_UUID,
    SEARCH_DISK_UUID,
    SEARCH_SET,
    SEARCH_NO_FLOPPY,
    SEARCH_QUIET,
    SEARCH_HINT,
    SEARCH_HINT_IEEE1275,
    SEARCH_HINT_BIOS,
    SEARCH_HINT_BAREMETAL,
    SEARCH_HINT_EFI,
    SEARCH_HINT_ARC,
 };

static grub_err_t
grub_cmd_search (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  const char *var = 0;
  const char *id = 0;
  int i = 0, j = 0, nhints = 0;
  char **hints = NULL;

  if (state[SEARCH_HINT].set)
    for (i = 0; state[SEARCH_HINT].args[i]; i++)
      nhints++;

#ifdef GRUB_MACHINE_IEEE1275
  if (state[SEARCH_HINT_IEEE1275].set)
    for (i = 0; state[SEARCH_HINT_IEEE1275].args[i]; i++)
      nhints++;
#endif

#ifdef GRUB_MACHINE_EFI
  if (state[SEARCH_HINT_EFI].set)
    for (i = 0; state[SEARCH_HINT_EFI].args[i]; i++)
      nhints++;
#endif

#ifdef GRUB_MACHINE_PCBIOS
  if (state[SEARCH_HINT_BIOS].set)
    for (i = 0; state[SEARCH_HINT_BIOS].args[i]; i++)
      nhints++;
#endif

#ifdef GRUB_MACHINE_ARC
  if (state[SEARCH_HINT_ARC].set)
    for (i = 0; state[SEARCH_HINT_ARC].args[i]; i++)
      nhints++;
#endif

  if (state[SEARCH_HINT_BAREMETAL].set)
    for (i = 0; state[SEARCH_HINT_BAREMETAL].args[i]; i++)
      nhints++;

  hints = grub_calloc (nhints, sizeof (hints[0]));
  if (!hints)
    return grub_errno;
  j = 0;

  if (state[SEARCH_HINT].set)
    for (i = 0; state[SEARCH_HINT].args[i]; i++)
      hints[j++] = state[SEARCH_HINT].args[i];

#ifdef GRUB_MACHINE_IEEE1275
  if (state[SEARCH_HINT_IEEE1275].set)
    for (i = 0; state[SEARCH_HINT_IEEE1275].args[i]; i++)
      hints[j++] = state[SEARCH_HINT_IEEE1275].args[i];
#endif

#ifdef GRUB_MACHINE_EFI
  if (state[SEARCH_HINT_EFI].set)
    for (i = 0; state[SEARCH_HINT_EFI].args[i]; i++)
      hints[j++] = state[SEARCH_HINT_EFI].args[i];
#endif

#ifdef GRUB_MACHINE_ARC
  if (state[SEARCH_HINT_ARC].set)
    for (i = 0; state[SEARCH_HINT_ARC].args[i]; i++)
      hints[j++] = state[SEARCH_HINT_ARC].args[i];
#endif

#ifdef GRUB_MACHINE_PCBIOS
  if (state[SEARCH_HINT_BIOS].set)
    for (i = 0; state[SEARCH_HINT_BIOS].args[i]; i++)
      hints[j++] = state[SEARCH_HINT_BIOS].args[i];
#endif

  if (state[SEARCH_HINT_BAREMETAL].set)
    for (i = 0; state[SEARCH_HINT_BAREMETAL].args[i]; i++)
      hints[j++] = state[SEARCH_HINT_BAREMETAL].args[i];

  /* Skip hints for future platforms.  */
  for (j = 0; j < argc; j++)
    if (grub_memcmp (args[j], "--hint-", sizeof ("--hint-") - 1) != 0)
      break;

  if (state[SEARCH_SET].set)
    var = state[SEARCH_SET].arg ? state[SEARCH_SET].arg : "root";

  if (argc != j)
    id = args[j];
  else if (state[SEARCH_SET].set && state[SEARCH_SET].arg)
    {
      id = state[SEARCH_SET].arg;
      var = "root";
    }
  else
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));
      goto out;
    }

  if (state[SEARCH_LABEL].set)
    grub_search_label (id, var, state[SEARCH_NO_FLOPPY].set, 
		       state[SEARCH_QUIET].set, hints, nhints);
  else if (state[SEARCH_FS_UUID].set)
    grub_search_fs_uuid (id, var, state[SEARCH_NO_FLOPPY].set,
			 state[SEARCH_QUIET].set, hints, nhints);
  else if (state[SEARCH_PART_LABEL].set)
    grub_search_part_label (id, var, state[SEARCH_NO_FLOPPY].set,
			    state[SEARCH_QUIET].set, hints, nhints);
  else if (state[SEARCH_PART_UUID].set)
    grub_search_part_uuid (id, var, state[SEARCH_NO_FLOPPY].set,
			   state[SEARCH_QUIET].set, hints, nhints);
  else if (state[SEARCH_DISK_UUID].set)
    grub_search_disk_uuid (id, var, state[SEARCH_NO_FLOPPY].set,
			   state[SEARCH_QUIET].set, hints, nhints);
  else if (state[SEARCH_FILE].set)
    grub_search_fs_file (id, var, state[SEARCH_NO_FLOPPY].set, 
			 state[SEARCH_QUIET].set, hints, nhints);
  else
    grub_error (GRUB_ERR_INVALID_COMMAND, "unspecified search type");

out:
  grub_free (hints);
  return grub_errno;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(search)
{
  cmd =
    grub_register_extcmd ("search", grub_cmd_search,
			  GRUB_COMMAND_FLAG_EXTRACTOR | GRUB_COMMAND_ACCEPT_DASH,
			  N_("[-f|-l|-u|-s|-n|-q] [--hint HINT [--hint HINT] ...]"
			     " NAME"),
			  N_("Search devices by file, filesystem label"
			     " or filesystem UUID."
			     " If --set is specified, the first device found is"
			     " set to a variable. If no variable name is"
			     " specified, `root' is used."),
			  options);
}

GRUB_MOD_FINI(search)
{
  grub_unregister_extcmd (cmd);
}
