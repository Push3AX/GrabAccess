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

#ifndef GRUB_BUFFER_H
#define GRUB_BUFFER_H	1

#include <grub/err.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/safemath.h>
#include <grub/types.h>

struct grub_buffer
{
  grub_uint8_t *data;
  grub_size_t sz;
  grub_size_t pos;
  grub_size_t used;
};

/*
 * grub_buffer_t represents a simple variable sized byte buffer with
 * read and write cursors. It currently only implements
 * functionality required by the only user in GRUB (append byte[s],
 * peeking data at a specified position and updating the read cursor.
 * Some things that this doesn't do yet are:
 * - Reading a portion of the buffer by copying data from the current
 *   read position in to a caller supplied destination buffer and then
 *   automatically updating the read cursor.
 * - Dropping the read part at the start of the buffer when an append
 *   requires more space.
 */
typedef struct grub_buffer *grub_buffer_t;

/* Allocate a new buffer with the specified initial size. */
extern grub_buffer_t grub_buffer_new (grub_size_t sz);

/* Free the buffer and its resources. */
extern void grub_buffer_free (grub_buffer_t buf);

/* Return the number of unread bytes in this buffer. */
static inline grub_size_t
grub_buffer_get_unread_bytes (grub_buffer_t buf)
{
  return buf->used - buf->pos;
}

/*
 * Ensure that the buffer size is at least the requested
 * number of bytes.
 */
extern grub_err_t grub_buffer_ensure_space (grub_buffer_t buf, grub_size_t req);

/*
 * Append the specified number of bytes from the supplied
 * data to the buffer.
 */
static inline grub_err_t
grub_buffer_append_data (grub_buffer_t buf, const void *data, grub_size_t len)
{
  grub_size_t req;

  if (grub_add (buf->used, len, &req))
    return grub_error (GRUB_ERR_OUT_OF_RANGE, N_("overflow is detected"));

  if (grub_buffer_ensure_space (buf, req) != GRUB_ERR_NONE)
    return grub_errno;

  grub_memcpy (&buf->data[buf->used], data, len);
  buf->used = req;

  return GRUB_ERR_NONE;
}

/* Append the supplied character to the buffer. */
static inline grub_err_t
grub_buffer_append_char (grub_buffer_t buf, char c)
{
  return grub_buffer_append_data (buf, &c, 1);
}

/*
 * Forget and return the underlying data buffer. The caller
 * becomes the owner of this buffer, and must free it when it
 * is no longer required.
 */
extern void *grub_buffer_take_data (grub_buffer_t buf);

/* Reset this buffer. Note that this does not deallocate any resources. */
void grub_buffer_reset (grub_buffer_t buf);

/*
 * Return a pointer to the underlying data buffer at the specified
 * offset from the current read position. Note that this pointer may
 * become invalid if the buffer is mutated further.
 */
static inline void *
grub_buffer_peek_data_at (grub_buffer_t buf, grub_size_t off)
{
  if (grub_add (buf->pos, off, &off))
    {
      grub_error (GRUB_ERR_OUT_OF_RANGE, N_("overflow is detected"));
      return NULL;
    }

  if (off >= buf->used)
    {
      grub_error (GRUB_ERR_OUT_OF_RANGE, N_("peek out of range"));
      return NULL;
    }

  return &buf->data[off];
}

/*
 * Return a pointer to the underlying data buffer at the current
 * read position. Note that this pointer may become invalid if the
 * buffer is mutated further.
 */
static inline void *
grub_buffer_peek_data (grub_buffer_t buf)
{
  return grub_buffer_peek_data_at (buf, 0);
}

/* Advance the read position by the specified number of bytes. */
extern grub_err_t grub_buffer_advance_read_pos (grub_buffer_t buf, grub_size_t n);

#endif /* GRUB_BUFFER_H */
