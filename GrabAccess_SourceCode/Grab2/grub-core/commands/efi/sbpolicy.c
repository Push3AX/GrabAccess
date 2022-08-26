/* sbpolicy.c - Install override security policy. */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
 *  Copyright (C) 2019  a1ive@github
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
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/term.h>
#include <grub/types.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
{
  {"install", 'i', 0, N_("Install override security policy"), 0, 0},
  {"uninstall", 'u', 0, N_("Uninstall security policy"), 0, 0},
  {"status", 's', 0, N_("Display security policy status"), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

struct grub_efi_security2_protocol;

typedef grub_efi_status_t (EFIAPI *efi_security2_file_authentication) (
            const struct grub_efi_security2_protocol *this,
            const grub_efi_device_path_protocol_t *device_path,
            void *file_buffer,
            grub_efi_uintn_t file_size,
            grub_efi_boolean_t  boot_policy);

struct grub_efi_security2_protocol
{
  efi_security2_file_authentication file_authentication;
};
typedef struct grub_efi_security2_protocol grub_efi_security2_protocol_t;

struct grub_efi_security_protocol;

typedef grub_efi_status_t (EFIAPI *efi_security_file_authentication_state) (
            const struct grub_efi_security_protocol *this,
            grub_efi_uint32_t authentication_status,
            const grub_efi_device_path_protocol_t *file);
struct grub_efi_security_protocol
{
  efi_security_file_authentication_state file_authentication_state;
};
typedef struct grub_efi_security_protocol grub_efi_security_protocol_t;

static efi_security2_file_authentication es2fa = NULL;
static efi_security_file_authentication_state esfas = NULL;

static grub_efi_status_t EFIAPI
security2_policy_authentication (
    const grub_efi_security2_protocol_t *this __attribute__ ((unused)),
    const grub_efi_device_path_protocol_t *device_path __attribute__ ((unused)),
    void *file_buffer __attribute__ ((unused)),
    grub_efi_uintn_t file_size __attribute__ ((unused)),
    grub_efi_boolean_t boot_policy __attribute__ ((unused)))
{
  return GRUB_EFI_SUCCESS;
}

static grub_efi_status_t EFIAPI
security_policy_authentication (
    const grub_efi_security_protocol_t *this __attribute__ ((unused)),
    grub_efi_uint32_t authentication_status __attribute__ ((unused)),
    const grub_efi_device_path_protocol_t *dp_const __attribute__ ((unused)))
{
  return GRUB_EFI_SUCCESS;
}

static grub_efi_status_t
security_policy_install(void)
{
  grub_efi_security2_protocol_t *security2_protocol = NULL;
  grub_efi_security_protocol_t *security_protocol;
  grub_efi_guid_t guid2 = GRUB_EFI_SECURITY2_PROTOCOL_GUID;
  grub_efi_guid_t guid = GRUB_EFI_SECURITY_PROTOCOL_GUID;

  /* Don't bother with status here.  The call is allowed
   * to fail, since SECURITY2 was introduced in PI 1.2.1
   * If it fails, use security2_protocol == NULL as indicator */
  grub_printf ("Locate: EFI_SECURITY2_PROTOCOL\n");
  security2_protocol = grub_efi_locate_protocol (&guid2, NULL);
  if (security2_protocol)
  {
    grub_printf ("Try: EFI_SECURITY2_PROTOCOL\n");
    es2fa = security2_protocol->file_authentication;
    security2_protocol->file_authentication = 
        security2_policy_authentication;
    /* check for security policy in write protected memory */
    if (security2_protocol->file_authentication
        !=  security2_policy_authentication)
        return GRUB_EFI_ACCESS_DENIED;
    grub_printf ("OK: EFI_SECURITY2_PROTOCOL\n");
  }
  else
    grub_printf ("EFI_SECURITY2_PROTOCOL not found\n");

  grub_printf ("Locate: EFI_SECURITY_PROTOCOL\n");
  security_protocol = grub_efi_locate_protocol (&guid, NULL);
  if (!security_protocol)
  {
    /* This one is mandatory, so there's a serious problem */
    grub_printf ("EFI_SECURITY_PROTOCOL not found\n");
    return GRUB_EFI_NOT_FOUND;
  }

  grub_printf ("Try: EFI_SECURITY_PROTOCOL\n");
  esfas = security_protocol->file_authentication_state;
  security_protocol->file_authentication_state =
    security_policy_authentication;
  /* check for security policy in write protected memory */
  if (security_protocol->file_authentication_state
    !=  security_policy_authentication)
    return GRUB_EFI_ACCESS_DENIED;
  grub_printf ("OK: EFI_SECURITY_PROTOCOL\n");

  return GRUB_EFI_SUCCESS;
}

static grub_efi_status_t
security_policy_uninstall(void)
{
  grub_efi_guid_t guid2 = GRUB_EFI_SECURITY2_PROTOCOL_GUID;
  grub_efi_guid_t guid = GRUB_EFI_SECURITY_PROTOCOL_GUID;

  if (esfas)
  {
    grub_printf ("Uninstall: EFI_SECURITY_PROTOCOL\n");
    grub_efi_security_protocol_t *security_protocol;
    security_protocol = grub_efi_locate_protocol (&guid, NULL);
    if (!security_protocol)
      return GRUB_EFI_NOT_FOUND;
    security_protocol->file_authentication_state = esfas;
    esfas = NULL;
    grub_printf ("OK: EFI_SECURITY_PROTOCOL\n");
  }
  else
    grub_printf ("Skip: EFI_SECURITY_PROTOCOL\n");

  if (es2fa)
  {
    grub_printf ("Uninstall: EFI_SECURITY2_PROTOCOL\n");
    grub_efi_security2_protocol_t *security2_protocol;
    security2_protocol = grub_efi_locate_protocol (&guid2, NULL);
    if (!security2_protocol)
      return GRUB_EFI_NOT_FOUND;
    security2_protocol->file_authentication = es2fa;
    es2fa = NULL;
    grub_printf ("OK: EFI_SECURITY2_PROTOCOL\n");
  }
  else
      grub_printf ("Skip: EFI_SECURITY2_PROTOCOL\n");
  return GRUB_EFI_SUCCESS;
}

static grub_err_t
grub_cmd_sbpolicy (grub_extcmd_context_t ctxt,
        int argc __attribute__ ((unused)),
        char **args __attribute__ ((unused)))
{
  struct grub_arg_list *state = ctxt->state;
  grub_uint8_t secure_boot;
  grub_efi_status_t status;
  grub_size_t datasize = 0;
  char *data = NULL;
  grub_efi_guid_t global = GRUB_EFI_GLOBAL_VARIABLE_GUID;

  if (state[2].set)
  {
    if (esfas)
      grub_printf ("Installed: EFI_SECURITY_PROTOCOL\n");
    else
      grub_printf ("Not installed: EFI_SECURITY_PROTOCOL\n");
    if (es2fa)
      grub_printf ("Installed: EFI_SECURITY2_PROTOCOL\n");
    else
      grub_printf ("Not installed: EFI_SECURITY2_PROTOCOL\n");
    goto done;
  }

  grub_efi_get_variable ("SecureBoot", &global, &datasize, (void **) &data);

  if (!data || !datasize)
  {
    grub_printf ("Not a Secure Boot Platform\n");
    grub_errno = GRUB_ERR_NONE;
    goto done;
  }

  secure_boot = *((grub_uint8_t *)data);
  if (secure_boot)
  {
    grub_printf ("SecureBoot Enabled\n");
    if (state[1].set)
    {
      status = security_policy_uninstall();
      if (status != GRUB_EFI_SUCCESS)
      {
        grub_error (GRUB_ERR_BAD_ARGUMENT,
                    N_("Failed to uninstall security policy"));
        goto done;
      }
    }
    else
    {
      status = security_policy_install();
      if (status != GRUB_EFI_SUCCESS)
      {
        grub_error (GRUB_ERR_BAD_ARGUMENT,
                    N_("Failed to install override security policy"));
          goto done;
      }
    }
  }
  else
    grub_printf ("SecureBoot Disabled\n");

  grub_errno = GRUB_ERR_NONE;

done:
  grub_free (data);
  if (esfas || es2fa)
    grub_env_set ("grub_sb_policy", "1");
  else
    grub_env_set ("grub_sb_policy", "0");
  return grub_errno;
}

static inline int
efi_strcmp (const unsigned char *s1, const grub_efi_char16_t *s2)
{
  while (*s1 && *s2)
  {
    if (*s1 != *s2)
      break;
    s1++;
    s2++;
  }
  return (int) (grub_uint8_t) *s1 - (int) (grub_uint16_t) *s2;
}

typedef grub_efi_status_t
(EFIAPI *get_variable) (grub_efi_char16_t *variable_name,
                        const grub_efi_guid_t *vendor_guid,
                        grub_efi_uint32_t *attributes,
                        grub_efi_uintn_t *data_size,
                        void *data);

typedef grub_efi_status_t
(EFIAPI *exit_bs) (grub_efi_handle_t image_handle,
                   grub_efi_uintn_t map_key);

static get_variable orig_get_variable = NULL;
static exit_bs orig_exit_bs = NULL;
static grub_uint8_t secureboot_status = 0;

static grub_efi_status_t EFIAPI
efi_get_variable_wrapper (grub_efi_char16_t *variable_name,
                          const grub_efi_guid_t *vendor_guid,
                          grub_efi_uint32_t *attributes,
                          grub_efi_uintn_t *data_size,
                          void *data)
{
  unsigned char sb[] = "SecureBoot";

  grub_efi_status_t status;

  status = efi_call_5 (orig_get_variable,
                       variable_name, vendor_guid, attributes, data_size, data);
  if (efi_strcmp (sb, variable_name) == 0)
  {
    if (*data_size)
      grub_memcpy (data, &secureboot_status, 1);
    *data_size = 1;
  }
  return status;
}

static grub_efi_status_t EFIAPI
efi_exit_bs_wrapper (grub_efi_handle_t image_handle,
                     grub_efi_uintn_t map_key)
{
  if (orig_get_variable)
  {
    grub_efi_runtime_services_t *r;
    r = grub_efi_system_table->runtime_services;
    *(get_variable *)&r->get_variable = orig_get_variable;
    orig_get_variable = NULL;
  }
  return efi_call_2 (orig_exit_bs,
                     image_handle, map_key);
}


static int
grub_efi_fucksb_status (void)
{
  return orig_get_variable ? 1:0;
}

static void
grub_efi_fucksb_install (int hook)
{
  if (grub_efi_fucksb_status ())
  {
    grub_printf ("fuck_sb: already installed.\n");
    return;
  }
  grub_efi_runtime_services_t *r;
  r = grub_efi_system_table->runtime_services;
  orig_get_variable = (get_variable) r->get_variable;
  *(get_variable *)&r->get_variable = efi_get_variable_wrapper;
  if (!hook)
    return;
  grub_efi_boot_services_t *b;
  b = grub_efi_system_table->boot_services;
  orig_exit_bs = (exit_bs) b->exit_boot_services;
  *(exit_bs *)&b->exit_boot_services = efi_exit_bs_wrapper;
}

static void
grub_efi_fucksb_disable (void)
{
  secureboot_status = 0;
}

static void
grub_efi_fucksb_enable (void)
{
  secureboot_status = 1;
}

static const struct grub_arg_option options_fuck[] =
{
  {"install", 'i', 0, N_("fuck sb"), 0, 0},
  {"on", 'y', 0, N_("sb on"), 0, 0},
  {"off", 'n', 0, N_("sb off"), 0, 0},
  {"nobs", 'u', 0, N_("don't hook exit_boot_services"), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

static grub_err_t
grub_cmd_fucksb (grub_extcmd_context_t ctxt,
        int argc __attribute__ ((unused)),
        char **args __attribute__ ((unused)))
{
  struct grub_arg_list *state = ctxt->state;
  int ret = 0;
  if (state[0].set)
    grub_efi_fucksb_install (state[3].set? 0: 1);
  else if (state[1].set)
    grub_efi_fucksb_enable ();
  else if (state[2].set)
    grub_efi_fucksb_disable ();
  else
    ret = grub_efi_fucksb_status ();

  return ret;
}

static grub_extcmd_t cmd, cmd_fuck;

GRUB_MOD_INIT(sbpolicy)
{
  cmd = grub_register_extcmd ("sbpolicy", grub_cmd_sbpolicy, 0,
                  N_("[-i|-u|-s]"),
                  N_("Install override security policy."), options);
  cmd_fuck = grub_register_extcmd ("fucksb", grub_cmd_fucksb, 0,
                  N_("[-i [-b]|-y|-n]"),
                  N_("Fuck secure boot."), options_fuck);
}

GRUB_MOD_FINI(sbpolicy)
{
  grub_unregister_extcmd (cmd);
  grub_unregister_extcmd (cmd_fuck);
}
