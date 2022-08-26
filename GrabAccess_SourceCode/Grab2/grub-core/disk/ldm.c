/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008,2009,2011  Free Software Foundation, Inc.
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
#include <grub/disk.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/diskfilter.h>
#include <grub/msdos_partition.h>
#include <grub/gpt_partition.h>
#include <grub/i18n.h>
#include <grub/safemath.h>

#ifdef GRUB_UTIL
#include <grub/emu/misc.h>
#include <grub/emu/hostdisk.h>
#endif

GRUB_MOD_LICENSE ("GPLv3+");

#define LDM_GUID_STRLEN 64
#define LDM_NAME_STRLEN 32

typedef grub_uint8_t *grub_ldm_id_t;

enum { STRIPE = 1, SPANNED = 2, RAID5 = 3 };

#define LDM_LABEL_SECTOR 6
struct grub_ldm_vblk {
  char magic[4];
  grub_uint8_t unused1[12];
  grub_uint16_t update_status;
  grub_uint8_t flags;
  grub_uint8_t type;
  grub_uint32_t unused2;
  grub_uint8_t dynamic[104];
} GRUB_PACKED;
#define LDM_VBLK_MAGIC "VBLK"

enum
  {
    STATUS_CONSISTENT = 0,
    STATUS_STILL_ACTIVE = 1,
    STATUS_NOT_ACTIVE_YET = 2
  };

enum
  {
    ENTRY_COMPONENT = 0x32,
    ENTRY_PARTITION = 0x33,
    ENTRY_DISK = 0x34,
    ENTRY_VOLUME = 0x51,
  };

struct grub_ldm_label
{
  char magic[8];
  grub_uint32_t unused1;
  grub_uint16_t ver_major;
  grub_uint16_t ver_minor;
  grub_uint8_t unused2[32];
  char disk_guid[LDM_GUID_STRLEN];
  char host_guid[LDM_GUID_STRLEN];
  char group_guid[LDM_GUID_STRLEN];
  char group_name[LDM_NAME_STRLEN];
  grub_uint8_t unused3[11];
  grub_uint64_t pv_start;
  grub_uint64_t pv_size;
  grub_uint64_t config_start;
  grub_uint64_t config_size;
} GRUB_PACKED;


#define LDM_MAGIC "PRIVHEAD"



static inline grub_uint64_t
read_int (grub_uint8_t *in, grub_size_t s)
{
  grub_uint8_t *ptr2;
  grub_uint64_t ret;
  ret = 0;
  for (ptr2 = in; ptr2 < in + s; ptr2++)
    {
      ret <<= 8;
      ret |= *ptr2;
    }
  return ret;
}

static int
check_ldm_partition (grub_disk_t disk __attribute__ ((unused)), const grub_partition_t p, void *data)
{
  int *has_ldm = data;

  if (p->number >= 4)
    return 1;
  if (p->msdostype == GRUB_PC_PARTITION_TYPE_LDM)
    {
      *has_ldm = 1;
      return 1;
    }
  return 0;
}

static int
msdos_has_ldm_partition (grub_disk_t dsk)
{
  grub_err_t err;
  int has_ldm = 0;

  err = grub_partition_msdos_iterate (dsk, check_ldm_partition, &has_ldm);
  if (err)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }

  return has_ldm;
}

static const grub_gpt_part_guid_t ldm_type = GRUB_GPT_PARTITION_TYPE_LDM;

/* Helper for gpt_ldm_sector.  */
static int
gpt_ldm_sector_iter (grub_disk_t disk, const grub_partition_t p, void *data)
{
  grub_disk_addr_t *sector = data;
  struct grub_gpt_partentry gptdata;
  grub_partition_t p2;

  p2 = disk->partition;
  disk->partition = p->parent;
  if (grub_disk_read (disk, p->offset, p->index,
		      sizeof (gptdata), &gptdata))
    {
      disk->partition = p2;
      return 0;
    }
  disk->partition = p2;

  if (! grub_memcmp (&gptdata.type, &ldm_type, 16))
    {
      *sector = p->start + p->len - 1;
      return 1;
    }
  return 0;
}

static grub_disk_addr_t
gpt_ldm_sector (grub_disk_t dsk)
{
  grub_disk_addr_t sector = 0;
  grub_err_t err;

  err = grub_gpt_partition_map_iterate (dsk, gpt_ldm_sector_iter, &sector);
  if (err)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }
  return sector;
}

static struct grub_diskfilter_vg *
make_vg (grub_disk_t disk,
	 const struct grub_ldm_label *label)
{
  grub_disk_addr_t startsec, endsec, cursec;
  struct grub_diskfilter_vg *vg;
  grub_err_t err;

  /* First time we see this volume group. We've to create the
     whole volume group structure. */
  vg = grub_malloc (sizeof (*vg));
  if (! vg)
    return NULL;
  vg->extent_size = 1;
  vg->name = grub_malloc (LDM_NAME_STRLEN + 1);
  vg->uuid = grub_malloc (LDM_GUID_STRLEN + 1);
  if (! vg->uuid || !vg->name)
    {
      grub_free (vg->uuid);
      grub_free (vg->name);
      grub_free (vg);
      return NULL;
    }
  grub_memcpy (vg->uuid, label->group_guid, LDM_GUID_STRLEN);
  grub_memcpy (vg->name, label->group_name, LDM_NAME_STRLEN);
  vg->name[LDM_NAME_STRLEN] = 0;
  vg->uuid[LDM_GUID_STRLEN] = 0;
  vg->uuid_len = grub_strlen (vg->uuid);

  vg->lvs = NULL;
  vg->pvs = NULL;

  startsec = grub_be_to_cpu64 (label->config_start);
  endsec = startsec + grub_be_to_cpu64 (label->config_size);

  /* First find disks.  */
  for (cursec = startsec + 0x12; cursec < endsec; cursec++)
    {
      struct grub_ldm_vblk vblk[GRUB_DISK_SECTOR_SIZE
				/ sizeof (struct grub_ldm_vblk)];
      unsigned i;
      err = grub_disk_read (disk, cursec, 0,
			    sizeof(vblk), &vblk);
      if (err)
	goto fail2;

      for (i = 0; i < ARRAY_SIZE (vblk); i++)
	{
	  struct grub_diskfilter_pv *pv;
	  grub_uint8_t *ptr;
	  if (grub_memcmp (vblk[i].magic, LDM_VBLK_MAGIC,
			   sizeof (vblk[i].magic)) != 0)
	    continue;
	  if (grub_be_to_cpu16 (vblk[i].update_status)
	      != STATUS_CONSISTENT
	      && grub_be_to_cpu16 (vblk[i].update_status)
	      != STATUS_STILL_ACTIVE)
	    continue;
	  if (vblk[i].type != ENTRY_DISK)
	    continue;
	  pv = grub_zalloc (sizeof (*pv));
	  if (!pv)
	    goto fail2;

	  pv->disk = 0;
	  ptr = vblk[i].dynamic;
	  if (ptr + *ptr + 1 >= vblk[i].dynamic
	      + sizeof (vblk[i].dynamic))
	    {
	      grub_free (pv);
	      goto fail2;
	    }
	  pv->internal_id = grub_malloc (ptr[0] + 2);
	  if (!pv->internal_id)
	    {
	      grub_free (pv);
	      goto fail2;
	    }
	  grub_memcpy (pv->internal_id, ptr, (grub_size_t) ptr[0] + 1);
	  pv->internal_id[(grub_size_t) ptr[0] + 1] = 0;
	  
	  ptr += *ptr + 1;
	  if (ptr + *ptr + 1 >= vblk[i].dynamic
	      + sizeof (vblk[i].dynamic))
	    {
	      grub_free (pv);
	      goto fail2;
	    }
	  /* ptr = name.  */
	  ptr += *ptr + 1;
	  if (ptr + *ptr + 1
	      >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      grub_free (pv);
	      goto fail2;
	    }
	  pv->id.uuidlen = *ptr;
	  pv->id.uuid = grub_malloc (pv->id.uuidlen + 1);
	  grub_memcpy (pv->id.uuid, ptr + 1, pv->id.uuidlen);
	  pv->id.uuid[pv->id.uuidlen] = 0;

	  pv->next = vg->pvs;
	  vg->pvs = pv;
	}
    }

  /* Then find LVs.  */
  for (cursec = startsec + 0x12; cursec < endsec; cursec++)
    {
      struct grub_ldm_vblk vblk[GRUB_DISK_SECTOR_SIZE
				/ sizeof (struct grub_ldm_vblk)];
      unsigned i;
      grub_size_t sz;
      err = grub_disk_read (disk, cursec, 0,
			    sizeof(vblk), &vblk);
      if (err)
	goto fail2;

      for (i = 0; i < ARRAY_SIZE (vblk); i++)
	{
	  struct grub_diskfilter_lv *lv;
	  grub_uint8_t *ptr;
	  if (grub_memcmp (vblk[i].magic, LDM_VBLK_MAGIC,
			   sizeof (vblk[i].magic)) != 0)
	    continue;
	  if (grub_be_to_cpu16 (vblk[i].update_status)
	      != STATUS_CONSISTENT
	      && grub_be_to_cpu16 (vblk[i].update_status)
	      != STATUS_STILL_ACTIVE)
	    continue;
	  if (vblk[i].type != ENTRY_VOLUME)
	    continue;
	  lv = grub_zalloc (sizeof (*lv));
	  if (!lv)
	    goto fail2;

	  lv->vg = vg;
	  lv->segment_count = 1;
	  lv->segment_alloc = 1;
	  lv->visible = 1;
	  lv->segments = grub_zalloc (sizeof (*lv->segments));
	  if (!lv->segments)
	    {
	      grub_free (lv);
	      goto fail2;
	    }
	  lv->segments->start_extent = 0;
	  lv->segments->type = GRUB_DISKFILTER_MIRROR;
	  lv->segments->node_count = 0;
	  lv->segments->node_alloc = 8;
	  lv->segments->nodes = grub_calloc (lv->segments->node_alloc,
					     sizeof (*lv->segments->nodes));
	  if (!lv->segments->nodes)
	    {
	      grub_free (lv);
	      goto fail2;
	    }
	  ptr = vblk[i].dynamic;
	  if (ptr + *ptr + 1 >= vblk[i].dynamic
	      + sizeof (vblk[i].dynamic))
	    {
	      grub_free (lv);
	      goto fail2;
	    }
	  lv->internal_id = grub_malloc ((grub_size_t) ptr[0] + 2);
	  if (!lv->internal_id)
	    {
	      grub_free (lv);
	      goto fail2;
	    }
	  grub_memcpy (lv->internal_id, ptr, ptr[0] + 1);
	  lv->internal_id[ptr[0] + 1] = 0;

	  ptr += *ptr + 1;
	  if (ptr + *ptr + 1 >= vblk[i].dynamic
	      + sizeof (vblk[i].dynamic))
	    {
	      grub_free (lv);
	      goto fail2;
	    }
	  if (grub_add (*ptr, 1, &sz))
	    {
	      grub_free (lv->internal_id);
	      grub_free (lv);
	      goto fail2;
	    }
	  lv->name = grub_malloc (sz);
	  if (!lv->name)
	    {
	      grub_free (lv->internal_id);
	      grub_free (lv);
	      goto fail2;
	    }
	  grub_memcpy (lv->name, ptr + 1, *ptr);
	  lv->name[*ptr] = 0;
	  lv->fullname = grub_xasprintf ("ldm/%s/%s",
					 vg->uuid, lv->name);
	  if (!lv->fullname)
	    {
	      grub_free (lv->internal_id);
	      grub_free (lv->name);
	      grub_free (lv);
	      goto fail2;
	    }
	  ptr += *ptr + 1;
	  if (ptr + *ptr + 1
	      >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      grub_free (lv->internal_id);
	      grub_free (lv->name);
	      grub_free (lv);
	      goto fail2;
	    }
	  /* ptr = volume type.  */
	  ptr += *ptr + 1;
	  if (ptr >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      grub_free (lv->internal_id);
	      grub_free (lv->name);
	      grub_free (lv);
	      goto fail2;
	    }
	  /* ptr = flags.  */
	  ptr += *ptr + 1;
	  if (ptr >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      grub_free (lv->internal_id);
	      grub_free (lv->name);
	      grub_free (lv);
	      goto fail2;
	    }

	  /* Skip state, type, unknown, volume number, zeros, flags. */
	  ptr += 14 + 1 + 1 + 1 + 3 + 1;
	  /* ptr = number of children.  */
	  if (ptr >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      grub_free (lv->internal_id);
	      grub_free (lv->name);
	      grub_free (lv);
	      goto fail2;
	    }
	  ptr += *ptr + 1;
	  if (ptr >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      grub_free (lv->internal_id);
	      grub_free (lv->name);
	      grub_free (lv);
	      goto fail2;
	    }

	  /* Skip 2 more fields.  */
	  ptr += 8 + 8;
	  if (ptr >= vblk[i].dynamic + sizeof (vblk[i].dynamic)
	      || ptr + *ptr + 1>= vblk[i].dynamic
	      + sizeof (vblk[i].dynamic))
	    {
	      grub_free (lv->internal_id);
	      grub_free (lv->name);
	      grub_free (lv);
	      goto fail2;
	    }
	  lv->size = read_int (ptr + 1, *ptr);
	  lv->segments->extent_count = lv->size;

	  lv->next = vg->lvs;
	  vg->lvs = lv;
	}
    }

  /* Now the components.  */
  for (cursec = startsec + 0x12; cursec < endsec; cursec++)
    {
      struct grub_ldm_vblk vblk[GRUB_DISK_SECTOR_SIZE
				/ sizeof (struct grub_ldm_vblk)];
      unsigned i;
      err = grub_disk_read (disk, cursec, 0,
			    sizeof(vblk), &vblk);
      if (err)
	goto fail2;

      for (i = 0; i < ARRAY_SIZE (vblk); i++)
	{
	  struct grub_diskfilter_lv *comp;
	  struct grub_diskfilter_lv *lv;
	  grub_uint8_t type;

	  grub_uint8_t *ptr;
	  if (grub_memcmp (vblk[i].magic, LDM_VBLK_MAGIC,
			   sizeof (vblk[i].magic)) != 0)
	    continue;
	  if (grub_be_to_cpu16 (vblk[i].update_status)
	      != STATUS_CONSISTENT
	      && grub_be_to_cpu16 (vblk[i].update_status)
	      != STATUS_STILL_ACTIVE)
	    continue;
	  if (vblk[i].type != ENTRY_COMPONENT)
	    continue;
	  comp = grub_zalloc (sizeof (*comp));
	  if (!comp)
	    goto fail2;
	  comp->visible = 0;
	  comp->name = 0;
	  comp->fullname = 0;

	  ptr = vblk[i].dynamic;
	  if (ptr + *ptr + 1 >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      grub_free (comp);
	      goto fail2;
	    }
	  comp->internal_id = grub_malloc ((grub_size_t) ptr[0] + 2);
	  if (!comp->internal_id)
	    {
	      grub_free (comp);
	      goto fail2;
	    }
	  grub_memcpy (comp->internal_id, ptr, ptr[0] + 1);
	  comp->internal_id[ptr[0] + 1] = 0;

	  ptr += *ptr + 1;
	  if (ptr + *ptr + 1 >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      grub_free (comp->internal_id);
	      grub_free (comp);
	      goto fail2;
	    }
	  /* ptr = name.  */
	  ptr += *ptr + 1;
	  if (ptr + *ptr + 1 >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      grub_free (comp->internal_id);
	      grub_free (comp);
	      goto fail2;
	    }
	  /* ptr = state.  */
	  ptr += *ptr + 1;
	  type = *ptr++;
	  /* skip zeros.  */
	  ptr += 4;
	  if (ptr >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      grub_free (comp->internal_id);
	      grub_free (comp);
	      goto fail2;
	    }

	  /* ptr = number of children. */
	  ptr += *ptr + 1;
	  if (ptr >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      grub_free (comp->internal_id);
	      grub_free (comp);
	      goto fail2;
	    }
	  ptr += 8 + 8;
	  if (ptr + *ptr + 1 >= vblk[i].dynamic
	      + sizeof (vblk[i].dynamic))
	    {
	      grub_free (comp->internal_id);
	      grub_free (comp);
	      goto fail2;
	    }
	  for (lv = vg->lvs; lv; lv = lv->next)
	    {
	      if (lv->internal_id[0] == ptr[0]
		  && grub_memcmp (lv->internal_id + 1, ptr + 1, ptr[0]) == 0)
		break;
	    }
	  if (!lv)
	    {
	      grub_free (comp->internal_id);
	      grub_free (comp);
	      continue;
	    }
	  comp->size = lv->size;
	  if (type == SPANNED)
	    {
	      comp->segment_alloc = 8;
	      comp->segment_count = 0;
	      comp->segments = grub_calloc (comp->segment_alloc,
					    sizeof (*comp->segments));
	      if (!comp->segments)
		{
		  grub_free (comp->internal_id);
		  grub_free (comp);
		  goto fail2;
		}
	    }
	  else
	    {
	      comp->segment_alloc = 1;
	      comp->segment_count = 1;
	      comp->segments = grub_malloc (sizeof (*comp->segments));
	      if (!comp->segments)
		{
		  grub_free (comp->internal_id);
		  grub_free (comp);
		  goto fail2;
		}
	      comp->segments->start_extent = 0;
	      comp->segments->extent_count = lv->size;
	      comp->segments->layout = 0;
	      if (type == STRIPE)
		comp->segments->type = GRUB_DISKFILTER_STRIPED;
	      else if (type == RAID5)
		{
		  comp->segments->type = GRUB_DISKFILTER_RAID5;
		  comp->segments->layout = GRUB_RAID_LAYOUT_SYMMETRIC_MASK;
		}
	      else
		{
		  grub_free (comp->segments);
		  grub_free (comp->internal_id);
		  grub_free (comp);
		  goto fail2;
		}
	      ptr += *ptr + 1;
	      ptr++;
	      if (!(vblk[i].flags & 0x10))
		{
		  grub_free (comp->segments);
		  grub_free (comp->internal_id);
		  grub_free (comp);
		  goto fail2;
		}
	      if (ptr >= vblk[i].dynamic + sizeof (vblk[i].dynamic)
		  || ptr + *ptr + 1 >= vblk[i].dynamic
		  + sizeof (vblk[i].dynamic))
		{
		  grub_free (comp->segments);
		  grub_free (comp->internal_id);
		  grub_free (comp);
		  goto fail2;
		}
	      comp->segments->stripe_size = read_int (ptr + 1, *ptr);
	      ptr += *ptr + 1;
	      if (ptr + *ptr + 1 >= vblk[i].dynamic
		  + sizeof (vblk[i].dynamic))
		{
		  grub_free (comp->segments);
		  grub_free (comp->internal_id);
		  grub_free (comp);
		  goto fail2;
		}
	      comp->segments->node_count = read_int (ptr + 1, *ptr);
	      comp->segments->node_alloc = comp->segments->node_count;
	      comp->segments->nodes = grub_calloc (comp->segments->node_alloc,
						   sizeof (*comp->segments->nodes));
	      if (!lv->segments->nodes)
		{
		  grub_free (comp->segments);
		  grub_free (comp->internal_id);
		  grub_free (comp);
		  goto fail2;
		}
	    }

	  if (lv->segments->node_alloc == lv->segments->node_count)
	    {
	      void *t;
	      grub_size_t sz;

	      if (grub_mul (lv->segments->node_alloc, 2, &lv->segments->node_alloc) ||
		  grub_mul (lv->segments->node_alloc, sizeof (*lv->segments->nodes), &sz))
		{
		  grub_free (comp->segments->nodes);
		  grub_free (comp->segments);
		  grub_free (comp->internal_id);
		  grub_free (comp);
		  goto fail2;
		}

	      t = grub_realloc (lv->segments->nodes, sz);
	      if (!t)
		{
		  grub_free (comp->segments->nodes);
		  grub_free (comp->segments);
		  grub_free (comp->internal_id);
		  grub_free (comp);
		  goto fail2;
		}
	      lv->segments->nodes = t;
	    }
	  lv->segments->nodes[lv->segments->node_count].pv = 0;
	  lv->segments->nodes[lv->segments->node_count].start = 0;
	  lv->segments->nodes[lv->segments->node_count++].lv = comp;
	  comp->next = vg->lvs;
	  vg->lvs = comp;
	}
    }
  /* Partitions.  */
  for (cursec = startsec + 0x12; cursec < endsec; cursec++)
    {
      struct grub_ldm_vblk vblk[GRUB_DISK_SECTOR_SIZE
				/ sizeof (struct grub_ldm_vblk)];
      unsigned i;
      err = grub_disk_read (disk, cursec, 0,
			    sizeof(vblk), &vblk);
      if (err)
	goto fail2;

      for (i = 0; i < ARRAY_SIZE (vblk); i++)
	{
	  struct grub_diskfilter_lv *comp;
	  struct grub_diskfilter_node part;
	  grub_disk_addr_t start, size;

	  grub_uint8_t *ptr;
	  part.name = 0;
	  if (grub_memcmp (vblk[i].magic, LDM_VBLK_MAGIC,
			   sizeof (vblk[i].magic)) != 0)
	    continue;
	  if (grub_be_to_cpu16 (vblk[i].update_status)
	      != STATUS_CONSISTENT
	      && grub_be_to_cpu16 (vblk[i].update_status)
	      != STATUS_STILL_ACTIVE)
	    continue;
	  if (vblk[i].type != ENTRY_PARTITION)
	    continue;
	  part.lv = 0;
	  part.pv = 0;

	  ptr = vblk[i].dynamic;
	  if (ptr + *ptr + 1 >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      goto fail2;
	    }
	  /* ID */
	  ptr += *ptr + 1;
	  if (ptr >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      goto fail2;
	    }
	  /* ptr = name.  */
	  ptr += *ptr + 1;
	  if (ptr >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      goto fail2;
	    }

	  /* skip zeros and logcommit id.  */
	  ptr += 4 + 8;
	  if (ptr + 16 >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      goto fail2;
	    }
	  part.start = read_int (ptr, 8);
	  start = read_int (ptr + 8, 8);
	  ptr += 16;
	  if (ptr >= vblk[i].dynamic + sizeof (vblk[i].dynamic)
	      || ptr + *ptr + 1 >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      goto fail2;
	    }
	  size = read_int (ptr + 1, *ptr);
	  ptr += *ptr + 1;
	  if (ptr >= vblk[i].dynamic + sizeof (vblk[i].dynamic)
	      || ptr + *ptr + 1 >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      goto fail2;
	    }

	  for (comp = vg->lvs; comp; comp = comp->next)
	    if (comp->internal_id[0] == ptr[0]
		&& grub_memcmp (ptr + 1, comp->internal_id + 1,
				comp->internal_id[0]) == 0)
	      goto out;
	  continue;
	out:
	  if (ptr >= vblk[i].dynamic + sizeof (vblk[i].dynamic)
	      || ptr + *ptr + 1 >= vblk[i].dynamic + sizeof (vblk[i].dynamic))
	    {
	      goto fail2;
	    }
	  ptr += *ptr + 1;
	  struct grub_diskfilter_pv *pv;
	  for (pv = vg->pvs; pv; pv = pv->next)
	    if (pv->internal_id[0] == ptr[0]
		&& grub_memcmp (pv->internal_id + 1, ptr + 1, ptr[0]) == 0)
	      part.pv = pv;

	  if (comp->segment_alloc == 1)
	    {
	      unsigned node_index;
	      ptr += *ptr + 1;
	      if (ptr + *ptr + 1 >= vblk[i].dynamic
		  + sizeof (vblk[i].dynamic))
		{
		  goto fail2;
		}
	      node_index = read_int (ptr + 1, *ptr);
	      if (node_index < comp->segments->node_count)
		comp->segments->nodes[node_index] = part;
	    }
	  else
	    {
	      if (comp->segment_alloc == comp->segment_count)
		{
		  void *t;
		  grub_size_t sz;

		  if (grub_mul (comp->segment_alloc, 2, &comp->segment_alloc) ||
		      grub_mul (comp->segment_alloc, sizeof (*comp->segments), &sz))
		    goto fail2;

		  t = grub_realloc (comp->segments, sz);
		  if (!t)
		    goto fail2;
		  comp->segments = t;
		}
	      comp->segments[comp->segment_count].start_extent = start;
	      comp->segments[comp->segment_count].extent_count = size;
	      comp->segments[comp->segment_count].type = GRUB_DISKFILTER_STRIPED;
	      comp->segments[comp->segment_count].node_count = 1;
	      comp->segments[comp->segment_count].node_alloc = 1;
	      comp->segments[comp->segment_count].nodes
		= grub_malloc (sizeof (*comp->segments[comp->segment_count].nodes));
	      if (!comp->segments[comp->segment_count].nodes)
		goto fail2;
	      comp->segments[comp->segment_count].nodes[0] = part;
	      comp->segment_count++;
	    }
	}
    }
  if (grub_diskfilter_vg_register (vg))
    goto fail2;
  return vg;
 fail2:
  {
    struct grub_diskfilter_lv *lv, *next_lv;
    struct grub_diskfilter_pv *pv, *next_pv;
    for (lv = vg->lvs; lv; lv = next_lv)
      {
	unsigned i;
	for (i = 0; i < lv->segment_count; i++)
	  grub_free (lv->segments[i].nodes);

	next_lv = lv->next;
	grub_free (lv->segments);
	grub_free (lv->internal_id);
	grub_free (lv->name);
	grub_free (lv->fullname);
	grub_free (lv);
      }
    for (pv = vg->pvs; pv; pv = next_pv)
      {
	next_pv = pv->next;
	grub_free (pv->id.uuid);
	grub_free (pv);
      }
  }
  grub_free (vg->uuid);
  grub_free (vg);
  return NULL;
}

static struct grub_diskfilter_vg * 
grub_ldm_detect (grub_disk_t disk,
		 struct grub_diskfilter_pv_id *id,
		 grub_disk_addr_t *start_sector)
{
  grub_err_t err;
  struct grub_ldm_label label;
  struct grub_diskfilter_vg *vg;

#ifdef GRUB_UTIL
  grub_util_info ("scanning %s for LDM", disk->name);
#endif

  {
    int i;
    int has_ldm = msdos_has_ldm_partition (disk);
    for (i = 0; i < 3; i++)
      {
	grub_disk_addr_t sector = LDM_LABEL_SECTOR;
	switch (i)
	  {
	  case 0:
	    if (!has_ldm)
	      continue;
	    sector = LDM_LABEL_SECTOR;
	    break;
	  case 1:
	    /* LDM is never inside a partition.  */
	    if (!has_ldm || disk->partition)
	      continue;
	    sector = grub_disk_native_sectors (disk);
	    if (sector == GRUB_DISK_SIZE_UNKNOWN)
	      continue;
	    sector--;
	    break;
	    /* FIXME: try the third copy.  */
	  case 2:
	    sector = gpt_ldm_sector (disk);
	    if (!sector)
	      continue;
	    break;
	  }
	err = grub_disk_read (disk, sector, 0,
			      sizeof(label), &label);
	if (err)
	  return NULL;
	if (grub_memcmp (label.magic, LDM_MAGIC, sizeof (label.magic)) == 0
	    && grub_be_to_cpu16 (label.ver_major) == 0x02
	    && grub_be_to_cpu16 (label.ver_minor) >= 0x0b
	    && grub_be_to_cpu16 (label.ver_minor) <= 0x0c)
	  break;
      }

    /* Return if we didn't find a label. */
    if (i == 3)
      {
#ifdef GRUB_UTIL
	grub_util_info ("no LDM signature found");
#endif
	return NULL;
      }
  }

  id->uuid = grub_malloc (LDM_GUID_STRLEN + 1);
  if (!id->uuid)
    return NULL;
  grub_memcpy (id->uuid, label.disk_guid, LDM_GUID_STRLEN);
  id->uuid[LDM_GUID_STRLEN] = 0;
  id->uuidlen = grub_strlen ((char *) id->uuid);
  *start_sector = grub_be_to_cpu64 (label.pv_start);

  {
    grub_size_t s;
    for (s = 0; s < LDM_GUID_STRLEN && label.group_guid[s]; s++);
    vg = grub_diskfilter_get_vg_by_uuid (s, label.group_guid);
    if (! vg)
      vg = make_vg (disk, &label);
  }

  if (!vg)
    {
      grub_free (id->uuid);
      return NULL;
    }
  return vg;
}

#ifdef GRUB_UTIL

char *
grub_util_get_ldm (grub_disk_t disk, grub_disk_addr_t start)
{
  struct grub_diskfilter_pv *pv = NULL;
  struct grub_diskfilter_vg *vg = NULL;
  struct grub_diskfilter_lv *res = 0, *lv, *res_lv = 0;

  pv = grub_diskfilter_get_pv_from_disk (disk, &vg);

  if (!pv)
    return NULL;

  for (lv = vg->lvs; lv; lv = lv->next)
    if (lv->segment_count == 1 && lv->segments->node_count == 1
	&& lv->segments->type == GRUB_DISKFILTER_STRIPED
	&& lv->segments->nodes->pv == pv
	&& lv->segments->nodes->start + pv->start_sector == start)
      {
	res_lv = lv;
	break;
      }
  if (!res_lv)
    return NULL;
  for (lv = vg->lvs; lv; lv = lv->next)
    if (lv->segment_count == 1 && lv->segments->node_count == 1
	&& lv->segments->type == GRUB_DISKFILTER_MIRROR
	&& lv->segments->nodes->lv == res_lv)
      {
	res = lv;
	break;
      }
  if (res && res->fullname)
    return grub_strdup (res->fullname);
  return NULL;
}

int
grub_util_is_ldm (grub_disk_t disk)
{
  int i;
  int has_ldm = msdos_has_ldm_partition (disk);
  for (i = 0; i < 3; i++)
    {
      grub_disk_addr_t sector = LDM_LABEL_SECTOR;
      grub_err_t err;
      struct grub_ldm_label label;

      switch (i)
	{
	case 0:
	  if (!has_ldm)
	    continue;
	  sector = LDM_LABEL_SECTOR;
	  break;
	case 1:
	  /* LDM is never inside a partition.  */
	  if (!has_ldm || disk->partition)
	    continue;
	  sector = grub_disk_native_sectors (disk);
	  if (sector == GRUB_DISK_SIZE_UNKNOWN)
	    continue;
	  sector--;
	  break;
	  /* FIXME: try the third copy.  */
	case 2:
	  sector = gpt_ldm_sector (disk);
	  if (!sector)
	    continue;
	  break;
	}
      err = grub_disk_read (disk, sector, 0, sizeof(label), &label);
      if (err)
	{
	  grub_errno = GRUB_ERR_NONE;
	  return 0;
	}
      /* This check is more relaxed on purpose.  */
      if (grub_memcmp (label.magic, LDM_MAGIC, sizeof (label.magic)) == 0)
	return 1;
    }

  return 0;
}

grub_err_t
grub_util_ldm_embed (struct grub_disk *disk, unsigned int *nsectors,
		     unsigned int max_nsectors,
		     grub_embed_type_t embed_type,
		     grub_disk_addr_t **sectors)
{
  struct grub_diskfilter_pv *pv = NULL;
  struct grub_diskfilter_vg *vg;
  struct grub_diskfilter_lv *lv;
  unsigned i;

  if (embed_type != GRUB_EMBED_PCBIOS)
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		       "LDM currently supports only PC-BIOS embedding");
  if (disk->partition)
    return grub_error (GRUB_ERR_BUG, "disk isn't LDM");
  pv = grub_diskfilter_get_pv_from_disk (disk, &vg);
  if (!pv)
    return grub_error (GRUB_ERR_BUG, "disk isn't LDM");
  for (lv = vg->lvs; lv; lv = lv->next)
    {
      struct grub_diskfilter_lv *comp;

      if (!lv->visible || !lv->fullname)
	continue;

      if (lv->segment_count != 1)
	continue;
      if (lv->segments->type != GRUB_DISKFILTER_MIRROR
	  || lv->segments->node_count != 1
	  || lv->segments->start_extent != 0
	  || lv->segments->extent_count != lv->size)
	continue;

      comp = lv->segments->nodes->lv;
      if (!comp)
	continue;

      if (comp->segment_count != 1 || comp->size != lv->size)
	continue;
      if (comp->segments->type != GRUB_DISKFILTER_STRIPED
	  || comp->segments->node_count != 1
	  || comp->segments->start_extent != 0
	  || comp->segments->extent_count != lv->size)
	continue;

      /* How to implement proper check is to be discussed.  */
#if 1
      if (1)
	continue;
#else
      if (grub_strcmp (lv->name, "Volume5") != 0)
	continue;
#endif
      if (lv->size < *nsectors)
	return grub_error (GRUB_ERR_OUT_OF_RANGE,
			   /* TRANSLATORS: it's a partition for embedding,
			      not a partition embed into something. GRUB
			      install tools put core.img into a place
			      usable for bootloaders (called generically
			      "embedding zone") and this operation is
			      called "embedding".  */
			   N_("your LDM Embedding Partition is too small;"
			      " embedding won't be possible"));
      *nsectors = lv->size;
      if (*nsectors > max_nsectors)
	*nsectors = max_nsectors;
      *sectors = grub_calloc (*nsectors, sizeof (**sectors));
      if (!*sectors)
	return grub_errno;
      for (i = 0; i < *nsectors; i++)
	(*sectors)[i] = (lv->segments->nodes->start
			 + comp->segments->nodes->start
			 + comp->segments->nodes->pv->start_sector + i);
      return GRUB_ERR_NONE;
    }

  return grub_error (GRUB_ERR_FILE_NOT_FOUND,
		     /* TRANSLATORS: it's a partition for embedding,
			not a partition embed into something.  */
		     N_("this LDM has no Embedding Partition;"
			" embedding won't be possible"));
}
#endif

static struct grub_diskfilter grub_ldm_dev = {
  .name = "ldm",
  .detect = grub_ldm_detect,
  .next = 0
};

GRUB_MOD_INIT (ldm)
{
  grub_diskfilter_register_back (&grub_ldm_dev);
}

GRUB_MOD_FINI (ldm)
{
  grub_diskfilter_unregister (&grub_ldm_dev);
}
