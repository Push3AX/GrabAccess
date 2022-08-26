 /*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2020  Free Software Foundation, Inc.
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
 *
 */

#include <grub/misc.h>

#include <stdint.h>
#include <xz.h>

void
grub_xz_decompress (const void *in, uint32_t in_size, void *out, uint32_t out_size)
{
  struct xz_buf buf;
  struct xz_dec *dec;
  enum xz_ret xzret;

  dec = xz_dec_init (1 << 16);
  if (!dec)
    return;

  buf.in = in;
  buf.in_pos = 0;
  buf.in_size = in_size;
  buf.out = out;
  buf.out_pos = 0;
  buf.out_size = out_size;

  xzret = xz_dec_run (dec, &buf);
  switch (xzret)
  {
    case XZ_MEMLIMIT_ERROR:
    case XZ_FORMAT_ERROR:
    case XZ_OPTIONS_ERROR:
    case XZ_DATA_ERROR:
    case XZ_BUF_ERROR:
      grub_error (GRUB_ERR_BAD_COMPRESSED_DATA, N_("xz file corrupted"));
    default:
      break;
  }
  xz_dec_end (dec);
  return;
}
