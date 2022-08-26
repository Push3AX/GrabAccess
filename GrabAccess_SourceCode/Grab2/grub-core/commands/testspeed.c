/* testspeed.c - Command to test file read speed  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2012  Free Software Foundation, Inc.
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

#include <grub/mm.h>
#include <grub/file.h>
#include <grub/time.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/normal.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define DEFAULT_BLOCK_SIZE	65536

static const struct grub_arg_option options[] =
  {
    {"size", 's', 0, N_("Specify size for each read operation"), 0, ARG_TYPE_INT},
    {0, 0, 0, 0, 0, 0}
  };

static grub_err_t
grub_cmd_testspeed (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  grub_uint64_t start;
  grub_uint64_t end;
  grub_ssize_t block_size;
  grub_disk_addr_t total_size;
  char *buffer;
  grub_file_t file;
  grub_uint64_t whole, fraction;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  block_size = (state[0].set) ?
    grub_strtoul (state[0].arg, 0, 0) : DEFAULT_BLOCK_SIZE;

  if (block_size <= 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("invalid block size"));

  buffer = grub_malloc (block_size);
  if (buffer == NULL)
    return grub_errno;

  file = grub_file_open (args[0], GRUB_FILE_TYPE_TESTLOAD);
  if (file == NULL)
    goto quit;

  total_size = 0;
  start = grub_get_time_ms ();
  while (1)
    {
      grub_ssize_t size = grub_file_read (file, buffer, block_size);
      if (size <= 0)
	break;
      total_size += size;
    }
  end = grub_get_time_ms ();
  grub_file_close (file);

  grub_printf_ (N_("File size: %s\n"),
		grub_get_human_size (total_size, GRUB_HUMAN_SIZE_NORMAL));
  whole = grub_divmod64 (end - start, 1000, &fraction);
  grub_printf_ (N_("Elapsed time: %d.%03d s \n"),
		(unsigned) whole,
		(unsigned) fraction);

  if (end != start)
    {
      grub_uint64_t speed =
	grub_divmod64 (total_size * 100ULL * 1000ULL, end - start, 0);

      grub_printf_ (N_("Speed: %s \n"),
		    grub_get_human_size (speed,
					 GRUB_HUMAN_SIZE_SPEED));
    }

 quit:
  grub_free (buffer);

  return grub_errno;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(testspeed)
{
  cmd = grub_register_extcmd ("testspeed", grub_cmd_testspeed, 0, N_("[-s SIZE] FILENAME"),
			      N_("Test file read speed."),
			      options);
}

GRUB_MOD_FINI(testspeed)
{
  grub_unregister_extcmd (cmd);
}
