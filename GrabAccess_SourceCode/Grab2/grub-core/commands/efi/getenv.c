/* getenv.c - retrieve EFI variables.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
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

#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/charset.h>
#include <grub/dl.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>

#pragma GCC diagnostic ignored "-Wcast-align"

#define INSYDE_SETUP_VAR_SIZE		(0x2bc)
#define MAX_VARIABLE_SIZE		(1024)
#define MAX_VAR_DATA_SIZE		(65536)

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options_getenv[] = {
  {"guid", 'g', GRUB_ARG_OPTION_OPTIONAL,
   N_("GUID of environment variable to query"),
   N_("GUID"), ARG_TYPE_STRING},
  {"type", 't', GRUB_ARG_OPTION_OPTIONAL,
   N_("Parse EFI_VAR as specific type (hex, uint8, string, wstring). Default: hex."),
   N_("TYPE"), ARG_TYPE_STRING},
  {0, 0, 0, 0, 0, 0}
};

enum options_getenv
{
  GETENV_VAR_GUID,
  GETENV_VAR_TYPE
};

enum efi_var_type
{
  EFI_VAR_STRING = 0,
  EFI_VAR_WSTRING,
  EFI_VAR_UINT8,
  EFI_VAR_HEX,
  EFI_VAR_INVALID = -1
};

static enum efi_var_type
parse_efi_var_type (const char *type)
{
  if (grub_strcmp (type, "string") == 0)
    return EFI_VAR_STRING;
  if (grub_strcmp (type, "wstring") == 0)
    return EFI_VAR_WSTRING;
  if (grub_strcmp (type, "uint8") == 0)
    return EFI_VAR_UINT8;
  if (grub_strcmp (type, "hex") == 0)
    return EFI_VAR_HEX;
  return EFI_VAR_INVALID;
}

static grub_err_t
grub_cmd_getenv (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  char *envvar = NULL, *guid = NULL, *data = NULL, *setvar = NULL;
  grub_size_t datasize = 0;
  grub_efi_guid_t efi_var_guid = GRUB_EFI_GLOBAL_VARIABLE_GUID;
  enum efi_var_type efi_type = EFI_VAR_HEX;
  unsigned int i;

  if (state[GETENV_VAR_TYPE].set)
    efi_type = parse_efi_var_type (state[GETENV_VAR_TYPE].arg);

  if (efi_type == EFI_VAR_INVALID)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("invalid EFI variable type"));

  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("unexpected arguments"));

  envvar = args[0];

  if (state[GETENV_VAR_GUID].set)
  {
    guid = state[GETENV_VAR_GUID].arg;
    if (grub_strlen(guid) != 36 ||
        guid[8] != '-' || guid[13] != '-' || guid[18] != '-' || guid[23] != '-')
      return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("invalid GUID"));

    guid[8] = 0;
    efi_var_guid.data1 = grub_strtoul(guid, NULL, 16);
    guid[13] = 0;
    efi_var_guid.data2 = grub_strtoul(guid + 9, NULL, 16);
    guid[18] = 0;
    efi_var_guid.data3 = grub_strtoul(guid + 14, NULL, 16);
    efi_var_guid.data4[7] = grub_strtoul(guid + 34, NULL, 16);
    guid[34] = 0;
    efi_var_guid.data4[6] = grub_strtoul(guid + 32, NULL, 16);
    guid[32] = 0;
    efi_var_guid.data4[5] = grub_strtoul(guid + 30, NULL, 16);
    guid[30] = 0;
    efi_var_guid.data4[4] = grub_strtoul(guid + 28, NULL, 16);
    guid[28] = 0;
    efi_var_guid.data4[3] = grub_strtoul(guid + 26, NULL, 16);
    guid[26] = 0;
    efi_var_guid.data4[2] = grub_strtoul(guid + 24, NULL, 16);
    guid[23] = 0;
    efi_var_guid.data4[1] = grub_strtoul(guid + 21, NULL, 16);
    guid[21] = 0;
    efi_var_guid.data4[0] = grub_strtoul(guid + 19, NULL, 16);
  }

  grub_efi_get_variable (envvar, &efi_var_guid, &datasize, (void **)&data);

  if (!data || !datasize)
  {
    grub_error (GRUB_ERR_FILE_NOT_FOUND, N_("No such variable"));
    goto done;
  }

  switch (efi_type)
  {
    case EFI_VAR_STRING:
      setvar = grub_malloc (datasize + 1);
      if (!setvar)
      {
        grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
        break;
      }
      grub_memcpy (setvar, data, datasize);
      setvar[datasize] = '\0';
      break;

    case EFI_VAR_WSTRING:
      setvar = grub_zalloc (datasize);
      if (!setvar)
      {
        grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
        break;
      }
      grub_utf16_to_utf8 ((grub_uint8_t *)setvar, (grub_uint16_t *)data, datasize);
      break;

    case EFI_VAR_UINT8:
      setvar = grub_malloc (4);
      if (!setvar)
      {
        grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
        break;
      }
      grub_snprintf (setvar, 4, "%u", *((grub_uint8_t *)data));
      break;

    case EFI_VAR_HEX:
      setvar = grub_malloc (datasize * 2 + 1);
      if (!setvar)
      {
        grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
        break;
      }
      for (i = 0; i < datasize; i++)
        grub_snprintf (setvar + (i * 2), 3, "%02x", ((grub_uint8_t *)data)[i]);
      break;

    default:
      grub_error (GRUB_ERR_BUG, N_("should not happen (bug in module?)"));
  }

  grub_env_set (args[1], setvar);

  if (setvar)
    grub_free (setvar);

  grub_errno = GRUB_ERR_NONE;

done:
  grub_free(data);
  return grub_errno;
}

static grub_err_t
grub_cmd_lsefienv (grub_command_t cmd __attribute__ ((unused)),
		   int argc __attribute__ ((unused)), char *argv[] __attribute__ ((unused)))
{
  grub_efi_status_t status;
  grub_efi_guid_t guid;
  grub_uint8_t tmp_data[MAX_VAR_DATA_SIZE];
  grub_efi_uintn_t setup_var_size = INSYDE_SETUP_VAR_SIZE;
  grub_efi_uint32_t setup_var_attr = 0x7;

  grub_efi_char16_t name[MAX_VARIABLE_SIZE/2];
  grub_efi_uintn_t name_size;
  unsigned char name_str[MAX_VARIABLE_SIZE/2+1];

  name[0] = 0x0;
  /* scan for Setup variable */
  grub_printf("NS varsize              var_guid                name\n");
  do
  {
    name_size = MAX_VARIABLE_SIZE;
    status = efi_call_3(grub_efi_system_table->runtime_services->get_next_variable_name,
      &name_size, name, &guid);

    if(status == GRUB_EFI_NOT_FOUND)
    { /* finished traversing VSS */
      break;
    }

    if(status)
    {
      grub_printf("status: 0x%02x\n", (grub_uint32_t) status);
    }
    if(! status)
    {
      setup_var_size = 1;
      status = efi_call_5(grub_efi_system_table->runtime_services->get_variable,
        name, &guid, &setup_var_attr, &setup_var_size, tmp_data);
      if (status && status != GRUB_EFI_BUFFER_TOO_SMALL)
      {
          grub_printf("error (0x%x) getting var size:\n  ", (grub_uint32_t)status);
          setup_var_size = 0;
      }
      status = 0;

      grub_utf16_to_utf8 (name_str, name, MAX_VARIABLE_SIZE/2+1);

      grub_printf("%02u %06u  %08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x %s\n",
      (grub_uint32_t) name_size, (grub_uint32_t) setup_var_size,
      guid.data1,
      guid.data2,
      guid.data3,
      guid.data4[0], guid.data4[1], guid.data4[2], guid.data4[3], guid.data4[4], guid.data4[5], guid.data4[6], guid.data4[7], 
      name_str
      );
    }
  } while (! status);

  return grub_errno;
}

static grub_extcmd_t cmd_getenv;
static grub_command_t cmd_lsefienv;

GRUB_MOD_INIT(getenv)
{
  cmd_getenv = grub_register_extcmd ("getenv", grub_cmd_getenv, 0,
				   N_("[-g GUID] [-t TYPE] ENVVAR SETVAR"),
				   N_("Read a firmware environment variable"),
				   options_getenv);
  cmd_lsefienv = grub_register_command ("lsefienv", grub_cmd_lsefienv,
					"lsefienv",
					"Lists all efi variables.");
}

GRUB_MOD_FINI(getenv)
{
  grub_unregister_extcmd (cmd_getenv);
  grub_unregister_command(cmd_lsefienv);
}
