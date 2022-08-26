/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2020  Free Software Foundation, Inc.
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
#include <grub/efi/disk.h>
#include <grub/charset.h>
#include <grub/command.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/dl.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/file.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/net.h>
#include <grub/types.h>

GRUB_MOD_LICENSE ("GPLv3+");

/*
  typedef struct _EFI_LOAD_OPTION
  {
    UINT32 Attributes;
    UINT16 FilePathListLength;
    CHAR16 Description[];
    EFI_DEVICE_PATH_PROTOCOL FilePathList[];
    UINT8 OptionalData[];
  } EFI_LOAD_OPTION;
*/

/*
 * EFI_LOAD_OPTION Attributes
 * All values 0x00000200-0x00001F00 are reserved
 * If a load option is marked as LOAD_OPTION_ACTIVE,
 * the boot manager will attempt to boot automatically
 * using the device path information in the load option.
 * This provides an easy way to disable or enable load options
 * without needing to delete and re-add them.
 *
 * If any Driver#### load option is marked as LOAD_OPTION_FORCE_RECONNECT,
 * then all of the UEFI drivers in the system
 * will be disconnected and reconnected after the last
 * Driver#### load option is processed.
 * This allows a UEFI driver loaded with a Driver#### load option
 * to override a UEFI driver that was loaded prior to the execution
 * of the UEFI Boot Manager.
 *
 * The LOAD_OPTION_CATEGORY is a sub-field of Attributes that provides
 * details to the boot manager to describe how it should group
 * the Boot#### load options. This field is ignored for variables
 * of the form Driver####, SysPrep####,or OsRecovery####.
 * Boot#### load options with LOAD_OPTION_CATEGORY
 * set to LOAD_OPTION_CATEGORY_BOOT are meant to be part of
 * the normal boot processing.
 *
 * Boot#### load options with LOAD_OPTION_CATEGORY
 * set to LOAD_OPTION_CATEGORY_APP are executables which are not
 * part of the normal boot processing but can be optionally chosen
 * for execution if boot menu is provided, or via Hot Keys.
 *
 * Boot options with reserved category values, will be ignored by
 * the boot manager. If any Boot#### load option is marked
 * as LOAD_OPTION_HIDDEN, then the load option will not appear
 * in the menu (if any) provided by the boot manager for load option selection.
 */
#define LOAD_OPTION_ACTIVE          0x00000001  // AC
#define LOAD_OPTION_FORCE_RECONNECT 0x00000002  // FR
#define LOAD_OPTION_HIDDEN          0x00000008  // HI
#define LOAD_OPTION_CATEGORY        0x00001F00  // CT
#define LOAD_OPTION_CATEGORY_BOOT   0x00000000  // CB
#define LOAD_OPTION_CATEGORY_APP    0x00000100  // CA

/*
 * Boot Manager Capabilities
 * The boot manager can report its capabilities through the
 * global variable BootOptionSupport.
 * If the global variable is not present, then an installer
 * or application must act as if a value of 0 was returned.
 *
 * If EFI_BOOT_OPTION_SUPPORT_KEY is set then the boot manager
 * supports launching of Boot#### load options using key presses.
 *
 * If EFI_BOOT_OPTION_SUPPORT_APP is set then the boot manager
 * supports boot options with LOAD_OPTION_CATEGORY_APP.
 *
 * If EFI_BOOT_OPTION_SUPPORT_SYSPREP is set then the boot manager
 * supports boot options of form SysPrep####.
 *
 * The value specified in EFI_BOOT_OPTION_SUPPORT_COUNT
 * describes the maximum number of key presses which the boot manager
 * supports in the EFI_KEY_OPTION.KeyData.InputKeyCount.
 * This value is only valid if EFI_BOOT_OPTION_SUPPORT_KEY is set.
 * Key sequences with more keys specified are ignored.
 */
#define EFI_BOOT_OPTION_SUPPORT_KEY     0x00000001
#define EFI_BOOT_OPTION_SUPPORT_APP     0x00000002
#define EFI_BOOT_OPTION_SUPPORT_SYSPREP 0x00000010
#define EFI_BOOT_OPTION_SUPPORT_COUNT   0x00000300

struct _efi_loadopt
{
  grub_uint32_t attr;
  grub_uint16_t dp_len;
  grub_uint8_t data[0];
} GRUB_PACKED;
typedef struct _efi_loadopt *efi_loadopt;

struct _bcfg_loadopt
{
  grub_uint32_t attr;
  char *desc;
  grub_efi_device_path_t *dp;
  void *data;
  grub_size_t data_len;
};
typedef struct _bcfg_loadopt *bcfg_loadopt;

typedef enum
{
  BCFG_LOADOPT_DATA_UNKNOWN,
  BCFG_LOADOPT_DATA_DESC,
  BCFG_LOADOPT_DATA_FILE,
  BCFG_LOADOPT_DATA_ATTR,
  BCFG_LOADOPT_DATA_ALL,
} bcfg_loadopt_data_type;

static grub_size_t
u16strsize (const grub_uint16_t *str)
{
  grub_size_t len = 0;
  while (*(str++))
    len++;
  len = sizeof (grub_uint16_t) * (len + 1);
  return len;
}

static int
u8u16strncmp (const grub_uint16_t *s1, const char *s2, grub_size_t n)
{
  if (!n)
    return 0;
  while (*s1 && *s2 && --n)
  {
    if (*s1 != (grub_uint16_t)*s2)
      break;
    s1++;
    s2++;
  }
  return (int) *s1 - (int) *s2;
}

static grub_efi_device_path_t *
str_to_dp (const char *file)
{
  char *devname = NULL;
  grub_device_t dev = 0;
  grub_efi_device_path_t *dp = NULL;
  grub_efi_device_path_t *file_dp = NULL;
  grub_efi_handle_t dev_handle = 0;

  devname = grub_file_get_device_name (file);
  dev = grub_device_open (devname);
  if (devname)
    grub_free (devname);

  if (dev->disk)
    dev_handle = grub_efidisk_get_device_handle (dev->disk);
  else if (dev->net && dev->net->server)
  {
    grub_net_network_level_address_t addr;
    struct grub_net_network_level_interface *inf;
    grub_net_network_level_address_t gateway;
    grub_err_t err;
    err = grub_net_resolve_address (dev->net->server, &addr);
    if (err)
      goto out;
    err = grub_net_route_address (addr, &gateway, &inf);
    if (err)
      goto out;
    dev_handle = grub_efinet_get_device_handle (inf->card);
  }
  if (dev_handle)
    dp = grub_efi_get_device_path (dev_handle);
  if (grub_strchr (file, '/'))
    file_dp = grub_efi_file_device_path (dp, file);
  else
    file_dp = grub_efi_duplicate_device_path (dp);
out:
  if (dev)
    grub_device_close (dev);
  return file_dp;
}

static char *dp_to_str (grub_efi_device_path_t *dp)
{
  grub_efi_device_path_t *p = dp;
  char *file = grub_efi_get_filename (dp);
  char *disk, *str;
  if (file)
  {
    while (p)
    {
      grub_efi_uint8_t type = GRUB_EFI_DEVICE_PATH_TYPE (p);
      grub_efi_uint8_t subtype = GRUB_EFI_DEVICE_PATH_SUBTYPE (p);
      if (type == GRUB_EFI_END_DEVICE_PATH_TYPE)
        break;
      if (type == GRUB_EFI_MEDIA_DEVICE_PATH_TYPE &&
          subtype == GRUB_EFI_FILE_PATH_DEVICE_PATH_SUBTYPE)
      {
        p->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
        p->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
        p->length = sizeof (grub_efi_device_path_protocol_t);
        break;
      }
      p = GRUB_EFI_NEXT_DEVICE_PATH (p);
    }
  }
  disk = grub_efidisk_get_device_name_from_dp (dp);
  str = grub_xasprintf ("(%s)%s", disk ? disk : "unknown", file ? file : "");
  if (disk)
    grub_free (disk);
  if (file)
    grub_free (file);
  return str;
}

static inline void efi_free_pool (void *data)
{
  efi_call_1 (grub_efi_system_table->boot_services->free_pool, data);
  data = NULL;
}

static void *efi_get_env (const char *var, grub_size_t *datasize_out)
{
  grub_efi_status_t status;
  grub_efi_uintn_t datasize = 0;
  grub_efi_runtime_services_t *r = grub_efi_system_table->runtime_services;
  grub_efi_boot_services_t *b = grub_efi_system_table->boot_services;
  grub_efi_char16_t *var16;
  grub_efi_guid_t guid = GRUB_EFI_GLOBAL_VARIABLE_GUID;
  void *data;
  grub_size_t len, len16;

  *datasize_out = 0;

  len = grub_strlen (var);
  len16 = len * GRUB_MAX_UTF16_PER_UTF8;
  var16 = grub_calloc (len16 + 1, sizeof (var16[0]));
  if (!var16)
    return NULL;
  len16 = grub_utf8_to_utf16 (var16, len16, (grub_uint8_t *) var, len, NULL);
  var16[len16] = 0;

  status = efi_call_5 (r->get_variable, var16, &guid, NULL, &datasize, NULL);

  if (status != GRUB_EFI_BUFFER_TOO_SMALL || !datasize)
  {
    grub_free (var16);
    return NULL;
  }

  status = efi_call_3 (b->allocate_pool, GRUB_EFI_BOOT_SERVICES_DATA,
                       datasize, (void **) &data);
  if (status != GRUB_EFI_SUCCESS)
  {
    grub_free (var16);
    return NULL;
  }
  grub_memset (data, 0, datasize);

  status = efi_call_5 (r->get_variable, var16, &guid, NULL, &datasize, data);
  grub_free (var16);

  if (status == GRUB_EFI_SUCCESS)
  {
    *datasize_out = datasize;
    return data;
  }

  efi_call_1 (b->free_pool, data);
  return NULL;
}

static grub_err_t bcfg_help (void)
{
  grub_printf ("bcfg\n  Manage the boot options that are stored in NVRAM.\n");
  grub_printf ("Usage\n");
  grub_printf ("  bcfg boot|driver list [VAR]\n");
  grub_printf ("  bcfg boot|driver dump #### [desc|file|attr [VAR]]\n");
  grub_printf ("  bcfg boot|driver add #### FILE DESC [ATTR]\n");
  grub_printf ("  bcfg boot|driver del ####\n");
  grub_printf ("  bcfg boot|driver edit #### desc|file|attr DATA\n");
  grub_printf ("  bcfg boot|driver list [VAR]\n");

  grub_printf ("  bcfg timeout|bootnext get VAR\n");
  grub_printf ("  bcfg timeout|bootnext set ####\n");
  grub_printf ("  bcfg timeout|bootnext unset\n");

  grub_printf ("  bcfg bootorder|driverorder dump [VAR]\n");
  grub_printf ("  bcfg bootorder|driverorder swap #### ####\n");
  grub_printf ("  bcfg bootorder|driverorder del ####\n");
  grub_printf ("  bcfg bootorder|driverorder add ####\n");

  return GRUB_ERR_NONE;
}

static bcfg_loadopt_data_type loadopt_check_type (const char *str)
{
  if (!str)
    return BCFG_LOADOPT_DATA_ALL;
  if (grub_strcmp (str, "desc") == 0)
    return BCFG_LOADOPT_DATA_DESC;
  if (grub_strcmp (str, "file") == 0)
    return BCFG_LOADOPT_DATA_FILE;
  if (grub_strcmp (str, "attr") == 0)
    return BCFG_LOADOPT_DATA_ATTR;
  return BCFG_LOADOPT_DATA_UNKNOWN;
}

static void parse_flag (grub_uint32_t *attr, grub_uint32_t flag, char op)
{
  if (op == '+')
    *attr |= flag;
  if (op == '-')
    *attr &= ~flag;
  if (op == '^')
    *attr ^= flag;
}

static void loadopt_str_to_attr (const char *str, grub_uint32_t *attr)
{
  grub_size_t len, i;
  if (!str)
    return;
  len = grub_strlen (str);
  if (len < 3 && len > 18)
    return;
  if (str[0] == '0' && str[1] == 'x')
    *attr = grub_strtoul (str, NULL, 16);
  for (i = 0; i < len; i += 3)
  {
    if (str[i+1] == '\0' || str[i+2] == '\0')
      break;
    if (grub_strncmp (&str[i], "AC", 2) == 0)
      parse_flag (attr, LOAD_OPTION_ACTIVE, str[i+2]);
    if (grub_strncmp (&str[i], "FR", 2) == 0)
      parse_flag (attr, LOAD_OPTION_FORCE_RECONNECT, str[i+2]);
    if (grub_strncmp (&str[i], "HI", 2) == 0)
      parse_flag (attr, LOAD_OPTION_HIDDEN, str[i+2]);
    if (grub_strncmp (&str[i], "CT", 2) == 0)
      parse_flag (attr, LOAD_OPTION_CATEGORY, str[i+2]);
    if (grub_strncmp (&str[i], "CB", 2) == 0)
      parse_flag (attr, LOAD_OPTION_CATEGORY_BOOT, str[i+2]);
    if (grub_strncmp (&str[i], "CA", 2) == 0)
      parse_flag (attr, LOAD_OPTION_CATEGORY_APP, str[i+2]);
  }
}

static char *loadopt_dump (bcfg_loadopt loadopt, bcfg_loadopt_data_type type)
{
  char *ret = NULL;
  switch (type)
  {
    case BCFG_LOADOPT_DATA_DESC:
      if (loadopt->desc)
        ret = grub_strdup (loadopt->desc);
      break;
    case BCFG_LOADOPT_DATA_FILE:
      if (loadopt->dp)
        ret = dp_to_str (loadopt->dp);
      break;
    case BCFG_LOADOPT_DATA_ATTR:
      ret = grub_xasprintf ("%s%s%s%s%s%s",
                (loadopt->attr & LOAD_OPTION_ACTIVE) ? "AC+" : "",
                (loadopt->attr & LOAD_OPTION_FORCE_RECONNECT) ? "FR+" : "",
                (loadopt->attr & LOAD_OPTION_HIDDEN) ? "HI+" : "",
                (loadopt->attr & LOAD_OPTION_CATEGORY) ? "CT+" : "",
                (loadopt->attr & LOAD_OPTION_CATEGORY_BOOT) ? "CB+" : "",
                (loadopt->attr & LOAD_OPTION_CATEGORY_APP) ? "CA+" : "");
      break;
    case BCFG_LOADOPT_DATA_ALL:
      ret = dp_to_str (loadopt->dp);
      grub_printf ("Description: %s\nAttributes: %s%s%s%s%s%s\nPath: %s\n",
              loadopt->desc ? loadopt->desc : "(null)",
              (loadopt->attr & LOAD_OPTION_ACTIVE) ? "AC+" : "",
              (loadopt->attr & LOAD_OPTION_FORCE_RECONNECT) ? "FR+" : "",
              (loadopt->attr & LOAD_OPTION_HIDDEN) ? "HI+" : "",
              (loadopt->attr & LOAD_OPTION_CATEGORY) ? "CT+" : "",
              (loadopt->attr & LOAD_OPTION_CATEGORY_BOOT) ? "CB+" : "",
              (loadopt->attr & LOAD_OPTION_CATEGORY_APP) ? "CA+" : "",
              ret ? ret : "(null)");
      break;
    default:
      grub_error (GRUB_ERR_BAD_OS, "unknown data type");
  }
  return ret;
}

static grub_err_t loadopt_edit (bcfg_loadopt loadopt, const char *data,
                                bcfg_loadopt_data_type type)
{
  switch (type)
  {
    case BCFG_LOADOPT_DATA_DESC:
      if (loadopt->desc)
        grub_free (loadopt->desc);
      loadopt->desc = grub_strdup (data);
      if (!loadopt->desc)
        grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
      break;
    case BCFG_LOADOPT_DATA_FILE:
      if (loadopt->dp)
        grub_free (loadopt->dp);
      loadopt->dp = str_to_dp (data);
      if (!loadopt->dp)
        grub_error (GRUB_ERR_BAD_OS, "cannot set device path");
      break;
    case BCFG_LOADOPT_DATA_ATTR:
      loadopt_str_to_attr (data, &loadopt->attr);
      break;
    default:
      grub_error (GRUB_ERR_BAD_OS, "unknown data type");
  }
  return grub_errno;
}

static void loadopt_free (bcfg_loadopt loadopt)
{
  if (loadopt->desc)
    grub_free (loadopt->desc);
  loadopt->desc = NULL;
  if (loadopt->dp)
    grub_free (loadopt->dp);
  loadopt->dp = NULL;
  if (loadopt->data)
    grub_free (loadopt->data);
  loadopt->data = NULL;
  loadopt->data_len = 0;
  loadopt->attr = 0;
}

static int bcfg_env_check_num (const char *str, grub_uint16_t *num)
{
  if (!str || grub_strlen (str) != 4 ||
      !grub_isxdigit (str[0]) || !grub_isxdigit (str[1]) ||
      !grub_isxdigit (str[2]) || !grub_isxdigit (str[3]))
    return 0;
  if (num)
    *num = (grub_uint16_t) grub_strtoul (str, NULL, 16);
  return 1;
}

static const char *bcfg_env_check_name (const char *str)
{
  static char prefix[20];
  if (!str)
    return NULL;
  if (grub_strcmp (str, "boot") == 0)
    grub_strcpy (prefix, "Boot");
  else if (grub_strcmp (str, "driver") == 0)
    grub_strcpy (prefix, "Driver");
  else if (grub_strcmp (str, "sysprep") == 0)
    grub_strcpy (prefix, "SysPrep");
  else
    return NULL;
  return prefix;
}

static grub_err_t bcfg_env_get (const char *env,
                                bcfg_loadopt loadopt)
{
  efi_loadopt data = NULL;
  grub_size_t size = 0, data_ofs;

  data = efi_get_env (env, &size);
  if (!data)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, N_("No such variable"));
  if (size < sizeof (grub_uint16_t) + sizeof (grub_uint32_t) ||
      size < data->dp_len + 2 * sizeof (grub_uint16_t) + sizeof (grub_uint32_t))
  {
    efi_free_pool (data);
    return grub_error (GRUB_ERR_BAD_OS, "invalid bootopt");
  }
  size = size - (sizeof (grub_uint16_t) + sizeof (grub_uint32_t));
  loadopt_free (loadopt);

  loadopt->attr = data->attr;

  data_ofs = u16strsize ((void *)data->data);
  loadopt->desc = grub_zalloc (data_ofs);
  if (!loadopt->desc)
  {
    efi_free_pool (data);
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
  }
  grub_utf16_to_utf8 ((void *)loadopt->desc, (void *)data->data, data_ofs);

  loadopt->dp = grub_efi_duplicate_device_path ((void *)(data->data + data_ofs));
  if (!loadopt->dp)
  {
    efi_free_pool (data);
    loadopt_free (loadopt);
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
  }

  data_ofs += data->dp_len;
  if (data_ofs < size)
  {
    loadopt->data_len = size - data_ofs;
    loadopt->data = grub_malloc (loadopt->data_len);
    if (!loadopt->data)
    {
      efi_free_pool (data);
      loadopt_free (loadopt);
      return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
    }
    grub_memcpy (loadopt->data, data->data + data_ofs,
                 loadopt->data_len);
  }
  efi_free_pool (data);
  return GRUB_ERR_NONE;
}

static grub_err_t bcfg_env_set (const char *env,
                                bcfg_loadopt loadopt)
{
  efi_loadopt data = NULL;
  grub_uint16_t *desc = NULL;
  grub_uint16_t dp_len;
  grub_size_t size, desc_len;
  grub_efi_guid_t guid = GRUB_EFI_GLOBAL_VARIABLE_GUID;

  desc_len = (grub_strlen (loadopt->desc) + 1) * sizeof (grub_uint16_t);
  desc = grub_zalloc (desc_len);
  if (!desc)
      return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
  grub_utf8_to_utf16 (desc, desc_len, (void *)loadopt->desc, -1, NULL);
  dp_len = grub_efi_get_dp_size (loadopt->dp);
  desc_len = u16strsize (desc);

  size = sizeof (grub_uint16_t) + sizeof (grub_uint32_t)
          + desc_len + dp_len + loadopt->data_len;
  data = grub_zalloc (size);
  if (!data)
  {
    grub_free (desc);
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
  }
  data->attr = loadopt->attr;
  data->dp_len = dp_len;
  grub_memcpy (data->data, desc, desc_len);
  grub_memcpy (data->data + desc_len, loadopt->dp, dp_len);
  grub_memcpy (data->data + desc_len + dp_len,
               loadopt->data, loadopt->data_len);

  grub_efi_set_variable (env, &guid, data, size);

  grub_free (desc);
  grub_free (data);
  return grub_errno;
}

static grub_err_t bcfg_env_del (const char *env)
{
  grub_efi_guid_t guid = GRUB_EFI_GLOBAL_VARIABLE_GUID;
  return grub_efi_set_variable (env, &guid, NULL, 0);
}

static char *bcfg_env_list (const char *prefix)
{
  grub_efi_status_t status;
  grub_efi_runtime_services_t *r = grub_efi_system_table->runtime_services;
  grub_uint16_t *name = NULL;
  grub_size_t name_size = 24 * sizeof (grub_uint16_t), new_size;
  grub_size_t len = grub_strlen (prefix);
  char *str = NULL, *new_str;
  grub_efi_guid_t guid;
  grub_efi_guid_t global = GRUB_EFI_GLOBAL_VARIABLE_GUID;
  name = grub_zalloc (name_size);
  if (!name)
    return NULL;
  while (1)
  {
    new_size = name_size;
    status = efi_call_3 (r->get_next_variable_name, &new_size, name, &guid);
    if (status == GRUB_EFI_NOT_FOUND)
      break;
    if (status == GRUB_EFI_BUFFER_TOO_SMALL)
    {
      grub_uint16_t *new_name = NULL;
      new_name = grub_realloc (name, new_size);
      if (!new_name)
        break;
      name = new_name;
      name_size = new_size;
      status = efi_call_3 (r->get_next_variable_name, &new_size, name, &guid);
    }
    if (status != GRUB_EFI_SUCCESS)
      continue;
    if (grub_memcmp (&global, &guid, sizeof (grub_efi_guid_t)))
      continue;
    if (u8u16strncmp (name, prefix, len) == 0 &&
        grub_isxdigit (name[len]) && grub_isxdigit (name[len+1]) &&
        grub_isxdigit (name[len+2]) && grub_isxdigit (name[len+3]))
    {
      new_str = NULL;
      new_str = grub_xasprintf ("%s%s%c%c%c%c", str ? str : "", str ? " " : "",
                            name[len], name[len+1], name[len+2], name[len+3]);
      if (!new_str)
        break;
      if (str)
        grub_free (str);
      str = new_str;
    }
  }
  return str;
}

static const char *bcfg_u16_check_name (const char *str)
{
  static char ret[20];
  if (!str)
    return NULL;
  if (grub_strcmp (str, "timeout") == 0)
    grub_strcpy (ret, "Timeout");
  else if (grub_strcmp (str, "bootnext") == 0)
    grub_strcpy (ret, "BootNext");
  else
    return NULL;
  return ret;
}

static const char *bcfg_u16_get (const char *env)
{
  static char ret[5];
  grub_uint16_t *data = NULL;
  grub_size_t size = 0;

  data = efi_get_env (env, &size);
  if (!data)
  {
    grub_error (GRUB_ERR_FILE_NOT_FOUND, N_("No such variable"));
    return NULL;
  }
  if (size != sizeof (grub_uint16_t))
  {
    grub_error (GRUB_ERR_BAD_OS, "invalid env size");
    efi_free_pool (data);
    return NULL;
  }
  grub_snprintf (ret, 5, "%04X", *data);
  efi_free_pool (data);
  return ret;
}

static grub_err_t bcfg_u16_set (const char *env, const char *str)
{
  grub_uint16_t data;
  grub_size_t size = sizeof (grub_uint16_t);
  grub_efi_guid_t guid = GRUB_EFI_GLOBAL_VARIABLE_GUID;
  data = grub_strtoul (str, NULL, 16);
  return grub_efi_set_variable (env, &guid, &data, size);
}

static grub_err_t bcfg_u16_unset (const char *env)
{
  grub_efi_guid_t guid = GRUB_EFI_GLOBAL_VARIABLE_GUID;
  return grub_efi_set_variable (env, &guid, NULL, 0);
}

struct _bcfg_order_list
{
  grub_uint32_t count;
  grub_uint16_t *entry;
};
typedef struct _bcfg_order_list *bcfg_order_list;

static const char *bcfg_order_check_name (const char *str)
{
  static char ret[20];
  if (!str)
    return NULL;
  if (grub_strcmp (str, "bootorder") == 0)
    grub_strcpy (ret, "BootOrder");
  else if (grub_strcmp (str, "driverorder") == 0)
    grub_strcpy (ret, "DriverOrder");
  else if (grub_strcmp (str, "syspreporder") == 0)
    grub_strcpy (ret, "SysPrepOrder");
  else
    return NULL;
  return ret;
}

static grub_err_t
order_swap (bcfg_order_list order, grub_uint16_t src, grub_uint16_t dst)
{
  grub_uint16_t tmp;
  grub_uint32_t src_pos = 0, dst_pos = 0, i;
  if (!order->entry || order->count < 2)
    return grub_error (GRUB_ERR_BAD_OS, "boot option list too small");
  if (src == dst)
    return grub_error (GRUB_ERR_BAD_OS, "invalid boot entry");
  for (i = 0; i < order->count; i++)
  {
    if (!src_pos && order->entry[i] == src)
      src_pos = i + 1;
    if (!dst_pos && order->entry[i] == dst)
      dst_pos = i + 1;
    if (src_pos && dst_pos)
    {
      tmp = order->entry[src_pos - 1];
      order->entry[src_pos - 1] = order->entry[dst_pos - 1];
      order->entry[dst_pos - 1] = tmp;
      return GRUB_ERR_NONE;
    }
  }
  return grub_error (GRUB_ERR_FILE_NOT_FOUND,
                     "entry %04X not found", src_pos ? dst : src);
}

static grub_err_t
order_rm (bcfg_order_list order, grub_uint16_t entry)
{
  grub_uint32_t i, j;
  if (!order->entry || !order->count)
    return grub_error (GRUB_ERR_BAD_OS, "boot option list too small");
  for (i = 0; i < order->count; i++)
  {
    if (entry != order->entry[i])
      continue;
    order->count--;
    for (j = i; j < order->count; j++)
      order->entry[j] = order->entry[j+1];
    return GRUB_ERR_NONE;
  }
  return grub_error (GRUB_ERR_FILE_NOT_FOUND, "entry %04X not found", entry);
}

static grub_err_t
order_add (bcfg_order_list order, grub_uint16_t entry)
{
  grub_uint16_t *new_entry = NULL;
  new_entry = grub_calloc (order->count + 1, sizeof (grub_uint16_t));
  if (!new_entry)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
  grub_memcpy (new_entry, order->entry, order->count * sizeof (grub_uint16_t));
  order->entry[order->count] = entry;
  order->count++;
  return GRUB_ERR_NONE;
}

static char *
order_dump (bcfg_order_list order)
{
  grub_size_t i;
  char *ret = NULL;
  if (!order->entry || !order->count)
    return NULL;
  ret = grub_zalloc (5 * order->count + 1);
  if (!ret)
  {
    grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
    return NULL;
  }
  for (i = 0; i < order->count; i++)
    grub_snprintf (&ret[5*i], 6, "%04X ", order->entry[i]);
  ret[5*order->count] = '\0';
  return ret;
}

static grub_err_t
bcfg_order_get (const char *env, bcfg_order_list order)
{
  grub_uint16_t *data = NULL;
  grub_size_t size = 0;
  if (!env)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "bad argument");

  order->count = 0;
  order->entry = NULL;

  data = efi_get_env (env, &size);
  if (!data)
    return GRUB_ERR_NONE;

  order->entry = grub_zalloc (size);
  if (!order->entry)
  {
    efi_free_pool (data);
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
  }
  grub_memcpy (order->entry, data, size);
  order->count = size / sizeof (grub_uint16_t);
  efi_free_pool (data);
  return GRUB_ERR_NONE;
}

static grub_err_t
bcfg_order_set (const char *env, bcfg_order_list order)
{
  grub_efi_guid_t guid = GRUB_EFI_GLOBAL_VARIABLE_GUID;
  grub_efi_set_variable (env, &guid, order->entry,
                         order->count * sizeof (grub_uint16_t));
  return grub_errno;
}

static grub_err_t
grub_cmd_bcfg (grub_command_t cmd __attribute__((__unused__)),
               int argc, char *argv[])
{
  char env[30];
  char *str = NULL;
  const char *prefix;
  struct _bcfg_loadopt loadopt = {0, NULL, NULL, NULL, 0};
  struct _bcfg_order_list order = {0, NULL};
  if (!argc || grub_strcmp (argv[0], "help") == 0)
    return bcfg_help ();
  prefix = bcfg_env_check_name (argv[0]);
  if (prefix && argc >= 2)
  {
    if (grub_strcmp (argv[1], "list") == 0)
    {
      str = bcfg_env_list (prefix);
      if (argc >= 3)
        grub_env_set (argv[2], str);
      else
        grub_printf ("%s\n", str);
      goto fail;
    }
    if (argc >= 3 && bcfg_env_check_num (argv[2], NULL))
    {
      grub_snprintf (env, 30, "%s%s", prefix, argv[2]);
      if (grub_strcmp (argv[1], "dump") == 0)
      {
        if (bcfg_env_get (env, &loadopt))
          goto fail;
        if (argc == 3)
        {
          str = loadopt_dump (&loadopt, BCFG_LOADOPT_DATA_ALL);
          goto fail;
        }
        str = loadopt_dump (&loadopt, loadopt_check_type (argv[3]));
        if (! str)
          goto fail;
        if (argc >= 5)
          grub_env_set (argv[4], str);
        else
          grub_printf ("%s\n", str);
        goto fail;
      }
      if (grub_strcmp (argv[1], "add") == 0 && argc >= 5)
      {
        loadopt_edit (&loadopt, argv[3], BCFG_LOADOPT_DATA_FILE);
        loadopt_edit (&loadopt, argv[4], BCFG_LOADOPT_DATA_DESC);
        if (argc >= 6)
          loadopt_edit (&loadopt, argv[5], BCFG_LOADOPT_DATA_ATTR);
        else
          loadopt_edit (&loadopt, "AC+", BCFG_LOADOPT_DATA_ATTR);
        bcfg_env_set (env, &loadopt);
        goto fail;
      }
      if (grub_strcmp (argv[1], "del") == 0)
      {
        bcfg_env_del (env);
        goto fail;
      }
      if (grub_strcmp (argv[1], "edit") == 0 && argc >= 5)
      {
        if (bcfg_env_get (env, &loadopt))
          goto fail;
        loadopt_edit (&loadopt, argv[4], loadopt_check_type (argv[3]));
        bcfg_env_set (env, &loadopt);
        goto fail;
      }
    }
  }
  prefix = bcfg_u16_check_name (argv[0]);
  if (prefix && argc >= 2)
  {
    if (grub_strcmp (argv[1], "unset") == 0)
    {
      bcfg_u16_unset (prefix);
      goto fail;
    }
    if (grub_strcmp (argv[1], "get") == 0 && argc >= 3)
    {
      grub_env_set (argv[2], bcfg_u16_get (prefix));
      goto fail;
    }
    if (grub_strcmp (argv[1], "set") == 0 && argc >= 3)
    {
      bcfg_u16_set (prefix, argv[2]);
      goto fail;
    }
  }
  prefix = bcfg_order_check_name (argv[0]);
  if (prefix && argc >= 2)
  {
    if (bcfg_order_get (prefix, &order))
      goto fail;
    if (grub_strcmp (argv[1], "dump") == 0)
    {
      str = order_dump (&order);
      if (argc >= 3)
        grub_env_set (argv[2], str);
      else
        grub_printf ("%s\n", str);
      goto fail;
    }
    if (grub_strcmp (argv[1], "swap") == 0 && argc >= 4)
    {
      if (!order_swap (&order, grub_strtoul (argv[2], NULL, 16),
                       grub_strtoul (argv[3], NULL, 16)))
        bcfg_order_set (prefix, &order);
      goto fail;
    }
    if (grub_strcmp (argv[1], "del") == 0 && argc >= 3)
    {
      if (!order_rm (&order, grub_strtoul (argv[2], NULL, 16)))
        bcfg_order_set (prefix, &order);
      goto fail;
    }
    if (grub_strcmp (argv[1], "add") == 0 && argc >= 3)
    {
      if (!order_add (&order, grub_strtoul (argv[2], NULL, 16)))
        bcfg_order_set (prefix, &order);
      goto fail;
    }
  }

  grub_error (GRUB_ERR_BAD_ARGUMENT, "bad argument");
fail:
  loadopt_free (&loadopt);
  if (order.entry)
    grub_free (order.entry);
  if (str)
    grub_free (str);
  return grub_errno;
}

static grub_command_t cmd;

GRUB_MOD_INIT(bcfg)
{
  cmd = grub_register_command ("bcfg", grub_cmd_bcfg, N_("OPTIONS"),
              N_("Manage the boot options that are stored in NVRAM."
                 " Type \'bcfg help\' for help."));
}

GRUB_MOD_FINI(bcfg)
{
  grub_unregister_command (cmd);
}
