/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2021  Free Software Foundation, Inc.
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

#include <grub/buffer.h>
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/safemath.h>
#include <grub/types.h>

grub_buffer_t
grub_buffer_new (grub_size_t sz)
{
  struct grub_buffer *ret;

  ret = (struct grub_buffer *) grub_malloc (sizeof (*ret));
  if (ret == NULL)
    return NULL;

  ret->data = (grub_uint8_t *) grub_malloc (sz);
  if (ret->data == NULL)
    {
      grub_free (ret);
      return NULL;
    }

  ret->sz = sz;
  ret->pos = 0;
  ret->used = 0;

  return ret;
}

void
grub_buffer_free (grub_buffer_t buf)
{
  grub_free (buf->data);
  grub_free (buf);
}

grub_err_t
grub_buffer_ensure_space (grub_buffer_t buf, grub_size_t req)
{
  grub_uint8_t *d;
  grub_size_t newsz = 1;

  /* Is the current buffer size adequate? */
  if (buf->sz >= req)
    return GRUB_ERR_NONE;

  /* Find the smallest power-of-2 size that satisfies the request. */
  while (newsz < req)
    {
      if (newsz == 0)
	return grub_error (GRUB_ERR_OUT_OF_RANGE,
			   N_("requested buffer size is too large"));
      newsz <<= 1;
    }

  d = (grub_uint8_t *) grub_realloc (buf->data, newsz);
  if (d == NULL)
    return grub_errno;

  buf->data = d;
  buf->sz = newsz;

  return GRUB_ERR_NONE;
}

void *
grub_buffer_take_data (grub_buffer_t buf)
{
  void *data = buf->data;

  buf->data = NULL;
  buf->sz = buf->pos = buf->used = 0;

  return data;
}

void
grub_buffer_reset (grub_buffer_t buf)
{
  buf->pos = buf->used = 0;
}

grub_err_t
grub_buffer_advance_read_pos (grub_buffer_t buf, grub_size_t n)
{
  grub_size_t newpos;

  if (grub_add (buf->pos, n, &newpos))
    return grub_error (GRUB_ERR_OUT_OF_RANGE, N_("overflow is detected"));

  if (newpos > buf->used)
    return grub_error (GRUB_ERR_OUT_OF_RANGE,
		       N_("new read is position beyond the end of the written data"));

  buf->pos = newpos;

  return GRUB_ERR_NONE;
}
