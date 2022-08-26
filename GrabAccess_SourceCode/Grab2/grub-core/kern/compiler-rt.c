/* compiler-rt.c - compiler helpers. */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2005,2006,2007,2008,2009,2010-2014  Free Software Foundation, Inc.
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

#include <grub/misc.h>
#include <grub/compiler-rt.h>

#ifndef GRUB_EMBED_DECOMPRESSOR
void * GRUB_BUILTIN_ATTR
memcpy (void *dest, const void *src, grub_size_t n)
{
	return grub_memmove (dest, src, n);
}
void * GRUB_BUILTIN_ATTR
memmove (void *dest, const void *src, grub_size_t n)
{
	return grub_memmove (dest, src, n);
}
int GRUB_BUILTIN_ATTR
memcmp (const void *s1, const void *s2, grub_size_t n)
{
  return grub_memcmp (s1, s2, n);
}
void * GRUB_BUILTIN_ATTR
memset (void *s, int c, grub_size_t n)
{
  return grub_memset (s, c, n);
}

#ifdef __APPLE__

void GRUB_BUILTIN_ATTR
__bzero (void *s, grub_size_t n)
{
  grub_memset (s, 0, n);
}

#endif

#if GRUB_DIVISION_IN_SOFTWARE

grub_uint32_t
__udivsi3 (grub_uint32_t a, grub_uint32_t b)
{
  return grub_divmod64 (a, b, 0);
}

grub_int32_t
__divsi3 (grub_int32_t a, grub_int32_t b)
{
  return grub_divmod64s (a, b, 0);
}

grub_uint32_t
__umodsi3 (grub_uint32_t a, grub_uint32_t b)
{
  grub_uint64_t ret;
  grub_divmod64 (a, b, &ret);
  return ret;
}

grub_int32_t
__modsi3 (grub_int32_t a, grub_int32_t b)
{
  grub_int64_t ret;
  grub_divmod64s (a, b, &ret);
  return ret;
}

grub_uint64_t
__udivdi3 (grub_uint64_t a, grub_uint64_t b)
{
  return grub_divmod64 (a, b, 0);
}

grub_uint64_t
__umoddi3 (grub_uint64_t a, grub_uint64_t b)
{
  grub_uint64_t ret;
  grub_divmod64 (a, b, &ret);
  return ret;
}

grub_int64_t
__divdi3 (grub_int64_t a, grub_int64_t b)
{
  return grub_divmod64s (a, b, 0);
}

grub_int64_t
__moddi3 (grub_int64_t a, grub_int64_t b)
{
  grub_int64_t ret;
  grub_divmod64s (a, b, &ret);
  return ret;
}

#endif

#endif

#ifdef NEED_CTZDI2

unsigned
__ctzdi2 (grub_uint64_t x)
{
  unsigned ret = 0;
  if (!x)
    return 64;
  if (!(x & 0xffffffff))
    {
      x >>= 32;
      ret |= 32;
    }
  if (!(x & 0xffff))
    {
      x >>= 16;
      ret |= 16;
    }
  if (!(x & 0xff))
    {
      x >>= 8;
      ret |= 8;
    }
  if (!(x & 0xf))
    {
      x >>= 4;
      ret |= 4;
    }
  if (!(x & 0x3))
    {
      x >>= 2;
      ret |= 2;
    }
  if (!(x & 0x1))
    {
      x >>= 1;
      ret |= 1;
    }
  return ret;
}
#endif

#ifdef NEED_CTZSI2
unsigned
__ctzsi2 (grub_uint32_t x)
{
  unsigned ret = 0;
  if (!x)
    return 32;

  if (!(x & 0xffff))
    {
      x >>= 16;
      ret |= 16;
    }
  if (!(x & 0xff))
    {
      x >>= 8;
      ret |= 8;
    }
  if (!(x & 0xf))
    {
      x >>= 4;
      ret |= 4;
    }
  if (!(x & 0x3))
    {
      x >>= 2;
      ret |= 2;
    }
  if (!(x & 0x1))
    {
      x >>= 1;
      ret |= 1;
    }
  return ret;
}

#endif


#if defined (__clang__) && !defined(GRUB_EMBED_DECOMPRESSOR)
/* clang emits references to abort().  */
void __attribute__ ((noreturn))
abort (void)
{
  grub_fatal ("compiler abort");
}
#endif

#if (defined (__MINGW32__) || defined (__CYGWIN__))
void __register_frame_info (void)
{
}

void __deregister_frame_info (void)
{
}

void ___chkstk_ms (void)
{
}

void __chkstk_ms (void)
{
}
#endif

union component64
{
  grub_uint64_t full;
  struct
  {
#ifdef GRUB_CPU_WORDS_BIGENDIAN
    grub_uint32_t high;
    grub_uint32_t low;
#else
    grub_uint32_t low;
    grub_uint32_t high;
#endif
  };
};

#if defined (__powerpc__) || defined (__arm__) || defined(__mips__) || \
    (defined(__riscv) && (__riscv_xlen == 32))

/* Based on libgcc2.c from gcc suite.  */
grub_uint64_t
__lshrdi3 (grub_uint64_t u, int b)
{
  if (b == 0)
    return u;

  const union component64 uu = {.full = u};
  const int bm = 32 - b;
  union component64 w;

  if (bm <= 0)
    {
      w.high = 0;
      w.low = (grub_uint32_t) uu.high >> -bm;
    }
  else
    {
      const grub_uint32_t carries = (grub_uint32_t) uu.high << bm;

      w.high = (grub_uint32_t) uu.high >> b;
      w.low = ((grub_uint32_t) uu.low >> b) | carries;
    }

  return w.full;
}

/* Based on libgcc2.c from gcc suite.  */
grub_uint64_t
__ashrdi3 (grub_uint64_t u, int b)
{
  if (b == 0)
    return u;

  const union component64 uu = {.full = u};
  const int bm = 32 - b;
  union component64 w;

  if (bm <= 0)
    {
      /* w.high = 1..1 or 0..0 */
      w.high = ((grub_int32_t) uu.high) >> (32 - 1);
      w.low = ((grub_int32_t) uu.high) >> -bm;
    }
  else
    {
      const grub_uint32_t carries = ((grub_uint32_t) uu.high) << bm;

      w.high = ((grub_int32_t) uu.high) >> b;
      w.low = ((grub_uint32_t) uu.low >> b) | carries;
    }

  return w.full;
}

/* Based on libgcc2.c from gcc suite.  */
grub_uint64_t
__ashldi3 (grub_uint64_t u, int b)
{
  if (b == 0)
    return u;

  const union component64 uu = {.full = u};
  const int bm = 32 - b;
  union component64 w;

  if (bm <= 0)
    {
      w.low = 0;
      w.high = (grub_uint32_t) uu.low << -bm;
    }
  else
    {
      const grub_uint32_t carries = (grub_uint32_t) uu.low >> bm;

      w.low = (grub_uint32_t) uu.low << b;
      w.high = ((grub_uint32_t) uu.high << b) | carries;
    }

  return w.full;
}

/* Based on libgcc2.c from gcc suite.  */
int
__ucmpdi2 (grub_uint64_t a, grub_uint64_t b)
{
  union component64 ac, bc;
  ac.full = a;
  bc.full = b;

  if (ac.high < bc.high)
    return 0;
  else if (ac.high > bc.high)
    return 2;

  if (ac.low < bc.low)
    return 0;
  else if (ac.low > bc.low)
    return 2;
  return 1;
}

#endif

#if defined (__powerpc__) || defined(__mips__) || defined(__sparc__) || \
    defined(__arm__) || defined(__riscv)

/* Based on libgcc2.c from gcc suite.  */
grub_uint32_t
__bswapsi2 (grub_uint32_t u)
{
  return ((((u) & 0xff000000) >> 24)
	  | (((u) & 0x00ff0000) >>  8)
	  | (((u) & 0x0000ff00) <<  8)
	  | (((u) & 0x000000ff) << 24));
}

/* Based on libgcc2.c from gcc suite.  */
grub_uint64_t
__bswapdi2 (grub_uint64_t u)
{
  return ((((u) & 0xff00000000000000ull) >> 56)
	  | (((u) & 0x00ff000000000000ull) >> 40)
	  | (((u) & 0x0000ff0000000000ull) >> 24)
	  | (((u) & 0x000000ff00000000ull) >>  8)
	  | (((u) & 0x00000000ff000000ull) <<  8)
	  | (((u) & 0x0000000000ff0000ull) << 24)
	  | (((u) & 0x000000000000ff00ull) << 40)
	  | (((u) & 0x00000000000000ffull) << 56));
}


#endif

#ifdef __arm__
grub_uint32_t
__aeabi_uidiv (grub_uint32_t a, grub_uint32_t b)
  __attribute__ ((alias ("__udivsi3")));
grub_int32_t
__aeabi_idiv (grub_int32_t a, grub_int32_t b)
  __attribute__ ((alias ("__divsi3")));
void *__aeabi_memcpy (void *dest, const void *src, grub_size_t n)
  __attribute__ ((alias ("grub_memcpy")));
void *__aeabi_memcpy4 (void *dest, const void *src, grub_size_t n)
  __attribute__ ((alias ("grub_memcpy")));
void *__aeabi_memcpy8 (void *dest, const void *src, grub_size_t n)
  __attribute__ ((alias ("grub_memcpy")));
void *__aeabi_memset (void *s, int c, grub_size_t n)
  __attribute__ ((alias ("memset")));

void
__aeabi_memclr (void *s, grub_size_t n)
{
  grub_memset (s, 0, n);
}

void __aeabi_memclr4 (void *s, grub_size_t n)
  __attribute__ ((alias ("__aeabi_memclr")));
void __aeabi_memclr8 (void *s, grub_size_t n)
  __attribute__ ((alias ("__aeabi_memclr")));

int
__aeabi_ulcmp (grub_uint64_t a, grub_uint64_t b)
{
  return __ucmpdi2 (a, b) - 1;
}

grub_uint64_t
__aeabi_lasr (grub_uint64_t u, int b)
  __attribute__ ((alias ("__ashrdi3")));
grub_uint64_t
__aeabi_llsr (grub_uint64_t u, int b)
  __attribute__ ((alias ("__lshrdi3")));

grub_uint64_t
__aeabi_llsl (grub_uint64_t u, int b)
  __attribute__ ((alias ("__ashldi3")));

#endif

#if defined(__mips__) || defined(__riscv) || defined(__sparc__)
/* Based on libgcc from gcc suite. */
int
__clzsi2 (grub_uint32_t val)
{
  int i = 32;
  int j = 16;
  int temp;

  for (; j; j >>= 1)
    {
      if ((temp = val) >> j)
        {
          if (j == 1)
            {
              return (i - 2);
            }
          else
            {
              i -= j;
              val = temp;
            }
        }
    }
  return (i - val);
}
#endif

#if defined(__mips__) || defined(__riscv) || defined(__sparc__)
int
__clzdi2 (grub_uint64_t val)
{
  if (val >> 32)
    {
      return __clzsi2 (val >> 32);
    }
  else
    {
      return __clzsi2 (val) + 32;
    }
}
#endif
