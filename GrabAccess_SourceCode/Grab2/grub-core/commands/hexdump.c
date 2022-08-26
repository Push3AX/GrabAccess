/* hexdump.c - command to dump the contents of a file or memory */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007,2008,2009  Free Software Foundation, Inc.
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
#include <grub/file.h>
#include <grub/disk.h>
#include <grub/misc.h>
#include <grub/lib/hexdump.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/env.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] = {
  {"skip", 's', 0, N_("Skip offset bytes from the beginning of file."), 0,
   ARG_TYPE_INT},
  {"length", 'n', 0, N_("Read only LENGTH bytes."), 0, ARG_TYPE_INT},
  {"quiet", 'q', 0, N_("Don't print error."), 0, 0},
  {"decompress", 'd', 0, N_("Decompress the file."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

static grub_err_t
grub_cmd_hexdump (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  char buf[GRUB_DISK_SECTOR_SIZE * 4];
  grub_ssize_t size, length;
  grub_disk_addr_t skip;
  grub_size_t var_len;
  char *var_name = NULL;
  char *var_buf = NULL;
  char *p = NULL;
  enum grub_file_type type = GRUB_FILE_TYPE_HEXCAT;

  if (argc < 1 || argc > 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  skip = (state[0].set) ? grub_strtoull (state[0].arg, 0, 0) : 0;
  length = (state[1].set) ? grub_strtoul (state[1].arg, 0, 0) : 256;
  if (!state[3].set)
    type |= GRUB_FILE_TYPE_NO_DECOMPRESS;

  if (argc == 2)
  {
    var_len = length + 1;
    var_buf = (char *) grub_malloc (var_len);
    if (var_buf)
    {
      var_name = args[1];
      p = var_buf;
    }
  }

  grub_file_t file;

  file = grub_file_open (args[0], type);
  if (! file)
    return 0;

  file->offset = skip;

  while ((size = grub_file_read (file, buf, sizeof (buf))) > 0)
  {
    unsigned long len;

    len = ((length) && (size > length)) ? length : size;
    if (! state[2].set)
      hexdump (skip, buf, len);

    if (var_name)
    {
      grub_memcpy (p, buf, len);
      p += len;
    }

    skip += len;
    if (length)
    {
      length -= len;
      if (!length)
        break;
    }
  }

  grub_file_close (file);

  if (var_name)
  {
    char *str = grub_zalloc (4 * var_len);
    grub_size_t i;

    for (i = 0; i < var_len - 1; i++)
      grub_snprintf (str + 4 * i, 5, "\\x%02x", (unsigned char)var_buf[i]);

    grub_env_set(var_name, str);
    if (var_buf)
      grub_free (var_buf);
    if (str)
      grub_free (str);
  }

  return 0;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT (hexdump)
{
  cmd = grub_register_extcmd ("hexdump", grub_cmd_hexdump, 0,
                  N_("[OPTIONS] FILE_OR_DEVICE [VARIABLE]"),
                  N_("Show raw contents of a file or memory."),
                  options);
}

GRUB_MOD_FINI (hexdump)
{
  grub_unregister_extcmd (cmd);
}
