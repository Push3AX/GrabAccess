/* qnx4.c - QNX 4 filesystem
 * Parts copied from the Linux kernel.
 */

/**
 * Overview of QNX 4 filesystem.
 *
 * The block size is 512 bytes.
 *
 * Inodes and directory entries are a bit conflated.
 * Inodes are 64 bytes may appear pretty much anywhere on the disk and derive
 * the inode number from the location on disk.
 *
 * Inode 8 is at offset 0 of block 1 and it must be the root "/".
 * Directory entries (inodes) live in that file's data.
 *
 * Extents are used for file data. The first few blocks are in the first extent.
 * The rest are chained in extent blocks.
 *
 * The only tricky bit about inodes is that they may be a link to another inode.
 * For example, '.' and '..' are always links.
 * You have to read the inode info from that other inode.
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

GRUB_MOD_LICENSE ("GPLv3+");

/* for di_status */
#define QNX4_FILE_USED           0x01
#define QNX4_FILE_MODIFIED       0x02
#define QNX4_FILE_BUSY           0x04
#define QNX4_FILE_LINK           0x08
#define QNX4_FILE_INODE          0x10
#define QNX4_FILE_FSYSCLEAN      0x20

#define QNX4_BLOCK_SIZE          0x200    /* blocksize of 512 bytes */
#define QNX4_BLOCK_SIZE_BITS     9        /* blocksize shift */
#define QNX4_INODE_SIZE          64
#define QNX4_INODES_PER_BLOCK    (QNX4_BLOCK_SIZE / QNX4_INODE_SIZE) // 0x08
#define QNX4_INODES_OFF_MASK     (QNX4_INODES_PER_BLOCK - 1)         // 0x07

#define QNX4_I_MAP_SLOTS         8
#define QNX4_Z_MAP_SLOTS         64
#define QNX4_VALID_FS            0x0001   /* Clean fs. */
#define QNX4_ERROR_FS            0x0002   /* fs has errors. */
#define QNX4_DIR_ENTRY_SIZE      0x040    /* dir entry size of 64 bytes */
#define QNX4_DIR_ENTRY_SIZE_BITS 6        /* dir entry size shift */
#define QNX4_XBLK_ENTRY_SIZE     0x200    /* xblk entry size */
#define QNX4_MAX_XTNTS_PER_XBLK  60

/* for filenames */
#define QNX4_SHORT_NAME_MAX      16
#define QNX4_NAME_MAX            48

/** all fields are little-endian */
typedef grub_uint16_t qnx4_mode_t;
typedef grub_uint16_t qnx4_muid_t;
typedef grub_uint16_t qnx4_mgid_t;
typedef grub_uint32_t qnx4_off_t;
typedef grub_uint16_t qnx4_nlink_t;
typedef grub_uint16_t qnx4_nxtnt_t;
typedef grub_uint8_t  qnx4_ftype_t;

typedef struct {
   grub_uint32_t xtnt_blk;
   grub_uint32_t xtnt_size;
} qnx4_xtnt_t;

struct qnx4_link_info {
   char              dl_fname[QNX4_NAME_MAX];
   grub_uint32_t     dl_inode_blk;
   grub_uint8_t      dl_inode_ndx;
   grub_uint8_t      dl_spare[10];
   grub_uint8_t      dl_status;
};

struct grub_qnx4_inode
{
   grub_uint32_t ino;

   /**
    * This is the on-disk inode structure for QNX4.
    */
   struct
   {
      char           di_fname[QNX4_SHORT_NAME_MAX]; // 0x00 16
      qnx4_off_t     di_size;                       // 0x10 4
      qnx4_xtnt_t    di_first_xtnt;                 // 0x14 8
      grub_uint32_t  di_xblk;                       // 0x1c 4
      grub_uint32_t  di_ftime;                      // 0x20 4
      grub_uint32_t  di_mtime;                      // 0x24 4
      grub_uint32_t  di_atime;                      // 0x28 4
      grub_uint32_t  di_ctime;                      // 0x2c 4
      qnx4_nxtnt_t   di_num_xtnts;                  // 0x30 2
      qnx4_mode_t    di_mode;                       // 0x32 2
      qnx4_muid_t    di_uid;                        // 0x34 2
      qnx4_mgid_t    di_gid;                        // 0x36 2
      qnx4_nlink_t   di_nlink;                      // 0x38 2
      grub_uint8_t   di_zero[4];                    // 0x3a 4
      qnx4_ftype_t   di_type;                       // 0x3e 1
      grub_uint8_t   di_status;                     // 0x3f 1
   } raw;
};

struct grub_qnx4_xblk {
   grub_uint32_t     xblk_next_xblk;
   grub_uint32_t     xblk_prev_xblk;
   grub_uint8_t      xblk_num_xtnts;
   grub_uint8_t      xblk_spare[3];
   grub_uint32_t     xblk_num_blocks;
   qnx4_xtnt_t       xblk_xtnts[QNX4_MAX_XTNTS_PER_XBLK];
   char              xblk_signature[8]; // "IamXblk"
   qnx4_xtnt_t       xblk_first_xtnt;
};

struct grub_fshelp_node
{
   struct grub_qnx4_data  *data;
   struct grub_qnx4_inode inode;
};

/* Information about a "mounted" filesystem.  */
struct grub_qnx4_data
{
   grub_disk_t               disk;
   struct grub_qnx4_inode    *inode;
   struct grub_fshelp_node   diropen;
};

static grub_dl_t my_mod;

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
#endif

static grub_uint32_t try_extent(qnx4_xtnt_t *extent, grub_uint32_t *offset)
{
   grub_uint32_t sblk = grub_le_to_cpu32(extent->xtnt_blk);
   grub_uint32_t size = grub_le_to_cpu32(extent->xtnt_size);

   if (*offset < size)
      return sblk + *offset - 1;
   *offset -= size;
   return 0;
}

static grub_disk_addr_t
grub_qnx4_get_block(grub_fshelp_node_t node, grub_disk_addr_t iblock)
{
   struct grub_qnx4_data  *data  = node->data;
   struct grub_qnx4_inode *inode = &node->inode;
   grub_uint16_t          nxtnt  = grub_le_to_cpu16(inode->raw.di_num_xtnts);
   grub_uint32_t          offset = iblock;
   grub_disk_addr_t       block;

   block = try_extent(&inode->raw.di_first_xtnt, &offset);
   if (block) {
      // iblock is in the first extent. This is easy.
   } else {
      // iblock is beyond first extent. We have to follow the extent chain.
      grub_uint32_t         i_xblk = grub_le_to_cpu32(inode->raw.di_xblk);
      struct grub_qnx4_xblk *xblk = NULL;
      grub_uint8_t          *bh;
      int                   ix = 0;

      bh = grub_zalloc(QNX4_XBLK_ENTRY_SIZE);
      if (!bh)
         return 0;

      while (--nxtnt > 0) {
         if (ix == 0) {
            // read next xtnt block.
            grub_disk_read(data->disk, i_xblk - 1, 0, QNX4_XBLK_ENTRY_SIZE, bh);
            if (grub_errno)
               break;

            xblk = (struct grub_qnx4_xblk *)bh;
            if (grub_memcmp(xblk->xblk_signature, "IamXblk", 8) != 0) {
               grub_error(GRUB_ERR_FILE_READ_ERROR, "Xblk corrupt");
               break;
            }
         }

         block = try_extent(&xblk->xblk_xtnts[ix], &offset);
         if (block) // got the block mapping
            break;
         if (++ix >= (int)grub_le_to_cpu32(xblk->xblk_num_xtnts)) {
            i_xblk = grub_le_to_cpu32(xblk->xblk_next_xblk);
            ix = 0;
         }
      }
      grub_free(bh);
   }
   return block;
}

/* Read LEN bytes from the file described by DATA starting with byte
   POS.  Return the amount of read bytes in READ.  */
static grub_ssize_t
grub_qnx4_read_file(grub_fshelp_node_t node,
                      grub_disk_read_hook_t read_hook, void *read_hook_data,
                      grub_off_t pos, grub_size_t len, char *buf)
{
   return grub_fshelp_read_file(node->data->disk, node,
                                read_hook, read_hook_data, 0,
                                pos, len, buf,
                                grub_qnx4_get_block,
                                grub_le_to_cpu32(node->inode.raw.di_size),
                                0, 0);
}

/* Read the inode INO for the file described by DATA into INODE.  */
static grub_err_t
grub_qnx4_read_inode(struct grub_qnx4_data *data,
                     grub_uint32_t ino, struct grub_qnx4_inode *inode)
{
   int blk_num = ino / QNX4_INODES_PER_BLOCK; // block size = sector size = 512
   int blk_off = (ino & QNX4_INODES_OFF_MASK) * QNX4_INODE_SIZE;

   inode->ino = ino;
   if (grub_disk_read(data->disk, blk_num, blk_off, sizeof(inode->raw), &inode->raw))
   {
      return grub_errno;
   }
   return 0;
}

static struct grub_qnx4_data *
grub_qnx4_mount(grub_disk_t disk)
{
   struct grub_qnx4_data   *data;
   struct grub_qnx4_inode  *iroot;

   data = grub_zalloc(sizeof(*data));
   if (!data)
      return 0;

   /* set up data */
   data->disk         = disk;
   data->diropen.data = data;
   data->inode        = &data->diropen.inode;

   /* Inode 8 is the root directory, starts in block 1 */
   iroot = data->inode;
   grub_qnx4_read_inode(data, QNX4_INODES_PER_BLOCK, iroot);
   if (grub_errno)
      goto fail;

   /* check the magic codes to see if this is a qnx4 partition */
   if (iroot->raw.di_fname[0] != 0x2f || iroot->raw.di_fname[1] != 0x00)
   {
      grub_error(GRUB_ERR_BAD_FS, "not qnx4 filesystem");
      goto fail;
   }

   return data;

fail:
   if (grub_errno == GRUB_ERR_OUT_OF_RANGE)
      grub_error(GRUB_ERR_BAD_FS, "not an qnx4 filesystem");
   grub_free(data);
   return NULL;
}

static char *
grub_qnx4_read_symlink(grub_fshelp_node_t node)
{
   char *symlink;
   struct grub_fshelp_node *diro = node;

   grub_uint32_t i_size = grub_le_to_cpu32(diro->inode.raw.di_size);

   symlink = grub_malloc(i_size + 1);
   if (!symlink)
      return 0;

   grub_qnx4_read_file(diro, 0, 0, 0, i_size, symlink);
   if (grub_errno)
   {
      grub_free(symlink);
      return 0;
   }
   symlink[i_size] = '\0';
   return symlink;
}

static int strnlen(const char *text, int max_size)
{
   int idx;

   for (idx = 0; idx < max_size; idx++)
   {
      if (!text[idx])
      {
         break;
      }
   }
   return idx;
}

static int
grub_qnx4_iterate_dir(grub_fshelp_node_t dir,
                      grub_fshelp_iterate_dir_hook_t hook, void *hook_data)
{
   unsigned int fpos = 0;
   struct grub_fshelp_node *diro = (struct grub_fshelp_node *)dir;

   grub_uint32_t    i_size = grub_le_to_cpu32(diro->inode.raw.di_size);
   grub_disk_addr_t fblk_prev = 0;
   grub_disk_addr_t dblk = 0;

   while (fpos < i_size) {
      grub_disk_addr_t       fblk;
      struct grub_qnx4_inode de;

      /* need to find the disk block to get the inode number */
      fblk = fpos >> QNX4_BLOCK_SIZE_BITS;
      if (!dblk || fblk != fblk_prev)
      {
         fblk_prev = fblk;
         dblk = grub_qnx4_get_block(dir, fpos >> QNX4_BLOCK_SIZE_BITS);
         if (!dblk)
            break;
      }

      /* read in the inode entry */
      int ino = ((dblk * QNX4_INODES_PER_BLOCK) +
                 ((fpos / QNX4_INODE_SIZE) & QNX4_INODES_OFF_MASK));
      grub_qnx4_read_inode(diro->data, ino, &de);
      if (grub_errno)
         break;

      fpos += QNX4_INODE_SIZE;

      if (!de.raw.di_fname[0] ||
          !(de.raw.di_status & (QNX4_FILE_USED | QNX4_FILE_LINK)))
         continue;

      char filename[QNX4_NAME_MAX + 1];
      int size;
      if (!(de.raw.di_status & QNX4_FILE_LINK))
         size = QNX4_SHORT_NAME_MAX;
      else
         size = QNX4_NAME_MAX;
      size = strnlen(de.raw.di_fname, size);
      grub_memcpy(filename, de.raw.di_fname, size);
      filename[size] = 0;

      if ((de.raw.di_status & QNX4_FILE_LINK) != 0)
      {
         struct qnx4_link_info *le = (struct qnx4_link_info *)&de.raw;
         ino = le->dl_inode_ndx +
            ((grub_le_to_cpu32(le->dl_inode_blk) - 1) * QNX4_INODES_PER_BLOCK);
         grub_qnx4_read_inode(diro->data, ino, &de);
      }

      struct grub_fshelp_node *fdiro = grub_malloc(sizeof(*fdiro));
      if (!fdiro)
         return 0;

      fdiro->data = diro->data;

      grub_qnx4_read_inode(diro->data, ino, &fdiro->inode);
      if (grub_errno)
      {
         grub_free(fdiro);
         return 0;
      }

      grub_uint16_t i_mode = grub_le_to_cpu16(fdiro->inode.raw.di_mode);
      enum grub_fshelp_filetype type = GRUB_FSHELP_UNKNOWN;
      if ((i_mode & FILETYPE_INO_MASK) == FILETYPE_INO_DIRECTORY)
         type = GRUB_FSHELP_DIR;
      else if ((i_mode & FILETYPE_INO_MASK) == FILETYPE_INO_SYMLINK)
         type = GRUB_FSHELP_SYMLINK;
      else if ((i_mode & FILETYPE_INO_MASK) == FILETYPE_INO_REG)
         type = GRUB_FSHELP_REG;

      if (hook(filename, type, fdiro, hook_data))
         return 1;
   }
   return 0;
}

/* Open a file named NAME and initialize FILE. */
static grub_err_t
grub_qnx4_open(struct grub_file *file, const char *name)
{
   struct grub_qnx4_data *data;
   struct grub_fshelp_node *fdiro = 0;
   grub_err_t err;

   grub_dl_ref(my_mod);

   data = grub_qnx4_mount(file->device->disk);
   if (!data)
   {
      err = grub_errno;
      goto fail;
   }

   err = grub_fshelp_find_file(name, &data->diropen, &fdiro,
                               grub_qnx4_iterate_dir,
                               grub_qnx4_read_symlink,
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

static grub_err_t
grub_qnx4_close(grub_file_t file)
{
   grub_free(file->data);
   grub_dl_unref(my_mod);
   return GRUB_ERR_NONE;
}

/* Read LEN bytes data from FILE into BUF.  */
static grub_ssize_t
grub_qnx4_read (grub_file_t file, char *buf, grub_size_t len)
{
   struct grub_qnx4_data *data = (struct grub_qnx4_data *) file->data;

   return grub_qnx4_read_file(&data->diropen,
                                file->read_hook, file->read_hook_data,
                                file->offset, len, buf);
}

/* Context for grub_qnx4_dir.  */
struct grub_qnx4_dir_ctx
{
   grub_fs_dir_hook_t      hook;
   void                    *hook_data;
   struct grub_qnx4_data *data;
};

/* Helper for grub_qnx4_dir.  */
static int
grub_qnx4_dir_iter(const char *filename, enum grub_fshelp_filetype filetype,
                   grub_fshelp_node_t node, void *data)
{
   struct grub_qnx4_dir_ctx *ctx = data;
   struct grub_dirhook_info info;

   grub_memset(&info, 0, sizeof (info));
   info.mtimeset = 1;
   info.mtime    = grub_le_to_cpu32(node->inode.raw.di_mtime);

   info.dir = ((filetype & GRUB_FSHELP_TYPE_MASK) == GRUB_FSHELP_DIR);
   return ctx->hook(filename, &info, ctx->hook_data);
}

static grub_err_t
grub_qnx4_dir(grub_device_t device, const char *path, grub_fs_dir_hook_t hook,
              void *hook_data)
{
   struct grub_qnx4_dir_ctx ctx = {
      .hook = hook,
      .hook_data = hook_data
   };
   struct grub_fshelp_node *fdiro = 0;

   grub_dl_ref(my_mod);

   ctx.data = grub_qnx4_mount(device->disk);
   if (!ctx.data)
      goto fail;

   grub_fshelp_find_file(path, &ctx.data->diropen, &fdiro,
                         grub_qnx4_iterate_dir, grub_qnx4_read_symlink,
                         GRUB_FSHELP_DIR);
   if (grub_errno)
   {
      goto fail;
   }

   grub_qnx4_iterate_dir(fdiro, grub_qnx4_dir_iter, &ctx);

fail:
   if (fdiro != &ctx.data->diropen)
      grub_free(fdiro);
   grub_free(ctx.data);
   grub_dl_unref(my_mod);
   return grub_errno;
}

static struct grub_fs grub_qnx4_fs =
{
   .name = "qnx4",
   .fs_dir = grub_qnx4_dir,
   .fs_open = grub_qnx4_open,
   .fs_read = grub_qnx4_read,
   .fs_close = grub_qnx4_close,
#ifdef GRUB_UTIL
   .reserved_first_sector = 1,
   .blocklist_install = 1,
#endif
};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

GRUB_MOD_INIT(qnx4)
{
   grub_fs_register(&grub_qnx4_fs);
   my_mod = mod;
}

GRUB_MOD_FINI(qnx4)
{
   grub_fs_unregister(&grub_qnx4_fs);
}
