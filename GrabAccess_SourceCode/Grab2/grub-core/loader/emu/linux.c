/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008,2009,2010  Free Software Foundation, Inc.
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

#include <grub/loader.h>
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/time.h>

#include <grub/emu/exec.h>
#include <grub/emu/hostfile.h>
#include <grub/emu/misc.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_dl_t my_mod;

static char *kernel_path;
static char *initrd_path;
static char *boot_cmdline;

static grub_err_t
grub_linux_boot (void)
{
  grub_err_t rc = GRUB_ERR_NONE;
  char *initrd_param;
  const char *kexec[] = { "kexec", "-l", kernel_path, boot_cmdline, NULL, NULL };
  const char *systemctl[] = { "systemctl", "kexec", NULL };
  int kexecute = grub_util_get_kexecute();

  if (initrd_path) {
    initrd_param = grub_xasprintf("--initrd=%s", initrd_path);
    kexec[3] = initrd_param;
    kexec[4] = boot_cmdline;
  } else {
    initrd_param = grub_xasprintf("%s", "");
  }

  grub_printf("%serforming 'kexec -l %s %s %s'\n",
	(kexecute) ? "P" : "Not p",
	kernel_path, initrd_param, boot_cmdline);

  if (kexecute)
    rc = grub_util_exec(kexec);

  grub_free(initrd_param);

  if (rc != GRUB_ERR_NONE) {
    grub_error (rc, N_("Error trying to perform kexec load operation."));
    grub_sleep (3);
    return rc;
  }
  if (kexecute < 1)
    grub_fatal (N_("Use '"PACKAGE"-emu --kexec' to force a system restart."));

  grub_printf("Performing 'systemctl kexec' (%s) ",
		(kexecute==1) ? "do-or-die" : "just-in-case");
  rc = grub_util_exec (systemctl);

  if (kexecute == 1)
    grub_fatal (N_("Error trying to perform 'systemctl kexec'"));

  /* need to check read-only root before resetting hard!? */
  grub_printf("Performing 'kexec -e'");
  kexec[1] = "-e";
  kexec[2] = NULL;
  rc = grub_util_exec(kexec);
  if ( rc != GRUB_ERR_NONE )
    grub_fatal (N_("Error trying to directly perform 'kexec -e'."));

  return rc;
}

static grub_err_t
grub_linux_unload (void)
{
  grub_dl_unref (my_mod);
  if ( boot_cmdline != NULL )
    grub_free (boot_cmdline);
  boot_cmdline = NULL;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_linux (grub_command_t cmd __attribute__ ((unused)), int argc, char *argv[])
{
  int i;
  char *tempstr;

  grub_dl_ref (my_mod);

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  if ( !grub_util_is_regular(argv[0]) )
    return grub_error(GRUB_ERR_FILE_NOT_FOUND, N_("Cannot find kernel file %s"), argv[0]);

  if ( kernel_path != NULL )
    grub_free(kernel_path);

  kernel_path = grub_xasprintf("%s", argv[0]);

  if ( boot_cmdline != NULL ) {
    grub_free(boot_cmdline);
    boot_cmdline = NULL;
  }

  if ( argc > 1 )
  {
    boot_cmdline = grub_xasprintf("--command-line=%s", argv[1]);
    for ( i = 2; i < argc; i++ ) {
      tempstr = grub_xasprintf("%s %s", boot_cmdline, argv[i]);
      grub_free(boot_cmdline);
      boot_cmdline = tempstr;
    }
  }

  grub_loader_set (grub_linux_boot, grub_linux_unload, 0);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_initrd (grub_command_t cmd __attribute__ ((unused)), int argc, char *argv[])
{
  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  if ( !grub_util_is_regular(argv[0]) )
    return grub_error(GRUB_ERR_FILE_NOT_FOUND, N_("Cannot find initrd file %s"), argv[0]);

  if ( initrd_path != NULL )
    grub_free(initrd_path);

  initrd_path = grub_xasprintf("%s", argv[0]);

  grub_dl_unref (my_mod);

  return GRUB_ERR_NONE;
}

static grub_command_t cmd_linux, cmd_initrd;

GRUB_MOD_INIT(linux)
{
  cmd_linux = grub_register_command ("linux", grub_cmd_linux, 0, N_("Load Linux."));
  cmd_initrd = grub_register_command ("initrd", grub_cmd_initrd, 0, N_("Load initrd."));
  my_mod = mod;
  kernel_path = NULL;
  initrd_path = NULL;
  boot_cmdline = NULL;
}

GRUB_MOD_FINI(linux)
{
  grub_unregister_command (cmd_linux);
  grub_unregister_command (cmd_initrd);
}
