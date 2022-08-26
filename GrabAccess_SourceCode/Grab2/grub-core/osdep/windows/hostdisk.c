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
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include <grub/util/windows.h>
#include <grub/charset.h>

#include <windows.h>
#include <winioctl.h>
#include <wincrypt.h>

#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif

#if SIZEOF_TCHAR == 1

LPTSTR
grub_util_utf8_to_tchar (const char *in)
{
  return xstrdup (in);
}

char *
grub_util_tchar_to_utf8 (LPCTSTR in)
{
  return xstrdup (in);
}

#elif SIZEOF_TCHAR == 2

LPTSTR
grub_util_utf8_to_tchar (const char *in)
{
  LPTSTR ret;
  size_t ssz = strlen (in);
  size_t tsz = 2 * (GRUB_MAX_UTF16_PER_UTF8 * ssz + 1);
  ret = xmalloc (tsz);
  tsz = grub_utf8_to_utf16 (ret, tsz,
			    (const grub_uint8_t *) in, ssz, NULL);
  ret[tsz] = 0;
  return ret;
}

char *
grub_util_tchar_to_utf8 (LPCTSTR in)
{
  size_t ssz;
  for (ssz = 0; in[ssz]; ssz++);

  size_t tsz = GRUB_MAX_UTF8_PER_UTF16 * ssz + 1;
  grub_uint8_t *ret = xmalloc (tsz);
  *grub_utf16_to_utf8 (ret, in, ssz) = '\0';
  return (char *) ret;
}

#else
#error "Unsupported TCHAR size"
#endif


static LPTSTR
grub_util_get_windows_path_real (const char *path)
{
  LPTSTR fpa;
  LPTSTR tpath;
  size_t alloc, len;

  tpath = grub_util_utf8_to_tchar (path);

  alloc = PATH_MAX;

  while (1)
    {
      fpa = xcalloc (alloc, sizeof (fpa[0]));

      len = GetFullPathName (tpath, alloc, fpa, NULL);
      if (len >= alloc)
	{
	  free (fpa);
	  alloc = 2 * (len + 2);
	  continue;
	}
      if (len == 0)
	{
	  free (fpa);
	  return tpath;
	}

      free (tpath);
      return fpa;
    }
}

#ifdef __CYGWIN__
LPTSTR
grub_util_get_windows_path (const char *path)
{
  LPTSTR winpath;
  /* Workaround cygwin bugs with //?/.  */
  if ((path[0] == '\\' || path[0] == '/')
      && (path[1] == '\\' || path[1] == '/')
      && (path[2] == '?' || path[2] == '.')
      && (path[3] == '\\' || path[3] == '/'))
    return grub_util_get_windows_path_real (path);

  winpath = xmalloc (sizeof (winpath[0]) * PATH_MAX);
  memset (winpath, 0, sizeof (winpath[0]) * PATH_MAX);
  if (cygwin_conv_path ((sizeof (winpath[0]) == 1 ? CCP_POSIX_TO_WIN_A
			 : CCP_POSIX_TO_WIN_W) | CCP_ABSOLUTE, path, winpath,
			sizeof (winpath[0]) * PATH_MAX))
    grub_util_error ("%s", _("cygwin_conv_path() failed"));
  return winpath;
}
#else
LPTSTR
grub_util_get_windows_path (const char *path)
{
  return grub_util_get_windows_path_real (path);
}
#endif

grub_uint64_t
grub_util_get_fd_size (grub_util_fd_t hd, const char *name_in,
		       unsigned *log_secsize)
{
  grub_int64_t size = -1LL;
  int log_sector_size = 9;
  LPTSTR name = grub_util_get_windows_path (name_in);

  if (log_secsize)
    *log_secsize = log_sector_size;

  if (((name[0] == '/') || (name[0] == '\\')) &&
      ((name[1] == '/') || (name[1] == '\\')) &&
      ((name[2] == '.') || (name[2] == '?')) &&
      ((name[3] == '/') || (name[3] == '\\')))
    {
      DWORD nr;
      DISK_GEOMETRY g;

      if (! DeviceIoControl (hd, IOCTL_DISK_GET_DRIVE_GEOMETRY,
                             0, 0, &g, sizeof (g), &nr, 0))
        goto fail;

      size = g.Cylinders.QuadPart;
      size *= g.TracksPerCylinder * g.SectorsPerTrack * g.BytesPerSector;

      for (log_sector_size = 0;
	   (1 << log_sector_size) < g.BytesPerSector;
	   log_sector_size++);
    }
  else
    {
      ULARGE_INTEGER s;

      s.LowPart = GetFileSize (hd, &s.HighPart);
      size = s.QuadPart;
    }

 fail:

  if (log_secsize)
    *log_secsize = log_sector_size;

  free (name);

  return size;
}

void
grub_hostdisk_flush_initial_buffer (const char *os_dev __attribute__ ((unused)))
{
}

int
grub_util_fd_seek (grub_util_fd_t fd, grub_uint64_t off)
{
  LARGE_INTEGER offset;
  offset.QuadPart = off;

  if (!SetFilePointerEx (fd, offset, NULL, FILE_BEGIN))
    return -1;
  return 0;
}

grub_util_fd_t
grub_util_fd_open (const char *os_dev, int flags)
{
  DWORD flg = 0, crt;
  LPTSTR dev = grub_util_get_windows_path (os_dev);
  grub_util_fd_t ret;

  if (flags & GRUB_UTIL_FD_O_WRONLY)
    flg |= GENERIC_WRITE;
  if (flags & GRUB_UTIL_FD_O_RDONLY)
    flg |= GENERIC_READ;

  if (flags & GRUB_UTIL_FD_O_CREATTRUNC)
    crt = CREATE_ALWAYS;
  else
    crt = OPEN_EXISTING;

  ret = CreateFile (dev, flg, FILE_SHARE_READ | FILE_SHARE_WRITE,
		    0, crt, 0, 0);
  free (dev);
  return ret;
}

ssize_t
grub_util_fd_read (grub_util_fd_t fd, char *buf, size_t len)
{
  DWORD real_read;
  if (!ReadFile(fd, buf, len, &real_read, NULL))
    {
      grub_util_info ("read err %x", (int) GetLastError ());
      return -1;
    }
  grub_util_info ("successful read");
  return real_read;
}

ssize_t
grub_util_fd_write (grub_util_fd_t fd, const char *buf, size_t len)
{
  DWORD real_read;
  if (!WriteFile(fd, buf, len, &real_read, NULL))
    {
      grub_util_info ("write err %x", (int) GetLastError ());
      return -1;
    }

  grub_util_info ("successful write");
  return real_read;
}

static int allow_fd_syncs = 1;

int
grub_util_fd_sync (grub_util_fd_t fd)
{
  if (allow_fd_syncs)
    {
      if (!FlushFileBuffers (fd))
	{
	  grub_util_info ("flush err %x", (int) GetLastError ());
	  return -1;
	}
    }
  return 0;
}

void
grub_util_disable_fd_syncs (void)
{
  allow_fd_syncs = 0;
}

int
grub_util_fd_close (grub_util_fd_t fd)
{
  if (!CloseHandle (fd))
    {
      grub_util_info ("close err %x", (int) GetLastError ());
      return -1;
    }
  return 0;
}

const char *
grub_util_fd_strerror (void)
{
  DWORD err = GetLastError ();
  LPTSTR tstr = NULL;
  static char *last;
  char *ret, *ptr;

  free (last);
  last = 0;

  FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
		 | FORMAT_MESSAGE_IGNORE_INSERTS,
		 NULL, err, 0, (void *) &tstr,
		 0, NULL);

  if (!tstr)
    return "unknown error";

  ret = grub_util_tchar_to_utf8 (tstr);

  LocalFree (tstr);

  last = ret;

  for (ptr = ret + strlen (ret) - 1;
       ptr >= ret && (*ptr == '\n' || *ptr == '\r');
       ptr--);
  ptr[1] = '\0';

  return ret;
}

char *
grub_canonicalize_file_name (const char *path)
{
  char *ret;
  LPTSTR windows_path;
  ret = xmalloc (PATH_MAX);

  windows_path = grub_util_get_windows_path (path);
  if (!windows_path)
    return NULL;
  ret = grub_util_tchar_to_utf8 (windows_path);
  free (windows_path);
 
  return ret;
}

void
grub_util_mkdir (const char *dir)
{
  LPTSTR windows_name;

  windows_name = grub_util_get_windows_path (dir);
  CreateDirectory (windows_name, NULL);
  free (windows_name);
}

#if 0
ssize_t
grub_util_readlink (const char *name, char *buf, size_t bufsize)
{
  return readlink(name, buf, bufsize);
}
#endif

int
grub_util_rename (const char *from, const char *to)
{
  LPTSTR windows_from, windows_to;
  int ret;

  windows_from = grub_util_get_windows_path (from);
  windows_to = grub_util_get_windows_path (to);
  ret = !MoveFile (windows_from, windows_to);
  free (windows_from);
  free (windows_to);
  return ret;
}

struct grub_util_fd_dir
{
  WIN32_FIND_DATA fd;
  HANDLE hnd;
  int is_end;
  char *last;
};

grub_util_fd_dir_t
grub_util_fd_opendir (const char *name)
{
  struct grub_util_fd_dir *ret;
  LPTSTR name_windows;
  LPTSTR pattern;
  ssize_t l;

  name_windows = grub_util_get_windows_path (name);
  for (l = 0; name_windows[l]; l++);
  for (l--; l >= 0 && (name_windows[l] == '\\' || name_windows[l] == '/'); l--);
  l++;
  pattern = xcalloc (l + 3, sizeof (pattern[0]));
  memcpy (pattern, name_windows, l * sizeof (pattern[0]));
  pattern[l] = '\\';
  pattern[l + 1] = '*';
  pattern[l + 2] = '\0';

  ret = xmalloc (sizeof (*ret));
  memset (ret, 0, sizeof (*ret));

  ret->hnd = FindFirstFile (pattern, &ret->fd);

  free (name_windows);
  free (pattern);

  if (ret->hnd == INVALID_HANDLE_VALUE)
    {
      DWORD err = GetLastError ();
      if (err == ERROR_FILE_NOT_FOUND)
	{
	  ret->is_end = 1;
	  return ret;
	}
      return NULL;
    }
  return ret;
}

void
grub_util_fd_closedir (grub_util_fd_dir_t dirp)
{
  if (dirp->hnd != INVALID_HANDLE_VALUE)
    CloseHandle (dirp->hnd);
  free (dirp->last);
  free (dirp);
}

grub_util_fd_dirent_t
grub_util_fd_readdir (grub_util_fd_dir_t dirp)
{
  char *ret;
  free (dirp->last);
  dirp->last = NULL;

  if (dirp->is_end)
    return NULL;

  ret = grub_util_tchar_to_utf8 (dirp->fd.cFileName);
  dirp->last = ret;

  if (!FindNextFile (dirp->hnd, &dirp->fd))
    dirp->is_end = 1;
  return (grub_util_fd_dirent_t) ret;
}

int
grub_util_unlink (const char *name)
{
  LPTSTR name_windows;
  int ret;

  name_windows = grub_util_get_windows_path (name);

  ret = !DeleteFile (name_windows);
  free (name_windows);
  return ret;
}

int
grub_util_rmdir (const char *name)
{
  LPTSTR name_windows;
  int ret;

  name_windows = grub_util_get_windows_path (name);

  ret = !RemoveDirectory (name_windows);
  free (name_windows);
  return ret;
}

#ifndef __CYGWIN__

static char *
get_temp_name (void)
{
  TCHAR rt[1024];
  TCHAR *ptr;
  HCRYPTPROV   hCryptProv;
  grub_uint8_t rnd[5];
  int i;

  GetTempPath (ARRAY_SIZE (rt) - 100, rt);

  if (!CryptAcquireContext (&hCryptProv,
			    NULL,
			    MS_DEF_PROV,
			    PROV_RSA_FULL,
			    CRYPT_VERIFYCONTEXT)
      || !CryptGenRandom (hCryptProv, 5, rnd))
    grub_util_error ("%s", _("couldn't retrieve random data"));

  CryptReleaseContext (hCryptProv, 0);

  for (ptr = rt; *ptr; ptr++);
  memcpy (ptr, TEXT("\\GRUB."), sizeof (TEXT("\\GRUB.")));
  ptr += sizeof ("\\GRUB.") - 1;

  for (i = 0; i < 8; i++)
    {
      grub_size_t b = i * 5;
      grub_uint8_t r;
      grub_size_t f1 = GRUB_CHAR_BIT - b % GRUB_CHAR_BIT;
      grub_size_t f2;
      if (f1 > 5)
	f1 = 5;
      f2 = 5 - f1;
      r = (rnd[b / GRUB_CHAR_BIT] >> (b % GRUB_CHAR_BIT)) & ((1 << f1) - 1);
      if (f2)
	r |= (rnd[b / GRUB_CHAR_BIT + 1] & ((1 << f2) - 1)) << f1;
      if (r < 10)
	*ptr++ = '0' + r;
      else
	*ptr++ = 'a' + (r - 10);
    }  
  *ptr = '\0';

  return grub_util_tchar_to_utf8 (rt);
}

char *
grub_util_make_temporary_file (void)
{
  char *ret = get_temp_name ();
  FILE *f;

  f = grub_util_fopen (ret, "wb");
  if (f)
    fclose (f);
  return ret;
}

char *
grub_util_make_temporary_dir (void)
{
  char *ret = get_temp_name ();

  grub_util_mkdir (ret);

  return ret;
}

#endif

int
grub_util_is_directory (const char *name)
{
  LPTSTR name_windows;
  DWORD attr;

  name_windows = grub_util_get_windows_path (name);
  if (!name_windows)
    return 0;

  attr = GetFileAttributes (name_windows);
  grub_free (name_windows);

  return !!(attr & FILE_ATTRIBUTE_DIRECTORY);
}

int
grub_util_is_regular (const char *name)
{
  LPTSTR name_windows;
  DWORD attr;

  name_windows = grub_util_get_windows_path (name);
  if (!name_windows)
    return 0;

  attr = GetFileAttributes (name_windows);
  grub_free (name_windows);

  return !(attr & FILE_ATTRIBUTE_DIRECTORY)
    && !(attr & FILE_ATTRIBUTE_REPARSE_POINT) && attr;
}

grub_uint32_t
grub_util_get_mtime (const char *path)
{
  LPTSTR name_windows;
  BOOL b;
  WIN32_FILE_ATTRIBUTE_DATA attr;
  ULARGE_INTEGER us_ul;

  name_windows = grub_util_get_windows_path (path);
  if (!name_windows)
    return 0;

  b = GetFileAttributesEx (name_windows, GetFileExInfoStandard, &attr);
  grub_free (name_windows);

  if (!b)
    return 0;

  us_ul.LowPart = attr.ftLastWriteTime.dwLowDateTime;
  us_ul.HighPart = attr.ftLastWriteTime.dwHighDateTime;

  return (us_ul.QuadPart / 10000000) 
    - 86400ULL * 365 * (1970 - 1601)
    - 86400ULL * ((1970 - 1601) / 4) + 86400ULL * ((1970 - 1601) / 100);
}


#ifdef __MINGW32__

FILE *
grub_util_fopen (const char *path, const char *mode)
{
  LPTSTR tpath;
  FILE *ret;
  tpath = grub_util_get_windows_path (path);
#if SIZEOF_TCHAR == 1
  ret = fopen (tpath, tmode);
#else
  LPTSTR tmode;
  tmode = grub_util_utf8_to_tchar (mode);
  ret = _wfopen (tpath, tmode);
  free (tmode);
#endif
  free (tpath);
  return ret;
}

int
grub_util_file_sync (FILE *f)
{
  HANDLE hnd;

  if (fflush (f) != 0)
    {
      grub_util_info ("fflush err %x", (int) GetLastError ());
      return -1;
    }
  if (!allow_fd_syncs)
    return 0;
  hnd = (HANDLE) _get_osfhandle (fileno (f));
  if (!FlushFileBuffers (hnd))
    {
      grub_util_info ("flush err %x", (int) GetLastError ());
      return -1;
    }
  return 0;
}

int
grub_util_is_special_file (const char *name)
{
  LPTSTR name_windows;
  DWORD attr;

  name_windows = grub_util_get_windows_path (name);
  if (!name_windows)
    return 1;

  attr = GetFileAttributes (name_windows);
  grub_free (name_windows);

  return !!(attr & FILE_ATTRIBUTE_REPARSE_POINT) || !attr;
}

#else

void
grub_util_file_sync (FILE *f)
{
  fflush (f);
  if (!allow_fd_syncs)
    return;
  fsync (fileno (f));
}

FILE *
grub_util_fopen (const char *path, const char *mode)
{
  return fopen (path, mode);
}

#endif
