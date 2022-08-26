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

#include <grub/dl.h>
#include <grub/extcmd.h>
#include <grub/file.h>
#include <grub/term.h>
#include <grub/misc.h>
#include <grub/acpi.h>
#include <grub/mm.h>
#include <grub/memory.h>
#include <grub/charset.h>
#include <grub/i18n.h>
#include <grub/efi/efi.h>
#include <grub/efi/api.h>

GRUB_MOD_LICENSE ("GPLv3+");

#pragma GCC diagnostic ignored "-Wcast-align"

static const struct grub_arg_option options[] =
{
  {"load", 'l', 0, N_("Load native exe."), 0, ARG_TYPE_STRING},
  {"cmdline", 'c', 0, N_("Set native exe cmdline."), 0, ARG_TYPE_STRING},
  {"disable", 'd', 0, N_("Disable WPBT table."), 0, ARG_TYPE_NONE},
  {0, 0, 0, 0, 0, 0}
};

enum options
{
  WPBT_L,
  WPBT_C,
  WPBT_D,
};

// Microsoft Data Management table structure
struct acpi_wpbt
{
  struct grub_acpi_table_header header;
  /* The size of the handoff memory buffer
   * containing a platform binary.*/
  grub_uint32_t binary_size;
  /* The 64-bit physical address of a memory
   * buffer containing a platform binary. */
  grub_uint64_t binary_addr;
  /* Description of the layout of the handoff memory buffer.
   * Possible values include:
   * 1 – Image location points to a single
   *     Portable Executable (PE) image at
   *     offset 0 of the specified memory
   *     location. The image is a flat image
   *     where sections have not been expanded
   *     and relocations have not been applied.
   */
  grub_uint8_t content_layout;
  /* Description of the content of the binary
   * image and the usage model of the
   * platform binary. Possible values include:
   * 1 – The platform binary is a native usermode application
   *     that should be executed by the Windows Session
   *     Manager during operating system initialization.
   */
  grub_uint8_t content_type;
  /* Content Type–Specific Fields
   *   Type 1–Specific (native user-mode application)
   */
  grub_uint16_t cmdline_length;
  grub_uint16_t cmdline[0];
} GRUB_PACKED;

static void *
malloc_acpi (grub_efi_uintn_t size)
{
  void *ret;
  grub_efi_status_t status;
  status = grub_efi_allocate_pool (GRUB_EFI_ACPI_RECLAIM_MEMORY,size, &ret);
  if (status != GRUB_EFI_SUCCESS)
    return 0;
  return ret;
}

static void
disable_wpbt (struct grub_acpi_table_header *xsdt)
{
  struct grub_acpi_table_header *entry;
  int entry_cnt, i;
  grub_uint64_t *entry_ptr;
  entry_cnt = (xsdt->length
               - sizeof (struct grub_acpi_table_header)) / sizeof(grub_uint64_t);
  entry_ptr = (grub_uint64_t *)(xsdt + 1);
  for (i = 0; i < entry_cnt; i++, entry_ptr++)
  {
    entry = (struct grub_acpi_table_header *)(grub_addr_t)(*entry_ptr);
    if (grub_memcmp(entry->signature, "WPBT", 4) == 0)
    {
      grub_printf ("WPBT: %p\n", entry);
      grub_memcpy (entry->signature, "WPBT", 4);
      grub_printf ("Patching checksum 0x%x", entry->checksum);
      entry->checksum = 0;
      entry->checksum = 1 + ~grub_byte_checksum (entry, entry->length);
      grub_printf ("->0x%x\n", entry->checksum);
      break;
    }
  }
}

static void
create_wpbt (struct grub_acpi_rsdp_v20 *rsdp,
             grub_file_t file, const grub_uint8_t *cmdline)
{
  struct grub_acpi_table_header *xsdt;
  struct acpi_wpbt *wpbt = NULL;
  void *wpbt_exe = NULL;
  grub_uint16_t *utf16_cmdline = NULL;
  grub_size_t utf16_len = 0;
  if (rsdp->rsdpv1.revision >= 0x02)
    xsdt = (struct grub_acpi_table_header *)(grub_addr_t)(rsdp->xsdt_addr);
  else
  {
    grub_printf ("ACPI rev %d, XSDT not found.\n", rsdp->rsdpv1.revision);
    goto fail;
  }
  if (grub_memcmp(xsdt->signature, "XSDT", 4) != 0)
  {
    grub_printf ("invalid XSDT table\n");
    goto fail;
  }
  if (!file || file->size > (1 << 28))
  {
    grub_printf ("invalid file\n");
    goto fail;
  }
  if (cmdline)
    utf16_len = 2 * (grub_strlen ((const char *)cmdline) + 1);
  if (utf16_len)
  {
    utf16_cmdline = grub_zalloc (utf16_len);
    if (utf16_cmdline)
      grub_utf8_to_utf16 (utf16_cmdline, utf16_len, cmdline, -1, NULL);
    else
      utf16_len = 0;
  }
  wpbt = malloc_acpi (sizeof (struct acpi_wpbt) + utf16_len);
  if (!wpbt)
  {
    grub_printf ("out of memory\n");
    goto fail;
  }
  wpbt_exe = malloc_acpi (file->size);
  if (!wpbt_exe)
  {
    grub_printf ("out of memory\n");
    goto fail;
  }
  grub_file_read (file, wpbt_exe, file->size);
  /* create wpbt header */
  grub_memcpy (wpbt->header.signature, "WPBT", 4);
  grub_memcpy (wpbt->header.oemid, "WPBT  ", 6);
  grub_memcpy (wpbt->header.oemtable, "WPBT    ", 8);
  grub_memcpy (wpbt->header.creator_id, "WPBT", 4);
  wpbt->header.creator_rev = 205;
  wpbt->header.oemrev = 1;
  wpbt->header.length = sizeof (struct acpi_wpbt) + utf16_len;
  wpbt->header.revision = 1;
  /* WPBT-Specific Fields */
  wpbt->binary_size = file->size;
  wpbt->binary_addr = (grub_uint64_t)(grub_addr_t) wpbt_exe;
  wpbt->content_layout = 1;
  wpbt->content_type = 1;
  wpbt->cmdline_length = utf16_len;
  if (utf16_len)
    grub_memcpy (wpbt->cmdline, utf16_cmdline, utf16_len);
  wpbt->header.checksum = 0;
  wpbt->header.checksum = 1 + ~grub_byte_checksum (wpbt, wpbt->header.length);
  /* create new xsdt */
  struct grub_acpi_table_header *new_xsdt =
      malloc_acpi (xsdt->length + sizeof(grub_uint64_t));
  grub_uint64_t *new_xsdt_entry;
  new_xsdt_entry = (grub_uint64_t *)(new_xsdt + 1);
  // copy over old entries
  grub_memcpy (new_xsdt, xsdt, xsdt->length);
  // insert entry
  new_xsdt->length += sizeof(grub_uint64_t);
  grub_uint32_t entry_count =
      (new_xsdt->length - sizeof (struct grub_acpi_table_header))
        / sizeof(grub_uint64_t);
  new_xsdt_entry[entry_count - 1] = (grub_uint64_t)(grub_addr_t) wpbt;
  new_xsdt->checksum = 0;
  new_xsdt->checksum = 1 + ~grub_byte_checksum (xsdt, xsdt->length);
  // invalidate old XSDT table signature and checksum
  grub_memcpy (xsdt, "WPBT", 4);
  // replace old XSDT
  rsdp->xsdt_addr = (grub_uint64_t)(grub_addr_t) new_xsdt;
  // re-calculate RSDP extended checksum
  rsdp->checksum = 0;
  rsdp->checksum = 1 + ~grub_byte_checksum (rsdp, rsdp->length);

  grub_printf ("New WPBT table inserted\n");

  return;
fail:
  if (utf16_cmdline)
    grub_free (utf16_cmdline);
  if (wpbt)
    grub_free (wpbt);
  if (wpbt_exe)
    grub_free (wpbt_exe);
}

static grub_err_t
grub_cmd_wpbt (grub_extcmd_context_t ctxt,
               int argc __attribute__ ((unused)),
               char **args __attribute__ ((unused)))
{
  struct grub_arg_list *state = ctxt->state;
  struct grub_acpi_rsdp_v20 *rsdp = NULL;
  struct grub_acpi_table_header *xsdt;
  grub_file_t file = 0;

  rsdp = grub_machine_acpi_get_rsdpv2 ();
  if (! rsdp)
    return grub_error (GRUB_ERR_BAD_OS, "RSDP V2 not found.");
  if (rsdp->rsdpv1.revision >= 0x02)
    xsdt = (struct grub_acpi_table_header *)(grub_addr_t)(rsdp->xsdt_addr);
  else
    return grub_error (GRUB_ERR_BAD_OS, "XSDT not found.");
  if (grub_memcmp(xsdt->signature, "XSDT", 4) != 0)
    return grub_error (GRUB_ERR_BAD_OS, "Invalid XSDT.");
  disable_wpbt (xsdt);
  if (state[WPBT_L].set)
  {
    file = grub_file_open (state[WPBT_L].arg, GRUB_FILE_TYPE_ACPI_TABLE);
    if (!file)
      return grub_error (GRUB_ERR_FILE_NOT_FOUND, "bad file");
    if (state[WPBT_C].set)
      create_wpbt (rsdp, file, (const grub_uint8_t *)state[WPBT_C].arg);
    else
      create_wpbt (rsdp, file, NULL);
  }
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(wpbt)
{
  cmd = grub_register_extcmd ("wpbt", grub_cmd_wpbt, 0, N_("[OPTIONS]"),
                  N_("Disable the Windows Platform Binary Table (WPBT)."), options);
}

GRUB_MOD_FINI(wpbt)
{
  grub_unregister_extcmd (cmd);
}
