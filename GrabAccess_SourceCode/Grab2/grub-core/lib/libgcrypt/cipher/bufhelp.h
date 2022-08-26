/* bufhelp.h  -  Some buffer manipulation helpers
 * Copyright (C) 2012 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 * This file is part of Libgcrypt.
 *
 * Libgcrypt is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * Libgcrypt is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef GCRYPT_BUFHELP_H
#define GCRYPT_BUFHELP_H


#include "bithelp.h"


#undef BUFHELP_FAST_UNALIGNED_ACCESS
#if defined(HAVE_GCC_ATTRIBUTE_PACKED) && \
    defined(HAVE_GCC_ATTRIBUTE_ALIGNED) && \
    (defined(__i386__) || defined(__x86_64__) || \
     (defined(__arm__) && defined(__ARM_FEATURE_UNALIGNED)) || \
     defined(__aarch64__))
/* These architectures are able of unaligned memory accesses and can
   handle those fast.
 */
# define BUFHELP_FAST_UNALIGNED_ACCESS 1
#endif


#ifdef BUFHELP_FAST_UNALIGNED_ACCESS
/* Define type with one-byte alignment on architectures with fast unaligned
   memory accesses.
 */
typedef struct bufhelp_int_s
{
  uintptr_t a;
} __attribute__((packed, aligned(1))) bufhelp_int_t;
#else
/* Define type with default alignment for other architectures (unaligned
   accessed handled in per byte loops).
 */
typedef struct bufhelp_int_s
{
  uintptr_t a;
} bufhelp_int_t;
#endif


/* Optimized function for small buffer copying */
static inline void
buf_cpy(void *_dst, const void *_src, size_t len)
{
#if __GNUC__ >= 4 && (defined(__x86_64__) || defined(__i386__))
  /* For AMD64 and i386, memcpy is faster.  */
  memcpy(_dst, _src, len);
#else
  byte *dst = _dst;
  const byte *src = _src;
  bufhelp_int_t *ldst;
  const bufhelp_int_t *lsrc;
#ifndef BUFHELP_FAST_UNALIGNED_ACCESS
  const unsigned int longmask = sizeof(bufhelp_int_t) - 1;

  /* Skip fast processing if buffers are unaligned.  */
  if (((uintptr_t)dst | (uintptr_t)src) & longmask)
    goto do_bytes;
#endif

  ldst = (bufhelp_int_t *)(void *)dst;
  lsrc = (const bufhelp_int_t *)(const void *)src;

  for (; len >= sizeof(bufhelp_int_t); len -= sizeof(bufhelp_int_t))
    (ldst++)->a = (lsrc++)->a;

  dst = (byte *)ldst;
  src = (const byte *)lsrc;

#ifndef BUFHELP_FAST_UNALIGNED_ACCESS
do_bytes:
#endif
  /* Handle tail.  */
  for (; len; len--)
    *dst++ = *src++;
#endif /*__GNUC__ >= 4 && (__x86_64__ || __i386__)*/
}


/* Optimized function for buffer xoring */
static inline void
buf_xor(void *_dst, const void *_src1, const void *_src2, size_t len)
{
  byte *dst = _dst;
  const byte *src1 = _src1;
  const byte *src2 = _src2;
  bufhelp_int_t *ldst;
  const bufhelp_int_t *lsrc1, *lsrc2;
#ifndef BUFHELP_FAST_UNALIGNED_ACCESS
  const unsigned int longmask = sizeof(bufhelp_int_t) - 1;

  /* Skip fast processing if buffers are unaligned.  */
  if (((uintptr_t)dst | (uintptr_t)src1 | (uintptr_t)src2) & longmask)
    goto do_bytes;
#endif

  ldst = (bufhelp_int_t *)(void *)dst;
  lsrc1 = (const bufhelp_int_t *)(const void *)src1;
  lsrc2 = (const bufhelp_int_t *)(const void *)src2;

  for (; len >= sizeof(bufhelp_int_t); len -= sizeof(bufhelp_int_t))
    (ldst++)->a = (lsrc1++)->a ^ (lsrc2++)->a;

  dst = (byte *)ldst;
  src1 = (const byte *)lsrc1;
  src2 = (const byte *)lsrc2;

#ifndef BUFHELP_FAST_UNALIGNED_ACCESS
do_bytes:
#endif
  /* Handle tail.  */
  for (; len; len--)
    *dst++ = *src1++ ^ *src2++;
}


/* Optimized function for in-place buffer xoring. */
static inline void
buf_xor_1(void *_dst, const void *_src, size_t len)
{
  byte *dst = _dst;
  const byte *src = _src;
  bufhelp_int_t *ldst;
  const bufhelp_int_t *lsrc;
#ifndef BUFHELP_FAST_UNALIGNED_ACCESS
  const unsigned int longmask = sizeof(bufhelp_int_t) - 1;

  /* Skip fast processing if buffers are unaligned.  */
  if (((uintptr_t)dst | (uintptr_t)src) & longmask)
    goto do_bytes;
#endif

  ldst = (bufhelp_int_t *)(void *)dst;
  lsrc = (const bufhelp_int_t *)(const void *)src;

  for (; len >= sizeof(bufhelp_int_t); len -= sizeof(bufhelp_int_t))
    (ldst++)->a ^= (lsrc++)->a;

  dst = (byte *)ldst;
  src = (const byte *)lsrc;

#ifndef BUFHELP_FAST_UNALIGNED_ACCESS
do_bytes:
#endif
  /* Handle tail.  */
  for (; len; len--)
    *dst++ ^= *src++;
}


/* Optimized function for buffer xoring with two destination buffers.  Used
   mainly by CFB mode encryption.  */
static inline void
buf_xor_2dst(void *_dst1, void *_dst2, const void *_src, size_t len)
{
  byte *dst1 = _dst1;
  byte *dst2 = _dst2;
  const byte *src = _src;
  bufhelp_int_t *ldst1, *ldst2;
  const bufhelp_int_t *lsrc;
#ifndef BUFHELP_FAST_UNALIGNED_ACCESS
  const unsigned int longmask = sizeof(bufhelp_int_t) - 1;

  /* Skip fast processing if buffers are unaligned.  */
  if (((uintptr_t)src | (uintptr_t)dst1 | (uintptr_t)dst2) & longmask)
    goto do_bytes;
#endif

  ldst1 = (bufhelp_int_t *)(void *)dst1;
  ldst2 = (bufhelp_int_t *)(void *)dst2;
  lsrc = (const bufhelp_int_t *)(const void *)src;

  for (; len >= sizeof(bufhelp_int_t); len -= sizeof(bufhelp_int_t))
    (ldst1++)->a = ((ldst2++)->a ^= (lsrc++)->a);

  dst1 = (byte *)ldst1;
  dst2 = (byte *)ldst2;
  src = (const byte *)lsrc;

#ifndef BUFHELP_FAST_UNALIGNED_ACCESS
do_bytes:
#endif
  /* Handle tail.  */
  for (; len; len--)
    *dst1++ = (*dst2++ ^= *src++);
}


/* Optimized function for combined buffer xoring and copying.  Used by mainly
   CBC mode decryption.  */
static inline void
buf_xor_n_copy_2(void *_dst_xor, const void *_src_xor, void *_srcdst_cpy,
		 const void *_src_cpy, size_t len)
{
  byte *dst_xor = _dst_xor;
  byte *srcdst_cpy = _srcdst_cpy;
  const byte *src_xor = _src_xor;
  const byte *src_cpy = _src_cpy;
  byte temp;
  bufhelp_int_t *ldst_xor, *lsrcdst_cpy;
  const bufhelp_int_t *lsrc_cpy, *lsrc_xor;
  uintptr_t ltemp;
#ifndef BUFHELP_FAST_UNALIGNED_ACCESS
  const unsigned int longmask = sizeof(bufhelp_int_t) - 1;

  /* Skip fast processing if buffers are unaligned.  */
  if (((uintptr_t)src_cpy | (uintptr_t)src_xor | (uintptr_t)dst_xor |
       (uintptr_t)srcdst_cpy) & longmask)
    goto do_bytes;
#endif

  ldst_xor = (bufhelp_int_t *)(void *)dst_xor;
  lsrc_xor = (const bufhelp_int_t *)(void *)src_xor;
  lsrcdst_cpy = (bufhelp_int_t *)(void *)srcdst_cpy;
  lsrc_cpy = (const bufhelp_int_t *)(const void *)src_cpy;

  for (; len >= sizeof(bufhelp_int_t); len -= sizeof(bufhelp_int_t))
    {
      ltemp = (lsrc_cpy++)->a;
      (ldst_xor++)->a = (lsrcdst_cpy)->a ^ (lsrc_xor++)->a;
      (lsrcdst_cpy++)->a = ltemp;
    }

  dst_xor = (byte *)ldst_xor;
  src_xor = (const byte *)lsrc_xor;
  srcdst_cpy = (byte *)lsrcdst_cpy;
  src_cpy = (const byte *)lsrc_cpy;

#ifndef BUFHELP_FAST_UNALIGNED_ACCESS
do_bytes:
#endif
  /* Handle tail.  */
  for (; len; len--)
    {
      temp = *src_cpy++;
      *dst_xor++ = *srcdst_cpy ^ *src_xor++;
      *srcdst_cpy++ = temp;
    }
}


/* Optimized function for combined buffer xoring and copying.  Used by mainly
   CFB mode decryption.  */
static inline void
buf_xor_n_copy(void *_dst_xor, void *_srcdst_cpy, const void *_src, size_t len)
{
  buf_xor_n_copy_2(_dst_xor, _src, _srcdst_cpy, _src, len);
}


/* Constant-time compare of two buffers.  Returns 1 if buffers are equal,
   and 0 if buffers differ.  */
static inline int
buf_eq_const(const void *_a, const void *_b, size_t len)
{
  const byte *a = _a;
  const byte *b = _b;
  size_t diff, i;

  /* Constant-time compare. */
  for (i = 0, diff = 0; i < len; i++)
    diff -= !!(a[i] - b[i]);

  return !diff;
}


#ifndef BUFHELP_FAST_UNALIGNED_ACCESS

/* Functions for loading and storing unaligned u32 values of different
   endianness.  */
static inline u32 buf_get_be32(const void *_buf)
{
  const byte *in = _buf;
  return ((u32)in[0] << 24) | ((u32)in[1] << 16) | \
         ((u32)in[2] << 8) | (u32)in[3];
}

static inline u32 buf_get_le32(const void *_buf)
{
  const byte *in = _buf;
  return ((u32)in[3] << 24) | ((u32)in[2] << 16) | \
         ((u32)in[1] << 8) | (u32)in[0];
}

static inline void buf_put_be32(void *_buf, u32 val)
{
  byte *out = _buf;
  out[0] = val >> 24;
  out[1] = val >> 16;
  out[2] = val >> 8;
  out[3] = val;
}

static inline void buf_put_le32(void *_buf, u32 val)
{
  byte *out = _buf;
  out[3] = val >> 24;
  out[2] = val >> 16;
  out[1] = val >> 8;
  out[0] = val;
}


/* Functions for loading and storing unaligned u64 values of different
   endianness.  */
static inline u64 buf_get_be64(const void *_buf)
{
  const byte *in = _buf;
  return ((u64)in[0] << 56) | ((u64)in[1] << 48) | \
         ((u64)in[2] << 40) | ((u64)in[3] << 32) | \
         ((u64)in[4] << 24) | ((u64)in[5] << 16) | \
         ((u64)in[6] << 8) | (u64)in[7];
}

static inline u64 buf_get_le64(const void *_buf)
{
  const byte *in = _buf;
  return ((u64)in[7] << 56) | ((u64)in[6] << 48) | \
         ((u64)in[5] << 40) | ((u64)in[4] << 32) | \
         ((u64)in[3] << 24) | ((u64)in[2] << 16) | \
         ((u64)in[1] << 8) | (u64)in[0];
}

static inline void buf_put_be64(void *_buf, u64 val)
{
  byte *out = _buf;
  out[0] = val >> 56;
  out[1] = val >> 48;
  out[2] = val >> 40;
  out[3] = val >> 32;
  out[4] = val >> 24;
  out[5] = val >> 16;
  out[6] = val >> 8;
  out[7] = val;
}

static inline void buf_put_le64(void *_buf, u64 val)
{
  byte *out = _buf;
  out[7] = val >> 56;
  out[6] = val >> 48;
  out[5] = val >> 40;
  out[4] = val >> 32;
  out[3] = val >> 24;
  out[2] = val >> 16;
  out[1] = val >> 8;
  out[0] = val;
}

#else /*BUFHELP_FAST_UNALIGNED_ACCESS*/

typedef struct bufhelp_u32_s
{
  u32 a;
} __attribute__((packed, aligned(1))) bufhelp_u32_t;

/* Functions for loading and storing unaligned u32 values of different
   endianness.  */
static inline u32 buf_get_be32(const void *_buf)
{
  return be_bswap32(((const bufhelp_u32_t *)_buf)->a);
}

static inline u32 buf_get_le32(const void *_buf)
{
  return le_bswap32(((const bufhelp_u32_t *)_buf)->a);
}

static inline void buf_put_be32(void *_buf, u32 val)
{
  bufhelp_u32_t *out = _buf;
  out->a = be_bswap32(val);
}

static inline void buf_put_le32(void *_buf, u32 val)
{
  bufhelp_u32_t *out = _buf;
  out->a = le_bswap32(val);
}


typedef struct bufhelp_u64_s
{
  u64 a;
} __attribute__((packed, aligned(1))) bufhelp_u64_t;

/* Functions for loading and storing unaligned u64 values of different
   endianness.  */
static inline u64 buf_get_be64(const void *_buf)
{
  return be_bswap64(((const bufhelp_u64_t *)_buf)->a);
}

static inline u64 buf_get_le64(const void *_buf)
{
  return le_bswap64(((const bufhelp_u64_t *)_buf)->a);
}

static inline void buf_put_be64(void *_buf, u64 val)
{
  bufhelp_u64_t *out = _buf;
  out->a = be_bswap64(val);
}

static inline void buf_put_le64(void *_buf, u64 val)
{
  bufhelp_u64_t *out = _buf;
  out->a = le_bswap64(val);
}


#endif /*BUFHELP_FAST_UNALIGNED_ACCESS*/

#endif /*GCRYPT_BUFHELP_H*/
