/* lynxfs.c - Lynx filesystem as used on LOS178 */

/**
 * Overview of LynxFS as used with LynxOS-178.
 *
 * LynxFS is a very simple non-power safe file system.
 * It is similar to ext2 in may important aspects.
 *
 * Inodes:
 * Inodes are 128 bytes each and start in block 2.
 * The first inode is 1, which is the filesystem root directory.
 * The block number and block offset are determined as follows:
 *   rel_off = (inode - 1) * 128
 *   blk_num = 2 + (reloff >> sb->block_bits)
 *   blk_off = reloff & (sb->block_size - 1)
 * Where block bits is the number of bits in the block size.
 * For example, if block_size is 2048, then block_bits is 11.
 * Note that LOS178 can only handle block sizes of 1024 and 2048 bytes.
 * You can create a filesystem with 4096+ byte blocks, but the kernel dies.
 *
 * Blocks:
 * The whole filesystem is addressed in terms of blocks.
 * Block 0 isn't used directly, but contains a shadow copy of the SuperBlock at
 *   offset 512. Only the magic, bsize_magic, and bsize_code should be used
 *   from this pseudo SuperBlock. The other data isn't likely to be accurate.
 * Block 1 contains the real SuperBlock.
 * Block 2 is the start of the inodes.
 * I don't recall what happens if block_size = 512. Don't do that.
 *
 * Inode Block list:
 * The inode block list is very similar to the 32-bit ext2 structure.
 * The blocks are packed as 24 bits in inode->i_blocks.
 * The first 10 entries are directly mapped blocks.
 * Entry 11 is 1-level indirect.
 * Entry 12 is 2-level indirect.
 * Entry 13 is 3-level indirect.
 * Block numbers in the indirect block are 32-bits, although only the lower 24
 * bits may be used.
 *
 * Directory entries:
 * Directory entries are just normal file data and are padded to 4 bytes.
 * The d_reclen field indicates the size of the record.  As a side note, the
 * last record in the block uses the remaining space in the block.
 *
 * Symlink entries:
 * The symlink target is stored in normal file data.
 *
 * Limitations:
 * The use of 24-bit block addresses limits the size of the filesystem to
 * ~(2^24 * block_size) bytes, or ~34 GB for 2048-byte blocks.
 */

/* Index of first indirect block in inode->i_blocks */
#define INDIRECT_BLOCKS          10

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

GRUB_MOD_LICENSE ("GPLv3+");

/* Log2 size of ext2 block in 512 blocks.  */
#define LOG2_512_BLOCK_SIZE(data) \
   (data->block_bits - GRUB_DISK_SECTOR_BITS)

/**
 * This is the on-disk super-block structure for LynxFS.
 * The superblock start at offset 512 in the partition.
 *
 * All fields are stored big-endian, so use grub_be_to_cpu32() to access.
 */
struct grub_lynxfs_sblock
{
   grub_uint32_t free_inodes;                   /* free inode list */
   grub_uint32_t free_blocks;                   /* free block list */
   grub_uint32_t num_inodes;                    /* number of inodes on this device */
   grub_uint32_t num_blocks;                    /* number of blocks on this device */
   grub_uint32_t root_inode;                    /* root of disk file system (?) */
   grub_uint32_t spare1;
   grub_uint32_t free_icount;                   /* number of free inodes */
   grub_uint32_t free_bcount;                   /* number of free blocks */
   grub_uint32_t spare2;
   grub_uint32_t status;                        /* dirty or ok */
   grub_uint32_t magic;                         /* magic number */
   grub_uint32_t time;                          /* creation time */
   grub_uint32_t bsize_magic;                   /* variable block size supported? */
   grub_uint32_t bsize_code;                    /* file system block size */
   grub_uint32_t bitmap_magic;                  /* bit map free list supported? */
   grub_uint32_t bitmap_blocks;                 /* block number of first bitmap block */
   grub_uint32_t bitmap_chunksize;              /* 1st lvl bit is how many 32bit masks*/
   grub_uint32_t spare[5];
   grub_uint32_t bitmap_map[0];                 /* 2 level block bitmap */
};

#define LYNXFS_SB_MAGIC        0x11112222UL     /* grub_lynxfs_sblock.magic */
#define LYNXFS_BSIZE_MAGIC     0xcafefecaUL     /* grub_lynxfs_sblock.bsize_magic */
#define LYNXFS_BITMAP_MAGIC    0x1fedfaceUL     /* grub_lynxfs_sblock.bitmap_magic */

/**
 * This is the on-disk inode structure for LynxFS.
 * Inodes start in block 2. Each inode is 128 bytes.
 * All fields are stored big-endian, including the 24-bit iblock values.
 * NOTES:
 *  o If i_uid == 0xffff then the inode is free.
 *  o On the free inode list, the first 4 bytes are the next free inode
 */
struct grub_lynxfs_inode
{
   grub_int16_t  i_mode;                 /* node type and mode */
   grub_int16_t  i_nlink;                /* number of hard links to this file */
   grub_uint32_t i_genid;                /* NFS generation count */
   grub_uint16_t i_uid;                  /* user id */
   grub_uint16_t i_guid;                 /* group id */
   grub_uint32_t i_size;                 /* file size (in bytes) */
   grub_uint32_t i_atime;                /* time when file data was last accessed */
   grub_uint32_t i_mtime;                /* time when file data was last modified */
   grub_uint32_t i_ctime;                /* time when file status was last changed */
   grub_uint32_t i_numblocks;            /* actual number of blocks used in file (up to 32) */
   grub_uint8_t  i_blocks[3 * 13];       /* direct and indirect blocks (24 bits) */
};

/**
 * On-disk directory entry structure.
 * It has a 4-byte alignment
 */
struct lynxfs_dirent
{
   grub_int32_t  d_ino;         /* inode number */
   grub_int16_t  d_reclen;      /* on-disk size, including padding (ignore it) */
   grub_int16_t  d_namlen;      /* length of the name */
#define MAX_NAMELEN        (512 - 9)   // a dir entry must fit in 512 bytes
   grub_uint8_t  d_name[0];     /* variable length name follows */
};

struct grub_fshelp_node
{
   struct grub_lynxfs_data  *data;
   struct grub_lynxfs_inode inode;
   int                      ino;        // virtual inode
   int                      inode_read; // whether the inode at ino has been read
};

/* Information about a "mounted" ext2 filesystem.  */
struct grub_lynxfs_data
{
   grub_disk_t               disk;
   struct grub_lynxfs_sblock sblock;
   grub_uint32_t             block_size;
   int                       block_bits;     /* 2^n = block_size (11 => 2048 bytes) */
   grub_uint32_t             num_inodes;
   grub_uint32_t             root_inode_no;  /* the root inode must be 1 */

   struct grub_lynxfs_inode  *inode;
   struct grub_fshelp_node   diropen;
};

static grub_dl_t my_mod;

/**
 * Extracts the 24-bit block number from inode->i_blocks.
 * No checking of the index is done.
 */
static grub_uint32_t
grub_lynxfs_inode_block_read(struct grub_lynxfs_inode *inode, int idx)
{
   grub_uint8_t *p8  = &inode->i_blocks[idx * 3];
   return (((grub_uint32_t)p8[0] << 16) |
           ((grub_uint32_t)p8[1] << 8) |
           (grub_uint32_t)p8[2]);
}

/**
 * Finds the physical block number for the fileblock.
 */
static grub_disk_addr_t
grub_lynxfs_read_block(grub_fshelp_node_t node, grub_disk_addr_t fileblock)
{
   struct grub_lynxfs_data  *data  = node->data;
   struct grub_lynxfs_inode *inode = &node->inode;
   unsigned int             blksz = data->block_size;
   grub_disk_addr_t         blksz_quarter = blksz / 4;
   int                      log2_blksz = LOG2_512_BLOCK_SIZE(data);
   int                      log_perblock = log2_blksz + 9 - 2;
   grub_uint32_t            indir; // big-endian
   int                      shift;

   /* Direct blocks.  */
   if (fileblock < INDIRECT_BLOCKS)
      return grub_lynxfs_inode_block_read(inode, fileblock);
   fileblock -= INDIRECT_BLOCKS;

   /* Indirect.  */
   if (fileblock < blksz_quarter)
   {
      indir = grub_cpu_to_be32(grub_lynxfs_inode_block_read(inode, 10));
      shift = 0;
      goto indirect;
   }
   fileblock -= blksz_quarter;

   /* Double indirect.  */
   if (fileblock < blksz_quarter * blksz_quarter)
   {
      indir = grub_cpu_to_be32(grub_lynxfs_inode_block_read(inode, 11));
      shift = 1;
      goto indirect;
   }
   fileblock -= blksz_quarter * blksz_quarter;

   /* Triple indirect.  */
   if (fileblock < blksz_quarter * blksz_quarter * (blksz_quarter + 1))
   {
      indir = grub_cpu_to_be32(grub_lynxfs_inode_block_read(inode, 12));
      shift = 2;
      goto indirect;
   }
   grub_error(GRUB_ERR_BAD_FS,
              "lynxfs doesn't support quadruple indirect blocks");
   return -1;

indirect:
   do {
      /* If the indirect block is zero, all child blocks are absent
          (i.e. filled with zeros.) */
      if (indir == 0)
         return 0;
      if (grub_disk_read(data->disk,
                         ((grub_disk_addr_t)grub_be_to_cpu32(indir)) << log2_blksz,
                         ((fileblock >> (log_perblock * shift)) & ((1 << log_perblock) - 1)) * sizeof(indir),
                         sizeof(indir), &indir))
         return -1;
   } while (shift--);

   return grub_be_to_cpu32(indir);
}

/* Read LEN bytes from the file described by DATA starting with byte
   POS.  Return the amount of read bytes in READ.  */
static grub_ssize_t
grub_lynxfs_read_file(grub_fshelp_node_t node,
                      grub_disk_read_hook_t read_hook, void *read_hook_data,
                      grub_off_t pos, grub_size_t len, char *buf)
{
   return grub_fshelp_read_file(node->data->disk, node,
                                read_hook, read_hook_data, 0,
                                pos, len, buf,
                                grub_lynxfs_read_block,
                                grub_be_to_cpu32(node->inode.i_size),
                                LOG2_512_BLOCK_SIZE(node->data), 0);
}

/* Read the inode INO for the file described by DATA into INODE.  */
static grub_err_t
grub_lynxfs_read_inode(struct grub_lynxfs_data *data,
                       grub_uint32_t ino, struct grub_lynxfs_inode *inode)
{
   int blk_num, blk_off;
   int reloff;

   /* Make sure the inode is valid */
   if ((ino < 1) || (ino > data->num_inodes))
   {
      return GRUB_ERR_FILE_NOT_FOUND;
   }
   reloff  = ((ino - 1) * 128); /* relative offset in inode area */
   blk_num = (2 + (reloff >> data->block_bits)) << (data->block_bits - 9);
   blk_off = reloff & (data->block_size - 1);

   if (grub_disk_read(data->disk, blk_num, blk_off, sizeof(*inode), inode))
   {
      return grub_errno;
   }
   return 0;
}

static struct grub_lynxfs_data *
grub_lynxfs_mount(grub_disk_t disk)
{
   struct grub_lynxfs_data   *data;
   struct grub_lynxfs_sblock *sb;

   data = grub_malloc(sizeof(*data));
   if (!data)
      return 0;
   sb = &data->sblock;

   /* Read the root superblock, which starts at ofset 512 */
   grub_disk_read(disk, 1, 0, sizeof(*sb), sb);
   if (grub_errno)
      goto fail;

   /* check the magic codes to see if this is a LynxFS partition */
   if ((sb->magic != grub_cpu_to_be32_compile_time(LYNXFS_SB_MAGIC)) ||
       (sb->bsize_magic != grub_cpu_to_be32_compile_time(LYNXFS_BSIZE_MAGIC)))
   {
      grub_error(GRUB_ERR_BAD_FS, "not an lynxfs filesystem");
      goto fail;
   }

   /* Grab the block size and calculate the number of bits */
   data->block_size = grub_be_to_cpu32(sb->bsize_code);
   data->block_bits = 0;
   {
      int tmp = data->block_size;
      while ((tmp >>= 1) > 0)
      {
         data->block_bits++;
      }
   }

   /* Now read the REAL superblock from block 1 */
   grub_disk_read(disk, data->block_size / 512, 0, sizeof(*sb), sb);
   if (grub_errno)
   {
      goto fail;
   }

   /* Sanity check */
   if ((sb->magic != grub_cpu_to_be32_compile_time(LYNXFS_SB_MAGIC)) ||
       (sb->bsize_magic != grub_cpu_to_be32_compile_time(LYNXFS_BSIZE_MAGIC)) ||
       (sb->bitmap_magic != grub_cpu_to_be32_compile_time(LYNXFS_BITMAP_MAGIC)))
   {
      grub_error(GRUB_ERR_BAD_FS, "not an lynxfs filesystem");
      goto fail;
   }

   /* Extract static (fixed) SuperBlock data */
   data->root_inode_no = grub_be_to_cpu32(sb->root_inode);
   data->num_inodes    = grub_be_to_cpu32(sb->num_inodes);

   data->disk = disk;

   // open the root inode
   data->diropen.data = data;
   data->diropen.ino = data->root_inode_no;
   data->diropen.inode_read = 1;
   data->inode = &data->diropen.inode;

   grub_lynxfs_read_inode(data, data->root_inode_no, data->inode);
   if (grub_errno)
      goto fail;
   return data;

fail:
   if (grub_errno == GRUB_ERR_OUT_OF_RANGE)
      grub_error(GRUB_ERR_BAD_FS, "not an lynxfs filesystem");
   grub_free(data);
   return NULL;
}

static char *
grub_lynxfs_read_symlink(grub_fshelp_node_t node)
{
   char *symlink;
   struct grub_fshelp_node *diro = node;

   if (!diro->inode_read)
   {
      grub_lynxfs_read_inode(diro->data, diro->ino, &diro->inode);
      if (grub_errno)
         return 0;
   }
   grub_uint32_t i_size = grub_be_to_cpu32(diro->inode.i_size);

   symlink = grub_malloc(i_size + 1);
   if (!symlink)
      return 0;

   grub_lynxfs_read_file(diro, 0, 0, 0, i_size, symlink);
   if (grub_errno)
   {
      grub_free(symlink);
      return 0;
   }
   symlink[i_size] = '\0';
   return symlink;
}

static int
grub_lynxfs_iterate_dir(grub_fshelp_node_t dir,
                        grub_fshelp_iterate_dir_hook_t hook, void *hook_data)
{
   unsigned int fpos = 0;
   struct grub_fshelp_node *diro = (struct grub_fshelp_node *)dir;

   if (!diro->inode_read)
   {
      grub_lynxfs_read_inode(diro->data, diro->ino, &diro->inode);
      if (grub_errno)
         return 0;
   }
   grub_uint32_t i_size = grub_be_to_cpu32(diro->inode.i_size);

   /* Search the file.  */
   while (fpos < i_size)
   {
      struct lynxfs_dirent dirent;

      grub_lynxfs_read_file(diro, 0, 0, fpos, sizeof(dirent), (char *)&dirent);
      if (grub_errno)
         return 0;

      grub_uint32_t d_ino    = grub_be_to_cpu32(dirent.d_ino);
      grub_int16_t  d_namlen = grub_be_to_cpu16(dirent.d_namlen);
      grub_int16_t  d_reclen = grub_be_to_cpu16(dirent.d_reclen);

      /* a reclen of 0 would cause issues, as we cannot continue the loop */
      if (d_reclen == 0)
         return 0;

      if (d_ino != 0 && d_namlen != 0 && d_namlen <= MAX_NAMELEN)
      {
         char filename[MAX_NAMELEN + 1];
         struct grub_fshelp_node *fdiro;
         enum grub_fshelp_filetype type = GRUB_FSHELP_UNKNOWN;

         grub_lynxfs_read_file(diro, 0, 0, fpos + sizeof(struct lynxfs_dirent),
                               d_namlen, filename);
         if (grub_errno)
            return 0;
         filename[d_namlen] = '\0';

         fdiro = grub_malloc(sizeof(*fdiro));
         if (!fdiro)
            return 0;

         fdiro->data = diro->data;
         fdiro->ino  = d_ino;

         grub_lynxfs_read_inode(diro->data, d_ino, &fdiro->inode);
         if (grub_errno)
         {
            grub_free(fdiro);
            return 0;
         }
         fdiro->inode_read = 1;

         grub_uint16_t i_mode = grub_be_to_cpu16(fdiro->inode.i_mode);
         if ((i_mode & FILETYPE_INO_MASK) == FILETYPE_INO_DIRECTORY)
            type = GRUB_FSHELP_DIR;
         else if ((i_mode & FILETYPE_INO_MASK) == FILETYPE_INO_SYMLINK)
            type = GRUB_FSHELP_SYMLINK;
         else if ((i_mode & FILETYPE_INO_MASK) == FILETYPE_INO_REG)
            type = GRUB_FSHELP_REG;

         if (hook(filename, type, fdiro, hook_data))
            return 1;
      }
      fpos += d_reclen;
   }

   return 0;
}

/* Open a file named NAME and initialize FILE. */
static grub_err_t
grub_lynxfs_open(struct grub_file *file, const char *name)
{
   struct grub_lynxfs_data *data;
   struct grub_fshelp_node *fdiro = 0;
   grub_err_t err;

   grub_dl_ref(my_mod);

   data = grub_lynxfs_mount(file->device->disk);
   if (!data)
   {
      err = grub_errno;
      goto fail;
   }

   err = grub_fshelp_find_file(name, &data->diropen, &fdiro,
                               grub_lynxfs_iterate_dir,
                               grub_lynxfs_read_symlink,
                               GRUB_FSHELP_REG);
   if (err)
      goto fail;

   if (!fdiro->inode_read)
   {
      err = grub_lynxfs_read_inode(data, fdiro->ino, &fdiro->inode);
      if (err)
         goto fail;
   }

   grub_memcpy(data->inode, &fdiro->inode, sizeof(*data->inode));
   data->diropen.ino = fdiro->ino;
   grub_free(fdiro);

   file->size = grub_be_to_cpu32(data->inode->i_size);
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

static grub_err_t
grub_lynxfs_close(grub_file_t file)
{
   grub_free(file->data);
   grub_dl_unref(my_mod);
   return GRUB_ERR_NONE;
}

/* Read LEN bytes data from FILE into BUF.  */
static grub_ssize_t
grub_lynxfs_read (grub_file_t file, char *buf, grub_size_t len)
{
   struct grub_lynxfs_data *data = (struct grub_lynxfs_data *) file->data;

   return grub_lynxfs_read_file(&data->diropen,
                                file->read_hook, file->read_hook_data,
                                file->offset, len, buf);
}

/* Context for grub_lynxfs_dir.  */
struct grub_lynxfs_dir_ctx
{
   grub_fs_dir_hook_t      hook;
   void                    *hook_data;
   struct grub_lynxfs_data *data;
};

/* Helper for grub_lynxfs_dir.  */
static int
grub_lynxfs_dir_iter(const char *filename, enum grub_fshelp_filetype filetype,
                     grub_fshelp_node_t node, void *data)
{
   struct grub_lynxfs_dir_ctx *ctx = data;
   struct grub_dirhook_info info;

   grub_memset (&info, 0, sizeof (info));
   if (!node->inode_read)
   {
      grub_lynxfs_read_inode(ctx->data, node->ino, &node->inode);
      if (!grub_errno)
         node->inode_read = 1;
      grub_errno = GRUB_ERR_NONE;
   }
   if (node->inode_read)
   {
      info.mtimeset = 1;
      info.mtime    = grub_be_to_cpu32(node->inode.i_mtime);
   }

   info.dir = ((filetype & GRUB_FSHELP_TYPE_MASK) == GRUB_FSHELP_DIR);
   grub_free(node);
   return ctx->hook(filename, &info, ctx->hook_data);
}

static grub_err_t
grub_lynxfs_dir(grub_device_t device, const char *path, grub_fs_dir_hook_t hook,
                void *hook_data)
{
   struct grub_lynxfs_dir_ctx ctx = {
      .hook = hook,
      .hook_data = hook_data
   };
   struct grub_fshelp_node *fdiro = 0;

   grub_dl_ref(my_mod);

   ctx.data = grub_lynxfs_mount (device->disk);
   if (!ctx.data)
      goto fail;

   grub_fshelp_find_file(path, &ctx.data->diropen, &fdiro,
                         grub_lynxfs_iterate_dir, grub_lynxfs_read_symlink,
                         GRUB_FSHELP_DIR);
   if (grub_errno)
   {
      goto fail;
   }

   grub_lynxfs_iterate_dir(fdiro, grub_lynxfs_dir_iter, &ctx);

fail:
   if (fdiro != &ctx.data->diropen)
      grub_free(fdiro);
   grub_free(ctx.data);
   grub_dl_unref(my_mod);
   return grub_errno;
}

static struct grub_fs grub_lynxfs_fs =
{
   .name = "lynxfs",
   .fs_dir = grub_lynxfs_dir,
   .fs_open = grub_lynxfs_open,
   .fs_read = grub_lynxfs_read,
   .fs_close = grub_lynxfs_close,
#ifdef GRUB_UTIL
   .reserved_first_sector = 1,
   .blocklist_install = 1,
#endif
};

GRUB_MOD_INIT(lynxfs)
{
   grub_fs_register(&grub_lynxfs_fs);
   my_mod = mod;
}

GRUB_MOD_FINI(lynxfs)
{
   grub_fs_unregister(&grub_lynxfs_fs);
}
