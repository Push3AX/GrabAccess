/* qnx6.c - QNX 6 filesystem
 * Parts copied from the Linux kernel.
 */

/**
 * Overview of QNX 6 filesystem.
 *
 * Block numbers are 32-bits.
 * Blocks sizes can be 512, 1024, 2048, 4096 bytes.
 *
 * There are 2 superblocks. The first starts at 0x2000, the second at the last
 * 4K of the partition.
 * The superblock with the largest serial is the active one.
 *
 * Inodes are 128 bytes each and are stored as an array in an inode file.
 *
 * Directory entries are 32 bytes each and are store in an array in the
 * directory inode data. An entry with the inode=0 is unused.
 * Filenames longer than 27 bytes are stored in the "longfile" file.
 * There is one filename per longfile block.
 *
 * Files use indirect block pointers. All pointers are at the same level.
 * There are 16 block pointers in inodes and in the superblock files.
 *
 * Inode numbers start at 1 at index 0.
 * Inode 1 is the root "/".
 *
 * Assumptions:
 *  - the QNX6 file system fills the whole partition (as required)
 *  - the QNX6 file system is valid
 */

/* Filetype information as used in inodes.  */
#define FILETYPE_INO_MASK        0170000
#define FILETYPE_INO_REG         0100000
#define FILETYPE_INO_DIRECTORY   0040000
#define FILETYPE_INO_SYMLINK     0120000

#include <grub/err.h>
#include <grub/file.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/disk.h>
#include <grub/dl.h>
#include <grub/types.h>
#include <grub/fshelp.h>
#include <stdbool.h>
GRUB_MOD_LICENSE ("GPLv3+");


#define QNX6_SUPER_MAGIC         0x68191122  /* qnx6 fs detection */

#define QNX6_SUPERBLOCK_SIZE     0x200       /* superblock always is 512 bytes */
#define QNX6_SUPERBLOCK_AREA     0x1000      /* area reserved for superblock */
#define QNX6_SUPERBLOCK_SECTORS  (QNX6_SUPERBLOCK_AREA >> GRUB_DISK_SECTOR_BITS) /* 8 sectors */
#define QNX6_BOOTBLOCK_SIZE      0x2000      /* heading bootblock area */
#define QNX6_DIR_ENTRY_SIZE      0x20        /* dir entry size of 32 bytes */
#define QNX6_BLOCK0_SEC          (0x3000 >> GRUB_DISK_SECTOR_BITS)

#define QNX6_INODE_SIZE          0x80        /* each inode is 128 bytes */
#define QNX6_INODE_SIZE_BITS     7           /* inode entry size shift */

#define QNX6_LF_SIZE             0x200       /* each long file entry is 512 bytes */
#define QNX6_LF_SIZE_BITS        9           /* long file entry size shift */

#define QNX6_NUM_BLKPTR          16          /* 16 blockptrs in sbl/inode */
#define QNX6_PTR_MAX_LEVELS      5           /* maximum indirect levels */

/* for filenames */
#define QNX6_SHORT_NAME_MAX      27
#define QNX6_LONG_NAME_MAX       510

/* aliases to indicate that the byte-order depends on the filesystem */
typedef grub_uint8_t  fs8_t; // not multi-byte, but why not?
typedef grub_uint16_t fs16_t;
typedef grub_uint32_t fs32_t;
typedef grub_uint64_t fs64_t;

/*
 * This is the qnx6 inode layout on disk.
 * Each inode is 128 byte long.
 */
struct qnx6_inode_entry {
   fs64_t         di_size;
   fs32_t         di_uid;
   fs32_t         di_gid;
   fs32_t         di_ftime;
   fs32_t         di_mtime;
   fs32_t         di_atime;
   fs32_t         di_ctime;
   fs16_t         di_mode;
   fs16_t         di_ext_mode;
   fs32_t         di_block_ptr[QNX6_NUM_BLKPTR];
   fs8_t          di_filelevels;
   fs8_t          di_status;
   fs8_t          di_unknown2[2];
   fs32_t         di_zero2[6];
};

/*
 * Each directory entry is maximum 32 bytes long.
 * If more characters or special characters required it is stored
 * in the longfilenames structure.
 */
typedef struct qnx6_dir_entry_short_t {
   fs32_t         de_inode;
   fs8_t          de_size;
   char           de_fname[QNX6_SHORT_NAME_MAX];
} qnx6_dir_entry_short_t;

/*
 * Longfilename direntries have a different structure
 */
typedef struct qnx6_dir_entry_long_t {
   fs32_t         de_inode;
   fs8_t          de_size;
   fs8_t          de_unknown[3];
   fs32_t         de_long_inode;
   fs32_t         de_checksum;
} qnx6_dir_entry_long_t;

typedef union qnx6_dir_entry_t
{
   qnx6_dir_entry_short_t s;
   qnx6_dir_entry_long_t  l;
} qnx6_dir_entry_t;

/*
 * Maps to a block in the Longfile file. Index is de_long_inode.
 */
typedef struct qnx6_long_filename_t {
   fs16_t         lf_size;
   fs8_t          lf_fname[QNX6_LONG_NAME_MAX];
} qnx6_long_filename_t;

/*
 * Sort of like an inode nested in the superblock.
 */
typedef struct qnx6_root_node_t {
   fs64_t         size;
   fs32_t         ptr[QNX6_NUM_BLKPTR];
   fs8_t          levels;
   fs8_t          mode;
   fs8_t          spare[6];
} qnx6_root_node_t;

struct qnx6_super_block {
   fs32_t            sb_magic;
   fs32_t            sb_checksum;
   fs64_t            sb_serial;
   fs32_t            sb_ctime;      /* time the fs was created */
   fs32_t            sb_atime;      /* last access time */
   fs32_t            sb_flags;
   fs16_t            sb_version1;   /* filesystem version information */
   fs16_t            sb_version2;   /* filesystem version information */
   fs8_t             sb_volumeid[16];
   fs32_t            sb_blocksize;
   fs32_t            sb_num_inodes;
   fs32_t            sb_free_inodes;
   fs32_t            sb_num_blocks;
   fs32_t            sb_free_blocks;
   fs32_t            sb_allocgroup;
   qnx6_root_node_t  Inode;
   qnx6_root_node_t  Bitmap;
   qnx6_root_node_t  Longfile;
   qnx6_root_node_t  Unknown;
};

/**
 * The superblock structure does not fill 512 bytes, but the checksum covers
 * the whole sector. Union with a byte array to ensure the full size.
 */
union qnx6_super_block_sec {
   struct qnx6_super_block s;
   grub_uint8_t            d[QNX6_SUPERBLOCK_SIZE];
};

/**
 * Caches the list of blocks that are part of a file.
 * We could be lazy read the indirect blocks as needed, but most files will be
 * small and doing it this way may be faster.
 */
typedef struct qnx6_blocklist_t
{
   grub_uint64_t size;  /* file size in bytes */
   grub_uint32_t nblk;  /* number of entries in blks[] */
   grub_uint32_t iblk;  /* used for building the block list iblk==nblk at end */
   grub_uint32_t blks[];/* array of FS block numbers */
} qnx6_blocklist_t;

/**
 * A local representation of an inode.
 * Holds the inode number, the raw on-disk inode data and the block cache.
 */
typedef struct grub_qnx6_inode_t
{
   grub_uint32_t     ino;
   struct qnx6_inode_entry  raw;
   qnx6_blocklist_t  *fd;
} grub_qnx6_inode_t;

//-----------------------------------------------------------------------------

// forward reference
typedef struct grub_qnx6_data_t grub_qnx6_data_t;

/**
 * Ties together an inode and the mount structure.
 * REVISIT: If grub_qnx6_inode_t included grub_qnx6_data_t*, this wouldn't be needed.
 */
struct grub_fshelp_node
{
   grub_qnx6_data_t  *data;
   grub_qnx6_inode_t inode;
};

/* Information about a "mounted" filesystem.  */
struct grub_qnx6_data_t
{
   grub_disk_t             disk;
   struct grub_fshelp_node diropen;       // one and only currently opened file

   bool                    is_be;
   int                     blk_sec_shft;  // shift to go from blocks to sectors (0, 1, 2, 3)
   int                     blocksize;     // 512, 1024, 2048, or 4096
   union qnx6_super_block_sec sb1;        // raw superblock #1 (not really needed!)
   union qnx6_super_block_sec sb2;        // raw superblock #2 (not really needed!)
   struct qnx6_super_block *sb;           // points to sb1 or sb2

   grub_qnx6_inode_t       *inode;        // root inode (&diropen.inode)
   struct grub_fshelp_node inodes;        // pseudo inode for inode array
   struct grub_fshelp_node longfile;      // pseudo inode for longfile array
};

static grub_dl_t my_mod;

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
#endif

//-----------------------------------------------------------------------------

/**
 * Lazy and slow ilog2() implementation.
 * Find the top set bit.
 * When using base-2 numbers, val==(1 << ilog2(val))
 */
static int qnx6_ilog2(int val)
{
   if (val > 0)
   {
      int bits = 0;

      while ((val >>= 1) != 0)
      {
         ++bits;
      }
      return bits;
   }
   return -1;
}

static inline
grub_uint64_t fs64_to_cpu(grub_qnx6_data_t *data, grub_uint64_t val)
{
   return data->is_be ? grub_be_to_cpu64(val) : grub_le_to_cpu64(val);
}

static inline
grub_uint32_t fs32_to_cpu(grub_qnx6_data_t *data, grub_uint32_t val)
{
   return data->is_be ? grub_be_to_cpu32(val) : grub_le_to_cpu32(val);
}

static inline
grub_uint16_t fs16_to_cpu(grub_qnx6_data_t *data, grub_uint16_t val)
{
   return data->is_be ? grub_be_to_cpu16(val) : grub_le_to_cpu16(val);
}

/**
 * Wraps grub_disk_read() to convert block number to sectors and add the offset.
 */
static grub_err_t qnx6_block_read(grub_qnx6_data_t *data,
                                  grub_uint32_t blk_num,
                                  grub_uint32_t blk_off,
                                  grub_size_t   size,
                                  void *buf)
{
   return grub_disk_read(data->disk,
                         (blk_num << data->blk_sec_shft) + QNX6_BLOCK0_SEC,
                         blk_off,
                         size,
                         buf);
}

/**
 * Parses the blockptr array and appends blocks to the blocklist.
 * Recursive up to 5 levels.
 */
static grub_err_t
qnx6_read_blocklist(grub_qnx6_data_t *data,
                    qnx6_blocklist_t *fd,
                    int level, const fs32_t *ptrs, int nptr)
{
   if (level == 0)
   {
      /* we only add blocks to the list at level 0 */
      while ((nptr-- > 0) && (fd->iblk < fd->nblk))
      {
         fd->blks[fd->iblk++] = fs32_to_cpu(data, *ptrs);
         ptrs++;
      }
      return 0;
   }

   /* handle indirect block */
   fs32_t *blk_ptrs = grub_malloc(data->blocksize);
   if (blk_ptrs)
   {
      int idx;
      for (idx = 0; idx < nptr; idx++)
      {
         grub_uint32_t blk = fs32_to_cpu(data, ptrs[idx]);

         if (blk != (fs32_t)~0)
         {
            /* read and recurse */
            if (qnx6_block_read(data, blk, 0, data->blocksize, blk_ptrs) ||
                qnx6_read_blocklist(data, fd, level - 1, blk_ptrs, data->blocksize / 4))
            {
               break;
            }
         }
      }
      grub_free(blk_ptrs);
   }
   return grub_errno;
}

/**
 * Reads the blocklist from an inode. Returns an allocated structure.
 */
static qnx6_blocklist_t *
qnx6_get_blocklist(grub_qnx6_data_t *data, struct qnx6_inode_entry *ino)
{
   grub_uint64_t size = fs64_to_cpu(data, ino->di_size);
   grub_uint32_t nblk = ((size + data->blocksize - 1)
                         >> (data->blk_sec_shft + GRUB_DISK_SECTOR_BITS));
   qnx6_blocklist_t *fd;
   grub_size_t fdsize = sizeof(*fd) + (nblk * sizeof(grub_uint32_t));

   fd = grub_zalloc(fdsize);
   if (fd)
   {
      grub_errno = GRUB_ERR_NONE;

      fd->size = size;
      fd->nblk = nblk;
      if (qnx6_read_blocklist(data, fd, ino->di_filelevels, ino->di_block_ptr, QNX6_NUM_BLKPTR))
      {
         grub_free(fd);
         fd = NULL;
      }
   }
   return fd;
}

/**
 * This translates a block from the file to the filesystem.
 * The return value is in filesystem blocks, which start at offset 0x3000.
 * The caller needs to convert to sectors and add the "block-0" offset.
 */
static grub_disk_addr_t
grub_qnx6_get_block(grub_fshelp_node_t node, grub_disk_addr_t iblock)
{
   qnx6_blocklist_t *fd;

   /* read the blocklist if we haven't already done so */
   if (!node->inode.fd)
   {
      node->inode.fd = qnx6_get_blocklist(node->data, &node->inode.raw);
   }

   fd = node->inode.fd;
   if (fd && (iblock < fd->nblk))
   {
      return fd->blks[iblock];
   }
   return 0;
}

/* Read LEN bytes from the file described by DATA starting with byte
   POS.  Return the amount of read bytes in READ.  */
static grub_ssize_t
grub_qnx6_read_file(grub_fshelp_node_t node,
                    grub_disk_read_hook_t read_hook, void *read_hook_data,
                    grub_off_t pos, grub_size_t len, char *buf)
{
   return grub_fshelp_read_file(node->data->disk, node,
                                read_hook, read_hook_data, 0,
                                pos, len, buf,
                                grub_qnx6_get_block,
                                grub_le_to_cpu32(node->inode.raw.di_size),
                                node->data->blk_sec_shft,
                                QNX6_BLOCK0_SEC);
}

/* Read the inode INO for the file described by DATA into INODE.  */
static grub_err_t
grub_qnx6_read_inode(grub_qnx6_data_t *data,
                     grub_uint32_t ino, grub_qnx6_inode_t *inode)
{
   inode->fd = NULL;

   inode->ino = ino;
   if (ino > 0)
   {
      /* inode file data is an array of inodes, starting with 1 at idx=0 */
      ino--;
      if (grub_qnx6_read_file(&data->inodes, NULL, NULL,
                              ino << QNX6_INODE_SIZE_BITS,
                              sizeof(inode->raw),
                              (char *)&inode->raw) == sizeof(inode->raw))
      {
         return 0;
      }
   }
   inode->ino = 0;
   grub_error(GRUB_ERR_BAD_FS, "qnx6 inode error");
   return grub_errno;
}

/* Read the longfile entry */
static grub_err_t
grub_qnx6_read_longfile(grub_qnx6_data_t *data,
                        qnx6_dir_entry_long_t *de,
                        qnx6_long_filename_t *lf)
{
   grub_uint32_t lf_ino = fs32_to_cpu(data, de->de_long_inode);

   /* lf_ino maps to the block number in the file */
   if (grub_qnx6_read_file(&data->longfile, NULL, NULL,
                           lf_ino << (9 + data->blk_sec_shft),
                           sizeof(*lf), (char *)lf) == sizeof(*lf))
   {
      grub_uint16_t lf_size = fs16_to_cpu(data, lf->lf_size);

      if (lf_size < sizeof(lf->lf_fname) - 1)
      {
         /* ensure termination */
         lf->lf_fname[lf_size] = 0;

         /* TODO: check de->de_checksum against qnx6_checksum(lf->lf_fname, lf_size)
          * Do we care? Files that grub would access would have short names, so
          * this code would likely never be used.
          */

         return 0;
      }
   }
   grub_error(GRUB_ERR_BAD_FS, "qnx6 longfile error");
   return grub_errno;
}

//---- This is standard CRC-32 BE code. Perhaps this should be in a utils file.
#define QNX6_CRCPOLY_BE 0x04c11db7

/**
 * crc32_be_generic() - Calculate bitwise big-endian Ethernet AUTODIN II CRC32
 * @crc: seed value for computation.  ~0 for Ethernet, sometimes 0 for
 * other uses, or the previous crc32 value if computing incrementally.
 * @p: pointer to buffer over which CRC32 is run
 * @len: length of buffer @p
 * @tab: big-endian Ethernet table
 * @polynomial: CRC32 BE polynomial
 */
static
grub_uint32_t qnx6_crc32_be_generic(grub_uint32_t crc, grub_uint8_t const *p,
                                    grub_size_t len, grub_uint32_t polynomial)
{
   int i;

   while (len--)
   {
      crc ^= *p++ << 24;
      for (i = 0; i < 8; i++)
      {
         crc = (crc << 1) ^ ((crc & 0x80000000) ? polynomial : 0);
      }
   }
   return crc;
}

static
grub_uint32_t qnx6_crc32_be(grub_uint32_t crc, grub_uint8_t const *p, grub_size_t len)
{
   return qnx6_crc32_be_generic(crc, p, len, QNX6_CRCPOLY_BE);
}
//---- end of CRC-32 BE code

/**
 * Do some limited checks to see if a superblock looks valid.
 * We really only need to check the MAGIC and CHECKSUM, but a little extra
 * doesn't hurt.
 */
static bool qnx6_superblock_valid(grub_qnx6_data_t *data,
                                  struct qnx6_super_block *sb,
                                  bool adjust_be)
{
   if (adjust_be)
   {
      /* Check the SB Magic and determine the byte order */
      data->is_be = false;
      if (fs32_to_cpu(data, sb->sb_magic) != QNX6_SUPER_MAGIC)
      {
         data->is_be = true;
      }
   }

   if (fs32_to_cpu(data, sb->sb_magic) != QNX6_SUPER_MAGIC)
   {
      grub_error(GRUB_ERR_BAD_FS, "qnx6 magic error");
      return false;
   }

   /* checksum check - start at byte 8 and end at byte 512 */
   grub_uint32_t my_crc = qnx6_crc32_be(0, ((grub_uint8_t *)sb) + 8, 504);
   if (fs32_to_cpu(data, sb->sb_checksum) != my_crc)
   {
      grub_error(GRUB_ERR_BAD_FS, "qnx6 checksum error");
      return false;
   }

   if (sb->Inode.levels > QNX6_PTR_MAX_LEVELS)
   {
      grub_error(GRUB_ERR_BAD_FS, "qnx6 inode error");
      return false;
   }

   if (sb->Longfile.levels > QNX6_PTR_MAX_LEVELS)
   {
      grub_error(GRUB_ERR_BAD_FS, "qnx6 longfile error");
      return false;
   }

   return true;
}

/**
 * Read a superblock 'file' into a regular inode structure.
 * This also pre-populates the blocklist. (REVISIT: Is that needed?)
 */
static bool
qnx6_get_root_blocklist(grub_qnx6_data_t *data,
                        qnx6_root_node_t *rn,
                        grub_qnx6_inode_t *ino, int fake_ino)
{
   /* fake an inode, copying the size, levels, and block ptrs */
   ino->raw.di_size       = rn->size;
   ino->raw.di_filelevels = rn->levels;
   grub_memcpy(ino->raw.di_block_ptr, rn->ptr, sizeof(ino->raw.di_block_ptr));

   /* then read the block list */
   ino->ino = fake_ino;
   ino->fd = qnx6_get_blocklist(data, &ino->raw);
   /* TODO: log something? */
   return (ino->fd) != NULL || !rn->size;
}

/**
 * Read and parse both superblocks. Use the valid one with the bigger serial.
 */
static grub_err_t
qnx6_parse_superblocks(grub_qnx6_data_t *data)
{
   grub_uint64_t sb1_sec = QNX6_BOOTBLOCK_SIZE >> GRUB_DISK_SECTOR_BITS;
   grub_uint64_t sb2_sec = (data->disk->total_sectors & ~(QNX6_SUPERBLOCK_SECTORS - 1)) - QNX6_SUPERBLOCK_SECTORS;
   bool          sb1_ok, sb2_ok;

   grub_errno = GRUB_ERR_NONE;

   /* read both superblocks by sector */
   if (grub_disk_read(data->disk, sb1_sec, 0, sizeof(data->sb1), &data->sb1) ||
       grub_disk_read(data->disk, sb2_sec, 0, sizeof(data->sb2), &data->sb2))
   {
      return grub_errno;
   }

   sb1_ok = qnx6_superblock_valid(data, &data->sb1.s, true);
   sb2_ok = qnx6_superblock_valid(data, &data->sb2.s, true);

   /* pick the valid superblock with the bigger sb_serial */
   if (sb1_ok && sb2_ok)
   {
      if (fs64_to_cpu(data, data->sb1.s.sb_serial) > fs64_to_cpu(data, data->sb2.s.sb_serial))
      {
         data->sb = &data->sb1.s;
      }
      else
      {
         data->sb = &data->sb2.s;
      }
   }
   else if (sb1_ok)
   {
      data->sb = &data->sb1.s;
   }
   else if (sb2_ok)
   {
      data->sb = &data->sb2.s;
   }
   else
   {
      /* neither superblock was OK - pass up the error */
      return grub_errno;
   }

   /* validate and save the logical blocksize */
   data->blocksize = fs32_to_cpu(data, data->sb->sb_blocksize);
   data->blk_sec_shft = qnx6_ilog2(data->blocksize) - 9;
   if ((data->blk_sec_shft < 0) || (data->blk_sec_shft > 3))
   {
      grub_error(GRUB_ERR_BAD_FS, "qxn6 blocksize error");
      return grub_errno;
   }

   /* grab the inode and logfile "files" */
   data->inodes.data   = data;
   data->longfile.data = data;
   if (!qnx6_get_root_blocklist(data, &data->sb->Inode, &data->inodes.inode, -1) ||
       !qnx6_get_root_blocklist(data, &data->sb->Longfile, &data->longfile.inode, -2))
   {
      return grub_errno;
   }

   /* read the root inode */
   if (grub_qnx6_read_inode(data, 1, data->inode))
   {
      return grub_errno;
   }

   return 0;
}

/**
 * Allocate and set up the 'mount' structure for this disk.
 */
static grub_qnx6_data_t *
grub_qnx6_mount(grub_disk_t disk)
{
   grub_qnx6_data_t *data;

   data = grub_zalloc(sizeof(*data));
   if (!data)
      return NULL;

   /* set up data */
   data->disk         = disk;
   data->diropen.data = data;
   data->inode        = &data->diropen.inode;

   /* validate superblock */
   if (qnx6_parse_superblocks(data) == GRUB_ERR_NONE)
   {
      return data;
   }

   if (grub_errno == GRUB_ERR_OUT_OF_RANGE)
      grub_error(GRUB_ERR_BAD_FS, "not an qnx6 filesystem");
   grub_free(data);
   return NULL;
}

/**
 * Read a symlink.
 * Symlinks are stored as file data, so read the whole file.
 * The value is allocated with grub_malloc(), so the caller call grub_free().
 */
static char *
grub_qnx6_read_symlink(grub_fshelp_node_t node)
{
   char *symlink;

   grub_uint32_t i_size = fs64_to_cpu(node->data, node->inode.raw.di_size);

   symlink = grub_malloc(i_size + 1);
   if (!symlink)
      return 0;

   grub_qnx6_read_file(node, 0, 0, 0, i_size, symlink);
   if (grub_errno)
   {
      grub_free(symlink);
      return 0;
   }
   symlink[i_size] = '\0';
   return symlink;
}

/**
 * Iterates over all directory entries.
 * Directory entries are stored in the file data.
 * I chose simplicity over efficiency. We could have read the directory entry
 * in page-sized batches instead of one-at-a-time.
 */
static int
grub_qnx6_iterate_dir(grub_fshelp_node_t dir,
                      grub_fshelp_iterate_dir_hook_t hook, void *hook_data)
{
   grub_qnx6_data_t     *data = dir->data;
   qnx6_long_filename_t fname;
   grub_off_t           i_size;
   grub_off_t           fpos = 0;

   i_size = fs64_to_cpu(dir->data, dir->inode.raw.di_size);
   while (fpos < i_size)
   {
      qnx6_dir_entry_t de;

      /* read the directory entry */
      grub_qnx6_read_file(dir, NULL, NULL, fpos, sizeof(de), (char *)&de);
      if (grub_errno)
      {
         return 0;
      }
      fpos += sizeof(de);

      /* empty entries have inode==0 */
      if (de.s.de_inode == 0)
      {
         continue;
      }

      /* see if the filename is local/short */
      if (de.s.de_size < sizeof(de.s.de_fname))
      {
         grub_memcpy(fname.lf_fname, de.s.de_fname, de.s.de_size);
         fname.lf_size = fs16_to_cpu(data, de.s.de_size);
         fname.lf_fname[fname.lf_size] = 0;
      }
      else if (de.s.de_size == 0xff)
      {
         if (grub_qnx6_read_longfile(data, &de.l, &fname))
         {
            /* REVISIT: this shouldn't happen */
            continue;
         }
      }
      else
      {
         /* REVISIT: invalid fname size */
         continue;
      }

      struct grub_fshelp_node *fdiro = grub_malloc(sizeof(*fdiro));
      if (!fdiro)
         return 0;

      /* read the inode */
      fdiro->data = dir->data;
      if (grub_qnx6_read_inode(data, fs32_to_cpu(data, de.s.de_inode), &fdiro->inode))
      {
         grub_free(fdiro);
         return 0;
      }

      grub_uint16_t i_mode = fs16_to_cpu(data, fdiro->inode.raw.di_mode);
      enum grub_fshelp_filetype type = GRUB_FSHELP_UNKNOWN;
      if ((i_mode & FILETYPE_INO_MASK) == FILETYPE_INO_DIRECTORY)
         type = GRUB_FSHELP_DIR;
      else if ((i_mode & FILETYPE_INO_MASK) == FILETYPE_INO_SYMLINK)
         type = GRUB_FSHELP_SYMLINK;
      else if ((i_mode & FILETYPE_INO_MASK) == FILETYPE_INO_REG)
         type = GRUB_FSHELP_REG;

      if (hook((const char *)fname.lf_fname, type, fdiro, hook_data))
         return 1;
   }
   return 0;
}

/* Open a file named NAME and initialize FILE. */
static grub_err_t
grub_qnx6_open(struct grub_file *file, const char *name)
{
   grub_qnx6_data_t *data;
   struct grub_fshelp_node *fdiro = 0;
   grub_err_t err;

   grub_dl_ref(my_mod);

   data = grub_qnx6_mount(file->device->disk);
   if (!data)
   {
      err = grub_errno;
      goto fail;
   }

   err = grub_fshelp_find_file(name, &data->diropen, &fdiro,
                               grub_qnx6_iterate_dir,
                               grub_qnx6_read_symlink,
                               GRUB_FSHELP_REG);
   if (err)
      goto fail;

   grub_memcpy(data->inode, &fdiro->inode, sizeof(*data->inode));
   grub_free(fdiro);

   file->size = grub_le_to_cpu32(data->inode->raw.di_size);
   file->data = data;
   file->offset = 0;

   return 0;

fail:
   if (fdiro != &data->diropen)
      grub_free(fdiro);
   grub_free(data);
   grub_dl_unref(my_mod);
   return err;
}

/**
 * Free resources allocated in grub_qnx6_open().
 */
static grub_err_t
grub_qnx6_close(grub_file_t file)
{
   grub_qnx6_data_t *data = file->data;

   if (data)
   {
      if (data->inode && data->inode->fd)
      {
         grub_free(data->inode->fd);
         data->inode->fd = NULL; // the same inode is reused on each open()
      }
      grub_free(data);
   }
   grub_dl_unref(my_mod);
   return GRUB_ERR_NONE;
}

/* Read LEN bytes data from FILE into BUF.  */
static grub_ssize_t
grub_qnx6_read(grub_file_t file, char *buf, grub_size_t len)
{
   grub_qnx6_data_t *data = (grub_qnx6_data_t *)file->data;

   return grub_qnx6_read_file(&data->diropen,
                              file->read_hook, file->read_hook_data,
                              file->offset, len, buf);
}

/* Context for grub_qnx6_dir.  */
struct grub_qnx6_dir_ctx
{
   grub_fs_dir_hook_t   hook;
   void                 *hook_data;
   grub_qnx6_data_t     *data;
};

/* Helper for grub_qnx6_dir.  */
static int
grub_qnx6_dir_iter(const char *filename,
                   enum grub_fshelp_filetype filetype,
                   grub_fshelp_node_t node,
                   void *data)
{
   struct grub_qnx6_dir_ctx *ctx = data;
   struct grub_dirhook_info info;

   grub_memset(&info, 0, sizeof (info));
   info.mtimeset = 1;
   info.mtime    = fs32_to_cpu(data, node->inode.raw.di_mtime);

   info.dir = ((filetype & GRUB_FSHELP_TYPE_MASK) == GRUB_FSHELP_DIR);
   return ctx->hook(filename, &info, ctx->hook_data);
}

static grub_err_t
grub_qnx6_dir(grub_device_t device, const char *path, grub_fs_dir_hook_t hook,
              void *hook_data)
{
   struct grub_qnx6_dir_ctx ctx = {
      .hook = hook,
      .hook_data = hook_data
   };
   struct grub_fshelp_node *fdiro = 0;

   grub_dl_ref(my_mod);

   ctx.data = grub_qnx6_mount(device->disk);
   if (!ctx.data)
      goto fail;

   grub_fshelp_find_file(path, &ctx.data->diropen, &fdiro,
                         grub_qnx6_iterate_dir, grub_qnx6_read_symlink,
                         GRUB_FSHELP_DIR);
   if (grub_errno)
   {
      goto fail;
   }

   grub_qnx6_iterate_dir(fdiro, grub_qnx6_dir_iter, &ctx);

fail:
   if (fdiro != &ctx.data->diropen)
      grub_free(fdiro);
   grub_free(ctx.data);
   grub_dl_unref(my_mod);
   return grub_errno;
}

static struct grub_fs grub_qnx6_fs =
{
   .name = "qnx6",
   .fs_dir = grub_qnx6_dir,
   .fs_open = grub_qnx6_open,
   .fs_read = grub_qnx6_read,
   .fs_close = grub_qnx6_close,
#ifdef GRUB_UTIL
   .reserved_first_sector = 1,
   .blocklist_install = 1,
#endif
};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

GRUB_MOD_INIT(qnx6)
{
   grub_fs_register(&grub_qnx6_fs);
   my_mod = mod;
}

GRUB_MOD_FINI(qnx6)
{
   grub_fs_unregister(&grub_qnx6_fs);
}
