/* 
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2012  Free Software Foundation, Inc.
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

#include <grub/types.h>
#include <grub/time.h>
#include <grub/misc.h>
#include <grub/acpi.h>

/* Simple checksum by summing all bytes. Used by ACPI and SMBIOS. */
grub_uint8_t
grub_byte_checksum (void *base, grub_size_t size)
{
  grub_uint8_t *ptr;
  grub_uint8_t ret = 0;
  for (ptr = (grub_uint8_t *) base; ptr < ((grub_uint8_t *) base) + size;
       ptr++)
    ret += *ptr;
  return ret;
}

static void *
grub_acpi_rsdt_find_table (struct grub_acpi_table_header *rsdt, const char *sig)
{
  grub_size_t s;
  grub_unaligned_uint32_t *ptr;

  if (!rsdt)
    return 0;

  if (grub_memcmp (rsdt->signature, "RSDT", 4) != 0)
    return 0;

  ptr = (grub_unaligned_uint32_t *) (rsdt + 1);
  s = (rsdt->length - sizeof (*rsdt)) / sizeof (grub_uint32_t);
  for (; s; s--, ptr++)
    {
      struct grub_acpi_table_header *tbl;
      tbl = (struct grub_acpi_table_header *) (grub_addr_t) ptr->val;
      if (grub_memcmp (tbl->signature, sig, 4) == 0)
	return tbl;
    }
  return 0;
}

static void *
grub_acpi_xsdt_find_table (struct grub_acpi_table_header *xsdt, const char *sig)
{
  grub_size_t s;
  grub_unaligned_uint64_t *ptr;

  if (!xsdt)
    return 0;

  if (grub_memcmp (xsdt->signature, "XSDT", 4) != 0)
    return 0;

  ptr = (grub_unaligned_uint64_t *) (xsdt + 1);
  s = (xsdt->length - sizeof (*xsdt)) / sizeof (grub_uint32_t);
  for (; s; s--, ptr++)
    {
      struct grub_acpi_table_header *tbl;
#if GRUB_CPU_SIZEOF_VOID_P != 8
      if (ptr->val >> 32)
	continue;
#endif
      tbl = (struct grub_acpi_table_header *) (grub_addr_t) ptr->val;
      if (grub_memcmp (tbl->signature, sig, 4) == 0)
	return tbl;
    }
  return 0;
}

struct grub_acpi_fadt *
grub_acpi_find_fadt (void)
{
  struct grub_acpi_fadt *fadt = 0;
  struct grub_acpi_rsdp_v10 *rsdpv1;
  struct grub_acpi_rsdp_v20 *rsdpv2;
  rsdpv1 = grub_machine_acpi_get_rsdpv1 ();
  if (rsdpv1)
    fadt = grub_acpi_rsdt_find_table ((struct grub_acpi_table_header *)
				      (grub_addr_t) rsdpv1->rsdt_addr,
				      GRUB_ACPI_FADT_SIGNATURE);
  if (fadt)
    return fadt;
  rsdpv2 = grub_machine_acpi_get_rsdpv2 ();
  if (rsdpv2)
    fadt = grub_acpi_rsdt_find_table ((struct grub_acpi_table_header *)
				      (grub_addr_t) rsdpv2->rsdpv1.rsdt_addr,
				      GRUB_ACPI_FADT_SIGNATURE);
  if (fadt)
    return fadt;
  if (rsdpv2
#if GRUB_CPU_SIZEOF_VOID_P != 8
      && !(rsdpv2->xsdt_addr >> 32)
#endif
      )
    fadt = grub_acpi_xsdt_find_table ((struct grub_acpi_table_header *)
				      (grub_addr_t) rsdpv2->xsdt_addr,
				      GRUB_ACPI_FADT_SIGNATURE);
  if (fadt)
    return fadt;
  return 0;
}
