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

#ifndef MAP_POSIX_STRING_H
#define MAP_POSIX_STRING_H	1

#include <grub/misc.h>
#include <stdint.h>
#include <stddef.h>

#define HAVE_STRCASECMP 1

static inline grub_size_t
strlen (const char *s)
{
  return grub_strlen (s);
}

static inline int
strcspn (const char *s1, const char *s2)
{
  return grub_strcspn (s1, s2);
}

static inline char *
strpbrk (const char *s1, const char *s2)
{
  return grub_strpbrk (s1, s2);
}

static inline int 
strcmp (const char *s1, const char *s2)
{
  return grub_strcmp (s1, s2);
}

static inline int
strncmp (const char *str1, const char *str2, size_t n)
{
  return grub_strncmp (str1, str2, n);
}

static inline int 
strcasecmp (const char *s1, const char *s2)
{
  return grub_strcasecmp (s1, s2);
}

static inline void
bcopy (const void *src, void *dest, grub_size_t n)
{
  grub_memcpy (dest, src, n);
}

static inline char *
strcpy (char *dest, const char *src)
{
  return grub_strcpy (dest, src);
}

static inline char *
strcat (char *dest, const char *src)
{
  return grub_strcat (dest, src);
}

static inline char *
strstr (const char *haystack, const char *needle)
{
  return grub_strstr (haystack, needle);
}

static inline char *
strchr (const char *s, int c)
{
  return grub_strchr (s, c);
}

static inline char *
strrchr (const char *s, int c)
{
    return grub_strrchr(s, c);
}

static inline char *
strncpy (char *dest, const char *src, grub_size_t n)
{
  return grub_strncpy (dest, src, n);
}

static inline int
strcoll (const char *s1, const char *s2)
{
  return grub_strcmp (s1, s2);
}

static inline unsigned long int
strtoul (const char *str, const char **endptr, int base)
{
  return grub_strtoul (str, endptr, base);
}

static inline long
strtol (const char *str, const char **endptr, int base)
{
  return grub_strtol (str, endptr, base);
}


static inline void *
memchr (const void *s, int c, grub_size_t n)
{
  return grub_memchr (s, c, n);
}

#define memcmp grub_memcmp
#define memcpy grub_memcpy
#define memmove grub_memmove
#define memset grub_memset

static inline int iswlower (wint_t c)
{
  return grub_islower (c);
}

static inline int iswupper (wint_t c)
{
  return grub_isupper (c);
}

static inline int towupper (wint_t c)
{
  return grub_toupper (c);
}

static inline int iswspace (wint_t c)
{
  return grub_isspace (c);
}

static inline int
wcscasecmp (const wchar_t *str1, const wchar_t *str2)
{
  int c1;
  int c2;
  do
  {
    c1 = towupper (*(str1++));
    c2 = towupper (*(str2++));
  }
  while ((c1 != L'\0') && (c1 == c2));
  return (c1 - c2);
}

static inline size_t
wcslen (const wchar_t *str)
{
  size_t len = 0;
  while (*(str++))
    len++;
  return len;
}

static inline wchar_t *
wcschr (const wchar_t *str, wchar_t c)
{
  for (; *str ; str++)
  {
    if (*str == c)
      return ((wchar_t *)str);
  }
  return NULL;
}


static inline size_t
mbstowcs (wchar_t *dst, const char *src, size_t n)
{
  if (!dst || !src)
    return 0;
  const char *p;
  wchar_t *q;
  size_t i;
  p = src;
  q = dst;
  for (i = 1; i <= n; i++)
  {
    *q++ = *p++;
    if (*p == '\0')
      break;
  }
  *q = L'\0';
  return i;
}

static inline size_t
wcstombs (char *dst, const wchar_t *src, size_t n)
{
  if (!dst || !src)
    return 0;
  const wchar_t *p;
  char *q;
  size_t i;
  p = src;
  q = dst;
  for (i = 1; i <= n; i++)
  {
    *q++ = *p++;
    if (*p == L'\0')
      break;
  }
  *q = '\0';
  return i;
}

#endif
