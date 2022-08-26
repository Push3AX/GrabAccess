/* halt.c - command to halt the computer.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2007,2008  Free Software Foundation, Inc.
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
#include <grub/extcmd.h>
#include <grub/misc.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
{
  {"no-apm", 'n', 0, N_("Do not use APM to halt the computer."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

static grub_err_t __attribute__ ((noreturn))
grub_cmd_halt (grub_extcmd_context_t ctxt,
               int argc __attribute__ ((unused)),
               char **args __attribute__ ((unused)))
{
  struct grub_arg_list *state = ctxt->state;
#if defined (GRUB_MACHINE_COREBOOT) || defined (GRUB_MACHINE_MULTIBOOT) || \
    defined (GRUB_MACHINE_PCBIOS) || defined (GRUB_MACHINE_QEMU)
  int no_apm = 0;
  if (state[0].set)
    no_apm = 1;
  grub_halt (no_apm);
#else
  if (state[0].set)
    grub_puts_ (N_("APM not supported."));
  grub_halt ();
#endif
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(halt)
{
  cmd = grub_register_extcmd ("halt", grub_cmd_halt, 0, "[-n]",
                              N_("Halt the system, if possible using APM."),
                              options);
}

GRUB_MOD_FINI(halt)
{
  grub_unregister_extcmd (cmd);
}
