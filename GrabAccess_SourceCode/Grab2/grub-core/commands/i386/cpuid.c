/* cpuid.c - test for CPU features */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006, 2007, 2009  Free Software Foundation, Inc.
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
#include <grub/i386/cpuid.h>
#include <grub/i386/rdmsr.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
{
  {"long-mode", 'l', 0,
    N_("Check if CPU supports 64-bit (long) mode (default)."), 0, 0},
  {"pae", 'p', 0, N_("Check if CPU supports Physical Address Extension."), 0, 0},
  {"set", 's', 0,
    N_("Save read value into variable VARNAME."), N_("VARNAME"), ARG_TYPE_STRING},
  /* eax = 0x00 */
  {"vendor", 'v', 0, N_("Get CPU's manufacturer ID string."), 0, 0},
  {"max", 'm', 0, N_("Get highest function parameter."), 0, 0},
  /* eax = 0x01 */
  {"vme", 0, 0, N_("Check if CPU supports Virtual 8086 mode extensions."), 0, 0},
  {"pse", 0, 0, N_("Check if CPU supports Page Size Extension."), 0, 0},
  {"tsc", 0, 0, N_("Check if CPU supports Time Stamp Counter."), 0, 0},
  {"msr", 0, 0, N_("Check if CPU supports Model-specific registers."), 0, 0},
  {"mtrr", 0, 0, N_("Check if CPU supports Memory Type Range Registers."), 0, 0},
  {"mmx", 0, 0, N_("Check if CPU supports MMX instructions."), 0, 0},
  {"sse", 0, 0, N_("Check if CPU supports SSE instructions."), 0, 0},
  {"sse2", 0, 0, N_("Check if CPU supports SSE2 instructions."), 0, 0},
  {"sse3", 0, 0, N_("Check if CPU supports SSE3 instructions."), 0, 0},
  {"vmx", 0, 0, N_("Check if CPU supports Virtual Machine eXtensions."), 0, 0},
  {"hypervisor", 0, 0, N_("Check if Hypervisor presents."), 0, 0},
  /* eax = 0x06 */
  {"dts", 0, 0, N_("Check if CPU supports DTS."), 0, 0},
  /* eax = 0x40000000 */
  {"vmsign", 0, 0, N_("Get hypervisor signature."), 0, 0},
  /* eax = 0x80000000 */
  {"emax", 'e', 0, N_("Get highest extended function parameter."), 0, 0},
  /* eax = 0x80000002, 0x80000003, 0x80000004 */
  {"brand", 'b', 0, N_("Get CPU's processor brand string."), 0, 0},
  {0, 0, 0, 0, 0, 0},
};

enum options
{
  CPUID_LONG,
  CPUID_PAE,
  CPUID_SET,

  CPUID_VENDOR,
  CPUID_MAX,

  CPUID_VME,
  CPUID_PSE,
  CPUID_TSC,
  CPUID_MSR,
  CPUID_MTRR,
  CPUID_MMX,
  CPUID_SSE,
  CPUID_SSE2,
  CPUID_SSE3,
  CPUID_VMX,
  CPUID_HYPER,

  CPUID_DTS,

  CPUID_VMSIGN,

  CPUID_EMAX,
  CPUID_BRAND,
};

enum
{
  bit_PAE = (1 << 6),
};

enum
{
  bit_LM = (1 << 29)
};

unsigned char grub_cpuid_has_longmode = 0, grub_cpuid_has_pae = 0;

static grub_err_t
cpuid_set_bool (grub_extcmd_context_t ctxt, unsigned int val)
{
  if (ctxt->state[CPUID_SET].set)
    grub_env_set (ctxt->state[CPUID_SET].arg, val ? "true" : "false");
  return val ? GRUB_ERR_NONE : GRUB_ERR_TEST_FAILURE;
}

static grub_err_t
cpuid_set_int (grub_extcmd_context_t ctxt, unsigned int val)
{
  char str[11];
  grub_snprintf (str, 11, "0x%08x", val);
  if (ctxt->state[CPUID_SET].set)
    grub_env_set (ctxt->state[CPUID_SET].arg, str);
  else
    grub_printf ("%s\n", str);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_cpuid (grub_extcmd_context_t ctxt, int argc, char **args)
{
  unsigned int eax, ebx, ecx, edx;
  if (ctxt->state[CPUID_LONG].set)
    return cpuid_set_bool (ctxt, grub_cpuid_has_longmode);
  else if (ctxt->state[CPUID_PAE].set)
    return cpuid_set_bool (ctxt, grub_cpuid_has_pae);
  if (!grub_cpu_is_cpuid_supported ())
    return GRUB_ERR_TEST_FAILURE;
  if (ctxt->state[CPUID_VENDOR].set)
  {
    char vendor[13];
    grub_cpuid (0, eax, ebx, ecx, edx);
    grub_memcpy (&vendor[0], &ebx, 4);
    grub_memcpy (&vendor[4], &edx, 4);
    grub_memcpy (&vendor[8], &ecx, 4);
    vendor[12] = '\0';
    if (ctxt->state[CPUID_SET].set)
      grub_env_set (ctxt->state[CPUID_SET].arg, vendor);
    else
      grub_printf ("%s\n", vendor);
    return GRUB_ERR_NONE;
  }
  else if (ctxt->state[CPUID_MAX].set)
  {
    grub_cpuid (0, eax, ebx, ecx, edx);
    return cpuid_set_int (ctxt, eax);
  }
  else if (ctxt->state[CPUID_VME].set)
  {
    grub_cpuid (1, eax, ebx, ecx, edx);
    return cpuid_set_bool (ctxt, edx & (1 << 1));
  }
  else if (ctxt->state[CPUID_PSE].set)
  {
    grub_cpuid (1, eax, ebx, ecx, edx);
    return cpuid_set_bool (ctxt, edx & (1 << 3));
  }
  else if (ctxt->state[CPUID_TSC].set)
  {
    grub_cpuid (1, eax, ebx, ecx, edx);
    return cpuid_set_bool (ctxt, edx & (1 << 4));
  }
  else if (ctxt->state[CPUID_MSR].set)
  {
    grub_cpuid (1, eax, ebx, ecx, edx);
    return cpuid_set_bool (ctxt, edx & (1 << 5));
  }
  else if (ctxt->state[CPUID_MTRR].set)
  {
    grub_cpuid (1, eax, ebx, ecx, edx);
    return cpuid_set_bool (ctxt, edx & (1 << 12));
  }
  else if (ctxt->state[CPUID_MMX].set)
  {
    grub_cpuid (1, eax, ebx, ecx, edx);
    return cpuid_set_bool (ctxt, edx & (1 << 23));
  }
  else if (ctxt->state[CPUID_SSE].set)
  {
    grub_cpuid (1, eax, ebx, ecx, edx);
    return cpuid_set_bool (ctxt, edx & (1 << 25));
  }
  else if (ctxt->state[CPUID_SSE2].set)
  {
    grub_cpuid (1, eax, ebx, ecx, edx);
    return cpuid_set_bool (ctxt, edx & (1 << 26));
  }
  else if (ctxt->state[CPUID_SSE3].set)
  {
    grub_cpuid (1, eax, ebx, ecx, edx);
    return cpuid_set_bool (ctxt, ecx & (1 << 0));
  }
  else if (ctxt->state[CPUID_VMX].set)
  {
    grub_cpuid (1, eax, ebx, ecx, edx);
    return cpuid_set_bool (ctxt, ecx & (1 << 5));
  }
  else if (ctxt->state[CPUID_HYPER].set)
  {
    grub_cpuid (1, eax, ebx, ecx, edx);
    return cpuid_set_bool (ctxt, ecx & (1 << 31));
  }
  else if (ctxt->state[CPUID_DTS].set)
  {
    grub_cpuid (6, eax, ebx, ecx, edx);
    return cpuid_set_bool (ctxt, eax & (1 << 0));
  }
  else if (ctxt->state[CPUID_VMSIGN].set)
  {
    char vmsign[13];
    grub_cpuid (0x40000000, eax, ebx, ecx, edx);
    grub_memcpy (&vmsign[0], &ebx, 4);
    grub_memcpy (&vmsign[4], &ecx, 4);
    grub_memcpy (&vmsign[8], &edx, 4);
    vmsign[12] = '\0';
    if (ctxt->state[CPUID_SET].set)
      grub_env_set (ctxt->state[CPUID_SET].arg, vmsign);
    else
      grub_printf ("%s\n", vmsign);
    return GRUB_ERR_NONE;
  }
  else if (ctxt->state[CPUID_EMAX].set)
  {
    grub_cpuid (0x80000000, eax, ebx, ecx, edx);
    return cpuid_set_int (ctxt, eax);
  }
  else if (ctxt->state[CPUID_BRAND].set)
  {
    char brand[50];
    grub_memset (brand, 0, 50);
    grub_cpuid (0x80000002, eax, ebx, ecx, edx);
    grub_memcpy (&brand[0], &eax, 4);
    grub_memcpy (&brand[4], &ebx, 4);
    grub_memcpy (&brand[8], &ecx, 4);
    grub_memcpy (&brand[12], &edx, 4);
    grub_cpuid (0x80000003, eax, ebx, ecx, edx);
    grub_memcpy (&brand[16], &eax, 4);
    grub_memcpy (&brand[20], &ebx, 4);
    grub_memcpy (&brand[24], &ecx, 4);
    grub_memcpy (&brand[28], &edx, 4);
    grub_cpuid (0x80000004, eax, ebx, ecx, edx);
    grub_memcpy (&brand[32], &eax, 4);
    grub_memcpy (&brand[36], &ebx, 4);
    grub_memcpy (&brand[40], &ecx, 4);
    grub_memcpy (&brand[44], &edx, 4);
    if (ctxt->state[CPUID_SET].set)
      grub_env_set (ctxt->state[CPUID_SET].arg, brand);
    else
      grub_printf ("%s\n", brand);
    return GRUB_ERR_NONE;
  }

  if (argc > 0)
  {
    char str[11];
    unsigned int num = 0;
    num = grub_strtoul (args[0], NULL, 0);
    grub_cpuid (num, eax, ebx, ecx, edx);
    if (argc > 1)
    {
      grub_snprintf (str, 11, "0x%08x", eax);
      grub_env_set (args[1], str);
    }
    if (argc > 2)
    {
      grub_snprintf (str, 11, "0x%08x", ebx);
      grub_env_set (args[2], str);
    }
    if (argc > 3)
    {
      grub_snprintf (str, 11, "0x%08x", ecx);
      grub_env_set (args[3], str);
    }
    if (argc > 4)
    {
      grub_snprintf (str, 11, "0x%08x", edx);
      grub_env_set (args[4], str);
    }
    return GRUB_ERR_NONE;
  }
  return grub_cpuid_has_longmode ? GRUB_ERR_NONE : GRUB_ERR_TEST_FAILURE;
}

static inline grub_err_t
report_err (int hide, const char *err)
{
  if (hide > 0)
    return GRUB_ERR_BAD_OS;
  else
    return grub_error (GRUB_ERR_BAD_OS, "%s", err);
}

static grub_err_t
grub_cmd_cputemp (grub_extcmd_context_t ctxt __attribute__ ((unused)),
                  int argc, char **args)
{
  char str[5];
  char vendor[13];
  unsigned int eax, ebx, ecx, edx;
  unsigned long long tjunction, dts, val;
  if (!grub_cpu_is_cpuid_supported ())
    return report_err (argc, "cpuid is not supported");
  grub_cpuid (0, eax, ebx, ecx, edx);
  if (eax < 6)
    return report_err (argc, "cpuid eax=0x06 is not supported");
  grub_memcpy (&vendor[0], &ebx, 4);
  grub_memcpy (&vendor[4], &edx, 4);
  grub_memcpy (&vendor[8], &ecx, 4);
  vendor[12] = '\0';
  if (grub_strcmp (vendor, "GenuineIntel") != 0)
    return report_err (argc, "cpu vendor not supported");
  grub_cpuid (6, eax, ebx, ecx, edx);
  if (!(eax & (1 << 0)))
    return report_err (argc, "cpu dts not supported");
  val = grub_msr_read (0x1a2);
  tjunction = (val >> 16) & 0x7f;
  val = grub_msr_read (0x19c);
  dts = (val >> 16) & 0x7f;
  grub_snprintf (str, 5, "%lld",
                 (tjunction > dts) ? (tjunction - dts) : tjunction);
  if (argc > 0)
    grub_env_set (args[0], str);
  else
    grub_printf ("%s\n", str);
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd, cmd_tmp;

GRUB_MOD_INIT(cpuid)
{
#ifdef __x86_64__
  /* grub-emu */
  grub_cpuid_has_longmode = 1;
  grub_cpuid_has_pae = 1;
#else
  unsigned int eax, ebx, ecx, edx;
  unsigned int max_level;
  unsigned int ext_level;

  /* See if we can use cpuid.  */
  asm volatile ("pushfl; pushfl; popl %0; movl %0,%1; xorl %2,%0;"
                "pushl %0; popfl; pushfl; popl %0; popfl"
                : "=&r" (eax), "=&r" (ebx)
                : "i" (0x00200000));
  if (((eax ^ ebx) & 0x00200000) == 0)
    goto done;

  /* Check the highest input value for eax.  */
  grub_cpuid (0, eax, ebx, ecx, edx);
  /* We only look at the first four characters.  */
  max_level = eax;
  if (max_level == 0)
    goto done;

  if (max_level >= 1)
  {
    grub_cpuid (1, eax, ebx, ecx, edx);
    grub_cpuid_has_pae = !!(edx & bit_PAE);
  }

  grub_cpuid (0x80000000, eax, ebx, ecx, edx);
  ext_level = eax;
  if (ext_level < 0x80000000)
    goto done;

  grub_cpuid (0x80000001, eax, ebx, ecx, edx);
  grub_cpuid_has_longmode = !!(edx & bit_LM);
done:
#endif
  cmd = grub_register_extcmd ("cpuid", grub_cmd_cpuid, 0,
                      "[OPTIONS] | EAX EAX_VAR EBX_VAR ECX_VAR EDX_VAR",
                      N_("Check for CPU features."), options);
  cmd_tmp = grub_register_extcmd ("cputemp", grub_cmd_cputemp, 0,
                      "[VAR]", N_("Read CPU temperature."), 0);
}

GRUB_MOD_FINI(cpuid)
{
  grub_unregister_extcmd (cmd);
  grub_unregister_extcmd (cmd_tmp);
}
