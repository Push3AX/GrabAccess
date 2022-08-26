/* xz.h - XZ decompressor */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010  Free Software Foundation, Inc.
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

#ifndef MAP_XZ_H
#define MAP_XZ_H

#include <stdint.h>
#include <grub/types.h>

enum xz_ret
{
  XZ_OK,
  XZ_STREAM_END,
  XZ_MEMLIMIT_ERROR,
  XZ_FORMAT_ERROR,
  XZ_OPTIONS_ERROR,
  XZ_DATA_ERROR,
  XZ_BUF_ERROR
};

struct xz_buf
{
  const uint8_t *in;
  size_t in_pos;
  size_t in_size;

  uint8_t *out;
  size_t out_pos;
  size_t out_size;
};

struct xz_dec;

struct xz_dec * xz_dec_init(uint32_t dict_max);

enum xz_ret xz_dec_run(struct xz_dec *s, struct xz_buf *b);

void xz_dec_reset(struct xz_dec *s);

void xz_dec_end(struct xz_dec *s);

void
grub_xz_decompress (const void *in, uint32_t in_size, void *out, uint32_t out_size);

#endif
