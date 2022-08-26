/* cmd.c - command to cmp an operating system */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2005,2006,2007,2009  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/file.h>
#include <grub/mm.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define BUFFER_SIZE 512

static const struct grub_arg_option options[] =
{
  {"quiet", 'q', 0, N_("Don't print info."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

static grub_err_t
grub_cmd_cmp (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  grub_ssize_t rd1, rd2;
  grub_off_t pos;
  grub_file_t file1 = 0;
  grub_file_t file2 = 0;
  char *buf1 = 0;
  char *buf2 = 0;
  grub_err_t err = GRUB_ERR_TEST_FAILURE;

  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("two arguments expected"));

  if (!state[0].set)
    grub_printf_ (N_("Compare file `%s' with `%s':\n"), args[0], args[1]);

  file1 = grub_file_open (args[0], GRUB_FILE_TYPE_CMP);
  file2 = grub_file_open (args[1], GRUB_FILE_TYPE_CMP);
  if (! file1 || ! file2)
    goto cleanup;

  if (grub_file_size (file1) != grub_file_size (file2))
  {
    if (!state[0].set)
      grub_printf_ (N_("Files differ in size: %llu [%s], %llu [%s]\n"),
          (unsigned long long) grub_file_size (file1), args[0],
          (unsigned long long) grub_file_size (file2), args[1]);
  }
  else
  {
    pos = 0;

    buf1 = grub_malloc (BUFFER_SIZE);
    buf2 = grub_malloc (BUFFER_SIZE);

    if (! buf1 || ! buf2)
      goto cleanup;

    do
    {
      int i;

      rd1 = grub_file_read (file1, buf1, BUFFER_SIZE);
      rd2 = grub_file_read (file2, buf2, BUFFER_SIZE);

      if (rd1 != rd2)
        goto cleanup;

      for (i = 0; i < rd2; i++)
      {
        if (buf1[i] != buf2[i])
        {
          if (!state[0].set)
            grub_printf_ (N_(
              "Files differ at the offset %llu: 0x%x [%s], 0x%x [%s]\n"),
                (unsigned long long) (i + pos), buf1[i],
                args[0], buf2[i], args[1]);
          goto cleanup;
        }
      }
      pos += BUFFER_SIZE;
    }
    while (rd2);

    /* TRANSLATORS: it's always exactly 2 files.  */
    if (!state[0].set)
      grub_printf_ (N_("The files are identical.\n"));
    err = GRUB_ERR_NONE;
  }

cleanup:

  grub_free (buf1);
  grub_free (buf2);
  if (file1)
    grub_file_close (file1);
  if (file2)
    grub_file_close (file2);

  return err;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(cmp)
{
  cmd = grub_register_extcmd ("cmp", grub_cmd_cmp, 0,
                    N_("FILE1 FILE2"), N_("Compare two files."), options);
}

GRUB_MOD_FINI(cmp)
{
  grub_unregister_extcmd (cmd);
}
