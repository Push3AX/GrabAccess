/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Peter Lustig
 *  Copyright (C) 2013,2020  Free Software Foundation, Inc.
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
#include <grub/mm.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/err.h>
#include <grub/env.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define HIBRFIL_MAGIC "HIBR"
#define HIBRFIL_MAGIC_LEN 4

static grub_err_t
grub_cmd_nthibr (grub_command_t cmd __attribute__ ((unused)),
                 int argc, char **args)
{
  char hibrfil_magic[HIBRFIL_MAGIC_LEN];
  grub_file_t file = 0;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));

  file = grub_file_open (args[0], GRUB_FILE_TYPE_HEXCAT);
  if (!file)
    return grub_errno;

  /* Try to read magic number of 'hiberfil.sys' */
  grub_memset (hibrfil_magic, 0, HIBRFIL_MAGIC_LEN);
  grub_file_read (file, hibrfil_magic, HIBRFIL_MAGIC_LEN);
  grub_file_close (file);

  if (!grub_strncasecmp (HIBRFIL_MAGIC, hibrfil_magic, HIBRFIL_MAGIC_LEN))
    return GRUB_ERR_TEST_FAILURE;

  return GRUB_ERR_NONE;
}

static grub_uint8_t NT_VERSION_SRC[] =
{ 0x50, 0x00, 0x72, 0x00, 0x6F, 0x00, 0x64, 0x00,
  0x75, 0x00, 0x63, 0x00, 0x74, 0x00, 0x56, 0x00,
  0x65, 0x00, 0x72, 0x00, 0x73, 0x00, 0x69, 0x00,
  0x6F, 0x00, 0x6E, 0x00 };

static grub_err_t
grub_cmd_ntver (grub_command_t cmd __attribute__ ((unused)),
                int argc, char **args)
{
  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("bad argument"));
  char dll_path[50];
  char ntver[8];
  grub_file_t file = 0;
  grub_size_t size;
  grub_uint8_t *data = NULL;
  grub_size_t i;
  grub_snprintf (dll_path, 50, "%s/Windows/System32/Version.dll", args[0]);
  file = grub_file_open (dll_path, GRUB_FILE_TYPE_HEXCAT);
  if (!file)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND,
                       N_("failed to open %s"), dll_path);
  size = file->size;
  if (size < sizeof(NT_VERSION_SRC) + 12)
  {
    grub_file_close (file);
    return grub_error (GRUB_ERR_FILE_READ_ERROR, N_("bad file size"));
  }
  data = grub_malloc (size);
  if (!data)
  {
    grub_file_close (file);
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
  }
  grub_file_read (file, data, size);
  grub_file_close (file);
  for (i = 0; i < size - sizeof(NT_VERSION_SRC) - 12; i++)
  {
    if (grub_memcmp (data + i, NT_VERSION_SRC, sizeof(NT_VERSION_SRC)) == 0)
    {
      grub_printf ("found version in %lld: ", (unsigned long long) i);
      ntver[0] = *(data + i + sizeof(NT_VERSION_SRC) + 2);
      ntver[1] = *(data + i + sizeof(NT_VERSION_SRC) + 4);
      ntver[2] = *(data + i + sizeof(NT_VERSION_SRC) + 6);
      ntver[3] = *(data + i + sizeof(NT_VERSION_SRC) + 8);
      if (!grub_isdigit(ntver[3]))
        ntver[3] = '\0';
      ntver[4] = '\0';
      grub_printf ("%s\n", ntver);
      grub_env_set (args[1], ntver);
      grub_free (data);
      return 0;
    }
  }
  grub_free (data);
  return 1;
}

static grub_command_t cmd_hibr, cmd_ver;

GRUB_MOD_INIT (nttools)
{
  cmd_hibr = grub_register_command ("nthibr", grub_cmd_nthibr, N_("FILE"),
                  N_("Test whether a hiberfil.sys is in hibernated state."));
  cmd_ver = grub_register_command ("ntversion", grub_cmd_ntver,
                  N_("(hdx,y) VARIABLE"), N_("Get NT version."));
}

GRUB_MOD_FINI (nttools)
{
  grub_unregister_command (cmd_hibr);
  grub_unregister_command (cmd_ver);
}
