/* hostdisk.c - emulate biosdisk */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2006,2007,2008,2009,2010,2011,2012,2013  Free Software Foundation, Inc.
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

#include <grub/disk.h>
#include <grub/partition.h>
#include <grub/msdos_partition.h>
#include <grub/types.h>
#include <grub/err.h>
#include <grub/emu/misc.h>
#include <grub/emu/hostdisk.h>
#include <grub/emu/getroot.h>
#include <grub/misc.h>
#include <grub/i18n.h>
#include <grub/list.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#if !defined (__CYGWIN__) && !defined (__MINGW32__) && !defined (__AROS__)

#ifdef __linux__
# include <sys/ioctl.h>         /* ioctl */
# include <sys/mount.h>
#endif /* __linux__ */

grub_uint64_t
grub_util_get_fd_size (grub_util_fd_t fd, const char *name, unsigned *log_secsize)
{
  struct stat st;
  grub_int64_t ret = -1;

  if (fstat (fd, &st) < 0)
    /* TRANSLATORS: "stat" comes from the name of POSIX function.  */
    grub_util_error (_("cannot stat `%s': %s"), name, strerror (errno));
#if GRUB_DISK_DEVS_ARE_CHAR
  if (S_ISCHR (st.st_mode))
#else
  if (S_ISBLK (st.st_mode))
#endif
    ret = grub_util_get_fd_size_os (fd, name, log_secsize);
  if (ret != -1LL)
    return ret;

  if (log_secsize)
    *log_secsize = 9;

  return st.st_size;
}

int
grub_util_fd_seek (grub_util_fd_t fd, grub_uint64_t off)
{
#if SIZEOF_OFF_T == 8
  off_t offset = (off_t) off;

  if (lseek (fd, offset, SEEK_SET) != offset)
    return -1;
#elif SIZEOF_OFF64_T == 8
  off64_t offset = (off64_t) off;

  if (lseek64 (fd, offset, SEEK_SET) != offset)
    return -1;
#else
#error "No large file support"
#endif
  return 0;
}


/* Read LEN bytes from FD in BUF. Return less than or equal to zero if an
   error occurs, otherwise return LEN.  */
ssize_t
grub_util_fd_read (grub_util_fd_t fd, char *buf, size_t len)
{
  ssize_t size = 0;

  while (len)
    {
      ssize_t ret = read (fd, buf, len);

      if (ret == 0)
	break;

      if (ret < 0)
        {
          if (errno == EINTR)
            continue;
          else
            return ret;
        }

      len -= ret;
      buf += ret;
      size += ret;
    }

  return size;
}

/* Write LEN bytes from BUF to FD. Return less than or equal to zero if an
   error occurs, otherwise return LEN.  */
ssize_t
grub_util_fd_write (grub_util_fd_t fd, const char *buf, size_t len)
{
  ssize_t size = 0;

  while (len)
    {
      ssize_t ret = write (fd, buf, len);

      if (ret == 0)
	break;

      if (ret < 0)
        {
          if (errno == EINTR)
            continue;
          else
            return ret;
        }

      len -= ret;
      buf += ret;
      size += ret;
    }

  return size;
}

#if !defined (__NetBSD__) && !defined (__APPLE__) && !defined (__FreeBSD__) && !defined(__FreeBSD_kernel__)
grub_util_fd_t
grub_util_fd_open (const char *os_dev, int flags)
{
#ifdef O_LARGEFILE
  flags |= O_LARGEFILE;
#endif
#ifdef O_BINARY
  flags |= O_BINARY;
#endif
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif

  return open (os_dev, flags, S_IROTH | S_IRGRP | S_IRUSR | S_IWUSR);
}
#endif

const char *
grub_util_fd_strerror (void)
{
  return strerror (errno);
}

static int allow_fd_syncs = 1;

int
grub_util_fd_sync (grub_util_fd_t fd)
{
  if (allow_fd_syncs)
    return fsync (fd);
  return 0;
}

int
grub_util_file_sync (FILE *f)
{
  if (fflush (f) != 0)
    return -1;
  if (!allow_fd_syncs)
    return 0;
  return fsync (fileno (f));
}

void
grub_util_disable_fd_syncs (void)
{
  allow_fd_syncs = 0;
}

int
grub_util_fd_close (grub_util_fd_t fd)
{
  return close (fd);
}

char *
grub_canonicalize_file_name (const char *path)
{
#if defined (PATH_MAX)
  char *ret;

  ret = xmalloc (PATH_MAX);
  if (!realpath (path, ret))
    return NULL;
  return ret;
#else
  return realpath (path, NULL);
#endif
}

FILE *
grub_util_fopen (const char *path, const char *mode)
{
  return fopen (path, mode);
}

int
grub_util_is_directory (const char *path)
{
  struct stat st;

  if (stat (path, &st) == -1)
    return 0;

  return S_ISDIR (st.st_mode);
}

int
grub_util_is_regular (const char *path)
{
  struct stat st;

  if (stat (path, &st) == -1)
    return 0;

  return S_ISREG (st.st_mode);
}

grub_uint32_t
grub_util_get_mtime (const char *path)
{
  struct stat st;

  if (stat (path, &st) == -1)
    return 0;

  return st.st_mtime;
}

#endif

#if defined (__CYGWIN__) || (!defined (__MINGW32__) && !defined (__AROS__))

int
grub_util_is_special_file (const char *path)
{
  struct stat st;

  if (lstat (path, &st) == -1)
    return 1;
  return (!S_ISREG (st.st_mode) && !S_ISDIR (st.st_mode));
}


char *
grub_util_make_temporary_file (void)
{
  const char *t = getenv ("TMPDIR");
  size_t tl;
  char *tmp;
  if (!t)
    t = "/tmp";
  tl = strlen (t);
  tmp = xmalloc (tl + sizeof ("/grub.XXXXXX"));
  memcpy (tmp, t, tl);
  memcpy (tmp + tl, "/grub.XXXXXX",
	  sizeof ("/grub.XXXXXX"));
  if (mkstemp (tmp) == -1)
    grub_util_error (_("cannot make temporary file: %s"), strerror (errno));
  return tmp;
}

char *
grub_util_make_temporary_dir (void)
{
  const char *t = getenv ("TMPDIR");
  size_t tl;
  char *tmp;
  if (!t)
    t = "/tmp";
  tl = strlen (t);
  tmp = xmalloc (tl + sizeof ("/grub.XXXXXX"));
  memcpy (tmp, t, tl);
  memcpy (tmp + tl, "/grub.XXXXXX",
	  sizeof ("/grub.XXXXXX"));
  if (!mkdtemp (tmp))
    grub_util_error (_("cannot make temporary directory: %s"),
		     strerror (errno));
  return tmp;
}

#endif
