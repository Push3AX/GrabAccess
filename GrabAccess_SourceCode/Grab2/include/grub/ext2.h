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

#ifndef GRUB_EXT2_H
#define GRUB_EXT2_H	1

#include <grub/types.h>

/* Magic value used to identify an ext2 filesystem.  */
#define	EXT2_MAGIC		0xEF53
/* Amount of indirect blocks in an inode.  */
#define INDIRECT_BLOCKS		12

/* The good old revision and the default inode size.  */
#define EXT2_GOOD_OLD_REVISION		0
#define EXT2_GOOD_OLD_INODE_SIZE	128

/* Filetype used in directory entry.  */
#define	FILETYPE_UNKNOWN	0
#define	FILETYPE_REG		1
#define	FILETYPE_DIRECTORY	2
#define	FILETYPE_SYMLINK	7

/* Filetype information as used in inodes.  */
#define FILETYPE_INO_MASK	0170000
#define FILETYPE_INO_REG	0100000
#define FILETYPE_INO_DIRECTORY	0040000
#define FILETYPE_INO_SYMLINK	0120000

/* Superblock filesystem feature flags (RW compatible)
 * A filesystem with any of these enabled can be read and written by a driver
 * that does not understand them without causing metadata/data corruption.  */
#define EXT2_FEATURE_COMPAT_DIR_PREALLOC	0x0001
#define EXT2_FEATURE_COMPAT_IMAGIC_INODES	0x0002
#define EXT3_FEATURE_COMPAT_HAS_JOURNAL		0x0004
#define EXT2_FEATURE_COMPAT_EXT_ATTR		0x0008
#define EXT2_FEATURE_COMPAT_RESIZE_INODE	0x0010
#define EXT2_FEATURE_COMPAT_DIR_INDEX		0x0020
/* Superblock filesystem feature flags (RO compatible)
 * A filesystem with any of these enabled can be safely read by a driver that
 * does not understand them, but should not be written to, usually because
 * additional metadata is required.  */
#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER	0x0001
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE	0x0002
#define EXT2_FEATURE_RO_COMPAT_BTREE_DIR	0x0004
#define EXT4_FEATURE_RO_COMPAT_GDT_CSUM		0x0010
#define EXT4_FEATURE_RO_COMPAT_DIR_NLINK	0x0020
#define EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE	0x0040
/* Superblock filesystem feature flags (back-incompatible)
 * A filesystem with any of these enabled should not be attempted to be read
 * by a driver that does not understand them, since they usually indicate
 * metadata format changes that might confuse the reader.  */
#define EXT2_FEATURE_INCOMPAT_COMPRESSION	0x0001
#define EXT2_FEATURE_INCOMPAT_FILETYPE		0x0002
#define EXT3_FEATURE_INCOMPAT_RECOVER		0x0004 /* Needs recovery */
#define EXT3_FEATURE_INCOMPAT_JOURNAL_DEV	0x0008 /* Volume is journal device */
#define EXT2_FEATURE_INCOMPAT_META_BG		0x0010
#define EXT4_FEATURE_INCOMPAT_EXTENTS		0x0040 /* Extents used */
#define EXT4_FEATURE_INCOMPAT_64BIT		0x0080
#define EXT4_FEATURE_INCOMPAT_MMP		0x0100
#define EXT4_FEATURE_INCOMPAT_FLEX_BG		0x0200
#define EXT4_FEATURE_INCOMPAT_CSUM_SEED		0x2000
#define EXT4_FEATURE_INCOMPAT_ENCRYPT          0x10000

/* The set of back-incompatible features this driver DOES support. Add (OR)
 * flags here as the related features are implemented into the driver.  */
#define EXT2_DRIVER_SUPPORTED_INCOMPAT ( EXT2_FEATURE_INCOMPAT_FILETYPE \
                                       | EXT4_FEATURE_INCOMPAT_EXTENTS  \
                                       | EXT4_FEATURE_INCOMPAT_FLEX_BG \
                                       | EXT2_FEATURE_INCOMPAT_META_BG \
                                       | EXT4_FEATURE_INCOMPAT_64BIT \
                                       | EXT4_FEATURE_INCOMPAT_ENCRYPT)
/* List of rationales for the ignored "incompatible" features:
 * needs_recovery: Not really back-incompatible - was added as such to forbid
 *                 ext2 drivers from mounting an ext3 volume with a dirty
 *                 journal because they will ignore the journal, but the next
 *                 ext3 driver to mount the volume will find the journal and
 *                 replay it, potentially corrupting the metadata written by
 *                 the ext2 drivers. Safe to ignore for this RO driver.
 * mmp:            Not really back-incompatible - was added as such to
 *                 avoid multiple read-write mounts. Safe to ignore for this
 *                 RO driver.
 * checksum seed:  Not really back-incompatible - was added to allow tools
 *                 such as tune2fs to change the UUID on a mounted metadata
 *                 checksummed filesystem. Safe to ignore for now since the
 *                 driver doesn't support checksum verification. However, it
 *                 has to be removed from this list if the support is added later.
 */
#define EXT2_DRIVER_IGNORED_INCOMPAT ( EXT3_FEATURE_INCOMPAT_RECOVER \
				     | EXT4_FEATURE_INCOMPAT_MMP \
				     | EXT4_FEATURE_INCOMPAT_CSUM_SEED)

#define EXT3_JOURNAL_MAGIC_NUMBER	0xc03b3998U

#define EXT3_JOURNAL_DESCRIPTOR_BLOCK	1
#define EXT3_JOURNAL_COMMIT_BLOCK	2
#define EXT3_JOURNAL_SUPERBLOCK_V1	3
#define EXT3_JOURNAL_SUPERBLOCK_V2	4
#define EXT3_JOURNAL_REVOKE_BLOCK	5

#define EXT3_JOURNAL_FLAG_ESCAPE	1
#define EXT3_JOURNAL_FLAG_SAME_UUID	2
#define EXT3_JOURNAL_FLAG_DELETED	4
#define EXT3_JOURNAL_FLAG_LAST_TAG	8

#define EXT4_ENCRYPT_FLAG              0x800
#define EXT4_EXTENTS_FLAG		0x80000

/* The ext2 superblock.  */
struct grub_ext2_sblock
{
  grub_uint32_t total_inodes;
  grub_uint32_t total_blocks;
  grub_uint32_t reserved_blocks;
  grub_uint32_t free_blocks;
  grub_uint32_t free_inodes;
  grub_uint32_t first_data_block;
  grub_uint32_t log2_block_size;
  grub_uint32_t log2_fragment_size;
  grub_uint32_t blocks_per_group;
  grub_uint32_t fragments_per_group;
  grub_uint32_t inodes_per_group;
  grub_uint32_t mtime;
  grub_uint32_t utime;
  grub_uint16_t mnt_count;
  grub_uint16_t max_mnt_count;
  grub_uint16_t magic;
  grub_uint16_t fs_state;
  grub_uint16_t error_handling;
  grub_uint16_t minor_revision_level;
  grub_uint32_t lastcheck;
  grub_uint32_t checkinterval;
  grub_uint32_t creator_os;
  grub_uint32_t revision_level;
  grub_uint16_t uid_reserved;
  grub_uint16_t gid_reserved;
  grub_uint32_t first_inode;
  grub_uint16_t inode_size;
  grub_uint16_t block_group_number;
  grub_uint32_t feature_compatibility;
  grub_uint32_t feature_incompat;
  grub_uint32_t feature_ro_compat;
  grub_uint16_t uuid[8];
  char volume_name[16];
  char last_mounted_on[64];
  grub_uint32_t compression_info;
  grub_uint8_t prealloc_blocks;
  grub_uint8_t prealloc_dir_blocks;
  grub_uint16_t reserved_gdt_blocks;
  grub_uint8_t journal_uuid[16];
  grub_uint32_t journal_inum;
  grub_uint32_t journal_dev;
  grub_uint32_t last_orphan;
  grub_uint32_t hash_seed[4];
  grub_uint8_t def_hash_version;
  grub_uint8_t jnl_backup_type;
  grub_uint16_t group_desc_size;
  grub_uint32_t default_mount_opts;
  grub_uint32_t first_meta_bg;
  grub_uint32_t mkfs_time;
  grub_uint32_t jnl_blocks[17];
};

/* The ext2 blockgroup.  */
struct grub_ext2_block_group
{
  grub_uint32_t block_id;
  grub_uint32_t inode_id;
  grub_uint32_t inode_table_id;
  grub_uint16_t free_blocks;
  grub_uint16_t free_inodes;
  grub_uint16_t used_dirs;
  grub_uint16_t pad;
  grub_uint32_t reserved[3];
  grub_uint32_t block_id_hi;
  grub_uint32_t inode_id_hi;
  grub_uint32_t inode_table_id_hi;
  grub_uint16_t free_blocks_hi;
  grub_uint16_t free_inodes_hi;
  grub_uint16_t used_dirs_hi;
  grub_uint16_t pad2;
  grub_uint32_t reserved2[3];
};

/* The ext2 inode.  */
struct grub_ext2_inode
{
  grub_uint16_t mode;
  grub_uint16_t uid;
  grub_uint32_t size;
  grub_uint32_t atime;
  grub_uint32_t ctime;
  grub_uint32_t mtime;
  grub_uint32_t dtime;
  grub_uint16_t gid;
  grub_uint16_t nlinks;
  grub_uint32_t blockcnt;  /* Blocks of 512 bytes!! */
  grub_uint32_t flags;
  grub_uint32_t osd1;
  union
  {
    struct datablocks
    {
      grub_uint32_t dir_blocks[INDIRECT_BLOCKS];
      grub_uint32_t indir_block;
      grub_uint32_t double_indir_block;
      grub_uint32_t triple_indir_block;
    } blocks;
    char symlink[60];
  };
  grub_uint32_t version;
  grub_uint32_t acl;
  grub_uint32_t size_high;
  grub_uint32_t fragment_addr;
  grub_uint32_t osd2[3];
};

/* The header of an ext2 directory entry.  */
struct ext2_dirent
{
  grub_uint32_t inode;
  grub_uint16_t direntlen;
  grub_uint8_t namelen;
  grub_uint8_t filetype;
};

struct grub_ext3_journal_header
{
  grub_uint32_t magic;
  grub_uint32_t block_type;
  grub_uint32_t sequence;
};

struct grub_ext3_journal_revoke_header
{
  struct grub_ext3_journal_header header;
  grub_uint32_t count;
  grub_uint32_t data[0];
};

struct grub_ext3_journal_block_tag
{
  grub_uint32_t block;
  grub_uint32_t flags;
};

struct grub_ext3_journal_sblock
{
  struct grub_ext3_journal_header header;
  grub_uint32_t block_size;
  grub_uint32_t maxlen;
  grub_uint32_t first;
  grub_uint32_t sequence;
  grub_uint32_t start;
};

#define EXT4_EXT_MAGIC		0xf30a

struct grub_ext4_extent_header
{
  grub_uint16_t magic;
  grub_uint16_t entries;
  grub_uint16_t max;
  grub_uint16_t depth;
  grub_uint32_t generation;
};

struct grub_ext4_extent
{
  grub_uint32_t block;
  grub_uint16_t len;
  grub_uint16_t start_hi;
  grub_uint32_t start;
};

struct grub_ext4_extent_idx
{
  grub_uint32_t block;
  grub_uint32_t leaf;
  grub_uint16_t leaf_hi;
  grub_uint16_t unused;
};

#endif
