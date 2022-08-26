/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013 Free Software Foundation, Inc.
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

#include <grub/emu/exec.h>
#include <grub/util/install.h>

int 
grub_install_compress_gzip (const char *src, const char *dest)
{
  return grub_util_exec_redirect ((const char * []) { "gzip", "--best",
	"--stdout", NULL }, src, dest);
}

int 
grub_install_compress_xz (const char *src, const char *dest)
{
  return grub_util_exec_redirect ((const char * []) { "xz",
	"--lzma2=dict=128KiB", "--check=none", "--stdout", NULL }, src, dest);
}

int 
grub_install_compress_lzop (const char *src, const char *dest)
{
  return grub_util_exec_redirect ((const char * []) { "lzop", "-9",  "-c",
	NULL }, src, dest);
}
