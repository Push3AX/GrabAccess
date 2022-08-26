/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2006
 *                2007, 2008, 2009, 2010, 2011, 2012, 2013  Free Software Foundation, Inc.
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

#include <grub/crypto.h>
#include <grub/mm.h>
#include <grub/term.h>

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int
grub_password_get (char buf[], unsigned buf_size)
{
  HANDLE hStdin = GetStdHandle (STD_INPUT_HANDLE); 
  DWORD mode = 0;
  char *ptr;

  grub_refresh ();
  
  GetConsoleMode (hStdin, &mode);
  SetConsoleMode (hStdin, mode & (~ENABLE_ECHO_INPUT));

  fgets (buf, buf_size, stdin);
  ptr = buf + strlen (buf) - 1;
  while (buf <= ptr && (*ptr == '\n' || *ptr == '\r'))
    *ptr-- = 0;

  SetConsoleMode (hStdin, mode);

  grub_refresh ();

  return 1;
}
