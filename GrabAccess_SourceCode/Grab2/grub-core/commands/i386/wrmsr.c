/* wrmsr.c - Write CPU model-specific registers */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
 *  Based on gcc/gcc/config/i386/driver-i386.c
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
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/command.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/i386/cpuid.h>
#include <grub/i386/wrmsr.h>

GRUB_MOD_LICENSE("GPLv3+");

static grub_command_t cmd_write;

static grub_err_t
grub_cmd_msr_write (grub_command_t cmd __attribute__ ((unused)), int argc, char **argv)
{
    grub_uint32_t manufacturer[3], max_cpuid, a, b, c, features, addr;
    grub_uint64_t value;
    const char *ptr;

    /* The CPUID instruction should be used to determine whether MSRs 
       are supported. (CPUID.01H:EDX[5] = 1) */
    if (! grub_cpu_is_cpuid_supported ())
        return grub_error (GRUB_ERR_BUG, N_("unsupported instruction"));

    grub_cpuid (0, max_cpuid, manufacturer[0], manufacturer[2], manufacturer[1]);

    if (max_cpuid < 1)
        return grub_error (GRUB_ERR_BUG, N_("unsupported instruction"));

    grub_cpuid (1, a, b, c, features);

    if (!(features & (1 << 5)))
        return grub_error (GRUB_ERR_BUG, N_("unsupported instruction"));

    if (argc != 2)
        return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("two arguments expected"));

    grub_errno = GRUB_ERR_NONE;
    ptr = argv[0];
    addr = grub_strtoul (ptr, &ptr, 0);

    if (grub_errno != GRUB_ERR_NONE)
        return grub_errno;
    if (*ptr != '\0')
        return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("invalid argument"));

    ptr = argv[1];
    value = grub_strtoull (ptr, &ptr, 0);

    if (grub_errno != GRUB_ERR_NONE)
        return grub_errno;
    if (*ptr != '\0')
        return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("invalid argument"));

    grub_msr_write (addr, value);

    return GRUB_ERR_NONE;
}

GRUB_MOD_INIT(wrmsr)
{
    cmd_write = grub_register_command ("wrmsr", grub_cmd_msr_write, N_("ADDR VALUE"),
                                      N_("Write a value to a CPU model specific register."));
}

GRUB_MOD_FINI(wrmsr)
{
    grub_unregister_command (cmd_write);
}
