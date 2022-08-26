/*
 * Copyright (C) 2012 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <vfat.h>
#include <misc.h>

#if __GNUC__ >= 9
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif

/** Virtual files */
struct vfat_file vfat_files[VDISK_MAX_FILES];

/**
 * Read from virtual Master Boot Record
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
static void
vfat_mbr (uint64_t lba __unused, unsigned int count __unused, void *data)
{
  struct vfat_mbr *mbr = data;
  /* Construct MBR */
  memset (mbr, 0, sizeof (*mbr));
  mbr->partitions[0].bootable = VDISK_MBR_BOOTABLE;
  mbr->partitions[0].type = VDISK_MBR_TYPE_FAT32;
  mbr->partitions[0].start = VDISK_PARTITION_LBA;
  mbr->partitions[0].length = VDISK_PARTITION_COUNT;
  mbr->signature = VDISK_MBR_SIGNATURE;
  mbr->magic = VDISK_MBR_MAGIC;
}

/**
 * Read from virtual Volume Boot Record
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
static void
vfat_vbr (uint64_t lba __unused, unsigned int count __unused, void *data)
{
  struct vfat_vbr *vbr = data;
  /* Construct VBR */
  memset (vbr, 0, sizeof (*vbr));
  vbr->jump[0] = VDISK_VBR_JUMP_WTF_MS;
  memcpy (vbr->oemid, VDISK_VBR_OEMID, sizeof (vbr->oemid));
  vbr->bytes_per_sector = VDISK_SECTOR_SIZE;
  vbr->sectors_per_cluster = VDISK_CLUSTER_COUNT;
  vbr->reserved_sectors = VDISK_RESERVED_COUNT;
  vbr->fats = 1;
  vbr->media = VDISK_VBR_MEDIA;
  vbr->sectors_per_track = VDISK_SECTORS_PER_TRACK;
  vbr->heads = VDISK_HEADS;
  vbr->hidden_sectors = VDISK_VBR_LBA;
  vbr->sectors = VDISK_PARTITION_COUNT;
  vbr->sectors_per_fat = VDISK_SECTORS_PER_FAT;
  vbr->root = VDISK_ROOT_CLUSTER;
  vbr->fsinfo = VDISK_FSINFO_SECTOR;
  vbr->backup = VDISK_BACKUP_VBR_SECTOR;
  vbr->signature = VDISK_VBR_SIGNATURE;
  vbr->serial = VDISK_VBR_SERIAL;
  memcpy (vbr->label, VDISK_VBR_LABEL, sizeof (vbr->label));
  memcpy (vbr->system, VDISK_VBR_SYSTEM, sizeof (vbr->system));
  vbr->magic = VDISK_VBR_MAGIC;
}

/**
 * Read from virtual FSInfo
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
static void
vfat_fsinfo (uint64_t lba __unused, unsigned int count __unused, void *data)
{
  struct vfat_fsinfo *fsinfo = data;
  /* Construct FSInfo */
  memset (fsinfo, 0, sizeof (*fsinfo));
  fsinfo->magic1 = VDISK_FSINFO_MAGIC1;
  fsinfo->magic2 = VDISK_FSINFO_MAGIC2;
  fsinfo->next_free = VDISK_FSINFO_NEXT_FREE;
  fsinfo->magic3 = VDISK_FSINFO_MAGIC3;
}

/**
 * Read from virtual FAT
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
static void vfat_fat (uint64_t lba, unsigned int count, void *data)
{
  uint32_t *next = data;
  uint32_t start;
  uint32_t end;
  uint32_t file_end_marker;
  unsigned int i;
  /* Calculate window within FAT */
  start = ((lba - VDISK_FAT_LBA) * (VDISK_SECTOR_SIZE / sizeof (*next)));
  end = (start + (count * (VDISK_SECTOR_SIZE / sizeof (*next))));
  next -= start;

  /* Start by marking each cluster as chaining to the next */
  for (i = start; i < end; i++)
    next[i] = (i + 1);

  /* Add first-sector special values, if applicable */
  if (start == 0)
  {
    next[0] = ((VDISK_FAT_END_MARKER & ~0xff) |
          VDISK_VBR_MEDIA);
    for (i = 1; i < (VDISK_SECTOR_SIZE / sizeof (*next)); i++)
      next[i] = VDISK_FAT_END_MARKER;
  }

  /* Add end-of-file markers, if applicable */
  for (i = 0; i < VDISK_MAX_FILES; i++)
  {
    if (vfat_files[i].read)
    {
      file_end_marker = (VDISK_FILE_CLUSTER (i) +
              ((vfat_files[i].xlen - 1) /
                VDISK_CLUSTER_SIZE));
      if ((file_end_marker >= start) &&
           (file_end_marker < end))
      {
        next[file_end_marker] = VDISK_FAT_END_MARKER;
      }
    }
  }
}

/**
 * Initialise empty directory
 *
 * @v dir    Virtual directory
 * @ret dirent    Starting (i.e. final) directory entry
 */
static union vfat_directory_entry *
vfat_empty_dir (struct vfat_directory *dir)
{
  unsigned int i;

  /* Mark all entries as present and deleted */
  memset (dir, 0, sizeof (*dir));
  for (i = 0; i < VDISK_DIRENT_PER_SECTOR; i++)
  {
    dir->entry[i].deleted = VDISK_DIRENT_DELETED;
  }

  return &dir->entry[ VDISK_DIRENT_PER_SECTOR - 1 ];
}

/**
 * Construct directory entry
 *
 * @v dirent    Starting (i.e. final) directory entry
 * @v name    File name
 * @v len    File length
 * @v attr    File attributes
 * @v cluster    File starting cluster
 * @ret next    Next available directory entry
 */
static union vfat_directory_entry *
vfat_directory_entry (union vfat_directory_entry *dirent, const char *name,
      size_t len,unsigned int attr, uint32_t cluster)
{
  union vfat_directory_entry *dos = dirent;
  union vfat_directory_entry *lfn = (dos - 1);
  uint8_t *checksum_data;
  uint8_t checksum;
  unsigned int sequence;
  uint16_t *lfn_char;
  char c;
  unsigned int i;

  /* Populate directory entry (with invalid 8.3 filename) */
  memset (dos->dos.filename.raw, ' ', sizeof (dos->dos.filename.raw));
  dos->dos.attr = attr;
  dos->dos.size = len;
  dos->dos.cluster_high = (cluster >> 16);
  dos->dos.cluster_low = (cluster & 0xffff);
  dos->dos.created_date = 0x2821;
  dos->dos.created_time = 0x0000;
  dos->dos.created_deciseconds = 0;
  dos->dos.modified_date = 0x2821;
  dos->dos.modified_time = 0x0000;

  /* Calculate checksum of 8.3 filename */
  checksum = 0;
  checksum_data = dos->dos.filename.raw;
  for (i = 0; i < sizeof (dos->dos.filename.raw); i++)
  {
    checksum = ((((checksum & 1) << 7) |
             (checksum >> 1)) +
           *(checksum_data++));
  }

  /* Construct long filename record */
  lfn_char = &lfn->lfn.name_1[0];
  sequence = 1;
  while (1)
  {
    /* Initialise long filename, if necessary */
    if (lfn->lfn.attr != VDISK_LFN_ATTR)
    {
      lfn->lfn.sequence = sequence++;
      memset (lfn->lfn.name_1, 0xff,
         sizeof (lfn->lfn.name_1));
      lfn->lfn.attr = VDISK_LFN_ATTR;
      lfn->lfn.checksum = checksum;
      memset (lfn->lfn.name_2, 0xff,
         sizeof (lfn->lfn.name_2));
      memset (lfn->lfn.name_3, 0xff,
         sizeof (lfn->lfn.name_3));
    }

    /* Add character to long filename */
    c = *(name++);
    *lfn_char = c;
    if (! c)
      break;

    /* Move to next character within long filename */
    if (lfn_char == &lfn->lfn.name_1[4])
    {
      lfn_char = &lfn->lfn.name_2[0];
    }
    else if (lfn_char == &lfn->lfn.name_2[5])
    {
      lfn_char = &lfn->lfn.name_3[0];
    }
    else if (lfn_char == &lfn->lfn.name_3[1])
    {
      lfn--;
      lfn_char = &lfn->lfn.name_1[0];
    }
    else
    {
      lfn_char++;
    }
  }
  lfn->lfn.sequence |= VDISK_LFN_END;

  return (lfn - 1);
}

/**
 * Read subdirectories from virtual root directory
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
static void
vfat_root (uint64_t lba __unused, unsigned int count __unused, void *data)
{
  struct vfat_directory *dir = data;
  union vfat_directory_entry *dirent;
  /* Construct subdirectories */
  dirent = vfat_empty_dir (dir);
  dirent = vfat_directory_entry (dirent, "BOOT", 0, VDISK_DIRECTORY,
           VDISK_BOOT_CLUSTER);
  dirent = vfat_directory_entry (dirent, "SOURCES", 0, VDISK_DIRECTORY,
           VDISK_SOURCES_CLUSTER);
  dirent = vfat_directory_entry (dirent, "EFI", 0, VDISK_DIRECTORY,
           VDISK_EFI_CLUSTER);
}

/**
 * Read subdirectories from virtual boot directory
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
static void
vfat_boot (uint64_t lba __unused, unsigned int count __unused, void *data)
{
  struct vfat_directory *dir = data;
  union vfat_directory_entry *dirent;

  /* Construct subdirectories */
  dirent = vfat_empty_dir (dir);
  dirent = vfat_directory_entry (dirent, "FONTS", 0, VDISK_DIRECTORY,
           VDISK_FONTS_CLUSTER);
  dirent = vfat_directory_entry (dirent, "RESOURCES", 0,
           VDISK_DIRECTORY,
           VDISK_RESOURCES_CLUSTER);
}

/**
 * Read subdirectories from virtual sources directory
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
static void
vfat_sources (uint64_t lba __unused, unsigned int count __unused, void *data)
{
  struct vfat_directory *dir = data;
  /* Construct subdirectories */
  vfat_empty_dir (dir);
}

/**
 * Read subdirectories from virtual fonts directory
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
static void
vfat_fonts (uint64_t lba __unused, unsigned int count __unused, void *data)
{
  struct vfat_directory *dir = data;
  /* Construct subdirectories */
  vfat_empty_dir (dir);
}

/**
 * Read subdirectories from virtual resources directory
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
static void
vfat_resources (uint64_t lba __unused, unsigned int count __unused, void *data)
{
  struct vfat_directory *dir = data;
  /* Construct subdirectories */
  vfat_empty_dir (dir);
}

/**
 * Read subdirectories from virtual EFI directory
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
static void
vfat_efi (uint64_t lba __unused, unsigned int count __unused, void *data)
{
  struct vfat_directory *dir = data;
  union vfat_directory_entry *dirent;
  /* Construct subdirectories */
  dirent = vfat_empty_dir (dir);
  dirent = vfat_directory_entry (dirent, "BOOT", 0, VDISK_DIRECTORY,
           VDISK_BOOT_CLUSTER);
  dirent = vfat_directory_entry (dirent, "MICROSOFT", 0,
           VDISK_DIRECTORY,
           VDISK_MICROSOFT_CLUSTER);
}

/**
 * Read subdirectories from virtual Microsoft directory
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
static void
vfat_microsoft (uint64_t lba __unused, unsigned int count __unused, void *data)
{
  struct vfat_directory *dir = data;
  union vfat_directory_entry *dirent;
  /* Construct subdirectories */
  dirent = vfat_empty_dir (dir);
  dirent = vfat_directory_entry (dirent, "BOOT", 0, VDISK_DIRECTORY,
           VDISK_BOOT_CLUSTER);
}

/**
 * Read files from virtual directory
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
static void
vfat_dir_files (uint64_t lba, unsigned int count, void *data)
{
  struct vfat_directory *dir;
  union vfat_directory_entry *dirent;
  struct vfat_file *file;
  unsigned int idx;

  for (; count; lba++, count--, data = (char *)data + VDISK_SECTOR_SIZE)
  {
    /* Initialise directory */
    dir = data;
    vfat_empty_dir (dir);
    dirent = &dir->entry[VDISK_DIRENT_PER_SECTOR - 1];

    /* Identify file */
    idx = VDISK_FILE_DIRENT_IDX (lba);
    assert (idx < (sizeof (vfat_files) /
         sizeof (vfat_files[0])));
    file = &vfat_files[idx];
    if (! file->read)
      continue;

    /* Populate directory entry */
    vfat_directory_entry (dirent, file->name, file->xlen,
          VDISK_READ_ONLY,
          VDISK_FILE_CLUSTER (idx));
  }
}

/**
 * Read from virtual file (or empty space)
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
static void vfat_file (uint64_t lba, unsigned int count, void *data)
{
  struct vfat_file *file;
  size_t offset;
  size_t len;
  size_t copy_len;
  size_t pad_len;
  size_t patch_len;

  /* Construct file portion */
  file = &vfat_files[VDISK_FILE_IDX (lba)];
  offset = VDISK_FILE_OFFSET (lba);
  len = (count * VDISK_SECTOR_SIZE);

  /* Copy any initialised-data portion */
  copy_len = ((offset < file->len) ? (file->len - offset) : 0);
  if (copy_len > len)
    copy_len = len;
  if (copy_len)
    file->read (file, data, offset, copy_len);

  /* Zero any uninitialised-data portion */
  pad_len = (len - copy_len);
  memset (((char *)data + copy_len), 0, pad_len);

  /* Patch any applicable portion */
  patch_len = ((offset < file->xlen) ? (file->xlen - offset) : 0);
  if (patch_len > len)
    patch_len = len;
  if (file->patch)
    file->patch (file, data, offset, patch_len);
}

/** A virtual disk region */
struct vfat_region
{
  /** Name */
  const char *name;
  /** Starting LBA */
  uint64_t lba;
  /** Number of blocks */
  unsigned int count;
  /**
   * Build data from this region
   *
   * @v start    Starting LBA
   * @v count    Number of blocks to read
   * @v data    Data buffer
   */
  void (* build) (uint64_t lba, unsigned int count, void *data);
};

/** Define a virtual disk region */
#define VDISK_REGION(_name, _build, _lba, _count) {    \
    .name = _name,          \
    .lba = _lba,          \
    .count = _count,        \
    .build = _build,        \
  }

/** Define a virtual disk directory region */
#define VDISK_DIRECTORY_REGION(_name, _build_subdirs, _lba) {  \
    .name = _name " subdirs",      \
    .lba = _lba,          \
    .count = 1,          \
    .build = _build_subdirs,      \
  }, {              \
    .name = _name " files",        \
    .lba = (_lba + 1),        \
    .count = (VDISK_CLUSTER_COUNT - 1),    \
    .build = vfat_dir_files,      \
  }

/** Virtual disk regions */
static struct vfat_region vfat_regions[] =
{
  VDISK_REGION ("MBR", vfat_mbr,
           VDISK_MBR_LBA, VDISK_MBR_COUNT),
  VDISK_REGION ("VBR", vfat_vbr,
           VDISK_VBR_LBA, VDISK_VBR_COUNT),
  VDISK_REGION ("FSInfo", vfat_fsinfo,
           VDISK_FSINFO_LBA, VDISK_FSINFO_COUNT),
  VDISK_REGION ("VBR Backup", vfat_vbr,
           VDISK_BACKUP_VBR_LBA, VDISK_BACKUP_VBR_COUNT),
  VDISK_REGION ("FAT", vfat_fat,
           VDISK_FAT_LBA, VDISK_FAT_COUNT),
  VDISK_DIRECTORY_REGION ("Root", vfat_root, VDISK_ROOT_LBA),
  VDISK_DIRECTORY_REGION ("Boot", vfat_boot, VDISK_BOOT_LBA),
  VDISK_DIRECTORY_REGION ("Sources", vfat_sources, VDISK_SOURCES_LBA),
  VDISK_DIRECTORY_REGION ("Fonts", vfat_fonts, VDISK_FONTS_LBA),
  VDISK_DIRECTORY_REGION ("Resources", vfat_resources,
         VDISK_RESOURCES_LBA),
  VDISK_DIRECTORY_REGION ("EFI", vfat_efi, VDISK_EFI_LBA),
  VDISK_DIRECTORY_REGION ("Microsoft", vfat_microsoft,
         VDISK_MICROSOFT_LBA),
};

/**
 * Read from virtual disk
 *
 * @v lba    Starting LBA
 * @v count    Number of blocks to read
 * @v data    Data buffer
 */
void vfat_read (uint64_t lba, unsigned int count, void *data)
{
  struct vfat_region *region;
  void (* build) (uint64_t lba, unsigned int count, void *data);
  //const char *name;
  uint64_t start = lba;
  uint64_t end = (lba + count);
  uint64_t frag_start = start;
  uint64_t frag_end;
  int file_idx;
  uint64_t file_end;
  uint64_t region_start;
  uint64_t region_end;
  unsigned int frag_count;
  unsigned int i;

  do
  {
    /* Initialise fragment to fill remaining space */
    frag_end = end;
    //name = NULL;
    build = NULL;

    /* Truncate fragment and generate data */
    file_idx = VDISK_FILE_IDX (frag_start);
    if (file_idx >= 0)
    {
      /* Truncate fragment to end of file */
      file_end = VDISK_FILE_LBA (file_idx + 1);
      if (frag_end > file_end)
        frag_end = file_end;

      /* Generate data from file */
      if (file_idx < VDISK_MAX_FILES)
      {
        //name = vfat_files[file_idx].name;
        build = vfat_file;
      }
    }
    else
    {
      /* Truncate fragment to region boundaries */
      for (i = 0; i < (sizeof (vfat_regions) /
              sizeof (vfat_regions[0])); i++)
      {
        region = &vfat_regions[i];
        region_start = region->lba;
        region_end = (region_start + region->count);
        /* Avoid crossing start of any region */
        if ((frag_start < region_start) &&
             (frag_end > region_start))
        {
          frag_end = region_start;
        }
        /* Ignore unless we overlap with this region */
        if ((frag_start >= region_end) ||
             (frag_end <= region_start))
        {
          continue;
        }
        /* Avoid crossing end of region */
        if (frag_end > region_end)
          frag_end = region_end;
        /* Found a suitable region */
        //name = region->name;
        build = region->build;
        break;
      }
    }
    /* Generate data from this region */
    frag_count = (frag_end - frag_start);
    if (build)
    {
      build (frag_start, frag_count, data);
    }
    else
    {
      memset (data, 0, (frag_count * VDISK_SECTOR_SIZE));
    }
    /* Move to next fragment */ 
    frag_start += frag_count;
    data = (char *)data + (frag_count * VDISK_SECTOR_SIZE);
  }
  while (frag_start != end);
}

/**
 * Add file to virtual disk
 *
 * @v name    Name
 * @v opaque    Opaque token
 * @v len    Length
 * @v read    Read data method
 * @ret file    Virtual file
 */
struct vfat_file *
vfat_add_file (const char *name, void *opaque, size_t len,
                void (* read) (struct vfat_file *file,
                               void *data, size_t offset, size_t len))
{
  static unsigned int index = 0;
  struct vfat_file *file;
  /* Sanity check */
  if (index >= VDISK_MAX_FILES)
    grub_pause_fatal ("Too many files\n");
  /* Store file */
  file = &vfat_files[index++];
  snprintf (file->name, sizeof (file->name), "%s", name);
  file->opaque = opaque;
  file->len = len;
  file->xlen = len;
  file->read = read;
  printf ("Using %s via %p len 0x%lx\n", file->name, file->opaque,
        file->len);
  return file;
}

/**
 * Patch virtual file
 *
 * @v file    Virtual file
 * @v patch    Patch method
 */
void
vfat_patch_file (struct vfat_file *file,
                  void (*patch) (struct vfat_file *file, void *data,
                                 size_t offset, size_t len))
{
  /* Record patch method */
  file->patch = patch;
  /* Allow patch method to update file length */
  patch (file, NULL, 0, 0);
}

#if __GNUC__ >= 9
#pragma GCC diagnostic pop
#endif
