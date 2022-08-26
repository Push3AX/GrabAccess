/* pc.c - Read PC style partition tables.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2004,2005,2006,2007,2008,2009  Free Software Foundation, Inc.
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

#include <grub/partition.h>
#include <grub/msdos_partition.h>
#include <grub/disk.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static struct grub_partition_map grub_msdos_partition_map;


#ifdef GRUB_UTIL
#include <grub/emu/misc.h>

struct embed_signature
{
  const char *name;
  const char *signature;
  int signature_len;
  enum { TYPE_SOFTWARE, TYPE_RAID } type;
};

const char message_warn[][200] = {
  /* TRANSLATORS: MBR gap and boot track is the same thing and is the space
     between MBR and first partitition. If your language translates well only
     "boot track", you can just use it everywhere. Next two messages are about
     RAID controllers/software bugs which GRUB has to live with. Please spread
     the message that these are bugs in other software and not merely
     suboptimal behaviour.  */
  [TYPE_RAID] = N_("Sector %llu is already in use by raid controller `%s';"
		   " avoiding it.  "
		   "Please ask the manufacturer not to store data in MBR gap"),
  [TYPE_SOFTWARE] = N_("Sector %llu is already in use by the program `%s';"
		       " avoiding it.  "
		       "This software may cause boot or other problems in "
		       "future.  Please ask its authors not to store data "
		       "in the boot track") 
};


/* Signatures of other software that may be using sectors in the embedding
   area.  */
struct embed_signature embed_signatures[] =
  {
    {
      .name = "ZISD",
      .signature = "ZISD",
      .signature_len = 4,
      .type = TYPE_SOFTWARE
    },
    {
      .name = "FlexNet",
      .signature = "\xd4\x41\xa0\xf5\x03\x00\x03\x00",
      .signature_len = 8,
      .type = TYPE_SOFTWARE
    },
    {
      .name = "FlexNet",
      .signature = "\xd8\x41\xa0\xf5\x02\x00\x02\x00",
      .signature_len = 8,
      .type = TYPE_SOFTWARE
    },
    {
      /* from Ryan Perkins */
      .name = "HP Backup and Recovery Manager (?)",
      .signature = "\x70\x8a\x5d\x46\x35\xc5\x1b\x93"
		   "\xae\x3d\x86\xfd\xb1\x55\x3e\xe0",
      .signature_len = 16,
      .type = TYPE_SOFTWARE
    },
    {
      .name = "HighPoint RAID controller",
      .signature = "ycgl",
      .signature_len = 4,
      .type = TYPE_RAID
    },
    {
      /* https://bugs.launchpad.net/bugs/987022 */
      .name = "Acer registration utility (?)",
      .signature = "GREGRegDone.Tag\x00",
      .signature_len = 16,
      .type = TYPE_SOFTWARE
    }
  };
#endif

grub_err_t
grub_partition_msdos_iterate (grub_disk_t disk,
			      grub_partition_iterate_hook_t hook,
			      void *hook_data)
{
  struct grub_partition p;
  struct grub_msdos_partition_mbr mbr;
  int labeln = 0;
  grub_disk_addr_t lastaddr;
  grub_disk_addr_t ext_offset;
  grub_disk_addr_t delta = 0;

  if (disk->partition && disk->partition->partmap == &grub_msdos_partition_map)
    {
      if (disk->partition->msdostype == GRUB_PC_PARTITION_TYPE_LINUX_MINIX)
	delta = disk->partition->start;
      else
	return grub_error (GRUB_ERR_BAD_PART_TABLE, "no embedding supported");
    }

  p.offset = 0;
  ext_offset = 0;
  p.number = -1;
  p.partmap = &grub_msdos_partition_map;

  /* Any value different than `p.offset' will satisfy the check during
     first loop.  */
  lastaddr = !p.offset;

  while (1)
    {
      int i;
      struct grub_msdos_partition_entry *e;

      /* Read the MBR.  */
      if (grub_disk_read (disk, p.offset, 0, sizeof (mbr), &mbr))
	goto finish;

      /* If this is a GPT partition, this MBR is just a dummy.  */
      if (p.offset == 0)
    {
      for (i = 0; i < 4; i++)
        if (mbr.entries[i].type == GRUB_PC_PARTITION_TYPE_GPT_DISK)
          return grub_error (GRUB_ERR_BAD_PART_TABLE, "dummy mbr");
      grub_memcpy (p.msdossign, mbr.unique_signature, 4);
    }

      /* This is our loop-detection algorithm. It works the following way:
	 It saves last position which was a power of two. Then it compares the
	 saved value with a current one. This way it's guaranteed that the loop
	 will be broken by at most third walk.
       */
      if (labeln && lastaddr == p.offset)
	return grub_error (GRUB_ERR_BAD_PART_TABLE, "loop detected");

      labeln++;
      if ((labeln & (labeln - 1)) == 0)
	lastaddr = p.offset;

      /* Check if it is valid.  */
      if (mbr.signature != grub_cpu_to_le16_compile_time (GRUB_PC_PARTITION_SIGNATURE))
	return grub_error (GRUB_ERR_BAD_PART_TABLE, "no signature");

      for (i = 0; i < 4; i++)
	if (mbr.entries[i].flag & 0x7f)
	  return grub_error (GRUB_ERR_BAD_PART_TABLE, "bad boot flag");

      /* Analyze DOS partitions.  */
      for (p.index = 0; p.index < 4; p.index++)
	{
	  e = mbr.entries + p.index;

	  p.start = p.offset
	    + ((grub_disk_addr_t)grub_le_to_cpu32 (e->start)
	       << (disk->log_sector_size - GRUB_DISK_SECTOR_BITS)) - delta;
	  p.len = (grub_uint64_t)grub_le_to_cpu32 (e->length)
	    << (disk->log_sector_size - GRUB_DISK_SECTOR_BITS);
	  p.msdostype = e->type;
      p.flag = e->flag;

	  grub_dprintf ("partition",
			"partition %d: flag 0x%x, type 0x%x, start 0x%llx, len 0x%llx\n",
			p.index, e->flag, e->type,
			(unsigned long long) p.start,
			(unsigned long long) p.len);

	  /* If this partition is a normal one, call the hook.  */
	  if (! grub_msdos_partition_is_empty (e->type)
	      && ! grub_msdos_partition_is_extended (e->type))
	    {
	      p.number++;

	      if (hook (disk, &p, hook_data))
		return grub_errno;
	    }
	  else if (p.number < 3)
	    /* If this partition is a logical one, shouldn't increase the
	       partition number.  */
	    p.number++;
	}

      /* Find an extended partition.  */
      for (i = 0; i < 4; i++)
	{
	  e = mbr.entries + i;

	  if (grub_msdos_partition_is_extended (e->type))
	    {
	      p.offset = ext_offset
		+ ((grub_disk_addr_t)grub_le_to_cpu32 (e->start)
		   << (disk->log_sector_size - GRUB_DISK_SECTOR_BITS));
	      if (! ext_offset)
		ext_offset = p.offset;

	      break;
	    }
	}

      /* If no extended partition, the end.  */
      if (i == 4)
	break;
    }

 finish:
  return grub_errno;
}

#ifdef GRUB_UTIL

#pragma GCC diagnostic ignored "-Wformat-nonliteral"

static grub_err_t
pc_partition_map_embed (struct grub_disk *disk, unsigned int *nsectors,
			unsigned int max_nsectors,
			grub_embed_type_t embed_type,
			grub_disk_addr_t **sectors,
			int warn_short)
{
  grub_disk_addr_t end = ~0ULL;
  struct grub_msdos_partition_mbr mbr;
  int labeln = 0;
  /* Any value different than `p.offset' will satisfy the check during
     first loop.  */
  grub_disk_addr_t lastaddr = 1;
  grub_disk_addr_t ext_offset = 0;
  grub_disk_addr_t offset = 0;

  if (embed_type != GRUB_EMBED_PCBIOS)
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		       "PC-style partitions currently support "
		       "only PC-BIOS embedding");

  if (disk->partition)
    return grub_error (GRUB_ERR_OUT_OF_RANGE,
		       "Embedding on MSDOS subpartition isn't supported");

  while (1)
    {
      int i;
      struct grub_msdos_partition_entry *e;
      grub_err_t err;

      /* Read the MBR.  */
      err = grub_disk_read (disk, offset, 0, sizeof (mbr), &mbr);
      if (err)
	return err;

      /* This is our loop-detection algorithm. It works the following way:
	 It saves last position which was a power of two. Then it compares the
	 saved value with a current one. This way it's guaranteed that the loop
	 will be broken by at most third walk.
       */
      if (labeln && lastaddr == offset)
	return grub_error (GRUB_ERR_BAD_PART_TABLE, "loop detected");

      labeln++;
      if ((labeln & (labeln - 1)) == 0)
	lastaddr = offset;

      /* Check if it is valid.  */
      if (mbr.signature != grub_cpu_to_le16_compile_time (GRUB_PC_PARTITION_SIGNATURE))
	return grub_error (GRUB_ERR_BAD_PART_TABLE, "no signature");

      for (i = 0; i < 4; i++)
	if (mbr.entries[i].flag & 0x7f)
	  return grub_error (GRUB_ERR_BAD_PART_TABLE, "bad boot flag");

      /* Analyze DOS partitions.  */
      for (i = 0; i < 4; i++)
	{
	  e = mbr.entries + i;

	  if (!grub_msdos_partition_is_empty (e->type)
	      && end > offset
	      + ((grub_disk_addr_t)grub_le_to_cpu32 (e->start)
		 << (disk->log_sector_size - GRUB_DISK_SECTOR_BITS)))
	    end = offset + ((grub_disk_addr_t)grub_le_to_cpu32 (e->start)
			    << (disk->log_sector_size - GRUB_DISK_SECTOR_BITS));

	  /* If this is a GPT partition, this MBR is just a dummy.  */
	  if (e->type == GRUB_PC_PARTITION_TYPE_GPT_DISK && i == 0)
	    return grub_error (GRUB_ERR_BAD_PART_TABLE, "dummy mbr");
	}

      /* Find an extended partition.  */
      for (i = 0; i < 4; i++)
	{
	  e = mbr.entries + i;

	  if (grub_msdos_partition_is_extended (e->type))
	    {
	      offset = ext_offset 
		+ ((grub_disk_addr_t)grub_le_to_cpu32 (e->start)
		   << (disk->log_sector_size - GRUB_DISK_SECTOR_BITS));
	      if (! ext_offset)
		ext_offset = offset;

	      break;
	    }
	}

      /* If no extended partition, the end.  */
      if (i == 4)
	break;
    }

  if (end >= *nsectors + 1)
    {
      unsigned i, j;
      char *embed_signature_check;
      unsigned int orig_nsectors, avail_nsectors;

      orig_nsectors = *nsectors;
      *nsectors = end - 1;
      avail_nsectors = *nsectors;
      if (*nsectors > max_nsectors)
	*nsectors = max_nsectors;
      *sectors = grub_calloc (*nsectors, sizeof (**sectors));
      if (!*sectors)
	return grub_errno;
      for (i = 0; i < *nsectors; i++)
	(*sectors)[i] = 1 + i;

      /* Check for software that is already using parts of the embedding
       * area.
       */
      embed_signature_check = grub_malloc (GRUB_DISK_SECTOR_SIZE);
      for (i = 0; i < *nsectors; i++)
	{
	  if (grub_disk_read (disk, (*sectors)[i], 0, GRUB_DISK_SECTOR_SIZE,
			      embed_signature_check))
	    continue;

	  for (j = 0; j < ARRAY_SIZE (embed_signatures); j++)
	    if (! grub_memcmp (embed_signatures[j].signature,
			       embed_signature_check,
			       embed_signatures[j].signature_len))
	      break;
	  if (j == ARRAY_SIZE (embed_signatures))
	    continue;
	  grub_util_warn (_(message_warn[embed_signatures[j].type]),
			  (*sectors)[i], embed_signatures[j].name);
	  avail_nsectors--;
	  if (avail_nsectors < *nsectors)
	    *nsectors = avail_nsectors;

	  /* Avoid this sector.  */
	  for (j = i; j < *nsectors; j++)
	    (*sectors)[j]++;

	  /* Have we run out of space?  */
	  if (avail_nsectors < orig_nsectors)
	    break;

	  /* Make sure to check the next sector.  */
	  i--;
	}
      grub_free (embed_signature_check);

      if (*nsectors < orig_nsectors)
	return grub_error (GRUB_ERR_OUT_OF_RANGE,
			   N_("other software is using the embedding area, and "
			      "there is not enough room for core.img.  Such "
			      "software is often trying to store data in a way "
			      "that avoids detection.  We recommend you "
			      "investigate"));

      return GRUB_ERR_NONE;
    }

  if (end < GRUB_MIN_RECOMMENDED_MBR_GAP && warn_short)
    grub_util_warn ("You have a short MBR gap and use advanced config. Please increase post-MBR gap.");

  if (end <= 1)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND,
		       N_("this msdos-style partition label has no "
			  "post-MBR gap; embedding won't be possible"));

  if (*nsectors > 62)
    return grub_error (GRUB_ERR_OUT_OF_RANGE,
		       N_("your core.img is unusually large.  "
			  "It won't fit in the embedding area"));

  return grub_error (GRUB_ERR_OUT_OF_RANGE,
		     N_("your embedding area is unusually small.  "
			"core.img won't fit in it."));
}

#pragma GCC diagnostic error "-Wformat-nonliteral"

#endif


/* Partition map type.  */
static struct grub_partition_map grub_msdos_partition_map =
  {
    .name = "msdos",
    .iterate = grub_partition_msdos_iterate,
#ifdef GRUB_UTIL
    .embed = pc_partition_map_embed
#endif
  };

GRUB_MOD_INIT(part_msdos)
{
  grub_partition_map_register (&grub_msdos_partition_map);
}

GRUB_MOD_FINI(part_msdos)
{
  grub_partition_map_unregister (&grub_msdos_partition_map);
}
