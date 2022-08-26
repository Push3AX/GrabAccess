/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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

#include <config.h>
#include <config-util.h>
#include <grub/util/misc.h>
#include <grub/osdep/hostfile.h>
#include <grub/util/windows.h>
#include <grub/emu/config.h>

#include <wincon.h>
#include <windows.h>

#include <grub/util/misc.h>

#include "progname.h"

struct grub_windows_console_font_infoex {
  ULONG cbSize;
  DWORD nFont;
  COORD dwFontSize;
  UINT  FontFamily;
  UINT  FontWeight;
  WCHAR FaceName[LF_FACESIZE];
};

static int
check_is_raster (HMODULE kernel32, HANDLE hnd)
{
  CONSOLE_FONT_INFO console_font_info;
  BOOL (WINAPI * func_GetCurrentConsoleFont) (HANDLE, BOOL,
					      PCONSOLE_FONT_INFO);

  func_GetCurrentConsoleFont = (void *)
    GetProcAddress (kernel32, "GetCurrentConsoleFont");

  if (!func_GetCurrentConsoleFont)
    return 1;

  if (!func_GetCurrentConsoleFont (hnd, FALSE, &console_font_info))
    return 1;
  return console_font_info.nFont < 12;
}

static void
set_console_unicode_font (void)
{
  BOOL (WINAPI * func_SetCurrentConsoleFontEx) (HANDLE, BOOL,
						struct grub_windows_console_font_infoex *);
  BOOL (WINAPI * func_SetConsoleFont)(HANDLE, DWORD);
  HMODULE kernel32;
  HANDLE out_handle = GetStdHandle (STD_OUTPUT_HANDLE);
  HANDLE err_handle = GetStdHandle (STD_ERROR_HANDLE);
  int out_raster, err_raster;

  kernel32 = GetModuleHandle(TEXT("kernel32.dll"));
  if (!kernel32)
    return;

  out_raster = check_is_raster (kernel32, out_handle);
  err_raster = check_is_raster (kernel32, err_handle);

  if (!out_raster && !err_raster)
    return;

  func_SetCurrentConsoleFontEx = (void *) GetProcAddress (kernel32, "SetCurrentConsoleFontEx");

  /* Newer windows versions.  */
  if (func_SetCurrentConsoleFontEx)
    {
      struct grub_windows_console_font_infoex new_console_font_info;
      new_console_font_info.cbSize = sizeof (new_console_font_info);
      new_console_font_info.nFont = 12;
      new_console_font_info.dwFontSize.X = 7;
      new_console_font_info.dwFontSize.Y = 12;
      new_console_font_info.FontFamily = FF_DONTCARE;
      new_console_font_info.FontWeight = 400;
      memcpy (new_console_font_info.FaceName, TEXT("Lucida Console"),
	      sizeof (TEXT("Lucida Console")));
      if (out_raster)
	func_SetCurrentConsoleFontEx (out_handle, FALSE,
				      &new_console_font_info);
      if (err_raster)
	func_SetCurrentConsoleFontEx (err_handle, FALSE,
				      &new_console_font_info);
      return;
    }

  /* Fallback for older versions.  */
  func_SetConsoleFont = (void *) GetProcAddress (kernel32, "SetConsoleFont");
  if (func_SetConsoleFont)
    {
      if (out_raster)
	func_SetConsoleFont (out_handle, 12);
      if (err_raster)
	func_SetConsoleFont (err_handle, 12);
    }
}

static char *grub_util_base_directory;
static char *locale_dir;

const char *
grub_util_get_config_filename (void)
{
  static char *value = NULL;
  if (!value)
    value = grub_util_path_concat (2, grub_util_base_directory, "grub.cfg");
  return value;
}

const char *
grub_util_get_pkgdatadir (void)
{
  return grub_util_base_directory;
}

const char *
grub_util_get_localedir (void)
{
  return locale_dir;
}

const char *
grub_util_get_pkglibdir (void)
{
  return grub_util_base_directory;
}

void
grub_util_host_init (int *argc __attribute__ ((unused)),
		     char ***argv)
{
  char *ptr;

  SetConsoleOutputCP (CP_UTF8);
  SetConsoleCP (CP_UTF8);

  set_console_unicode_font ();

#if SIZEOF_TCHAR == 1

#elif SIZEOF_TCHAR == 2
  LPWSTR tcmdline = GetCommandLineW ();
  int i;
  LPWSTR *targv;

  targv = CommandLineToArgvW (tcmdline, argc);
  *argv = xcalloc (*argc + 1, sizeof (argv[0]));

  for (i = 0; i < *argc; i++)
    (*argv)[i] = grub_util_tchar_to_utf8 (targv[i]); 
  (*argv)[i] = NULL;
#else
#error "Unsupported TCHAR size"
#endif

  grub_util_base_directory = grub_canonicalize_file_name ((*argv)[0]);
  if (!grub_util_base_directory)
    grub_util_base_directory = xstrdup ((*argv)[0]);
  for (ptr = grub_util_base_directory + strlen (grub_util_base_directory) - 1;
       ptr >= grub_util_base_directory && *ptr != '/' && *ptr != '\\'; ptr--);
  if (ptr >= grub_util_base_directory)
    *ptr = '\0';

  locale_dir = grub_util_path_concat (2, grub_util_base_directory, "locale");

  set_program_name ((*argv)[0]);

#if (defined (GRUB_UTIL) && defined(ENABLE_NLS) && ENABLE_NLS)
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, locale_dir);
  textdomain (PACKAGE);
#endif /* (defined(ENABLE_NLS) && ENABLE_NLS) */
}
