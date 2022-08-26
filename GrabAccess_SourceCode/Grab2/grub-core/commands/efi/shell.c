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
 *
 */

#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/disk.h>
#include <grub/device.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/file.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/net.h>
#include <grub/types.h>
#include <grub/term.h>
#include <grub/i386/efi/shell.h>
#include <grub/i386/efi/shell_efi.h>
#include <grub/procfs.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options_shell[] = {
  {"nostartup", 0, 0, N_("The default startup script will not be executed."), 0, 0},
  {"noconsoleout", 0, 0, N_("Console output will not be displayed."), 0, 0},
  {"noconsolein", 0, 0, N_("Console input will not be accepted from the user."), 0, 0},
  {"delay", 0, 0, N_("Specifies seconds the shell will delay prior to the execution of startup.nsh."), N_("n"), ARG_TYPE_INT},
  {"nomap", 0, 0, N_("The default mappings will not be displayed."), 0, 0},
  {"noversion", 0, 0, N_("The version information will not be displayed."), 0, 0},
  {"startup", 0, 0, N_("The default startup script startup.nsh will be executed."), 0, 0},
  {"nointerrupt", 0, 0, N_("Execution interruption is not allowed."), 0, 0},
  {"nonesting", 0, 0, N_("Specifies that the EFI_SHELL_PROTOCOL.Execute API nesting of a new Shell instance is optional and dependent on the nonesting shell environment variable."), 0, 0},
  {"exit", 0, 0, N_("After running the command line specified when launched, the UEFI Shell must immediately exit."), 0, 0},
  {"device", 0, 0, N_("Specifies the device path."), N_("(hdx,y)"), ARG_TYPE_STRING},
  {0, 0, 0, 0, 0, 0}
};

enum options_shell
{
  SHELL_NOSTARTUP,
  SHELL_NOCONSOLEOUT,
  SHELL_NOCONSOLEIN,
  SHELL_DELAY,
  SHELL_NOMAP,
  SHELL_NOVERSION,
  SHELL_STARTUP,
  SHELL_NOINTERRUPT,
  SHELL_NONESTING,
  SHELL_EXIT,
  SHELL_DEVICE
};

grub_err_t
grub_efi_shell_chain (int argc, char *argv[], grub_efi_device_path_t *dp)
{
  grub_efi_status_t status;
  grub_efi_boot_services_t *b;
  grub_efi_char16_t *cmdline = NULL;
  grub_efi_loaded_image_t *loaded_image;
  grub_efi_handle_t image_handle;
  grub_efi_uintn_t pages;
  grub_efi_physical_address_t address;
  void *shell_image = 0;

  b = grub_efi_system_table->boot_services;

  pages = (((grub_efi_uintn_t) shell_efi_len + ((1 << 12) - 1)) >> 12);
  status = efi_call_4 (b->allocate_pages, GRUB_EFI_ALLOCATE_ANY_PAGES,
                       GRUB_EFI_LOADER_CODE, pages, &address);
  if (status != GRUB_EFI_SUCCESS)
  {
    grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
    goto fail;
  }
  grub_script_execute_sourcecode ("terminal_output console");

  shell_image = (void *) ((grub_addr_t) address);
  grub_memcpy (shell_image, shell_efi, shell_efi_len); 

  status = efi_call_6 (b->load_image, 0, grub_efi_image_handle, dp,
                       shell_image, shell_efi_len, &image_handle);
  if (status != GRUB_EFI_SUCCESS)
  {
    if (status == GRUB_EFI_OUT_OF_RESOURCES)
      grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of resources");
    else
      grub_error (GRUB_ERR_BAD_OS, "cannot load image");
    goto fail;
  }
  loaded_image = grub_efi_get_loaded_image (image_handle);
  if (! loaded_image)
  {
    grub_error (GRUB_ERR_BAD_OS, "no loaded image available");
    goto fail;
  }

  if (argc > 1)
  {
    int i, len;
    grub_efi_char16_t *p16;

    for (i = 1, len = 0; i < argc; i++)
      len += grub_strlen (argv[i]) + 1;

    len *= sizeof (grub_efi_char16_t);
    cmdline = p16 = grub_malloc (len);
    if (! cmdline)
      goto fail;

    for (i = 1; i < argc; i++)
    {
      char *p8;
      grub_printf ("arg[%d] : %s\n", i, argv[i]);
      p8 = argv[i];
      while (*p8)
        *(p16++) = *(p8++);

      *(p16++) = ' ';
    }
    *(--p16) = 0;

    loaded_image->load_options = cmdline;
    loaded_image->load_options_size = len;
  }
  efi_call_3 (b->start_image, image_handle, NULL, NULL);

  status = efi_call_1 (b->unload_image, image_handle);
  if (status != GRUB_EFI_SUCCESS)
    grub_printf ("Exit status code: 0x%08lx\n", (long unsigned int) status);
  grub_free (cmdline);
fail:
  efi_call_2 (b->free_pages, address, pages);
  return grub_errno;
}

static grub_err_t
grub_cmd_shell (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;

  char **shell_args;
  int i = 1;
  shell_args = (char **) grub_malloc ((11 + argc) * sizeof (char **));
  if (!shell_args)
  {
    grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
    goto fail;
  }
  shell_args[0] = (char *) "\\shell.efi";

  if (state[SHELL_NOSTARTUP].set)
  {
    shell_args[i] = (char *)"-nostartup";
    i++;
  }
  if (state[SHELL_NOCONSOLEOUT].set)
  {
    shell_args[i] = (char *)"-noconsoleout";
    i++;
  }
  if (state[SHELL_NOCONSOLEIN].set)
  {
    shell_args[i] = (char *)"-noconsolein";
    i++;
  }
  if (state[SHELL_DELAY].set)
  {
    shell_args[i] = (char *)"-delay ";
    grub_strcat(shell_args[i], state[SHELL_DELAY].arg);
    i++;
  }
  if (state[SHELL_NOMAP].set)
  {
    shell_args[i] = (char *)"-nomap";
    i++;
  }
  if (state[SHELL_NOVERSION].set)
  {
    shell_args[i] = (char *)"-noversion";
    i++;
  }
  if (state[SHELL_STARTUP].set)
  {
    shell_args[i] = (char *)"-startup";
    i++;
  }
  if (state[SHELL_NOINTERRUPT].set)
  {
    shell_args[i] = (char *)"-nointerrupt";
    i++;
  }
  if (state[SHELL_NONESTING].set)
  {
    shell_args[i] = (char *)"-nonesting";
    i++;
  }
  if (state[SHELL_EXIT].set)
  {
    shell_args[i] = (char *)"-exit";
    i++;
  }
  int j;
  for (j = 0; j < argc; j++)
  {
    shell_args[i+j] = args[j];
  }
  i += argc;
  grub_device_t dev = 0;
  grub_efi_device_path_t *dp = NULL;
  grub_efi_handle_t dev_handle = 0;
  if (state[SHELL_DEVICE].set)
  {
    int namelen = grub_strlen (state[SHELL_DEVICE].arg);
    if ((state[SHELL_DEVICE].arg[0] == '(')
         && (state[SHELL_DEVICE].arg[namelen - 1] == ')'))
    {
      state[SHELL_DEVICE].arg[namelen - 1] = 0;
      dev = grub_device_open (&state[SHELL_DEVICE].arg[1]);
    }
    else
      dev = grub_device_open (state[SHELL_DEVICE].arg);
  }
  if (!dev)
    goto chain;
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
      goto chain;
    err = grub_net_route_address (addr, &gateway, &inf);
    if (err)
      goto chain;
    dev_handle = grub_efinet_get_device_handle (inf->card);
  }
  if (dev_handle)
    dp = grub_efi_get_device_path (dev_handle);
chain:
  grub_printf ("DevicePath: ");
  if (!dp)
    grub_printf ("NULL");
  else
    grub_efi_print_device_path (dp);
  grub_printf ("\n");
  grub_efi_shell_chain (i, shell_args, dp);
fail:
  return grub_errno;
}

static grub_extcmd_t cmd_shell;

static char *
get_shell (grub_size_t *sz)
{
  *sz = shell_efi_len;
  char *ret = NULL;
  ret = grub_malloc (*sz);
  if (!ret)
    return NULL;
  grub_memcpy (ret, shell_efi, *sz);
  return ret;
}

struct grub_procfs_entry proc_shell =
{
  .name = "shell.efi",
  .get_contents = get_shell,
};

GRUB_MOD_INIT(shell)
{
  cmd_shell = grub_register_extcmd ("shell", grub_cmd_shell,
                  GRUB_COMMAND_ACCEPT_DASH | GRUB_COMMAND_OPTIONS_AT_START, 
                  N_("PARAM"),
                  N_("Load UEFI shell."), options_shell);
  grub_procfs_register ("shell.efi", &proc_shell);
}

GRUB_MOD_FINI(shell)
{
  grub_unregister_extcmd (cmd_shell);
  grub_procfs_unregister (&proc_shell);
}
