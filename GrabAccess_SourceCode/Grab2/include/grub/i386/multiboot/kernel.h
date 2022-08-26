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

#ifndef GRUB_MULTIBOOT_KERNEL_HEADER
#define GRUB_MULTIBOOT_KERNEL_HEADER 1

#include <grub/types.h>
#include <grub/dl.h>
#include <grub/i386/coreboot/kernel.h>
#include <grub/acpi.h>
#include <grub/smbios.h>
#include <multiboot.h>
#include <multiboot2.h>

#define CHECK_FLAG(flags, bit) ((flags) & (1 << (bit)))

struct mbi2_extra_info
{
  grub_uint32_t efibs;
  grub_uint32_t systab32;
  grub_uint64_t systab64;
  grub_uint32_t ih32;
  grub_uint64_t ih64;
  struct grub_acpi_rsdp_v10 acpi1;
  struct grub_acpi_rsdp_v20 acpi2;
  struct grub_smbios_eps eps;
  struct grub_smbios_eps3 eps3;
};

extern struct multiboot_info *EXPORT_VAR(grub_multiboot_info);
extern struct mbi2_extra_info *EXPORT_VAR(grub_multiboot2_info);

extern grub_uint32_t EXPORT_VAR(grub_boot_device);

static inline grub_uint32_t grub_mb_check_bios_int (grub_uint8_t intno)
{
  grub_uint32_t *ivt = 0x00;
  return ivt[intno];
}

void EXPORT_FUNC(grub_bios_warm_reset) (void) __attribute__ ((noreturn));
void EXPORT_FUNC(grub_bios_cold_reset) (void) __attribute__ ((noreturn));

#endif
