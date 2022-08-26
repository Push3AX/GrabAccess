/* rdmsr.c - Read CPU model-specific registers */
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
#include <grub/i386/rdmsr.h>

GRUB_MOD_LICENSE("GPLv3+");

static grub_extcmd_t cmd_read;

static const struct grub_arg_option options[] =
{
    {0, 'v', 0, N_("Save read value into variable VARNAME."),
    N_("VARNAME"), ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
};

static grub_err_t
grub_cmd_msr_read (grub_extcmd_context_t ctxt, int argc, char **argv)
{
    grub_uint32_t manufacturer[3], max_cpuid, a, b, c, features, addr;
    grub_uint64_t value;
    const char *ptr;
    char buf[sizeof("1122334455667788")];

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

    if (argc != 1)
        return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));

    grub_errno = GRUB_ERR_NONE;
    ptr = argv[0];
    addr = grub_strtoul (ptr, &ptr, 0);

    if (grub_errno != GRUB_ERR_NONE)
        return grub_errno;
    if (*ptr != '\0')
        return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("invalid argument"));

    value = grub_msr_read (addr);

    if (ctxt->state[0].set)
    {
        grub_snprintf (buf, sizeof(buf), "%llx", (unsigned long long) value);
        grub_env_set (ctxt->state[0].arg, buf);
    }
    else
        grub_printf ("0x%llx\n", (unsigned long long) value);

    return GRUB_ERR_NONE;
}

GRUB_MOD_INIT(rdmsr)
{
    cmd_read = grub_register_extcmd ("rdmsr", grub_cmd_msr_read, 0, N_("ADDR"),
                                    N_("Read a CPU model specific register."),
                                    options);
}

GRUB_MOD_FINI(rdmsr)
{
    grub_unregister_extcmd (cmd_read);
}
