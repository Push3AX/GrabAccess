/* acpi.c - modify acpi tables. */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
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
#include <grub/disk.h>
#include <grub/term.h>
#include <grub/misc.h>
#include <grub/acpi.h>
#include <grub/mm.h>
#include <grub/memory.h>
#include <grub/normal.h>
#include <grub/i18n.h>
#include <grub/procfs.h>

#ifdef GRUB_MACHINE_EFI
#include <grub/efi/efi.h>
#include <grub/efi/api.h>
#include <grub/efi/graphics_output.h>
#include <grub/video.h>
#endif

#pragma GCC diagnostic ignored "-Wcast-align"

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] = {
  {"exclude", 'x', 0,
   N_("Don't load host tables specified by comma-separated list."),
   0, ARG_TYPE_STRING},
  {"load-only", 'n', 0,
   N_("Load only tables specified by comma-separated list."), 0, ARG_TYPE_STRING},
  {"v1", '1', 0, N_("Export version 1 tables to the OS."), 0, ARG_TYPE_NONE},
  {"v2", '2', 0, N_("Export version 2 and version 3 tables to the OS."), 0, ARG_TYPE_NONE},
  {"oemid", 'o', 0, N_("Set OEMID of RSDP, XSDT and RSDT."), 0, ARG_TYPE_STRING},
  {"oemtable", 't', 0,
   N_("Set OEMTABLE ID of RSDP, XSDT and RSDT."), 0, ARG_TYPE_STRING},
  {"oemtablerev", 'r', 0,
   N_("Set OEMTABLE revision of RSDP, XSDT and RSDT."), 0, ARG_TYPE_INT},
  {"oemtablecreator", 'c', 0,
   N_("Set creator field of RSDP, XSDT and RSDT."), 0, ARG_TYPE_STRING},
  {"oemtablecreatorrev", 'd', 0,
   N_("Set creator revision of RSDP, XSDT and RSDT."), 0, ARG_TYPE_INT},
  /* TRANSLATORS: "hangs" here is a noun, not a verb.  */
  {"no-ebda", 'e', 0, N_("Don't update EBDA. May fix failures or hangs on some "
   "BIOSes but makes it ineffective with OS not receiving RSDP from GRUB."),
   0, ARG_TYPE_NONE},
  {"slic", 's', 0, N_("Load SLIC table."), 0, ARG_TYPE_NONE},
  {"msdm", 0, 0, N_("Load/Print MSDM table."), 0, ARG_TYPE_NONE},
#ifdef GRUB_MACHINE_EFI
  {"bgrt", 0, 0, N_("Load BMP file as BGRT image."), 0, ARG_TYPE_NONE},
#endif
  {0, 0, 0, 0, 0, 0}
};

enum options
{
  ACPI_X,
  ACPI_N,
  ACPI_V1,
  ACPI_V2,
  ACPI_ID,
  ACPI_TABLE,
  ACPI_OREV,
  ACPI_C,
  ACPI_CREV,
  ACPI_EBDA,
  ACPI_SLIC,
  ACPI_MSDM,
#ifdef GRUB_MACHINE_EFI
  ACPI_BGRT,
#endif
};

/* rev1 is 1 if ACPIv1 is to be generated, 0 otherwise.
   rev2 contains the revision of ACPIv2+ to generate or 0 if none. */
static int rev1, rev2;
/* OEMID of RSDP, RSDT and XSDT. */
static char root_oemid[6];
/* OEMTABLE of the same tables. */
static char root_oemtable[8];
/* OEMREVISION of the same tables. */
static grub_uint32_t root_oemrev;
/* CreatorID of the same tables. */
static char root_creator_id[4];
/* CreatorRevision of the same tables. */
static grub_uint32_t root_creator_rev;
static struct grub_acpi_rsdp_v10 *rsdpv1_new = 0;
static struct grub_acpi_rsdp_v20 *rsdpv2_new = 0;
static char *playground = 0, *playground_ptr = 0;
static int playground_size = 0;

/* Linked list of ACPI tables. */
struct efiemu_acpi_table
{
  void *addr;
  grub_size_t size;
  struct efiemu_acpi_table *next;
};
static struct efiemu_acpi_table *acpi_tables = 0;

/* DSDT isn't in RSDT. So treat it specially. */
static void *table_dsdt = 0;
/* Pointer to recreated RSDT. */
static void *rsdt_addr = 0;

/* Allocation handles for different tables. */
static grub_size_t dsdt_size = 0;

/* Address of original FACS. */
static grub_uint32_t facs_addr = 0;

struct grub_acpi_rsdp_v20 *
grub_acpi_get_rsdpv2 (void)
{
  if (rsdpv2_new)
    return rsdpv2_new;
  if (rsdpv1_new)
    return 0;
  return grub_machine_acpi_get_rsdpv2 ();
}

struct grub_acpi_rsdp_v10 *
grub_acpi_get_rsdpv1 (void)
{
  if (rsdpv1_new)
    return rsdpv1_new;
  if (rsdpv2_new)
    return 0;
  return grub_machine_acpi_get_rsdpv1 ();
}

#if defined (__i386__) || defined (__x86_64__)

static inline int
iszero (grub_uint8_t *reg, int size)
{
  int i;
  for (i = 0; i < size; i++)
    if (reg[i])
      return 0;
  return 1;
}

/* Context for grub_acpi_create_ebda.  */
struct grub_acpi_create_ebda_ctx
{
  int ebda_len;
  grub_uint64_t highestlow;
};

/* Helper for grub_acpi_create_ebda.  */
static int
find_hook (grub_uint64_t start, grub_uint64_t size, grub_memory_type_t type,
       void *data)
{
  struct grub_acpi_create_ebda_ctx *ctx = data;
  grub_uint64_t end = start + size;
  if (type != GRUB_MEMORY_AVAILABLE)
    return 0;
  if (end > 0x100000)
    end = 0x100000;
  if (end > start + ctx->ebda_len
      && ctx->highestlow < ((end - ctx->ebda_len) & (~0xf)) )
    ctx->highestlow = (end - ctx->ebda_len) & (~0xf);
  return 0;
}

grub_err_t
grub_acpi_create_ebda (void)
{
  struct grub_acpi_create_ebda_ctx ctx = {
    .highestlow = 0
  };
  int ebda_kb_len = 0;
  int mmapregion = 0;
  grub_uint8_t *ebda, *v1inebda = 0, *v2inebda = 0;
  grub_uint8_t *targetebda, *target;
  struct grub_acpi_rsdp_v10 *v1;
  struct grub_acpi_rsdp_v20 *v2;

  ebda = (grub_uint8_t *) (grub_addr_t) ((*((grub_uint16_t *)0x40e)) << 4);
  grub_dprintf ("acpi", "EBDA @%p\n", ebda);
  if (ebda)
    ebda_kb_len = *(grub_uint16_t *) ebda;
  grub_dprintf ("acpi", "EBDA length 0x%x\n", ebda_kb_len);
  if (ebda_kb_len > 16)
    ebda_kb_len = 0;
  ctx.ebda_len = (ebda_kb_len + 1) << 10;

  /* FIXME: use low-memory mm allocation once it's available. */
  grub_mmap_iterate (find_hook, &ctx);
  targetebda = (grub_uint8_t *) (grub_addr_t) ctx.highestlow;
  grub_dprintf ("acpi", "creating ebda @%llx\n",
        (unsigned long long) ctx.highestlow);
  if (! ctx.highestlow)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY,
               "couldn't find space for the new EBDA");

  mmapregion = grub_mmap_register ((grub_addr_t) targetebda, ctx.ebda_len,
                   GRUB_MEMORY_RESERVED);
  if (! mmapregion)
    return grub_errno;

  /* XXX: EBDA is unstandardized, so this implementation is heuristical. */
  if (ebda_kb_len)
    grub_memcpy (targetebda, ebda, 0x400);
  else
    grub_memset (targetebda, 0, 0x400);
  *((grub_uint16_t *) targetebda) = ebda_kb_len + 1;
  target = targetebda;

  v1 = grub_acpi_get_rsdpv1 ();
  v2 = grub_acpi_get_rsdpv2 ();
  if (v2 && v2->length > 40)
    v2 = 0;

  /* First try to replace already existing rsdp. */
  if (v2)
    {
      grub_dprintf ("acpi", "Scanning EBDA for old rsdpv2\n");
      for (; target < targetebda + 0x400 - v2->length; target += 0x10)
    if (grub_memcmp (target, GRUB_RSDP_SIGNATURE, GRUB_RSDP_SIGNATURE_SIZE) == 0
        && grub_byte_checksum (target,
                   sizeof (struct grub_acpi_rsdp_v10)) == 0
        && ((struct grub_acpi_rsdp_v10 *) target)->revision != 0
        && ((struct grub_acpi_rsdp_v20 *) target)->length <= v2->length)
      {
        grub_memcpy (target, v2, v2->length);
        grub_dprintf ("acpi", "Copying rsdpv2 to %p\n", target);
        v2inebda = target;
        target += v2->length;
        target = (grub_uint8_t *) ALIGN_UP((grub_addr_t) target, 16);
        v2 = 0;
        break;
      }
    }

  if (v1)
    {
      grub_dprintf ("acpi", "Scanning EBDA for old rsdpv1\n");
      for (; target < targetebda + 0x400 - sizeof (struct grub_acpi_rsdp_v10);
       target += 0x10)
    if (grub_memcmp (target, GRUB_RSDP_SIGNATURE, GRUB_RSDP_SIGNATURE_SIZE) == 0
        && grub_byte_checksum (target,
                   sizeof (struct grub_acpi_rsdp_v10)) == 0)
      {
        grub_memcpy (target, v1, sizeof (struct grub_acpi_rsdp_v10));
        grub_dprintf ("acpi", "Copying rsdpv1 to %p\n", target);
        v1inebda = target;
        target += sizeof (struct grub_acpi_rsdp_v10);
        target = (grub_uint8_t *) ALIGN_UP((grub_addr_t) target, 16);
        v1 = 0;
        break;
      }
    }

  target = targetebda + 0x100;

  /* Try contiguous zeros. */
  if (v2)
    {
      grub_dprintf ("acpi", "Scanning EBDA for block of zeros\n");
      for (; target < targetebda + 0x400 - v2->length; target += 0x10)
    if (iszero (target, v2->length))
      {
        grub_dprintf ("acpi", "Copying rsdpv2 to %p\n", target);
        grub_memcpy (target, v2, v2->length);
        v2inebda = target;
        target += v2->length;
        target = (grub_uint8_t *) ALIGN_UP((grub_addr_t) target, 16);
        v2 = 0;
        break;
      }
    }

  if (v1)
    {
      grub_dprintf ("acpi", "Scanning EBDA for block of zeros\n");
      for (; target < targetebda + 0x400 - sizeof (struct grub_acpi_rsdp_v10);
       target += 0x10)
    if (iszero (target, sizeof (struct grub_acpi_rsdp_v10)))
      {
        grub_dprintf ("acpi", "Copying rsdpv1 to %p\n", target);
        grub_memcpy (target, v1, sizeof (struct grub_acpi_rsdp_v10));
        v1inebda = target;
        target += sizeof (struct grub_acpi_rsdp_v10);
        target = (grub_uint8_t *) ALIGN_UP((grub_addr_t) target, 16);
        v1 = 0;
        break;
      }
    }

  if (v1 || v2)
    {
      grub_mmap_unregister (mmapregion);
      return grub_error (GRUB_ERR_OUT_OF_MEMORY,
             "couldn't find suitable spot in EBDA");
    }

  /* Remove any other RSDT. */
  for (target = targetebda;
       target < targetebda + 0x400 - sizeof (struct grub_acpi_rsdp_v10);
       target += 0x10)
    if (grub_memcmp (target, GRUB_RSDP_SIGNATURE, GRUB_RSDP_SIGNATURE_SIZE) == 0
    && grub_byte_checksum (target,
                   sizeof (struct grub_acpi_rsdp_v10)) == 0
    && target != v1inebda && target != v2inebda)
      *target = 0;

  grub_dprintf ("acpi", "Switching EBDA\n");
  (*((grub_uint16_t *) 0x40e)) = ((grub_addr_t) targetebda) >> 4;
  grub_dprintf ("acpi", "EBDA switched\n");

  return GRUB_ERR_NONE;
}
#endif

/* Create tables common to ACPIv1 and ACPIv2+ */
static void
setup_common_tables (void)
{
  struct efiemu_acpi_table *cur;
  struct grub_acpi_table_header *rsdt;
  grub_uint32_t *rsdt_entry;
  int numoftables;

  /* Treat DSDT. */
  grub_memcpy (playground_ptr, table_dsdt, dsdt_size);
  grub_free (table_dsdt);
  table_dsdt = playground_ptr;
  playground_ptr += dsdt_size;

  /* Treat other tables. */
  for (cur = acpi_tables; cur; cur = cur->next)
    {
      struct grub_acpi_fadt *fadt;

      grub_memcpy (playground_ptr, cur->addr, cur->size);
      grub_free (cur->addr);
      cur->addr = playground_ptr;
      playground_ptr += cur->size;

      /* If it's FADT correct DSDT and FACS addresses. */
      fadt = (struct grub_acpi_fadt *) cur->addr;
      if (grub_memcmp (fadt->hdr.signature, GRUB_ACPI_FADT_SIGNATURE,
               sizeof (fadt->hdr.signature)) == 0)
    {
      fadt->dsdt_addr = (grub_addr_t) table_dsdt;
      fadt->facs_addr = facs_addr;

      /* Does a revision 2 exist at all? */
      if (fadt->hdr.revision >= 3)
        {
          fadt->dsdt_xaddr = (grub_addr_t) table_dsdt;
          fadt->facs_xaddr = facs_addr;
        }

      /* Recompute checksum. */
      fadt->hdr.checksum = 0;
      fadt->hdr.checksum = 1 + ~grub_byte_checksum (fadt, fadt->hdr.length);
    }
    }

  /* Fill RSDT entries. */
  numoftables = 0;
  for (cur = acpi_tables; cur; cur = cur->next)
    numoftables++;

  rsdt_addr = rsdt = (struct grub_acpi_table_header *) playground_ptr;
  playground_ptr += sizeof (struct grub_acpi_table_header) + sizeof (grub_uint32_t) * numoftables;

  rsdt_entry = (grub_uint32_t *) (rsdt + 1);

  /* Fill RSDT header. */
  grub_memcpy (&(rsdt->signature), "RSDT", 4);
  rsdt->length = sizeof (struct grub_acpi_table_header) + sizeof (grub_uint32_t) * numoftables;
  rsdt->revision = 1;
  grub_memcpy (&(rsdt->oemid), root_oemid, sizeof (rsdt->oemid));
  grub_memcpy (&(rsdt->oemtable), root_oemtable, sizeof (rsdt->oemtable));
  rsdt->oemrev = root_oemrev;
  grub_memcpy (&(rsdt->creator_id), root_creator_id, sizeof (rsdt->creator_id));
  rsdt->creator_rev = root_creator_rev;

  for (cur = acpi_tables; cur; cur = cur->next)
    *(rsdt_entry++) = (grub_addr_t) cur->addr;

  /* Recompute checksum. */
  rsdt->checksum = 0;
  rsdt->checksum = 1 + ~grub_byte_checksum (rsdt, rsdt->length);
}

/* Regenerate ACPIv1 RSDP */
static void
setv1table (void)
{
  /* Create RSDP. */
  rsdpv1_new = (struct grub_acpi_rsdp_v10 *) playground_ptr;
  playground_ptr += sizeof (struct grub_acpi_rsdp_v10);
  grub_memcpy (&(rsdpv1_new->signature), GRUB_RSDP_SIGNATURE,
           sizeof (rsdpv1_new->signature));
  grub_memcpy (&(rsdpv1_new->oemid), root_oemid, sizeof  (rsdpv1_new->oemid));
  rsdpv1_new->revision = 0;
  rsdpv1_new->rsdt_addr = (grub_addr_t) rsdt_addr;
  rsdpv1_new->checksum = 0;
  rsdpv1_new->checksum = 1 + ~grub_byte_checksum (rsdpv1_new,
                          sizeof (*rsdpv1_new));
  grub_dprintf ("acpi", "Generated ACPIv1 tables\n");
}

static void
setv2table (void)
{
  struct grub_acpi_table_header *xsdt;
  struct efiemu_acpi_table *cur;
  grub_uint64_t *xsdt_entry;
  int numoftables;

  numoftables = 0;
  for (cur = acpi_tables; cur; cur = cur->next)
    numoftables++;

  /* Create XSDT. */
  xsdt = (struct grub_acpi_table_header *) playground_ptr;
  playground_ptr += sizeof (struct grub_acpi_table_header) + sizeof (grub_uint64_t) * numoftables;

  xsdt_entry = (grub_uint64_t *)(xsdt + 1);
  for (cur = acpi_tables; cur; cur = cur->next)
    *(xsdt_entry++) = (grub_addr_t) cur->addr;
  grub_memcpy (&(xsdt->signature), "XSDT", 4);
  xsdt->length = sizeof (struct grub_acpi_table_header) + sizeof (grub_uint64_t) * numoftables;
  xsdt->revision = 1;
  grub_memcpy (&(xsdt->oemid), root_oemid, sizeof (xsdt->oemid));
  grub_memcpy (&(xsdt->oemtable), root_oemtable, sizeof (xsdt->oemtable));
  xsdt->oemrev = root_oemrev;
  grub_memcpy (&(xsdt->creator_id), root_creator_id, sizeof (xsdt->creator_id));
  xsdt->creator_rev = root_creator_rev;
  xsdt->checksum = 0;
  xsdt->checksum = 1 + ~grub_byte_checksum (xsdt, xsdt->length);

  /* Create RSDPv2. */
  rsdpv2_new = (struct grub_acpi_rsdp_v20 *) playground_ptr;
  playground_ptr += sizeof (struct grub_acpi_rsdp_v20);
  grub_memcpy (&(rsdpv2_new->rsdpv1.signature), GRUB_RSDP_SIGNATURE,
           sizeof (rsdpv2_new->rsdpv1.signature));
  grub_memcpy (&(rsdpv2_new->rsdpv1.oemid), root_oemid,
           sizeof (rsdpv2_new->rsdpv1.oemid));
  rsdpv2_new->rsdpv1.revision = rev2;
  rsdpv2_new->rsdpv1.rsdt_addr = (grub_addr_t) rsdt_addr;
  rsdpv2_new->rsdpv1.checksum = 0;
  rsdpv2_new->rsdpv1.checksum = 1 + ~grub_byte_checksum
    (&(rsdpv2_new->rsdpv1), sizeof (rsdpv2_new->rsdpv1));
  rsdpv2_new->length = sizeof (*rsdpv2_new);
  rsdpv2_new->xsdt_addr = (grub_addr_t) xsdt;
  rsdpv2_new->checksum = 0;
  rsdpv2_new->checksum = 1 + ~grub_byte_checksum (rsdpv2_new,
                          rsdpv2_new->length);
  grub_dprintf ("acpi", "Generated ACPIv2 tables\n");
}

static void
free_tables (void)
{
  struct efiemu_acpi_table *cur, *t;
  if (table_dsdt)
    grub_free (table_dsdt);
  for (cur = acpi_tables; cur;)
    {
      t = cur;
      grub_free (cur->addr);
      cur = cur->next;
      grub_free (t);
    }
  acpi_tables = 0;
  table_dsdt = 0;
}

#define SLIC_LENGTH 0x176

static void
slic_print (const char *slic_str, grub_size_t n, const char *line)
{
  grub_size_t i;
  if (line)
    grub_printf ("%s", line);
  for (i = 0; i < n; i++)
    grub_printf ("%c", slic_str[i]);
  grub_printf ("\n");
}

static struct grub_acpi_table_header *
acpi_find_slic (struct grub_acpi_rsdp_v10 *rsdp)
{
  grub_uint32_t len;
  grub_uint32_t *desc;
  struct grub_acpi_table_header *t;
  t = (void *)(grub_addr_t)(rsdp->rsdt_addr);
  len = t->length - sizeof (*t);
  desc = (grub_uint32_t *) (t + 1);
  for (; len >= sizeof (*desc); desc++, len -= sizeof (*desc))
  {
    t = (struct grub_acpi_table_header *) (grub_addr_t) *desc;
    if (t == NULL)
      continue;
    if (grub_memcmp (t->signature, "SLIC", sizeof (t->signature)) == 0)
      return t;
  }
  return NULL;
}

struct software_licensing
{
  grub_uint32_t version;
  grub_uint32_t reserved;
  grub_uint32_t data_type;
  grub_uint32_t data_reserved;
  grub_uint32_t data_length;
  char data[29];
} GRUB_PACKED;

// Microsoft Data Management table structure
struct acpi_msdm
{
  struct grub_acpi_table_header header;
  struct software_licensing soft;
} GRUB_PACKED;

/* Empty MSDM
unsigned char empty_msdm[] =
{
  0x4D,0x53,0x44,0x4D,
  0x55,
  0x00,0x00,0x00,0x03,
  0x00,
  0x20,0x20,0x20,0x20,0x20,0x20,
  0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
  0x01,0x00,0x00,0x00,
  0x4D,0x53,0x46,0x54,
  0x13,0x00,0x00,0x01,
  0x01,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x01,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x1D,0x00,0x00,0x00,
  0x20,0x20,0x20,0x20,0x20,0x20,
  0x20,0x20,0x20,0x20,0x20,0x20,
  0x20,0x20,0x20,0x20,0x20,0x20,
  0x20,0x20,0x20,0x20,0x20,0x20,
  0x20,0x20,0x20,0x20,0x20
};*/

static struct acpi_msdm *
acpi_get_msdm (struct grub_acpi_rsdp_v20 *rsdp)
{
  struct grub_acpi_table_header *xsdt, *entry;
  int entry_cnt, i;
  grub_uint64_t *entry_ptr;
  if (rsdp->rsdpv1.revision >= 0x02)
    xsdt = (struct grub_acpi_table_header *)(grub_addr_t)(rsdp->xsdt_addr);
  else
  {
    grub_printf ("ACPI rev %d, XSDT not found.\n", rsdp->rsdpv1.revision);
    return NULL;
  }
  if (grub_memcmp(xsdt->signature, "XSDT", 4) != 0)
  {
    grub_printf ("invalid XSDT table\n");
    return NULL;
  }
  entry_cnt = (xsdt->length
               - sizeof (struct grub_acpi_table_header)) / sizeof(grub_uint64_t);
  entry_ptr = (grub_uint64_t *)(xsdt + 1);
  for (i = 0; i < entry_cnt; i++, entry_ptr++)
  {
    entry = (struct grub_acpi_table_header *)(grub_addr_t)(*entry_ptr);
    if (grub_memcmp(entry->signature, "MSDM", 4) == 0)
    {
      grub_printf ("found MSDM: %p\n", (struct acpi_msdm *)entry);
      return (struct acpi_msdm *)entry;
    }
  }
  grub_printf ("MSDM not found.\n");
  return NULL;
}

static void
print_msdm (struct acpi_msdm *msdm)
{
  if (! msdm)
    return;
  grub_printf ("ACPI Standard Header\n");
  slic_print ((char *)msdm->header.signature, 4, "Signature: ");
  grub_printf ("Length: 0x%08x\n", msdm->header.length);
  grub_printf ("Revision: 0x%02x\n", msdm->header.revision);
  grub_printf ("Checksum: 0x%02x\n", msdm->header.checksum);
  slic_print ((char *)msdm->header.oemid,
              sizeof (root_oemid), "OEM ID: ");
  slic_print ((char *)msdm->header.oemtable,
              sizeof (root_oemtable), "OEM Table ID: ");
  grub_printf ("OEM Revision: 0x%08x\n", msdm->header.oemrev);
  slic_print ((char *)msdm->header.creator_id,
              sizeof (root_creator_id), "Creator ID: ");
  grub_printf ("Creator Revision: 0x%08x\n", msdm->header.creator_rev);

  grub_printf ("Software Licensing\n");
  grub_printf ("Version: 0x%08x\n", msdm->soft.version);
  grub_printf ("Reserved: 0x%08x\n", msdm->soft.reserved);
  grub_printf ("Data Type: 0x%08x\n", msdm->soft.data_type);
  grub_printf ("Data Reserved: 0x%08x\n", msdm->soft.data_reserved);
  grub_printf ("Data Length: 0x%08x\n", msdm->soft.data_length);
  slic_print ((char *)msdm->soft.data, 29, "Data: ");
}

#ifdef GRUB_MACHINE_EFI

/* https://github.com/Jamesits/BGRTInjector */

struct bmp_header
{
  // bmfh
  grub_uint8_t bftype[2];
  grub_uint32_t bfsize;
  grub_uint16_t bfreserved1;
  grub_uint16_t bfreserved2;
  grub_uint32_t bfoffbits;
  // bmih
  grub_uint32_t bisize;
  grub_int32_t biwidth;
  grub_int32_t biheight;
  grub_uint16_t biplanes;
  grub_uint16_t bibitcount;
  grub_uint32_t bicompression;
  grub_uint32_t bisizeimage;
  grub_int32_t bixpelspermeter;
  grub_int32_t biypelspermeter;
  grub_uint32_t biclrused;
  grub_uint32_t biclrimportant;
} GRUB_PACKED;

static grub_efi_boolean_t
bmp_sanity_check(char *buf, grub_size_t size)
{
  // check BMP magic
  if (grub_strncmp ("BM", buf, 2))
  {
    grub_printf ("Unsupported image file.\n");
    return FALSE;
  }
  // check BMP header size
  struct bmp_header *bmp = (struct bmp_header *) buf;
  if (size < bmp->bfsize)
  {
    grub_printf ("Bad BMP file.\n");
    return FALSE;
  }

  return TRUE;
}

static void *
malloc_acpi (grub_efi_uintn_t size)
{
  void *ret;
  grub_efi_status_t status;

  status = grub_efi_allocate_pool (GRUB_EFI_ACPI_RECLAIM_MEMORY,
                        size, &ret);

  if (status != GRUB_EFI_SUCCESS)
  {
    grub_fatal ("malloc failed\n");
    return 0;
  }

  return ret;
}

struct acpi_bgrt
{
  struct grub_acpi_table_header header;
  // 2-bytes (16 bit) version ID. This value must be 1.
  grub_uint16_t version;
  // 1-byte status field indicating current status about the table.
  // Bits[7:1] = Reserved (must be zero)
  // Bit [0] = Valid. A one indicates the boot image graphic is valid.
  grub_uint8_t status;
  // 0 = Bitmap
  // 1 - 255  Reserved (for future use)
  grub_uint8_t type;
  // physical address pointing to the firmware's in-memory copy of the image.
  grub_uint64_t addr;
  // (X, Y) display offset of the top left corner of the boot image.
  // The top left corner of the display is at offset (0, 0).
  grub_uint32_t x;
  grub_uint32_t y;
} GRUB_PACKED;

static struct acpi_bgrt *bgrt = NULL;
static char *bgrt_bmp = NULL;
static int bgrt_patched = 0;

static void
acpi_get_bgrt (struct grub_acpi_table_header *xsdt)
{
  struct grub_acpi_table_header *entry;
  int entry_cnt, i;
  grub_uint64_t *entry_ptr;
  bgrt_patched = 0;
  entry_cnt = (xsdt->length
               - sizeof (struct grub_acpi_table_header)) / sizeof(grub_uint64_t);
  entry_ptr = (grub_uint64_t *)(xsdt + 1);
  for (i = 0; i < entry_cnt; i++, entry_ptr++)
  {
    entry = (struct grub_acpi_table_header *)(grub_addr_t)(*entry_ptr);
    if (grub_memcmp(entry->signature, "BGRT", 4) == 0)
    {
      grub_printf ("found BGRT: %p\n", (struct acpi_bgrt *)entry);
      /* blow up the old table */
      grub_memcpy ((char *)(grub_addr_t)*entry_ptr, "WPBT", 4);
      *entry_ptr = (grub_uint64_t)(grub_addr_t) bgrt;
      bgrt_patched = 1;
      return;
    }
  }
  grub_printf ("BGRT not found.\n");
  return;
}

static void
get_bgrt_xy (struct bmp_header *bmp, grub_uint32_t *x, grub_uint32_t *y)
{
  *x = *y = 0;
  grub_uint32_t screen_width = 0;
  grub_uint32_t screen_height = 0;
  grub_uint32_t bmp_width = (grub_uint32_t) bmp->biwidth;
  grub_uint32_t bmp_height = (grub_uint32_t) bmp->biheight;
  struct grub_video_mode_info info;
  struct grub_efi_gop *gop = NULL;
  grub_efi_guid_t gop_guid = GRUB_EFI_GOP_GUID;
  grub_efi_status_t status;
  grub_efi_boot_services_t *b;
  b = grub_efi_system_table->boot_services;
  status = efi_call_3 (b->locate_protocol, &gop_guid, NULL, (void **)&gop);
  if (status == GRUB_EFI_SUCCESS)
  {
    screen_width = gop->mode->info->width;
    screen_height = gop->mode->info->height;
  }
  if (grub_video_get_info (&info) == GRUB_ERR_NONE)
  {
    screen_width = (screen_width < info.width) ? info.width : screen_width;
    screen_height = (screen_height < info.height) ? info.height : screen_height;
  }
  grub_printf ("screen: %ux%u\n", screen_width, screen_height);
  grub_printf ("image : %ux%u\n", bmp_width, bmp_height);
  if (screen_width > bmp_width)
    *x = (screen_width - bmp_width) / 2;
  if (screen_height > bmp_height)
    *y = (screen_height - bmp_height) / 2;
  grub_printf ("offset_x=%u, offset_y=%u\n", *x, *y);
}

static void
create_bgrt (grub_file_t file, struct grub_acpi_rsdp_v20 *rsdp)
{
  struct grub_acpi_table_header *xsdt;
  if (rsdp->rsdpv1.revision >= 0x02)
    xsdt = (struct grub_acpi_table_header *)(grub_addr_t)(rsdp->xsdt_addr);
  else
  {
    grub_printf ("ACPI rev %d, XSDT not found.\n", rsdp->rsdpv1.revision);
    return;
  }
  if (grub_memcmp(xsdt->signature, "XSDT", 4) != 0)
  {
    grub_printf ("invalid XSDT table\n");
    return;
  }

  bgrt = malloc_acpi (sizeof (struct acpi_bgrt));
  bgrt_bmp = malloc_acpi (file->size);

  grub_file_read (file, bgrt_bmp, file->size);
  struct bmp_header *bmp = (struct bmp_header *) bgrt_bmp;
  if (!bmp_sanity_check(bgrt_bmp, file->size))
  {
    grub_efi_free_pool (bgrt_bmp);
    grub_efi_free_pool (bgrt);
    return;
  }
  grub_uint32_t x,y;
  get_bgrt_xy (bmp, &x, &y);
  bgrt->x = x;
  bgrt->y = y;
  grub_memcpy (bgrt->header.signature, "WPBT", 4);
  grub_memcpy (bgrt->header.oemid, "WPBT  ", 6);
  grub_memcpy (bgrt->header.oemtable, "WPBT    ", 8);
  grub_memcpy (bgrt->header.creator_id, "WPBT", 4);
  bgrt->header.creator_rev = 205;
  bgrt->header.oemrev = 1;
  bgrt->header.length = sizeof (struct acpi_bgrt);
  bgrt->header.revision = 1;
  bgrt->version = 1;
  bgrt->status = 0x01;
  bgrt->type = 0;
  bgrt->addr = (grub_uint64_t)(grub_addr_t) bgrt_bmp;
  bgrt->header.checksum = 0;
  bgrt->header.checksum = 1 + ~grub_byte_checksum (bgrt, bgrt->header.length);

  acpi_get_bgrt (xsdt);
  if (bgrt_patched)
    return;
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
  new_xsdt_entry[entry_count - 1] = (grub_uint64_t)(grub_addr_t) bgrt;

  new_xsdt->checksum = 0;
  new_xsdt->checksum = 1 + ~grub_byte_checksum (xsdt, xsdt->length);

  // invalidate old XSDT table signature and checksum
  grub_memcpy (xsdt, "WPBT", 4);

  // replace old XSDT
  rsdp->xsdt_addr = (grub_uint64_t)(grub_addr_t) new_xsdt;

  // re-calculate RSDP extended checksum
  rsdp->checksum = 0;
  rsdp->checksum = 1 + ~grub_byte_checksum (rsdp, rsdp->length);

  grub_printf ("New BGRT table inserted\n");
}

static void *
init_bgrt_bmp (void)
{
  struct grub_acpi_rsdp_v20 *rsdp = NULL;
  struct grub_acpi_table_header *xsdt, *entry;
  int entry_cnt, i;
  struct acpi_bgrt *bgrt_table = NULL;
  grub_uint64_t *entry_ptr;
  rsdp = grub_machine_acpi_get_rsdpv2 ();
  if (! rsdp)
    return NULL;
  if (rsdp->rsdpv1.revision >= 0x02)
    xsdt = (struct grub_acpi_table_header *)(grub_addr_t)(rsdp->xsdt_addr);
  else
    return NULL;

  if (grub_memcmp(xsdt->signature, "XSDT", 4) != 0)
    return NULL;

  entry_cnt = (xsdt->length
               - sizeof (struct grub_acpi_table_header)) / sizeof(grub_uint64_t);
  entry_ptr = (grub_uint64_t *)(xsdt + 1);
  for (i = 0; i < entry_cnt; i++, entry_ptr++)
  {
    entry = (struct grub_acpi_table_header *)(grub_addr_t)(*entry_ptr);
    if (grub_memcmp(entry->signature, "BGRT", 4) == 0)
    {
      bgrt_table = (struct acpi_bgrt *)entry;
      return (void *)(grub_addr_t)bgrt_table->addr;
    }
  }
  return NULL;
}

static char *
get_bgrt_bmp (grub_size_t *sz)
{
  *sz = 0;
  char *ret = NULL;
  void *bgrt_bmp_data = NULL;
  bgrt_bmp_data = init_bgrt_bmp ();
  if (!bgrt_bmp_data)
    return ret;
  *sz = ((struct bmp_header *)bgrt_bmp_data)->bfsize;
  if (!*sz)
    return ret;
  ret = grub_malloc (*sz);
  if (!ret)
    return ret;
  grub_memcpy (ret, bgrt_bmp_data, *sz);
  return ret;
}

struct grub_procfs_entry proc_bgrt_bmp =
{
  .name = "bgrt.bmp",
  .get_contents = get_bgrt_bmp,
};

#endif

static grub_err_t
grub_cmd_acpi (struct grub_extcmd_context *ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  struct grub_acpi_rsdp_v10 *rsdp;
  struct efiemu_acpi_table *cur, *t;
  int i, mmapregion;
  int numoftables;

  struct grub_acpi_table_header *slic = NULL;
  grub_size_t slic_size = SLIC_LENGTH;
  char msdm_key[29];

  /* Default values if no RSDP is found. */
  rev1 = 1;
  rev2 = 3;

  facs_addr = 0;
  playground = playground_ptr = 0;
  playground_size = 0;

  rsdp = (struct grub_acpi_rsdp_v10 *) grub_machine_acpi_get_rsdpv2 ();

  if (! rsdp)
    rsdp = grub_machine_acpi_get_rsdpv1 ();

  grub_dprintf ("acpi", "RSDP @%p\n", rsdp);

  if (rsdp)
    {
      grub_uint32_t *entry_ptr;
      char *exclude = 0;
      char *load_only = 0;
      char *ptr;
      /* RSDT consists of header and an array of 32-bit pointers. */
      struct grub_acpi_table_header *rsdt;

      exclude = state[ACPI_X].set ? grub_strdup (state[ACPI_X].arg) : 0;
      if (exclude)
    {
      for (ptr = exclude; *ptr; ptr++)
        *ptr = grub_tolower (*ptr);
    }

      load_only = state[ACPI_N].set ? grub_strdup (state[ACPI_N].arg) : 0;
      if (load_only)
    {
      for (ptr = load_only; *ptr; ptr++)
        *ptr = grub_tolower (*ptr);
    }

      /* Set revision variables to replicate the same version as host. */
      rev1 = ! rsdp->revision;
      rev2 = rsdp->revision;
      rsdt = (struct grub_acpi_table_header *) (grub_addr_t) rsdp->rsdt_addr;
      /* Load host tables. */
      for (entry_ptr = (grub_uint32_t *) (rsdt + 1);
       entry_ptr < (grub_uint32_t *) (((grub_uint8_t *) rsdt)
                      + rsdt->length);
       entry_ptr++)
    {
      char signature[5];
      struct efiemu_acpi_table *table;
      struct grub_acpi_table_header *curtable
        = (struct grub_acpi_table_header *) (grub_addr_t) *entry_ptr;
      signature[4] = 0;
      for (i = 0; i < 4;i++)
        signature[i] = grub_tolower (curtable->signature[i]);

      /* If it's FADT it contains addresses of DSDT and FACS. */
      if (grub_strcmp (signature, "facp") == 0)
        {
          struct grub_acpi_table_header *dsdt;
          struct grub_acpi_fadt *fadt = (struct grub_acpi_fadt *) curtable;

          /* Set root header variables to the same values
         as FADT by default. */
          grub_memcpy (&root_oemid, &(fadt->hdr.oemid),
               sizeof (root_oemid));
          grub_memcpy (&root_oemtable, &(fadt->hdr.oemtable),
               sizeof (root_oemtable));
          root_oemrev = fadt->hdr.oemrev;
          grub_memcpy (&root_creator_id, &(fadt->hdr.creator_id),
               sizeof (root_creator_id));
          root_creator_rev = fadt->hdr.creator_rev;

          /* Load DSDT if not excluded. */
          dsdt = (struct grub_acpi_table_header *)
        (grub_addr_t) fadt->dsdt_addr;
          if (dsdt && (! exclude || ! grub_strword (exclude, "dsdt"))
          && (! load_only || grub_strword (load_only, "dsdt"))
          && dsdt->length >= sizeof (*dsdt))
        {
          dsdt_size = dsdt->length;
          table_dsdt = grub_malloc (dsdt->length);
          if (! table_dsdt)
            {
              free_tables ();
              grub_free (exclude);
              grub_free (load_only);
              return grub_errno;
            }
          grub_memcpy (table_dsdt, dsdt, dsdt->length);
        }

          /* Save FACS address. FACS shouldn't be overridden. */
          facs_addr = fadt->facs_addr;
        }

      /* Skip excluded tables. */
      if (exclude && grub_strword (exclude, signature))
        continue;
      if (load_only && ! grub_strword (load_only, signature))
        continue;

      /* Sanity check. */
      if (curtable->length < sizeof (*curtable))
        continue;

      table = (struct efiemu_acpi_table *) grub_malloc
        (sizeof (struct efiemu_acpi_table));
      if (! table)
        {
          free_tables ();
          grub_free (exclude);
          grub_free (load_only);
          return grub_errno;
        }
      table->size = curtable->length;
      table->addr = grub_malloc (table->size);
      playground_size += table->size;
      if (! table->addr)
        {
          free_tables ();
          grub_free (exclude);
          grub_free (load_only);
          grub_free (table);
          return grub_errno;
        }
      table->next = acpi_tables;
      acpi_tables = table;
      grub_memcpy (table->addr, curtable, table->size);
    }
      grub_free (exclude);
      grub_free (load_only);
    }

  if (state[ACPI_MSDM].set && argc == 0)
  {
    struct acpi_msdm *msdm = NULL;
    msdm = acpi_get_msdm ((struct grub_acpi_rsdp_v20 *)rsdp);
    if (! msdm)
    {
      free_tables ();
      return GRUB_ERR_NONE;
    }
    print_msdm (msdm);
    free_tables ();
    return GRUB_ERR_NONE;
  }

#ifdef GRUB_MACHINE_EFI
  if (state[ACPI_BGRT].set && argc == 1)
  {
    grub_file_t file = 0;
    file = grub_file_open (args[0], GRUB_FILE_TYPE_ACPI_TABLE);
    if (! file)
    {
      free_tables ();
      return grub_errno;
    }
    create_bgrt (file, (struct grub_acpi_rsdp_v20 *) rsdp);
    free_tables ();
    return GRUB_ERR_NONE;
  }
#endif

  /* Does user specify versions to generate? */
  if (state[ACPI_V1].set || state[ACPI_V2].set)
    {
      rev1 = state[ACPI_V1].set;
      if (state[ACPI_V2].set)
    rev2 = rev2 ? : 2;
      else
    rev2 = 0;
    }

  /* Does user override root header information? */
  if (state[ACPI_ID].set)
    grub_strncpy (root_oemid, state[ACPI_ID].arg, sizeof (root_oemid));
  if (state[ACPI_TABLE].set)
    grub_strncpy (root_oemtable, state[ACPI_TABLE].arg, sizeof (root_oemtable));
  if (state[ACPI_OREV].set)
    root_oemrev = grub_strtoul (state[ACPI_OREV].arg, 0, 0);
  if (state[ACPI_C].set)
    grub_strncpy (root_creator_id, state[ACPI_C].arg, sizeof (root_creator_id));
  if (state[ACPI_CREV].set)
    root_creator_rev = grub_strtoul (state[ACPI_CREV].arg, 0, 0);

  /* Load user tables */
  for (i = 0; i < argc; i++)
    {
      grub_file_t file;
      grub_size_t size;
      char *buf;

      file = grub_file_open (args[i], GRUB_FILE_TYPE_ACPI_TABLE);
      if (! file)
    {
      free_tables ();
      return grub_errno;
    }

      size = grub_file_size (file);
      if (size < sizeof (struct grub_acpi_table_header))
    {
      grub_file_close (file);
      free_tables ();
      return grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
                 args[i]);
    }

      buf = (char *) grub_malloc (size);
      if (! buf)
    {
      grub_file_close (file);
      free_tables ();
      return grub_errno;
    }

      if (grub_file_read (file, buf, size) != (int) size)
    {
      grub_file_close (file);
      free_tables ();
      if (!grub_errno)
        grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
            args[i]);
      return grub_errno;
    }
      grub_file_close (file);

      if (grub_memcmp (((struct grub_acpi_table_header *) buf)->signature,
               "DSDT", 4) == 0)
    {
      grub_free (table_dsdt);
      table_dsdt = buf;
      dsdt_size = size;
    }
      else
    {
      struct efiemu_acpi_table *table;
      table = (struct efiemu_acpi_table *) grub_malloc
        (sizeof (struct efiemu_acpi_table));
      if (! table)
        {
          free_tables ();
          return grub_errno;
        }

      table->size = size;
      table->addr = buf;
      playground_size += table->size;

      table->next = acpi_tables;
      acpi_tables = table;
    }

    if (state[ACPI_SLIC].set || state[ACPI_MSDM].set)
    {
      slic = acpi_find_slic (rsdp);
    }

    if (state[ACPI_SLIC].set)
    {
      if (size < slic_size)
        slic_size = size;
      grub_memcpy (root_oemid,
                   ((struct grub_acpi_table_header *) buf)->oemid,
                   sizeof (root_oemid));
      slic_print (root_oemid, sizeof (root_oemid), "slic oemid:");
      grub_memcpy (root_oemtable,
                   ((struct grub_acpi_table_header *) buf)->oemtable,
                   sizeof (root_oemtable));
      slic_print (root_oemtable, sizeof (root_oemtable), "slic oemid:");
      if (slic)
      {
        grub_printf ("found slic in acpi table: %p\n", slic);
        grub_memcpy (slic, buf, slic_size);
      }
    }
    if (state[ACPI_MSDM].set)
    {
      if (size != sizeof (struct acpi_msdm))
      {
        grub_printf ("Bad MSDM table.\n");
        free_tables ();
        return grub_errno;
      }
      if (slic)
        grub_printf ("found slic in acpi table: %p\n", slic);
      else
      {
        grub_printf ("SLIC table not found.\n");
        free_tables ();
        return grub_errno;
      }
      struct acpi_msdm *msdm = ((struct acpi_msdm *) buf);
      grub_memcpy (root_oemid, slic->oemid, sizeof (root_oemid));
      slic_print (root_oemid, sizeof (root_oemid), "msdm oemid:");
      grub_memcpy (root_oemtable, slic->oemtable, sizeof (root_oemtable));
      slic_print (root_oemtable, sizeof (root_oemtable), "msdm oemid:");
      grub_memcpy (msdm_key, msdm->soft.data, 29);
      slic_print (msdm_key, sizeof (msdm_key), "msdm key:");
    }
    }

  numoftables = 0;
  for (cur = acpi_tables; cur; cur = cur->next)
    numoftables++;

  /* DSDT. */
  playground_size += dsdt_size;
  /* RSDT. */
  playground_size += sizeof (struct grub_acpi_table_header) + sizeof (grub_uint32_t) * numoftables;
  /* RSDPv1. */
  playground_size += sizeof (struct grub_acpi_rsdp_v10);
  /* XSDT. */
  playground_size += sizeof (struct grub_acpi_table_header) + sizeof (grub_uint64_t) * numoftables;
  /* RSDPv2. */
  playground_size += sizeof (struct grub_acpi_rsdp_v20);

  if (state[ACPI_SLIC].set && slic)
  {
    struct grub_acpi_table_header *rsdt;
    rsdt = (struct grub_acpi_table_header *)(grub_addr_t)(rsdp->rsdt_addr);
    grub_memcpy (rsdt->oemid, root_oemid, sizeof (root_oemid));
    grub_memcpy (rsdt->oemtable, root_oemtable, sizeof (root_oemtable));
    grub_printf ("recalculating rsdt checksum: %d\n", rsdt->length);
    rsdt->checksum = 0;
    rsdt->checksum = 1 + ~grub_byte_checksum (rsdt, rsdt->length);
    if (rev2)
    {
      struct grub_acpi_rsdp_v20 *new_rsdp;
      struct grub_acpi_table_header *xsdt;
      new_rsdp = (struct grub_acpi_rsdp_v20 *)rsdp;
      xsdt = (struct grub_acpi_table_header *)(grub_addr_t)(new_rsdp->xsdt_addr);
      grub_memcpy (xsdt->oemid, root_oemid, sizeof (root_oemid));
      grub_memcpy (xsdt->oemtable, root_oemtable, sizeof (root_oemtable));
      grub_printf ("recalculating xsdt checksum: %d\n", xsdt->length);
      xsdt->checksum = 0;
      xsdt->checksum = 1 + ~grub_byte_checksum (xsdt, xsdt->length);
    }
    free_tables ();
    return GRUB_ERR_NONE;
  }

  playground = playground_ptr
    = grub_mmap_malign_and_register (1, playground_size, &mmapregion,
                    GRUB_MEMORY_ACPI, 0);

  if (! playground)
    {
      free_tables ();
      return grub_error (GRUB_ERR_OUT_OF_MEMORY,
             "couldn't allocate space for ACPI tables");
    }

  setup_common_tables ();

  /* Request space for RSDPv1. */
  if (rev1)
    setv1table ();

  /* Request space for RSDPv2+ and XSDT. */
  if (rev2)
    setv2table ();

  for (cur = acpi_tables; cur;)
    {
      t = cur;
      cur = cur->next;
      grub_free (t);
    }
  acpi_tables = 0;

#if defined (__i386__) || defined (__x86_64__)
  if (! state[ACPI_EBDA].set)
    {
      grub_err_t err;
      err = grub_acpi_create_ebda ();
      if (err)
    {
      rsdpv1_new = 0;
      rsdpv2_new = 0;
      grub_mmap_free_and_unregister (mmapregion);
      return err;
    }
    }
#endif

#ifdef GRUB_MACHINE_EFI
  {
    struct grub_efi_guid acpi = GRUB_EFI_ACPI_TABLE_GUID;
    struct grub_efi_guid acpi20 = GRUB_EFI_ACPI_20_TABLE_GUID;

    efi_call_2 (grub_efi_system_table->boot_services->install_configuration_table,
      &acpi20, grub_acpi_get_rsdpv2 ());
    efi_call_2 (grub_efi_system_table->boot_services->install_configuration_table,
      &acpi, grub_acpi_get_rsdpv1 ());
  }
#endif

  return GRUB_ERR_NONE;
}

static void
print_strn (grub_uint8_t *str, grub_size_t len)
{
  for (; *str && len; str++, len--)
    grub_printf ("%c", *str);
  for (len++; len; len--)
    grub_printf (" ");
}

#define print_field(x) print_strn(x, sizeof (x))

static void
disp_acpi_table (struct grub_acpi_table_header *t)
{
  print_field (t->signature);
  grub_printf ("%4" PRIuGRUB_UINT32_T "B rev=%u chksum=0x%02x (%s) OEM=",
               t->length, t->revision, t->checksum,
               grub_byte_checksum (t, t->length) == 0 ? "valid" : "invalid");
  print_field (t->oemid);
  print_field (t->oemtable);
  grub_printf ("OEMrev=%08" PRIxGRUB_UINT32_T " ", t->oemrev);
  print_field (t->creator_id);
  grub_printf (" %08" PRIxGRUB_UINT32_T "\n", t->creator_rev);
}

static void
disp_madt_table (struct grub_acpi_madt *t)
{
  struct grub_acpi_madt_entry_header *d;
  grub_uint32_t len;

  disp_acpi_table (&t->hdr);
  grub_printf ("Local APIC=%08" PRIxGRUB_UINT32_T "  Flags=%08"
           PRIxGRUB_UINT32_T "\n",
           t->lapic_addr, t->flags);
  len = t->hdr.length - sizeof (struct grub_acpi_madt);
  d = t->entries;
  for (;len > 0; len -= d->len, d = (void *) ((grub_uint8_t *) d + d->len))
  {
    switch (d->type)
    {
      case GRUB_ACPI_MADT_ENTRY_TYPE_LAPIC:
      {
        struct grub_acpi_madt_entry_lapic *dt = (void *) d;
        grub_printf ("  LAPIC ACPI_ID=%02x APIC_ID=%02x Flags=%08x\n",
             dt->acpiid, dt->apicid, dt->flags);
        if (dt->hdr.len != sizeof (*dt))
          grub_printf ("   table size mismatch %d != %d\n", dt->hdr.len,
               (int) sizeof (*dt));
        break;
      }
      case GRUB_ACPI_MADT_ENTRY_TYPE_IOAPIC:
      {
        struct grub_acpi_madt_entry_ioapic *dt = (void *) d;
        grub_printf ("  IOAPIC ID=%02x address=%08x GSI=%08x\n",
             dt->id, dt->address, dt->global_sys_interrupt);
        if (dt->hdr.len != sizeof (*dt))
          grub_printf ("   table size mismatch %d != %d\n", dt->hdr.len,
               (int) sizeof (*dt));
        if (dt->pad)
          grub_printf ("   non-zero pad: %02x\n", dt->pad);
        break;
      }
      case GRUB_ACPI_MADT_ENTRY_TYPE_INTERRUPT_OVERRIDE:
      {
        struct grub_acpi_madt_entry_interrupt_override *dt = (void *) d;
        grub_printf ("  Int Override bus=%x src=%x GSI=%08x Flags=%04x\n",
             dt->bus, dt->source, dt->global_sys_interrupt,
             dt->flags);
        if (dt->hdr.len != sizeof (*dt))
          grub_printf ("   table size mismatch %d != %d\n", dt->hdr.len,
               (int) sizeof (*dt));
        break;
      }
      case GRUB_ACPI_MADT_ENTRY_TYPE_LAPIC_NMI:
      {
        struct grub_acpi_madt_entry_lapic_nmi *dt = (void *) d;
        grub_printf ("  LAPIC_NMI ACPI_ID=%02x Flags=%04x lint=%02x\n",
             dt->acpiid, dt->flags, dt->lint);
        if (dt->hdr.len != sizeof (*dt))
          grub_printf ("   table size mismatch %d != %d\n", dt->hdr.len,
               (int) sizeof (*dt));
        break;
      }
      case GRUB_ACPI_MADT_ENTRY_TYPE_SAPIC:
      {
        struct grub_acpi_madt_entry_sapic *dt = (void *) d;
        grub_printf ("  IOSAPIC Id=%02x GSI=%08x Addr=%016" PRIxGRUB_UINT64_T
             "\n", dt->id, dt->global_sys_interrupt_base, dt->addr);
        if (dt->hdr.len != sizeof (*dt))
          grub_printf ("   table size mismatch %d != %d\n", dt->hdr.len,
               (int) sizeof (*dt));
        if (dt->pad)
          grub_printf ("   non-zero pad: %02x\n", dt->pad);
        break;
      }
      case GRUB_ACPI_MADT_ENTRY_TYPE_LSAPIC:
      {
        struct grub_acpi_madt_entry_lsapic *dt = (void *) d;
        grub_printf ("  LSAPIC ProcId=%02x ID=%02x EID=%02x Flags=%x",
             dt->cpu_id, dt->id, dt->eid, dt->flags);
        if (dt->flags & GRUB_ACPI_MADT_ENTRY_SAPIC_FLAGS_ENABLED)
          grub_printf (" Enabled\n");
        else
          grub_printf (" Disabled\n");
        if (d->len > sizeof (struct grub_acpi_madt_entry_sapic))
          grub_printf ("  UID val=%08x, Str=%s\n", dt->cpu_uid,
               dt->cpu_uid_str);
        if (dt->hdr.len != sizeof (*dt)
                  + grub_strlen ((char *) dt->cpu_uid_str) + 1)
          grub_printf ("   table size mismatch %d != %d\n", dt->hdr.len,
               (int) sizeof (*dt));
        if (dt->pad[0] || dt->pad[1] || dt->pad[2])
          grub_printf ("   non-zero pad: %02x%02x%02x\n",
                       dt->pad[0], dt->pad[1], dt->pad[2]);
        break;
      }
      case GRUB_ACPI_MADT_ENTRY_TYPE_PLATFORM_INT_SOURCE:
      {
        struct grub_acpi_madt_entry_platform_int_source *dt = (void *) d;
        static const char * const platint_type[] =
          {"Nul", "PMI", "INIT", "CPEI"};

        grub_printf ("  Platform INT flags=%04x type=%02x (%s)"
             " ID=%02x EID=%02x\n",
             dt->flags, dt->inttype,
             (dt->inttype < ARRAY_SIZE (platint_type))
             ? platint_type[dt->inttype] : "??", dt->cpu_id,
             dt->cpu_eid);
        grub_printf ("  IOSAPIC Vec=%02x GSI=%08x source flags=%08x\n",
             dt->sapic_vector, dt->global_sys_int, dt->src_flags);
        break;
      }
      default:
        grub_printf ("  type=%x l=%u ", d->type, d->len);
        grub_printf (" ??\n");
    }
  }
}

static void
disp_acpi_xsdt_table (struct grub_acpi_table_header *t)
{
  grub_uint32_t len;
  grub_uint64_t *desc;

  disp_acpi_table (t);
  len = t->length - sizeof (*t);
  desc = (grub_uint64_t *) (t + 1);
  for (; len >= sizeof (*desc); desc++, len -= sizeof (*desc))
  {
#if GRUB_CPU_SIZEOF_VOID_P == 4
    if (*desc >= (1ULL << 32))
    {
      grub_printf ("Unreachable table\n");
      continue;
    }
#endif
    t = (struct grub_acpi_table_header *) (grub_addr_t) *desc;

    if (t == NULL)
      continue;

    if (grub_memcmp (t->signature, GRUB_ACPI_MADT_SIGNATURE,
               sizeof (t->signature)) == 0)
      disp_madt_table ((struct grub_acpi_madt *) t);
    else
      disp_acpi_table (t);
  }
}

static void
disp_acpi_rsdt_table (struct grub_acpi_table_header *t)
{
  grub_uint32_t len;
  grub_uint32_t *desc;

  disp_acpi_table (t);
  len = t->length - sizeof (*t);
  desc = (grub_uint32_t *) (t + 1);
  for (; len >= sizeof (*desc); desc++, len -= sizeof (*desc))
  {
    t = (struct grub_acpi_table_header *) (grub_addr_t) *desc;
    if (t == NULL)
      continue;

    if (grub_memcmp (t->signature, GRUB_ACPI_MADT_SIGNATURE,
               sizeof (t->signature)) == 0)
      disp_madt_table ((struct grub_acpi_madt *) t);
    else
      disp_acpi_table (t);
  }
}

static void
disp_acpi_rsdpv1 (struct grub_acpi_rsdp_v10 *rsdp)
{
  print_field (rsdp->signature);
  grub_printf ("chksum:%02x (%s), OEM-ID: ", rsdp->checksum,
        grub_byte_checksum (rsdp, sizeof (*rsdp)) == 0 ? "valid" : "invalid");
  print_field (rsdp->oemid);
  grub_printf ("rev=%d\n", rsdp->revision);
  grub_printf ("RSDT=%08" PRIxGRUB_UINT32_T "\n", rsdp->rsdt_addr);
}

static void
disp_acpi_rsdpv2 (struct grub_acpi_rsdp_v20 *rsdp)
{
  disp_acpi_rsdpv1 (&rsdp->rsdpv1);
  grub_printf ("len=%d chksum=%02x (%s) XSDT=%016" PRIxGRUB_UINT64_T "\n",
          rsdp->length, rsdp->checksum,
          grub_byte_checksum (rsdp, rsdp->length) == 0 ? "valid" : "invalid",
          rsdp->xsdt_addr);
  if (rsdp->length != sizeof (*rsdp))
    grub_printf (" length mismatch %d != %d\n", rsdp->length, (int) sizeof (*rsdp));
  if (rsdp->reserved[0] || rsdp->reserved[1] || rsdp->reserved[2])
    grub_printf (" non-zero reserved %02x%02x%02x\n",
                 rsdp->reserved[0], rsdp->reserved[1], rsdp->reserved[2]);
}

static const struct grub_arg_option options_ls[] = {
  {"v1", '1', 0, N_("Show version 1 tables only."), 0, ARG_TYPE_NONE},
  {"v2", '2', 0, N_("Show version 2 and version 3 tables only."), 0, ARG_TYPE_NONE},
  {0, 0, 0, 0, 0, 0}
};

static grub_err_t
grub_cmd_lsacpi (struct grub_extcmd_context *ctxt,
         int argc __attribute__ ((unused)),
         char **args __attribute__ ((unused)))
{
  if (!ctxt->state[1].set)
  {
    struct grub_acpi_rsdp_v10 *rsdp1 = grub_acpi_get_rsdpv1 ();
    if (!rsdp1)
      grub_printf ("No RSDPv1\n");
    else
    {
      grub_printf ("RSDPv1 signature:");
      disp_acpi_rsdpv1 (rsdp1);
      disp_acpi_rsdt_table ((void *) (grub_addr_t) rsdp1->rsdt_addr);
    }
  }

  if (!ctxt->state[0].set)
  {
    struct grub_acpi_rsdp_v20 *rsdp2 = grub_acpi_get_rsdpv2 ();
    if (!rsdp2)
      grub_printf ("No RSDPv2\n");
    else
    {
#if GRUB_CPU_SIZEOF_VOID_P == 4
      if (rsdp2->xsdt_addr >= (1ULL << 32))
        grub_printf ("Unreachable RSDPv2\n");
      else
#endif
      {
        grub_printf ("RSDPv2 signature:");
        disp_acpi_rsdpv2 (rsdp2);
        disp_acpi_xsdt_table ((void *) (grub_addr_t) rsdp2->xsdt_addr);
        grub_printf ("\n");
      }
    }
  }
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd, cmd_ls;

static char *
get_acpi_rsdp (grub_size_t *sz)
{
  *sz = 0;
  char *ret = NULL;
  void *rsdp = NULL;
  rsdp = grub_acpi_get_rsdpv2 ();
  if (rsdp)
    *sz = sizeof (struct grub_acpi_rsdp_v20);
  else
  {
    rsdp = grub_acpi_get_rsdpv1 ();
    if (!rsdp)
      return NULL;
    *sz = sizeof (struct grub_acpi_rsdp_v10);
  }
  ret = grub_malloc (*sz);
  if (!ret)
    return NULL;
  grub_memcpy (ret, rsdp, *sz);
  return ret;
}

struct grub_procfs_entry proc_acpi_rsdp =
{
  .name = "acpi_rsdp",
  .get_contents = get_acpi_rsdp,
};

GRUB_MOD_INIT(acpi)
{
  cmd = grub_register_extcmd ("acpi", grub_cmd_acpi, 0,
                  N_("[-1|-2] [--exclude=TABLE1,TABLE2|"
                  "--load-only=TABLE1,TABLE2] FILE1"
                  " [FILE2] [...]"),
                  N_("Load host ACPI tables and tables "
                  "specified by arguments."),
                  options);
  cmd_ls = grub_register_extcmd ("lsacpi", grub_cmd_lsacpi, 0, "[-1|-2]",
                              N_("Show ACPI information."), options_ls);
#ifdef GRUB_MACHINE_EFI
  grub_procfs_register ("bgrt.bmp", &proc_bgrt_bmp);
#endif
  grub_procfs_register ("acpi_rsdp", &proc_acpi_rsdp);
}

GRUB_MOD_FINI(acpi)
{
  grub_unregister_extcmd (cmd);
  grub_unregister_extcmd (cmd_ls);
#ifdef GRUB_MACHINE_EFI
  grub_procfs_unregister (&proc_bgrt_bmp);
#endif
  grub_procfs_unregister (&proc_acpi_rsdp);
}
