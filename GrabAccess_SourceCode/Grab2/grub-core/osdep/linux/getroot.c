/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2006,2007,2008,2009,2010,2011,2012,2013  Free Software Foundation, Inc.
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

#include <config-util.h>
#include <config.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#include <grub/osdep/major.h>
#include <grub/types.h>
#include <sys/ioctl.h>         /* ioctl */
#include <sys/mount.h>

#include <grub/util/misc.h>

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/emu/misc.h>
#include <grub/emu/hostdisk.h>
#include <grub/emu/getroot.h>

#include <sys/wait.h>

#include <linux/types.h>
#include <linux/major.h>
#include <linux/raid/md_p.h>
#include <linux/raid/md_u.h>
#include <grub/i18n.h>
#include <grub/emu/exec.h>
#include <grub/btrfs.h>

#define LVM_DEV_MAPPER_STRING "/dev/mapper/"

/* Defines taken from btrfs/ioctl.h.  */

struct btrfs_ioctl_dev_info_args
{
  grub_uint64_t devid;
  grub_uint8_t uuid[16];
  grub_uint64_t bytes_used;
  grub_uint64_t total_bytes;
  grub_uint64_t unused[379];
  grub_uint8_t path[1024];
};

struct btrfs_ioctl_fs_info_args
{
  grub_uint64_t max_id;
  grub_uint64_t num_devices;
  grub_uint8_t fsid[16];
  grub_uint64_t reserved[124];
};

struct btrfs_ioctl_ino_lookup_args
{
  grub_uint64_t treeid;
  grub_uint64_t objectid;
  char name[4080];
};

struct btrfs_ioctl_search_key
{
  grub_uint64_t tree_id;
  grub_uint64_t min_objectid;
  grub_uint64_t max_objectid;
  grub_uint64_t min_offset;
  grub_uint64_t max_offset;
  grub_uint64_t min_transid;
  grub_uint64_t max_transid;
  grub_uint32_t min_type;
  grub_uint32_t max_type;
  grub_uint32_t nr_items;
  grub_uint32_t unused[9];
};

struct btrfs_ioctl_search_header {
  grub_uint64_t transid;
  grub_uint64_t objectid;
  grub_uint64_t offset;
  grub_uint32_t type;
  grub_uint32_t len;
};

struct btrfs_ioctl_search_args {
  struct btrfs_ioctl_search_key key;
  grub_uint64_t buf[(4096 - sizeof(struct btrfs_ioctl_search_key))
		    / sizeof (grub_uint64_t)];
};

#define BTRFS_IOC_TREE_SEARCH _IOWR(0x94, 17, \
				   struct btrfs_ioctl_search_args)
#define BTRFS_IOC_INO_LOOKUP _IOWR(0x94, 18, \
				   struct btrfs_ioctl_ino_lookup_args)
#define BTRFS_IOC_DEV_INFO _IOWR(0x94, 30, \
                                 struct btrfs_ioctl_dev_info_args)
#define BTRFS_IOC_FS_INFO _IOR(0x94, 31, \
                               struct btrfs_ioctl_fs_info_args)

static int
grub_util_is_imsm (const char *os_dev);


#define ESCAPED_PATH_MAX (4 * PATH_MAX)
struct mountinfo_entry
{
  int id;
  int major, minor;
  char enc_root[ESCAPED_PATH_MAX + 1], enc_path[ESCAPED_PATH_MAX + 1];
  char fstype[ESCAPED_PATH_MAX + 1], device[ESCAPED_PATH_MAX + 1];
};

/*
 * GET_DISK_INFO nr_disks (total count) does not map to disk.number,
 * which is an internal kernel index. Instead, do what mdadm does
 * and keep scanning until we find enough valid disks. The limit is
 * copied from there, which notes that it is sufficiently high given
 * that the on-disk metadata for v1.x can only support 1920.
 */
#define MD_MAX_DISKS       4096

static char **
grub_util_raid_getmembers (const char *name, int bootable)
{
  int fd, ret, i, j;
  int remaining;
  char **devicelist;
  mdu_version_t version;
  mdu_array_info_t info;
  mdu_disk_info_t disk;

  fd = open (name, O_RDONLY);

  if (fd == -1)
    grub_util_error (_("cannot open `%s': %s"), name, strerror (errno));

  ret = ioctl (fd, RAID_VERSION, &version);
  if (ret != 0)
    grub_util_error (_("ioctl RAID_VERSION error: %s"), strerror (errno));

  if ((version.major != 0 || version.minor != 90)
      && (version.major != 1 || version.minor != 0)
      && (version.major != 1 || version.minor != 1)
      && (version.major != 1 || version.minor != 2))
    grub_util_error (_("unsupported RAID version: %d.%d"),
		     version.major, version.minor);

  if (bootable && (version.major != 0 || version.minor != 90))
    grub_util_error (_("unsupported RAID version: %d.%d"),
		     version.major, version.minor);

  ret = ioctl (fd, GET_ARRAY_INFO, &info);
  if (ret != 0)
    grub_util_error (_("ioctl GET_ARRAY_INFO error: %s"), strerror (errno));

  devicelist = xcalloc (info.nr_disks + 1, sizeof (char *));

  remaining = info.nr_disks;
  for (i = 0, j = 0; i < MD_MAX_DISKS && remaining > 0; i++)
    {
      disk.number = i;
      ret = ioctl (fd, GET_DISK_INFO, &disk);
      if (ret != 0)
	grub_util_error (_("ioctl GET_DISK_INFO error: %s"), strerror (errno));

      /* Skip: MD_DISK_REMOVED slots don't contribute to "remaining" count. */
      if (disk.state & (1 << MD_DISK_REMOVED))
	continue;
      remaining--;

      /* Only record disks that are actively participating in the array. */
      if (disk.state & (1 << MD_DISK_ACTIVE))
        devicelist[j++] = grub_find_device (NULL, makedev (disk.major, disk.minor));
    }

  devicelist[j] = NULL;

  close (fd);

  return devicelist;
}

/* Statting something on a btrfs filesystem always returns a virtual device
   major/minor pair rather than the real underlying device, because btrfs
   can span multiple underlying devices (and even if it's currently only
   using a single device it can be dynamically extended onto another).  We
   can't deal with the multiple-device case yet, but in the meantime, we can
   at least cope with the single-device case by scanning
   /proc/self/mountinfo.  */
static void
unescape (char *str)
{
  char *optr;
  const char *iptr;
  for (iptr = optr = str; *iptr; optr++)
    {
      if (iptr[0] == '\\' && iptr[1] >= '0' && iptr[1] < '8'
	  && iptr[2] >= '0' && iptr[2] < '8'
	  && iptr[3] >= '0' && iptr[3] < '8')
	{
	  *optr = (((iptr[1] - '0') << 6) | ((iptr[2] - '0') << 3)
		   | (iptr[3] - '0'));
	  iptr += 4;
	}
      else
	*optr = *iptr++;
    }
  *optr = 0;
}

static char **
grub_find_root_devices_from_btrfs (const char *dir)
{
  int fd;
  struct btrfs_ioctl_fs_info_args fsi;
  int i, j = 0;
  char **ret;

  fd = open (dir, 0);
  if (fd < 0)
    return NULL;

  if (ioctl (fd, BTRFS_IOC_FS_INFO, &fsi) < 0)
    {
      close (fd);
      return NULL;
    }

  ret = xcalloc (fsi.num_devices + 1, sizeof (ret[0]));

  for (i = 1; i <= fsi.max_id && j < fsi.num_devices; i++)
    {
      struct btrfs_ioctl_dev_info_args devi;
      memset (&devi, 0, sizeof (devi));
      devi.devid = i;
      if (ioctl (fd, BTRFS_IOC_DEV_INFO, &devi) < 0)
	{
	  close (fd);
	  free (ret);
	  return NULL;
	}
      ret[j++] = xstrdup ((char *) devi.path);
      if (j >= fsi.num_devices)
	break;
    }
  close (fd);
  ret[j] = 0;
  return ret;
}

static char *
get_btrfs_fs_prefix (const char *mount_path)
{
  struct btrfs_ioctl_ino_lookup_args args;
  struct stat st;
  int fd;
  grub_uint64_t tree_id, inode_id;
  char *ret = NULL;

  fd = open (mount_path, O_RDONLY);
	  
  if (fd < 0)
    return NULL;
  memset (&args, 0, sizeof(args));
  args.objectid = GRUB_BTRFS_TREE_ROOT_OBJECTID;
  
  if (ioctl (fd, BTRFS_IOC_INO_LOOKUP, &args) < 0)
    goto fail;
  tree_id = args.treeid;

  if (fstat (fd, &st) < 0)
    goto fail;
  inode_id = st.st_ino;

  while (tree_id != GRUB_BTRFS_ROOT_VOL_OBJECTID
	 || inode_id != GRUB_BTRFS_TREE_ROOT_OBJECTID)
    {
      const char *name;
      size_t namelen;
      struct btrfs_ioctl_search_args sargs;
      char *old;

      memset (&sargs, 0, sizeof(sargs));

      if (inode_id == GRUB_BTRFS_TREE_ROOT_OBJECTID)
	{
	  struct grub_btrfs_root_backref *br;

	  sargs.key.tree_id = 1;
	  sargs.key.min_objectid = tree_id;
	  sargs.key.max_objectid = tree_id;

	  sargs.key.min_offset = 0;
	  sargs.key.max_offset = ~0ULL;
	  sargs.key.min_transid = 0;
	  sargs.key.max_transid = ~0ULL;
	  sargs.key.min_type = GRUB_BTRFS_ITEM_TYPE_ROOT_BACKREF;
	  sargs.key.max_type = GRUB_BTRFS_ITEM_TYPE_ROOT_BACKREF;

	  sargs.key.nr_items = 1;

	  if (ioctl (fd, BTRFS_IOC_TREE_SEARCH, &sargs) < 0)
	    goto fail;

	  if (sargs.key.nr_items == 0)
	    goto fail;

	  tree_id = sargs.buf[2];
	  br = (struct grub_btrfs_root_backref *) (sargs.buf + 4);
	  inode_id = grub_le_to_cpu64 (br->inode_id);
	  name = br->name;
	  namelen = grub_le_to_cpu16 (br->n);
	}
      else
	{
	  struct grub_btrfs_inode_ref *ir;

	  sargs.key.tree_id = tree_id;
	  sargs.key.min_objectid = inode_id;
	  sargs.key.max_objectid = inode_id;

	  sargs.key.min_offset = 0;
	  sargs.key.max_offset = ~0ULL;
	  sargs.key.min_transid = 0;
	  sargs.key.max_transid = ~0ULL;
	  sargs.key.min_type = GRUB_BTRFS_ITEM_TYPE_INODE_REF;
	  sargs.key.max_type = GRUB_BTRFS_ITEM_TYPE_INODE_REF;

	  if (ioctl (fd, BTRFS_IOC_TREE_SEARCH, &sargs) < 0)
	    goto fail;

	  if (sargs.key.nr_items == 0)
	    goto fail;

	  inode_id = sargs.buf[2];

	  ir = (struct grub_btrfs_inode_ref *) (sargs.buf + 4);
	  name = ir->name;
	  namelen = grub_le_to_cpu16 (ir->n);
	}
      old = ret;
      ret = xmalloc (namelen + (old ? strlen (old) : 0) + 2);
      ret[0] = '/';
      memcpy (ret + 1, name, namelen);
      if (old)
	{
	  strcpy (ret + 1 + namelen, old);
	  free (old);
	}
      else
	ret[1+namelen] = '\0';
    }
  if (!ret)
    ret = xstrdup ("/");
  close (fd);
  return ret;

 fail:
  free (ret);
  close (fd);
  return NULL;
}

int use_relative_path_on_btrfs = 0;

static char *
get_btrfs_subvol (const char *path)
{
  struct btrfs_ioctl_ino_lookup_args args;
  grub_uint64_t tree_id;
  int fd = -1;
  char *ret = NULL;

  fd = open (path, O_RDONLY);

  if (fd < 0)
    return NULL;

  memset (&args, 0, sizeof(args));
  args.objectid = GRUB_BTRFS_TREE_ROOT_OBJECTID;

  if (ioctl (fd, BTRFS_IOC_INO_LOOKUP, &args) < 0)
    goto error;

  tree_id = args.treeid;

  while (tree_id != GRUB_BTRFS_ROOT_VOL_OBJECTID)
    {
      struct btrfs_ioctl_search_args sargs;
      struct grub_btrfs_root_backref *br;
      struct btrfs_ioctl_search_header *search_header;
      char *old;
      grub_uint16_t len;
      grub_uint64_t inode_id;

      memset (&sargs, 0, sizeof(sargs));

      sargs.key.tree_id = 1;
      sargs.key.min_objectid = tree_id;
      sargs.key.max_objectid = tree_id;

      sargs.key.min_offset = 0;
      sargs.key.max_offset = ~0ULL;
      sargs.key.min_transid = 0;
      sargs.key.max_transid = ~0ULL;
      sargs.key.min_type = GRUB_BTRFS_ITEM_TYPE_ROOT_BACKREF;
      sargs.key.max_type = GRUB_BTRFS_ITEM_TYPE_ROOT_BACKREF;

      sargs.key.nr_items = 1;

      if (ioctl (fd, BTRFS_IOC_TREE_SEARCH, &sargs) < 0)
	goto error;

      if (sargs.key.nr_items == 0)
	goto error;

      search_header = (struct btrfs_ioctl_search_header *)sargs.buf;
      br = (struct grub_btrfs_root_backref *) (search_header + 1);

      len = grub_le_to_cpu16 (br->n);
      inode_id = grub_le_to_cpu64 (br->inode_id);
      tree_id = search_header->offset;

      old = ret;
      ret = malloc (len + 1);
      memcpy (ret, br->name, len);
      ret[len] = '\0';

      if (inode_id != GRUB_BTRFS_TREE_ROOT_OBJECTID)
	{
	  char *s;

	  memset(&args, 0, sizeof(args));
	  args.treeid = search_header->offset;
	  args.objectid = inode_id;

	  if (ioctl (fd, BTRFS_IOC_INO_LOOKUP, &args) < 0)
	    goto error;

	  s = xasprintf ("%s%s", args.name, ret);
	  free (ret);
	  ret = s;
	}

      if (old)
	{
	  char *s = xasprintf ("%s/%s", ret, old);
	  free (ret);
	  free (old);
	  ret = s;
	}
    }

  close (fd);
  return ret;

error:

  if (fd >= 0)
    close (fd);
  if (ret)
    free (ret);

  return NULL;
}

void (*grub_find_root_btrfs_mount_path_hook)(const char *mount_path);

char **
grub_find_root_devices_from_mountinfo (const char *dir, char **relroot)
{
  FILE *fp = NULL;
  char *buf = NULL;
  size_t len = 0;
  grub_size_t entry_len, entry_max = 4;
  struct mountinfo_entry *entries;
  struct mountinfo_entry parent_entry = { 0, 0, 0, "", "", "", "" };
  int i;
  int retry = 0;
  int dir_fd = -1;
  char **ret = NULL;

  if (! *dir)
    dir = "/";
  if (relroot)
    *relroot = NULL;

  entries = xcalloc (entry_max, sizeof (*entries));

again:
  fp = grub_util_fopen ("/proc/self/mountinfo", "r");
  if (! fp)
    goto out; /* fall through to other methods */

  entry_len = 0;

  /* First, build a list of relevant visible mounts.  */
  while (getline (&buf, &len, fp) > 0)
    {
      struct mountinfo_entry entry;
      int count;
      size_t enc_path_len;
      const char *sep;

      if (sscanf (buf, "%d %d %u:%u %s %s%n",
		  &entry.id, &parent_entry.id, &entry.major, &entry.minor,
		  entry.enc_root, entry.enc_path, &count) < 6)
	continue;

      unescape (entry.enc_root);
      unescape (entry.enc_path);

      enc_path_len = strlen (entry.enc_path);
      /* Check that enc_path is a prefix of dir.  The prefix must either be
         the entire string, or end with a slash, or be immediately followed
         by a slash.  */
      if (strncmp (dir, entry.enc_path, enc_path_len) != 0 ||
	  (enc_path_len && dir[enc_path_len - 1] != '/' &&
	   dir[enc_path_len] && dir[enc_path_len] != '/'))
	continue;

      sep = strstr (buf + count, " - ");
      if (!sep)
	continue;

      sep += sizeof (" - ") - 1;
      if (sscanf (sep, "%s %s", entry.fstype, entry.device) != 2)
	continue;

      unescape (entry.device);

      /* Using the mount IDs, find out where this fits in the list of
	 visible mount entries we've seen so far.  There are three
	 interesting cases.  Firstly, it may be inserted at the end: this is
	 the usual case of /foo/bar being mounted after /foo.  Secondly, it
	 may be inserted at the start: for example, this can happen for
	 filesystems that are mounted before / and later moved under it.
	 Thirdly, it may occlude part or all of the existing filesystem
	 tree, in which case the end of the list needs to be pruned and this
	 new entry will be inserted at the end.  */
      if (entry_len >= entry_max)
	{
	  entry_max <<= 1;
	  entries = xrealloc (entries, entry_max * sizeof (*entries));
	}

      if (!entry_len)
	{
	  /* Initialise list.  */
	  entry_len = 2;
	  entries[0] = parent_entry;
	  entries[1] = entry;
	}
      else
	{
	  for (i = entry_len - 1; i >= 0; i--)
	    {
	      if (entries[i].id == parent_entry.id)
		{
		  /* Insert at end, pruning anything previously above this.  */
		  entry_len = i + 2;
		  entries[i + 1] = entry;
		  break;
		}
	      else if (i == 0 && entries[i].id == entry.id)
		{
		  /* Insert at start.  */
		  entry_len++;
		  memmove (entries + 1, entries,
			   (entry_len - 1) * sizeof (*entries));
		  entries[0] = parent_entry;
		  entries[1] = entry;
		  break;
		}
	    }
	}
    }

  /* Now scan visible mounts for the ones we're interested in.  */
  for (i = entry_len - 1; i >= 0; i--)
    {
      char *fs_prefix = NULL;
      if (!*entries[i].device)
	continue;

      if (grub_strcmp (entries[i].fstype, "fuse.zfs") == 0
	  || grub_strcmp (entries[i].fstype, "zfs") == 0)
	{
	  char *slash;
	  slash = strchr (entries[i].device, '/');
	  if (slash)
	    *slash = 0;
	  ret = grub_util_find_root_devices_from_poolname (entries[i].device);
	  if (slash)
	    *slash = '/';
	  if (relroot)
	    {
	      if (!slash)
		fs_prefix = xasprintf ("/@%s", entries[i].enc_root);
	      else if (strchr (slash + 1, '@'))
		fs_prefix = xasprintf ("/%s%s", slash + 1, entries[i].enc_root);
	      else
		fs_prefix = xasprintf ("/%s@%s", slash + 1,
				       entries[i].enc_root);
	    }
	}
      else if (grub_strcmp (entries[i].fstype, "btrfs") == 0)
	{
	  ret = grub_find_root_devices_from_btrfs (dir);
	  if (use_relative_path_on_btrfs)
	    {
	      fs_prefix = xstrdup ("/");
	      if (grub_find_root_btrfs_mount_path_hook)
		grub_find_root_btrfs_mount_path_hook (entries[i].enc_path);
	    }
	  else
	    {
	      fs_prefix = get_btrfs_fs_prefix (entries[i].enc_path);
	    }
	}
      else if (!retry && grub_strcmp (entries[i].fstype, "autofs") == 0)
	{
	  /* If the best match is automounted, try to trigger mount. We cannot
	     simply return here because stat() on automounted directory does not
	     trigger mount and returns bogus (pseudo)device number instead.
	     We keep mountpoint open until end of scan to prevent timeout. */

	  int flags = O_RDONLY|O_DIRECTORY;

	  fclose (fp);
#ifdef O_LARGEFILE
	  flags |= O_LARGEFILE;
#endif
	  dir_fd = open (entries[i].enc_path, flags);
	  retry = 1;
	  goto again;
	}
      if (!ret)
	{
	  ret = xmalloc (2 * sizeof (ret[0]));
	  ret[0] = strdup (entries[i].device);
	  ret[1] = 0;
	}
      if (!fs_prefix)
	fs_prefix = entries[i].enc_root;
      if (relroot)
	{
	  char *ptr;
	  grub_size_t enc_root_len = strlen (fs_prefix);
	  grub_size_t enc_path_len = strlen (entries[i].enc_path);
	  grub_size_t dir_strlen = strlen (dir);
	  *relroot = xmalloc (enc_root_len +
			      2 + dir_strlen);
	  ptr = grub_stpcpy (*relroot, fs_prefix);
	  if (dir_strlen > enc_path_len)
	    {
	      while (ptr > *relroot && *(ptr - 1) == '/')
		ptr--;
	      if (dir[enc_path_len] != '/')
		*ptr++ = '/';
	      ptr = grub_stpcpy (ptr, dir + enc_path_len);
	    }
	  *ptr = 0;
	}
      if (fs_prefix != entries[i].enc_root)
	free (fs_prefix);
      break;
    }

out:
  free (buf);
  free (entries);
  if (fp)
    fclose (fp);
  if (dir_fd != -1)
    close (dir_fd);
  return ret;
}

static char *
get_mdadm_uuid (const char *os_dev)
{
  const char *argv[5];
  int fd;
  pid_t pid;
  FILE *mdadm;
  char *buf = NULL;
  size_t len = 0;
  char *name = NULL;

  argv[0] = "mdadm";
  argv[1] = "--detail";
  argv[2] = "--export";
  argv[3] = os_dev;
  argv[4] = NULL;

  pid = grub_util_exec_pipe (argv, &fd);

  if (!pid)
    return NULL;

  /* Parent.  Read mdadm's output.  */
  mdadm = fdopen (fd, "r");
  if (! mdadm)
    {
      grub_util_warn (_("Unable to open stream from %s: %s"),
		      "mdadm", strerror (errno));
      goto out;
    }

  while (getline (&buf, &len, mdadm) > 0)
    {
      if (strncmp (buf, "MD_UUID=", sizeof ("MD_UUID=") - 1) == 0)
	{
	  char *name_start, *ptri, *ptro;
	  
	  free (name);
	  name_start = buf + sizeof ("MD_UUID=") - 1;
	  ptro = name = xmalloc (strlen (name_start) + 1);
	  for (ptri = name_start; *ptri && *ptri != '\n' && *ptri != '\r';
	       ptri++)
	    if ((*ptri >= '0' && *ptri <= '9')
		|| (*ptri >= 'a' && *ptri <= 'f')
		|| (*ptri >= 'A' && *ptri <= 'F'))
	      *ptro++ = *ptri;
	  *ptro = 0;
	}
    }

out:
  close (fd);
  waitpid (pid, NULL, 0);
  free (buf);

  return name;
}

static int
grub_util_is_imsm (const char *os_dev)
{
  int retry;
  int is_imsm = 0;
  int container_seen = 0;
  const char *dev = os_dev;

  do
    {
      const char *argv[5];
      int fd;
      pid_t pid;
      FILE *mdadm;
      char *buf = NULL;
      size_t len = 0;

      retry = 0; /* We'll do one more pass if device is part of container */

      argv[0] = "mdadm";
      argv[1] = "--detail";
      argv[2] = "--export";
      argv[3] = dev;
      argv[4] = NULL;

      pid = grub_util_exec_pipe (argv, &fd);

      if (!pid)
	{
	  if (dev != os_dev)
	    free ((void *) dev);
	  return 0;
	}

      /* Parent.  Read mdadm's output.  */
      mdadm = fdopen (fd, "r");
      if (! mdadm)
	{
	  grub_util_warn (_("Unable to open stream from %s: %s"),
			  "mdadm", strerror (errno));
	  close (fd);
	  waitpid (pid, NULL, 0);
	  if (dev != os_dev)
	    free ((void *) dev);
	  return 0;
	}

      while (getline (&buf, &len, mdadm) > 0)
	{
	  if (strncmp (buf, "MD_CONTAINER=", sizeof ("MD_CONTAINER=") - 1) == 0
	      && !container_seen)
	    {
	      char *newdev, *ptr;
	      newdev = xstrdup (buf + sizeof ("MD_CONTAINER=") - 1);
	      ptr = newdev + strlen (newdev) - 1;
	      for (; ptr >= newdev && (*ptr == '\n' || *ptr == '\r'); ptr--);
	      ptr[1] = 0;
	      grub_util_info ("Container of %s is %s", dev, newdev);
	      dev = newdev;
	      container_seen = retry = 1;
	      break;
	    }
	  if (strncmp (buf, "MD_METADATA=imsm",
		       sizeof ("MD_METADATA=imsm") - 1) == 0)
	    {
	      is_imsm = 1;
	      grub_util_info ("%s is imsm", dev);	      
	      break;
	    }
	}

      free (buf);
      close (fd);
      waitpid (pid, NULL, 0);
    }
  while (retry);

  if (dev != os_dev)
    free ((void *) dev);
  return is_imsm;
}

char *
grub_util_part_to_disk (const char *os_dev, struct stat *st,
			int *is_part)
{
  char *path;

  if (! S_ISBLK (st->st_mode))
    {
      *is_part = 0;
      return xstrdup (os_dev);
    }

  path = xmalloc (PATH_MAX);

  if (! realpath (os_dev, path))
    return NULL;

  if (strncmp ("/dev/", path, 5) == 0)
    {
      char *p = path + 5;

      /* If this is an IDE disk.  */
      if (strncmp ("ide/", p, 4) == 0)
	{
	  p = strstr (p, "part");
	  if (p)
	    {
	      *is_part = 1;
	      strcpy (p, "disc");
	    }

	  return path;
	}

      /* If this is a SCSI disk.  */
      if (strncmp ("scsi/", p, 5) == 0)
	{
	  p = strstr (p, "part");
	  if (p)
	    {
	      *is_part = 1;
	      strcpy (p, "disc");
	    }

	  return path;
	}

      /* If this is a DAC960 disk.  */
      if (strncmp ("rd/c", p, 4) == 0)
	{
	  /* /dev/rd/c[0-9]+d[0-9]+(p[0-9]+)? */
	  p = strchr (p, 'p');
	  if (p)
	    {
	      *is_part = 1;
	      *p = '\0';
	    }

	  return path;
	}

      /* If this is a Mylex AcceleRAID Array.  */
      if (strncmp ("rs/c", p, 4) == 0)
	{
	  /* /dev/rd/c[0-9]+d[0-9]+(p[0-9]+)? */
	  p = strchr (p, 'p');
	  if (p)
	    {
	      *is_part = 1;
	      *p = '\0';
	    }

	  return path;
	}
      /* If this is a CCISS disk.  */
      if (strncmp ("cciss/c", p, sizeof ("cciss/c") - 1) == 0)
	{
	  /* /dev/cciss/c[0-9]+d[0-9]+(p[0-9]+)? */
	  p = strchr (p, 'p');
	  if (p)
	    {
	      *is_part = 1;
	      *p = '\0';
	    }

	  return path;
	}

      /* If this is an AOE disk.  */
      if (strncmp ("etherd/e", p, sizeof ("etherd/e") - 1) == 0)
	{
	  /* /dev/etherd/e[0-9]+\.[0-9]+(p[0-9]+)? */
	  p = strchr (p, 'p');
	  if (p)
	    {
	      *is_part = 1;
	      *p = '\0';
	    }

	  return path;
	}

      /* If this is a Compaq Intelligent Drive Array.  */
      if (strncmp ("ida/c", p, sizeof ("ida/c") - 1) == 0)
	{
	  /* /dev/ida/c[0-9]+d[0-9]+(p[0-9]+)? */
	  p = strchr (p, 'p');
	  if (p)
	    {
	      *is_part = 1;
	      *p = '\0';
	    }

	  return path;
	}

      /* If this is an I2O disk.  */
      if (strncmp ("i2o/hd", p, sizeof ("i2o/hd") - 1) == 0)
      	{
	  /* /dev/i2o/hd[a-z]([0-9]+)? */
	  if (p[sizeof ("i2o/hda") - 1])
	    *is_part = 1;
	  p[sizeof ("i2o/hda") - 1] = '\0';
	  return path;
	}

      /* If this is a MultiMediaCard (MMC).  */
      if (strncmp ("mmcblk", p, sizeof ("mmcblk") - 1) == 0)
	{
	  /* /dev/mmcblk[0-9]+(p[0-9]+)? */
	  p = strchr (p, 'p');
	  if (p)
	    {
	      *is_part = 1;
	      *p = '\0';
	    }

	  return path;
	}

      if (strncmp ("md", p, 2) == 0
	  && p[2] >= '0' && p[2] <= '9')
	{
	  char *ptr = p + 2;
	  while (*ptr >= '0' && *ptr <= '9')
	    ptr++;
	  if (*ptr)
	    *is_part = 1;
	  *ptr = 0;
	  return path;
	}

      if (strncmp ("nbd", p, 3) == 0
	  && p[3] >= '0' && p[3] <= '9')
	{
	  char *ptr = p + 3;
	  while (*ptr >= '0' && *ptr <= '9')
	    ptr++;
	  if (*ptr)
	    *is_part = 1;
	  *ptr = 0;
	  return path;
	}

      /* If this is an IDE, SCSI or Virtio disk.  */
      if (strncmp ("vdisk", p, 5) == 0
	  && p[5] >= 'a' && p[5] <= 'z')
	{
	  /* /dev/vdisk[a-z][0-9]* */
	  if (p[6])
	    *is_part = 1;
	  p[6] = '\0';
	  return path;
	}
      if ((strncmp ("hd", p, 2) == 0
	   || strncmp ("vd", p, 2) == 0
	   || strncmp ("sd", p, 2) == 0)
	  && p[2] >= 'a' && p[2] <= 'z')
	{
	  char *pp = p + 2;
	  while (*pp >= 'a' && *pp <= 'z')
	    pp++;
	  if (*pp)
	    *is_part = 1;
	  /* /dev/[hsv]d[a-z]+[0-9]* */
	  *pp = '\0';
	  return path;
	}

      /* If this is a Xen virtual block device.  */
      if ((strncmp ("xvd", p, 3) == 0) && p[3] >= 'a' && p[3] <= 'z')
	{
	  char *pp = p + 3;
	  while (*pp >= 'a' && *pp <= 'z')
	    pp++;
	  if (*pp)
	    *is_part = 1;
	  /* /dev/xvd[a-z]+[0-9]* */
	  *pp = '\0';
	  return path;
	}

      /* If this is an rssd device. */
      if ((strncmp ("rssd", p, 4) == 0) && p[4] >= 'a' && p[4] <= 'z')
	{
	  char *pp = p + 4;
	  while (*pp >= 'a' && *pp <= 'z')
	    pp++;
	  if (*pp)
	    *is_part = 1;
	  /* /dev/rssd[a-z]+[0-9]* */
	  *pp = '\0';
	  return path;
	}

      /* If this is an rssd device. */
      if ((strncmp ("rssd", p, 4) == 0) && p[4] >= 'a' && p[4] <= 'z')
	{
	  char *pp = p + 4;
	  while (*pp >= 'a' && *pp <= 'z')
	    pp++;
	  if (*pp)
	    *is_part = 1;
	  /* /dev/rssd[a-z]+[0-9]* */
	  *pp = '\0';
	  return path;
	}

      /* If this is a loop device */
      if ((strncmp ("loop", p, 4) == 0) && p[4] >= '0' && p[4] <= '9')
	{
	  char *pp = p + 4;
	  while (*pp >= '0' && *pp <= '9')
	    pp++;
	  if (*pp == 'p')
	    *is_part = 1;
	  /* /dev/loop[0-9]+p[0-9]* */
	  *pp = '\0';
	  return path;
	}

      /* If this is a NVMe device */
      if ((strncmp ("nvme", p, 4) == 0) && p[4] >= '0' && p[4] <= '9')
	{
	  char *pp = p + 4;
	  while (*pp >= '0' && *pp <= '9')
	    pp++;
	  if (*pp == 'n')
	    pp++;
	  while (*pp >= '0' && *pp <= '9')
	    pp++;
	  if (*pp == 'p')
	    *is_part = 1;
	  /* /dev/nvme[0-9]+n[0-9]+p[0-9]* */
	  *pp = '\0';
	  return path;
	}
    }

  return path;
}

static char *
grub_util_get_raid_grub_dev (const char *os_dev)
{
  char *grub_dev = NULL;
  if (os_dev[7] == '_' && os_dev[8] == 'd')
    {
      /* This a partitionable RAID device of the form /dev/md_dNNpMM. */

      char *p, *q;

      p = strdup (os_dev + sizeof ("/dev/md_d") - 1);

      q = strchr (p, 'p');
      if (q)
	*q = ',';

      grub_dev = xasprintf ("md%s", p);
      free (p);
    }
  else if (os_dev[7] == '/' && os_dev[8] == 'd')
    {
      /* This a partitionable RAID device of the form /dev/md/dNNpMM. */

      char *p, *q;

      p = strdup (os_dev + sizeof ("/dev/md/d") - 1);

      q = strchr (p, 'p');
      if (q)
	*q = ',';

      grub_dev = xasprintf ("md%s", p);
      free (p);
    }
  else if (os_dev[7] >= '0' && os_dev[7] <= '9')
    {
      char *p , *q;

      p = strdup (os_dev + sizeof ("/dev/md") - 1);

      q = strchr (p, 'p');
      if (q)
	*q = ',';

      grub_dev = xasprintf ("md%s", p);
      free (p);
    }
  else if (os_dev[7] == '/' && os_dev[8] >= '0' && os_dev[8] <= '9')
    {
      char *p , *q;

      p = strdup (os_dev + sizeof ("/dev/md/") - 1);

      q = strchr (p, 'p');
      if (q)
	*q = ',';

      grub_dev = xasprintf ("md%s", p);
      free (p);
    }
  else if (os_dev[7] == '/')
    {
      /* mdraid 1.x with a free name.  */
      char *p , *q;

      p = strdup (os_dev + sizeof ("/dev/md/") - 1);

      q = strchr (p, 'p');
      if (q)
	*q = ',';

      grub_dev = xasprintf ("md/%s", p);
      free (p);
    }
  else
    grub_util_error (_("unknown kind of RAID device `%s'"), os_dev);

  {
    char *mdadm_name = get_mdadm_uuid (os_dev);

    if (mdadm_name)
      {
	const char *q;

	for (q = os_dev + strlen (os_dev) - 1; q >= os_dev
	       && grub_isdigit (*q); q--);

	if (q >= os_dev && *q == 'p')
	  {
	    free (grub_dev);
	    grub_dev = xasprintf ("mduuid/%s,%s", mdadm_name, q + 1);
	    goto done;
	  }
	free (grub_dev);
	grub_dev = xasprintf ("mduuid/%s", mdadm_name);

      done:
	free (mdadm_name);
      }
  }
  return grub_dev;
}

enum grub_dev_abstraction_types
grub_util_get_dev_abstraction_os (const char *os_dev)
{
#ifndef HAVE_DEVICE_MAPPER
  if ((strncmp ("/dev/mapper/", os_dev, 12) == 0))
    return GRUB_DEV_ABSTRACTION_LVM;
#endif

  /* Check for RAID.  */
  if (!strncmp (os_dev, "/dev/md", 7) && ! grub_util_device_is_mapped (os_dev)
      && !grub_util_is_imsm (os_dev))
    return GRUB_DEV_ABSTRACTION_RAID;
  return GRUB_DEV_ABSTRACTION_NONE;
}

int
grub_util_pull_device_os (const char *os_dev,
			  enum grub_dev_abstraction_types ab)
{
  switch (ab)
    {
    case GRUB_DEV_ABSTRACTION_RAID:
      {
	char **devicelist = grub_util_raid_getmembers (os_dev, 0);
	int i;
	for (i = 0; devicelist[i];i++)
	  {
	    grub_util_pull_device (devicelist[i]);
	    free (devicelist[i]);
	  }
	free (devicelist);
      }
      return 1;
    default:
      return 0;
    }
}

char *
grub_util_get_grub_dev_os (const char *os_dev)
{
  char *grub_dev = NULL;

  switch (grub_util_get_dev_abstraction (os_dev))
    {
      /* Fallback for non-devmapper build. In devmapper-builds LVM is handled
	 in grub_util_get_devmapper_grub_dev and this point isn't reached.
       */
    case GRUB_DEV_ABSTRACTION_LVM:
      {
	unsigned short len;
	grub_size_t offset = sizeof (LVM_DEV_MAPPER_STRING) - 1;

	len = strlen (os_dev) - offset + 1;
	grub_dev = xmalloc (len + sizeof ("lvm/"));

	grub_memcpy (grub_dev, "lvm/", sizeof ("lvm/") - 1);
	grub_memcpy (grub_dev + sizeof ("lvm/") - 1, os_dev + offset, len);
      }
      break;

    case GRUB_DEV_ABSTRACTION_RAID:
      grub_dev = grub_util_get_raid_grub_dev (os_dev);
      break;

    default:  /* GRUB_DEV_ABSTRACTION_NONE */
      break;
    }

  return grub_dev;
}


char *
grub_util_get_btrfs_subvol (const char *path, char **mount_path)
{
  char *mp = NULL;

  if (mount_path)
    *mount_path = NULL;

  auto void
  mount_path_hook (const char *m)
  {
    mp = strdup (m);
  }

  grub_find_root_btrfs_mount_path_hook = mount_path_hook;
  grub_free (grub_find_root_devices_from_mountinfo (path, NULL));
  grub_find_root_btrfs_mount_path_hook = NULL;

  if (!mp)
    return NULL;

  if (mount_path)
    *mount_path = mp;

  return get_btrfs_subvol (mp);
}

char *
grub_make_system_path_relative_to_its_root_os (const char *path)
{
  char *bind = NULL;
  grub_size_t len;
  grub_free (grub_find_root_devices_from_mountinfo (path, &bind));
  if (bind && bind[0])
    {
      len = strlen (bind);
      while (len > 0 && bind[len - 1] == '/')
	{
	  bind[len - 1] = '\0';
	  len--;
	}
      return bind;
    }
  grub_free (bind);
  return NULL;
}
