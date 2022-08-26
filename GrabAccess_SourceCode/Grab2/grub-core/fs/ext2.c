/* ext2.c - Second Extended filesystem */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2004,2005,2007,2008,2009,2020  Free Software Foundation, Inc.
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

#include <grub/err.h>
#include <grub/file.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/disk.h>
#include <grub/dl.h>
#include <grub/types.h>
#include <grub/fshelp.h>
#include <grub/ext2.h>
#include <grub/safemath.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define MAX_NAMELEN 255

/* Log2 size of ext2 block in 512 blocks.  */
#define LOG2_EXT2_BLOCK_SIZE(data)			\
	(grub_le_to_cpu32 (data->sblock.log2_block_size) + 1)

/* Log2 size of ext2 block in bytes.  */
#define LOG2_BLOCK_SIZE(data)					\
	(grub_le_to_cpu32 (data->sblock.log2_block_size) + 10)

/* The size of an ext2 block in bytes.  */
#define EXT2_BLOCK_SIZE(data)		(1U << LOG2_BLOCK_SIZE (data))

/* The revision level.  */
#define EXT2_REVISION(data)	grub_le_to_cpu32 (data->sblock.revision_level)

/* The inode size.  */
#define EXT2_INODE_SIZE(data)	\
  (data->sblock.revision_level \
   == grub_cpu_to_le32_compile_time (EXT2_GOOD_OLD_REVISION)	\
         ? EXT2_GOOD_OLD_INODE_SIZE \
         : grub_le_to_cpu16 (data->sblock.inode_size))

struct grub_fshelp_node
{
  struct grub_ext2_data *data;
  struct grub_ext2_inode inode;
  int ino;
  int inode_read;
};

/* Information about a "mounted" ext2 filesystem.  */
struct grub_ext2_data
{
  struct grub_ext2_sblock sblock;
  int log_group_desc_size;
  grub_disk_t disk;
  struct grub_ext2_inode *inode;
  struct grub_fshelp_node diropen;
};

static grub_dl_t my_mod;

/* Check is a = b^x for some x.  */
static inline int
is_power_of (grub_uint64_t a, grub_uint32_t b)
{
  grub_uint64_t c;
  /* Prevent overflow assuming b < 8.  */
  if (a >= (1LL << 60))
    return 0;
  for (c = 1; c <= a; c *= b);
  return (c == a);
}

static inline int
group_has_super_block (struct grub_ext2_data *data, grub_uint64_t group)
{
  if (!(data->sblock.feature_ro_compat
	& grub_cpu_to_le32_compile_time(EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER)))
    return 1;
  /* Algorithm looked up in Linux source.  */
  if (group <= 1)
    return 1;
  /* Even number is never a power of odd number.  */
  if (!(group & 1))
    return 0;
  return (is_power_of(group, 7) || is_power_of(group, 5) ||
	  is_power_of(group, 3));
}

/* Read into BLKGRP the blockgroup descriptor of blockgroup GROUP of
   the mounted filesystem DATA.  */
inline static grub_err_t
grub_ext2_blockgroup (struct grub_ext2_data *data, grub_uint64_t group,
		      struct grub_ext2_block_group *blkgrp)
{
  grub_uint64_t full_offset = (group << data->log_group_desc_size);
  grub_uint64_t block, offset;
  block = (full_offset >> LOG2_BLOCK_SIZE (data));
  offset = (full_offset & ((1 << LOG2_BLOCK_SIZE (data)) - 1));
  if ((data->sblock.feature_incompat
       & grub_cpu_to_le32_compile_time (EXT2_FEATURE_INCOMPAT_META_BG))
      && block >= grub_le_to_cpu32(data->sblock.first_meta_bg))
    {
      grub_uint64_t first_block_group;
      /* Find the first block group for which a descriptor
	 is stored in given block. */
      first_block_group = (block << (LOG2_BLOCK_SIZE (data)
				     - data->log_group_desc_size));

      block = (first_block_group
	       * grub_le_to_cpu32(data->sblock.blocks_per_group));

      if (group_has_super_block (data, first_block_group))
	block++;
    }
  else
    /* Superblock.  */
    block++;
  return grub_disk_read (data->disk,
                         ((grub_le_to_cpu32 (data->sblock.first_data_block)
			   + block)
                          << LOG2_EXT2_BLOCK_SIZE (data)), offset,
			 sizeof (struct grub_ext2_block_group), blkgrp);
}

static grub_err_t
grub_ext4_find_leaf (struct grub_ext2_data *data,
                     struct grub_ext4_extent_header *ext_block,
                     grub_uint32_t fileblock,
                     struct grub_ext4_extent_header **leaf)
{
  struct grub_ext4_extent_idx *index;
  void *buf = NULL;
  *leaf = NULL;

  while (1)
    {
      int i;
      grub_disk_addr_t block;

      index = (struct grub_ext4_extent_idx *) (ext_block + 1);

      if (ext_block->magic != grub_cpu_to_le16_compile_time (EXT4_EXT_MAGIC))
	goto fail;

      if (ext_block->depth == 0)
        {
          *leaf = ext_block;
          return GRUB_ERR_NONE;
        }

      for (i = 0; i < grub_le_to_cpu16 (ext_block->entries); i++)
        {
          if (fileblock < grub_le_to_cpu32(index[i].block))
            break;
        }

      if (--i < 0)
        {
          grub_free (buf);
          return GRUB_ERR_NONE;
        }

      block = grub_le_to_cpu16 (index[i].leaf_hi);
      block = (block << 32) | grub_le_to_cpu32 (index[i].leaf);
      if (!buf)
	buf = grub_malloc (EXT2_BLOCK_SIZE(data));
      if (!buf)
	goto fail;
      if (grub_disk_read (data->disk,
                          block << LOG2_EXT2_BLOCK_SIZE (data),
                          0, EXT2_BLOCK_SIZE(data), buf))
	goto fail;

      ext_block = buf;
    }
 fail:
  grub_free (buf);
  return GRUB_ERR_BAD_FS;
}

static grub_disk_addr_t
grub_ext2_read_block (grub_fshelp_node_t node, grub_disk_addr_t fileblock)
{
  struct grub_ext2_data *data = node->data;
  struct grub_ext2_inode *inode = &node->inode;
  unsigned int blksz = EXT2_BLOCK_SIZE (data);
  grub_disk_addr_t blksz_quarter = blksz / 4;
  int log2_blksz = LOG2_EXT2_BLOCK_SIZE (data);
  int log_perblock = log2_blksz + 9 - 2;
  grub_uint32_t indir;
  int shift;

  if (inode->flags & grub_cpu_to_le32_compile_time (EXT4_EXTENTS_FLAG))
    {
      struct grub_ext4_extent_header *leaf;
      struct grub_ext4_extent *ext;
      int i;
      grub_disk_addr_t ret;

      if (grub_ext4_find_leaf (data, (struct grub_ext4_extent_header *) inode->blocks.dir_blocks,
			       fileblock, &leaf) != GRUB_ERR_NONE)
        {
          grub_error (GRUB_ERR_BAD_FS, "invalid extent");
          return -1;
        }

      if (leaf == NULL)
        /* Leaf for the given block is absent (i.e. sparse) */
        return 0;

      ext = (struct grub_ext4_extent *) (leaf + 1);
      for (i = 0; i < grub_le_to_cpu16 (leaf->entries); i++)
        {
          if (fileblock < grub_le_to_cpu32 (ext[i].block))
            break;
        }

      if (--i >= 0)
        {
          fileblock -= grub_le_to_cpu32 (ext[i].block);
          if (fileblock >= grub_le_to_cpu16 (ext[i].len))
	    ret = 0;
          else
            {
              grub_disk_addr_t start;

              start = grub_le_to_cpu16 (ext[i].start_hi);
              start = (start << 32) + grub_le_to_cpu32 (ext[i].start);

              ret = fileblock + start;
            }
        }
      else
        {
          grub_error (GRUB_ERR_BAD_FS, "something wrong with extent");
	  ret = -1;
        }

      if (leaf != (struct grub_ext4_extent_header *) inode->blocks.dir_blocks)
	grub_free (leaf);

      return ret;
    }

  /* Direct blocks.  */
  if (fileblock < INDIRECT_BLOCKS)
    return grub_le_to_cpu32 (inode->blocks.dir_blocks[fileblock]);
  fileblock -= INDIRECT_BLOCKS;
  /* Indirect.  */
  if (fileblock < blksz_quarter)
    {
      indir = inode->blocks.indir_block;
      shift = 0;
      goto indirect;
    }
  fileblock -= blksz_quarter;
  /* Double indirect.  */
  if (fileblock < blksz_quarter * blksz_quarter)
    {
      indir = inode->blocks.double_indir_block;
      shift = 1;
      goto indirect;
    }
  fileblock -= blksz_quarter * blksz_quarter;
  /* Triple indirect.  */
  if (fileblock < blksz_quarter * blksz_quarter * (blksz_quarter + 1))
    {
      indir = inode->blocks.triple_indir_block;
      shift = 2;
      goto indirect;
    }
  grub_error (GRUB_ERR_BAD_FS,
	      "ext2fs doesn't support quadruple indirect blocks");
  return -1;

indirect:
  do {
    /* If the indirect block is zero, all child blocks are absent
       (i.e. filled with zeros.) */
    if (indir == 0)
      return 0;
    if (grub_disk_read (data->disk,
			((grub_disk_addr_t) grub_le_to_cpu32 (indir))
			<< log2_blksz,
			((fileblock >> (log_perblock * shift))
			 & ((1 << log_perblock) - 1))
			* sizeof (indir),
			sizeof (indir), &indir))
      return -1;
  } while (shift--);

  return grub_le_to_cpu32 (indir);
}

/* Read LEN bytes from the file described by DATA starting with byte
   POS.  Return the amount of read bytes in READ.  */
static grub_ssize_t
grub_ext2_read_file (grub_fshelp_node_t node,
		     grub_disk_read_hook_t read_hook, void *read_hook_data, int blocklist,
		     grub_off_t pos, grub_size_t len, char *buf)
{
  return grub_fshelp_read_file (node->data->disk, node,
				read_hook, read_hook_data, blocklist,
				pos, len, buf, grub_ext2_read_block,
				grub_cpu_to_le32 (node->inode.size)
				| (((grub_off_t) grub_cpu_to_le32 (node->inode.size_high)) << 32),
				LOG2_EXT2_BLOCK_SIZE (node->data), 0);

}


/* Read the inode INO for the file described by DATA into INODE.  */
static grub_err_t
grub_ext2_read_inode (struct grub_ext2_data *data,
		      int ino, struct grub_ext2_inode *inode)
{
  struct grub_ext2_block_group blkgrp;
  struct grub_ext2_sblock *sblock = &data->sblock;
  int inodes_per_block;
  unsigned int blkno;
  unsigned int blkoff;
  grub_disk_addr_t base;

  /* It is easier to calculate if the first inode is 0.  */
  ino--;

  grub_ext2_blockgroup (data,
                        ino / grub_le_to_cpu32 (sblock->inodes_per_group),
			&blkgrp);
  if (grub_errno)
    return grub_errno;

  inodes_per_block = EXT2_BLOCK_SIZE (data) / EXT2_INODE_SIZE (data);
  blkno = (ino % grub_le_to_cpu32 (sblock->inodes_per_group))
    / inodes_per_block;
  blkoff = (ino % grub_le_to_cpu32 (sblock->inodes_per_group))
    % inodes_per_block;

  base = grub_le_to_cpu32 (blkgrp.inode_table_id);
  if (data->log_group_desc_size >= 6)
    base |= (((grub_disk_addr_t) grub_le_to_cpu32 (blkgrp.inode_table_id_hi))
	     << 32);

  /* Read the inode.  */
  if (grub_disk_read (data->disk,
		      ((base + blkno) << LOG2_EXT2_BLOCK_SIZE (data)),
		      EXT2_INODE_SIZE (data) * blkoff,
		      sizeof (struct grub_ext2_inode), inode))
    return grub_errno;

  return 0;
}

static struct grub_ext2_data *
grub_ext2_mount (grub_disk_t disk)
{
  struct grub_ext2_data *data;

  data = grub_malloc (sizeof (struct grub_ext2_data));
  if (!data)
    return 0;

  /* Read the superblock.  */
  grub_disk_read (disk, 1 * 2, 0, sizeof (struct grub_ext2_sblock),
                  &data->sblock);
  if (grub_errno)
    goto fail;

  /* Make sure this is an ext2 filesystem.  */
  if (data->sblock.magic != grub_cpu_to_le16_compile_time (EXT2_MAGIC)
      || grub_le_to_cpu32 (data->sblock.log2_block_size) >= 16
      || data->sblock.inodes_per_group == 0
      /* 20 already means 1GiB blocks. We don't want to deal with blocks overflowing int32. */
      || grub_le_to_cpu32 (data->sblock.log2_block_size) > 20
      || EXT2_INODE_SIZE (data) == 0
      || EXT2_BLOCK_SIZE (data) / EXT2_INODE_SIZE (data) == 0)
    {
      grub_error (GRUB_ERR_BAD_FS, "not an ext2 filesystem");
      goto fail;
    }

  /* Check the FS doesn't have feature bits enabled that we don't support. */
  if (data->sblock.revision_level != grub_cpu_to_le32_compile_time (EXT2_GOOD_OLD_REVISION)
      && (data->sblock.feature_incompat
	  & grub_cpu_to_le32_compile_time (~(EXT2_DRIVER_SUPPORTED_INCOMPAT
					     | EXT2_DRIVER_IGNORED_INCOMPAT))))
    {
      grub_error (GRUB_ERR_BAD_FS, "filesystem has unsupported incompatible features");
      goto fail;
    }

  if (data->sblock.revision_level != grub_cpu_to_le32_compile_time (EXT2_GOOD_OLD_REVISION)
      && (data->sblock.feature_incompat
	  & grub_cpu_to_le32_compile_time (EXT4_FEATURE_INCOMPAT_64BIT))
      && data->sblock.group_desc_size != 0
      && ((data->sblock.group_desc_size & (data->sblock.group_desc_size - 1))
	  == 0)
      && (data->sblock.group_desc_size & grub_cpu_to_le16_compile_time (0x1fe0)))
    {
      grub_uint16_t b = grub_le_to_cpu16 (data->sblock.group_desc_size);
      for (data->log_group_desc_size = 0; b != (1 << data->log_group_desc_size);
	   data->log_group_desc_size++);
    }
  else
    data->log_group_desc_size = 5;

  data->disk = disk;

  data->diropen.data = data;
  data->diropen.ino = 2;
  data->diropen.inode_read = 1;

  data->inode = &data->diropen.inode;

  grub_ext2_read_inode (data, 2, data->inode);
  if (grub_errno)
    goto fail;

  return data;

 fail:
  if (grub_errno == GRUB_ERR_OUT_OF_RANGE)
    grub_error (GRUB_ERR_BAD_FS, "not an ext2 filesystem");

  grub_free (data);
  return 0;
}

static char *
grub_ext2_read_symlink (grub_fshelp_node_t node)
{
  char *symlink;
  struct grub_fshelp_node *diro = node;
  grub_size_t sz;

  if (! diro->inode_read)
    {
      grub_ext2_read_inode (diro->data, diro->ino, &diro->inode);
      if (grub_errno)
	return 0;

      if (diro->inode.flags & grub_cpu_to_le32_compile_time (EXT4_ENCRYPT_FLAG))
       {
         grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET, "symlink is encrypted");
         return 0;
       }
    }

  if (grub_add (grub_le_to_cpu32 (diro->inode.size), 1, &sz))
    {
      grub_error (GRUB_ERR_OUT_OF_RANGE, N_("overflow is detected"));
      return NULL;
    }

  symlink = grub_malloc (sz);
  if (! symlink)
    return 0;

  /* If the filesize of the symlink is bigger than
     60 the symlink is stored in a separate block,
     otherwise it is stored in the inode.  */
  if (grub_le_to_cpu32 (diro->inode.size) < sizeof (diro->inode.symlink))
    grub_memcpy (symlink,
		 diro->inode.symlink,
		 grub_le_to_cpu32 (diro->inode.size));
  else
    {
      grub_ext2_read_file (diro, 0, 0, 0, 0,
			   grub_le_to_cpu32 (diro->inode.size),
			   symlink);
      if (grub_errno)
	{
	  grub_free (symlink);
	  return 0;
	}
    }

  symlink[grub_le_to_cpu32 (diro->inode.size)] = '\0';
  return symlink;
}

static int
grub_ext2_iterate_dir (grub_fshelp_node_t dir,
		       grub_fshelp_iterate_dir_hook_t hook, void *hook_data)
{
  unsigned int fpos = 0;
  struct grub_fshelp_node *diro = (struct grub_fshelp_node *) dir;

  if (! diro->inode_read)
    {
      grub_ext2_read_inode (diro->data, diro->ino, &diro->inode);
      if (grub_errno)
	return 0;
    }

  if (diro->inode.flags & grub_cpu_to_le32_compile_time (EXT4_ENCRYPT_FLAG))
    {
      grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET, "directory is encrypted");
      return 0;
    }

  /* Search the file.  */
  while (fpos < grub_le_to_cpu32 (diro->inode.size))
    {
      struct ext2_dirent dirent;

      grub_ext2_read_file (diro, 0, 0, 0, fpos, sizeof (struct ext2_dirent),
			   (char *) &dirent);
      if (grub_errno)
	return 0;

      if (dirent.direntlen == 0)
        return 0;

      if (dirent.inode != 0 && dirent.namelen != 0)
	{
	  char filename[MAX_NAMELEN + 1];
	  struct grub_fshelp_node *fdiro;
	  enum grub_fshelp_filetype type = GRUB_FSHELP_UNKNOWN;

	  grub_ext2_read_file (diro, 0, 0, 0, fpos + sizeof (struct ext2_dirent),
			       dirent.namelen, filename);
	  if (grub_errno)
	    return 0;

	  fdiro = grub_malloc (sizeof (struct grub_fshelp_node));
	  if (! fdiro)
	    return 0;

	  fdiro->data = diro->data;
	  fdiro->ino = grub_le_to_cpu32 (dirent.inode);

	  filename[dirent.namelen] = '\0';

	  if (dirent.filetype != FILETYPE_UNKNOWN)
	    {
	      fdiro->inode_read = 0;

	      if (dirent.filetype == FILETYPE_DIRECTORY)
		type = GRUB_FSHELP_DIR;
	      else if (dirent.filetype == FILETYPE_SYMLINK)
		type = GRUB_FSHELP_SYMLINK;
	      else if (dirent.filetype == FILETYPE_REG)
		type = GRUB_FSHELP_REG;
	    }
	  else
	    {
	      /* The filetype can not be read from the dirent, read
		 the inode to get more information.  */
	      grub_ext2_read_inode (diro->data,
                                    grub_le_to_cpu32 (dirent.inode),
				    &fdiro->inode);
	      if (grub_errno)
		{
		  grub_free (fdiro);
		  return 0;
		}

	      fdiro->inode_read = 1;

	      if ((grub_le_to_cpu16 (fdiro->inode.mode)
		   & FILETYPE_INO_MASK) == FILETYPE_INO_DIRECTORY)
		type = GRUB_FSHELP_DIR;
	      else if ((grub_le_to_cpu16 (fdiro->inode.mode)
			& FILETYPE_INO_MASK) == FILETYPE_INO_SYMLINK)
		type = GRUB_FSHELP_SYMLINK;
	      else if ((grub_le_to_cpu16 (fdiro->inode.mode)
			& FILETYPE_INO_MASK) == FILETYPE_INO_REG)
		type = GRUB_FSHELP_REG;
	    }

	  if (hook (filename, type, fdiro, hook_data))
	    return 1;
	}

      fpos += grub_le_to_cpu16 (dirent.direntlen);
    }

  return 0;
}

/* Open a file named NAME and initialize FILE.  */
static grub_err_t
grub_ext2_open (struct grub_file *file, const char *name)
{
  struct grub_ext2_data *data;
  struct grub_fshelp_node *fdiro = 0;
  grub_err_t err;

  grub_dl_ref (my_mod);

  data = grub_ext2_mount (file->device->disk);
  if (! data)
    {
      err = grub_errno;
      goto fail;
    }

  err = grub_fshelp_find_file (name, &data->diropen, &fdiro,
			       grub_ext2_iterate_dir,
			       grub_ext2_read_symlink, GRUB_FSHELP_REG);
  if (err)
    goto fail;

  if (! fdiro->inode_read)
    {
      err = grub_ext2_read_inode (data, fdiro->ino, &fdiro->inode);
      if (err)
	goto fail;
    }

  if (fdiro->inode.flags & grub_cpu_to_le32_compile_time (EXT4_ENCRYPT_FLAG))
    {
      err = grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET, "file is encrypted");
      goto fail;
    }

  grub_memcpy (data->inode, &fdiro->inode, sizeof (struct grub_ext2_inode));
  grub_free (fdiro);

  file->size = grub_le_to_cpu32 (data->inode->size);
  file->size |= ((grub_off_t) grub_le_to_cpu32 (data->inode->size_high)) << 32;
  file->data = data;
  file->offset = 0;

  return 0;

 fail:
  if (fdiro != &data->diropen)
    grub_free (fdiro);
  grub_free (data);

  grub_dl_unref (my_mod);

  return err;
}

static grub_err_t
grub_ext2_close (grub_file_t file)
{
  grub_free (file->data);

  grub_dl_unref (my_mod);

  return GRUB_ERR_NONE;
}

/* Read LEN bytes data from FILE into BUF.  */
static grub_ssize_t
grub_ext2_read (grub_file_t file, char *buf, grub_size_t len)
{
  struct grub_ext2_data *data = (struct grub_ext2_data *) file->data;

  return grub_ext2_read_file (&data->diropen,
			      file->read_hook, file->read_hook_data, file->blocklist,
			      file->offset, len, buf);
}


/* Context for grub_ext2_dir.  */
struct grub_ext2_dir_ctx
{
  grub_fs_dir_hook_t hook;
  void *hook_data;
  struct grub_ext2_data *data;
};

/* Helper for grub_ext2_dir.  */
static int
grub_ext2_dir_iter (const char *filename, enum grub_fshelp_filetype filetype,
		    grub_fshelp_node_t node, void *data)
{
  struct grub_ext2_dir_ctx *ctx = data;
  struct grub_dirhook_info info;

  grub_memset (&info, 0, sizeof (info));
  if (! node->inode_read)
    {
      grub_ext2_read_inode (ctx->data, node->ino, &node->inode);
      if (!grub_errno)
	node->inode_read = 1;
      grub_errno = GRUB_ERR_NONE;
    }
  if (node->inode_read)
    {
      info.mtimeset = 1;
      info.mtime = grub_le_to_cpu32 (node->inode.mtime);
    }

  info.dir = ((filetype & GRUB_FSHELP_TYPE_MASK) == GRUB_FSHELP_DIR);
  grub_free (node);
  return ctx->hook (filename, &info, ctx->hook_data);
}

static grub_err_t
grub_ext2_dir (grub_device_t device, const char *path, grub_fs_dir_hook_t hook,
	       void *hook_data)
{
  struct grub_ext2_dir_ctx ctx = {
    .hook = hook,
    .hook_data = hook_data
  };
  struct grub_fshelp_node *fdiro = 0;

  grub_dl_ref (my_mod);

  ctx.data = grub_ext2_mount (device->disk);
  if (! ctx.data)
    goto fail;

  grub_fshelp_find_file (path, &ctx.data->diropen, &fdiro,
			 grub_ext2_iterate_dir, grub_ext2_read_symlink,
			 GRUB_FSHELP_DIR);
  if (grub_errno)
    goto fail;

  grub_ext2_iterate_dir (fdiro, grub_ext2_dir_iter, &ctx);

 fail:
  if (fdiro != &ctx.data->diropen)
    grub_free (fdiro);
  grub_free (ctx.data);

  grub_dl_unref (my_mod);

  return grub_errno;
}

static grub_err_t
grub_ext2_label (grub_device_t device, char **label)
{
  struct grub_ext2_data *data;
  grub_disk_t disk = device->disk;

  grub_dl_ref (my_mod);

  data = grub_ext2_mount (disk);
  if (data)
    *label = grub_strndup (data->sblock.volume_name,
			   sizeof (data->sblock.volume_name));
  else
    *label = NULL;

  grub_dl_unref (my_mod);

  grub_free (data);

  return grub_errno;
}

static grub_err_t
grub_ext2_uuid (grub_device_t device, char **uuid)
{
  struct grub_ext2_data *data;
  grub_disk_t disk = device->disk;

  grub_dl_ref (my_mod);

  data = grub_ext2_mount (disk);
  if (data)
    {
      *uuid = grub_xasprintf ("%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
			     grub_be_to_cpu16 (data->sblock.uuid[0]),
			     grub_be_to_cpu16 (data->sblock.uuid[1]),
			     grub_be_to_cpu16 (data->sblock.uuid[2]),
			     grub_be_to_cpu16 (data->sblock.uuid[3]),
			     grub_be_to_cpu16 (data->sblock.uuid[4]),
			     grub_be_to_cpu16 (data->sblock.uuid[5]),
			     grub_be_to_cpu16 (data->sblock.uuid[6]),
			     grub_be_to_cpu16 (data->sblock.uuid[7]));
    }
  else
    *uuid = NULL;

  grub_dl_unref (my_mod);

  grub_free (data);

  return grub_errno;
}

/* Get mtime.  */
static grub_err_t
grub_ext2_mtime (grub_device_t device, grub_int64_t *tm)
{
  struct grub_ext2_data *data;
  grub_disk_t disk = device->disk;

  grub_dl_ref (my_mod);

  data = grub_ext2_mount (disk);
  if (!data)
    *tm = 0;
  else
    *tm = grub_le_to_cpu32 (data->sblock.utime);

  grub_dl_unref (my_mod);

  grub_free (data);

  return grub_errno;

}



static struct grub_fs grub_ext2_fs =
  {
    .name = "ext",
    .fs_dir = grub_ext2_dir,
    .fs_open = grub_ext2_open,
    .fs_read = grub_ext2_read,
    .fs_close = grub_ext2_close,
    .fs_label = grub_ext2_label,
    .fs_uuid = grub_ext2_uuid,
    .fs_mtime = grub_ext2_mtime,
#ifdef GRUB_UTIL
    .reserved_first_sector = 1,
    .blocklist_install = 1,
#endif
    .fast_blocklist = 1,
    .next = 0
  };

GRUB_MOD_INIT(ext2)
{
  grub_fs_register (&grub_ext2_fs);
  my_mod = mod;
}

GRUB_MOD_FINI(ext2)
{
  grub_fs_unregister (&grub_ext2_fs);
}
