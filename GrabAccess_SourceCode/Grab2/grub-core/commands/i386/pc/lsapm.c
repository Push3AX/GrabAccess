/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010  Free Software Foundation, Inc.
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

#include <grub/machine/int.h>
#include <grub/machine/apm.h>
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/machine/kernel.h>

GRUB_MOD_LICENSE ("GPLv3+");

int
grub_apm_get_info (struct grub_apm_info *info)
{
  struct grub_bios_int_registers regs;

  /* detect APM */
  regs.eax = 0x5300;
  regs.ebx = 0;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x15, &regs);

  if (regs.flags & GRUB_CPU_INT_FLAGS_CARRY)
    return 0;
  info->version = regs.eax & 0xffff;
  info->flags = regs.ecx & 0xffff;

  /* disconnect APM first */
  regs.eax = 0x5304;
  regs.ebx = 0;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x15, &regs);

  /* connect APM */
  regs.eax = 0x5303;
  regs.ebx = 0;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x15, &regs);

  if (regs.flags & GRUB_CPU_INT_FLAGS_CARRY)
    return 0;

  info->cseg = regs.eax & 0xffff;
  info->offset = regs.ebx;
  info->cseg_16 = regs.ecx & 0xffff;
  info->dseg = regs.edx & 0xffff;
  info->cseg_len = regs.esi >> 16;
  info->cseg_16_len = regs.esi & 0xffff;
  info->dseg_len = regs.edi;

  return 1;
}

static grub_err_t
grub_cmd_lsapm (grub_command_t cmd __attribute__ ((unused)),
		int argc __attribute__ ((unused)), char **args __attribute__ ((unused)))
{
  struct grub_apm_info info;
  if (!grub_apm_get_info (&info))
    return grub_error (GRUB_ERR_IO, N_("no APM found"));

  grub_printf_ (N_("Version %u.%u\n"
		   "32-bit CS = 0x%x, len = 0x%x, offset = 0x%x\n"
		   "16-bit CS = 0x%x, len = 0x%x\n"
		   "DS = 0x%x, len = 0x%x\n"),
		info.version >> 8, info.version & 0xff,
		info.cseg, info.cseg_len, info.offset,
		info.cseg_16, info.cseg_16_len,
		info.dseg, info.dseg_len);
  grub_xputs (info.flags & GRUB_APM_FLAGS_16BITPROTECTED_SUPPORTED
	      ? _("16-bit protected interface supported\n")
	      : _("16-bit protected interface unsupported\n"));
  grub_xputs (info.flags & GRUB_APM_FLAGS_32BITPROTECTED_SUPPORTED
	      ? _("32-bit protected interface supported\n")
	      : _("32-bit protected interface unsupported\n"));
  grub_xputs (info.flags & GRUB_APM_FLAGS_CPUIDLE_SLOWS_DOWN
	      ? _("CPU Idle slows down processor\n")
	      : _("CPU Idle doesn't slow down processor\n"));
  grub_xputs (info.flags & GRUB_APM_FLAGS_DISABLED
	      ? _("APM disabled\n") : _("APM enabled\n"));
  grub_xputs (info.flags & GRUB_APM_FLAGS_DISENGAGED
	      ? _("APM disengaged\n") : _("APM engaged\n"));

  return GRUB_ERR_NONE;
}

static grub_command_t cmd;

GRUB_MOD_INIT(lsapm)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (grub_mb_check_bios_int (0x15))
#endif
    cmd = grub_register_command ("lsapm", grub_cmd_lsapm, 0,
                                 N_("Show APM information."));
}

GRUB_MOD_FINI(lsapm)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (grub_mb_check_bios_int (0x15))
#endif
    grub_unregister_command (cmd);
}


