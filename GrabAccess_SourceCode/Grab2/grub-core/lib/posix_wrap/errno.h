/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009, 2010  Free Software Foundation, Inc.
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

#ifndef GRUB_POSIX_ERRNO_H
#define GRUB_POSIX_ERRNO_H	1

#include <grub/err.h>

#undef errno
#define errno  grub_errno
#define EDOM GRUB_ERR_BAD_ARGUMENT
#define EINVAL GRUB_ERR_BAD_NUMBER
#define EISDIR GRUB_ERR_BAD_FILE_TYPE
#define ENOENT GRUB_ERR_FILE_NOT_FOUND
#define ENOMEM GRUB_ERR_OUT_OF_MEMORY
#define ENOTDIR GRUB_ERR_BAD_FILE_TYPE
#define ENXIO GRUB_ERR_UNKNOWN_DEVICE
#define ERANGE GRUB_ERR_OUT_OF_RANGE
#define EINTR GRUB_ERR_TIMEOUT

#endif
