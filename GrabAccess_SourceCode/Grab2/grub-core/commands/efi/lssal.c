/* lssal.c  - Display EFI SAL systab.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008  Free Software Foundation, Inc.
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
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/normal.h>
#include <grub/charset.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/dl.h>

GRUB_MOD_LICENSE ("GPLv3+");

static void
disp_sal (void *table)
{
  struct grub_efi_sal_system_table *t = table;
  void *desc;
  grub_uint32_t len, l, i;

  grub_printf ("SAL rev: %02x, signature: %x, len:%x\n",
	       t->sal_rev, t->signature, t->total_table_len);
  grub_printf ("nbr entry: %d, chksum: %02x, SAL version A: %02x B: %02x\n",
	       t->entry_count, t->checksum,
	       t->sal_a_version, t->sal_b_version);
  grub_printf ("OEM-ID: %-32s\n", t->oem_id);
  grub_printf ("Product-ID: %-32s\n", t->product_id);

  desc = t->entries;
  len = t->total_table_len - sizeof (struct grub_efi_sal_system_table);
  if (t->total_table_len <= sizeof (struct grub_efi_sal_system_table))
    return;
  for (i = 0; i < t->entry_count; i++)
    {
      switch (*(grub_uint8_t *) desc)
	{
	case GRUB_EFI_SAL_SYSTEM_TABLE_TYPE_ENTRYPOINT_DESCRIPTOR:
	  {
	    struct grub_efi_sal_system_table_entrypoint_descriptor *c = desc;
	    l = sizeof (*c);
	    grub_printf (" Entry point: PAL=%016" PRIxGRUB_UINT64_T
			 " SAL=%016" PRIxGRUB_UINT64_T " GP=%016"
			 PRIxGRUB_UINT64_T "\n",
			 c->pal_proc_addr, c->sal_proc_addr,
			 c->global_data_ptr);
	  }
	  break;
	case GRUB_EFI_SAL_SYSTEM_TABLE_TYPE_MEMORY_DESCRIPTOR:
	  {
	    struct grub_efi_sal_system_table_memory_descriptor *c = desc;
	    l = sizeof (*c);
	    grub_printf (" Memory descriptor entry addr=%016" PRIxGRUB_UINT64_T
			 " len=%" PRIuGRUB_UINT64_T "KB\n",
			 c->addr, c->len * 4);
	    grub_printf ("     sal_used=%d attr=%x AR=%x attr_mask=%x "
			 "type=%x usage=%x\n",
			 c->sal_used, c->attr, c->ar, c->attr_mask, c->mem_type,
			 c->usage);
	  }
	  break;
	case GRUB_EFI_SAL_SYSTEM_TABLE_TYPE_PLATFORM_FEATURES:
	  {
	    struct grub_efi_sal_system_table_platform_features *c = desc;
	    l = sizeof (*c);
	    grub_printf (" Platform features: %02x", c->flags);
	    if (c->flags & GRUB_EFI_SAL_SYSTEM_TABLE_PLATFORM_FEATURE_BUSLOCK)
	      grub_printf (" BusLock");
	    if (c->flags & GRUB_EFI_SAL_SYSTEM_TABLE_PLATFORM_FEATURE_IRQREDIRECT)
	      grub_printf (" IrqRedirect");
	    if (c->flags & GRUB_EFI_SAL_SYSTEM_TABLE_PLATFORM_FEATURE_IPIREDIRECT)

	      grub_printf (" IPIRedirect");
	    if (c->flags & GRUB_EFI_SAL_SYSTEM_TABLE_PLATFORM_FEATURE_ITCDRIFT)

	      grub_printf (" ITCDrift");
	    grub_printf ("\n");
	  }
	  break;
	case GRUB_EFI_SAL_SYSTEM_TABLE_TYPE_TRANSLATION_REGISTER_DESCRIPTOR:
	  {
	    struct grub_efi_sal_system_table_translation_register_descriptor *c
	      = desc;
	    l = sizeof (*c);
	    grub_printf (" TR type=%d num=%d va=%016" PRIxGRUB_UINT64_T
			 " pte=%016" PRIxGRUB_UINT64_T "\n",
			 c->register_type, c->register_number,
			 c->addr, c->page_size);
	  }
	  break;
	case GRUB_EFI_SAL_SYSTEM_TABLE_TYPE_PURGE_TRANSLATION_COHERENCE:
	  {
	    struct grub_efi_sal_system_table_purge_translation_coherence *c
	      = desc;
	    l = sizeof (*c);
	    grub_printf (" PTC coherence nbr=%d addr=%016" PRIxGRUB_UINT64_T "\n",
			 c->ndomains, c->coherence);
	  }
	  break;
	case GRUB_EFI_SAL_SYSTEM_TABLE_TYPE_AP_WAKEUP:
	  {
	    struct grub_efi_sal_system_table_ap_wakeup *c = desc;
	    l = sizeof (*c);
	    grub_printf (" AP wake-up: mec=%d vect=%" PRIxGRUB_UINT64_T "\n",
			 c->mechanism, c->vector);
	  }
	  break;
	default:
	  grub_printf (" unknown entry 0x%x\n", *(grub_uint8_t *)desc);
	  return;
	}
      desc = (grub_uint8_t *)desc + l;
      if (len <= l)
	return;
      len -= l;
    }
}

static grub_err_t
grub_cmd_lssal (struct grub_command *cmd __attribute__ ((unused)),
		int argc __attribute__ ((unused)),
		char **args __attribute__ ((unused)))
{
  const grub_efi_system_table_t *st = grub_efi_system_table;
  grub_efi_configuration_table_t *t = st->configuration_table;
  unsigned int i;
  grub_efi_packed_guid_t guid = GRUB_EFI_SAL_TABLE_GUID;

  for (i = 0; i < st->num_table_entries; i++)
    {
      if (grub_memcmp (&guid, &t->vendor_guid,
		       sizeof (grub_efi_packed_guid_t)) == 0)
	{
	  disp_sal (t->vendor_table);
	  return GRUB_ERR_NONE;
	}
      t++;
    }
  grub_printf ("SAL not found\n");
  return GRUB_ERR_NONE;
}

static grub_command_t cmd;

GRUB_MOD_INIT(lssal)
{
  cmd = grub_register_command ("lssal", grub_cmd_lssal, "",
			       "Display SAL system table.");
}

GRUB_MOD_FINI(lssal)
{
  grub_unregister_command (cmd);
}
