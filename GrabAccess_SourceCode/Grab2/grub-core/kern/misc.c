/* misc.c - definitions of misc functions */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2005,2006,2007,2008,2009,2010  Free Software Foundation, Inc.
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
#include <grub/err.h>
#include <grub/mm.h>
#include <stdarg.h>
#include <grub/term.h>
#include <grub/env.h>
#include <grub/i18n.h>

union printf_arg
{
  /* Yes, type is also part of union as the moment we fill the value
     we don't need to store its type anymore (when we'll need it, we'll
     have format spec again. So save some space.  */
  enum
    {
      INT, LONG, LONGLONG,
      UNSIGNED_INT = 3, UNSIGNED_LONG, UNSIGNED_LONGLONG,
      STRING
    } type;
  long long ll;
};

struct printf_args
{
  union printf_arg prealloc[32];
  union printf_arg *ptr;
  grub_size_t count;
};

static void
parse_printf_args (const char *fmt0, struct printf_args *args,
		   va_list args_in);
static int
grub_vsnprintf_real (char *str, grub_size_t max_len, const char *fmt0,
		     struct printf_args *args);

static void
free_printf_args (struct printf_args *args)
{
  if (args->ptr != args->prealloc)
    grub_free (args->ptr);
}

static int
grub_iswordseparator (int c)
{
  return (grub_isspace (c) || c == ',' || c == ';' || c == '|' || c == '&');
}

/* grub_gettext_dummy is not translating anything.  */
static const char *
grub_gettext_dummy (const char *s)
{
  return s;
}

const char* (*grub_gettext) (const char *s) = grub_gettext_dummy;

void *
grub_memmove (void *dest, const void *src, grub_size_t n)
{
  char *d = (char *) dest;
  const char *s = (const char *) src;

  if (d < s)
    while (n--)
      *d++ = *s++;
  else
    {
      d += n;
      s += n;

      while (n--)
	*--d = *--s;
    }

  return dest;
}

char *
grub_strcpy (char *dest, const char *src)
{
  char *p = dest;

  while ((*p++ = *src++) != '\0')
    ;

  return dest;
}

int
grub_printf (const char *fmt, ...)
{
  va_list ap;
  int ret;

#if defined(MM_DEBUG) && !defined(GRUB_UTIL) && !defined (GRUB_MACHINE_EMU)
  /*
   * To prevent infinite recursion when grub_mm_debug is on, disable it
   * when calling grub_vprintf(). One such call loop is:
   *   grub_vprintf() -> parse_printf_args() -> parse_printf_arg_fmt() ->
   *     grub_debug_calloc() -> grub_printf() -> grub_vprintf().
   */
  int grub_mm_debug_save = 0;

  if (grub_mm_debug)
    {
      grub_mm_debug_save = grub_mm_debug;
      grub_mm_debug = 0;
    }
#endif

  va_start (ap, fmt);
  ret = grub_vprintf (fmt, ap);
  va_end (ap);

#if defined(MM_DEBUG) && !defined(GRUB_UTIL) && !defined (GRUB_MACHINE_EMU)
  grub_mm_debug = grub_mm_debug_save;
#endif

  return ret;
}

int
grub_printf_ (const char *fmt, ...)
{
  va_list ap;
  int ret;

  va_start (ap, fmt);
  ret = grub_vprintf (_(fmt), ap);
  va_end (ap);

  return ret;
}

int
grub_puts_ (const char *s)
{
  return grub_puts (_(s));
}

#if defined (__APPLE__) && ! defined (GRUB_UTIL)
int
grub_err_printf (const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start (ap, fmt);
	ret = grub_vprintf (fmt, ap);
	va_end (ap);

	return ret;
}
#endif

#if ! defined (__APPLE__) && ! defined (GRUB_UTIL)
int grub_err_printf (const char *fmt, ...)
__attribute__ ((alias("grub_printf")));
#endif

int
grub_debug_enabled (const char * condition)
{
  const char *debug, *found;
  grub_size_t clen;
  int ret = 0;

  debug = grub_env_get ("debug");
  if (!debug)
    return 0;

  if (grub_strword (debug, "all"))
    {
      if (debug[3] == '\0')
	return 1;
      ret = 1;
    }

  clen = grub_strlen (condition);
  found = debug-1;
  while(1)
    {
      found = grub_strstr (found+1, condition);

      if (found == NULL)
	break;

      /* Found condition is not a whole word, so ignore it. */
      if (*(found + clen) != '\0' && *(found + clen) != ','
	 && !grub_isspace (*(found + clen)))
	continue;

      /*
       * If found condition is at the start of debug or the start is on a word
       * boundary, then enable debug. Else if found condition is prefixed with
       * '-' and the start is on a word boundary, then disable debug. If none
       * of these cases, ignore.
       */
      if (found == debug || *(found - 1) == ',' || grub_isspace (*(found - 1)))
	ret = 1;
      else if (*(found - 1) == '-' && ((found == debug + 1) || (*(found - 2) == ','
			       || grub_isspace (*(found - 2)))))
	ret = 0;
    }

  return ret;
}

void
grub_real_dprintf (const char *file, const int line, const char *condition,
		   const char *fmt, ...)
{
  va_list args;

  if (grub_debug_enabled (condition))
    {
      grub_printf ("%s:%d:%s: ", file, line, condition);
      va_start (args, fmt);
      grub_vprintf (fmt, args);
      va_end (args);
      grub_refresh ();
    }
}

#define PREALLOC_SIZE 255

int
grub_vprintf (const char *fmt, va_list ap)
{
  grub_size_t s;
  static char buf[PREALLOC_SIZE + 1];
  char *curbuf = buf;
  struct printf_args args;

  parse_printf_args (fmt, &args, ap);

  s = grub_vsnprintf_real (buf, PREALLOC_SIZE, fmt, &args);
  if (s > PREALLOC_SIZE)
    {
      curbuf = grub_malloc (s + 1);
      if (!curbuf)
	{
	  grub_errno = GRUB_ERR_NONE;
	  buf[PREALLOC_SIZE - 3] = '.';
	  buf[PREALLOC_SIZE - 2] = '.';
	  buf[PREALLOC_SIZE - 1] = '.';
	  buf[PREALLOC_SIZE] = 0;
	  curbuf = buf;
	}
      else
	s = grub_vsnprintf_real (curbuf, s, fmt, &args);
    }

  free_printf_args (&args);

  grub_xputs (curbuf);

  if (curbuf != buf)
    grub_free (curbuf);

  return s;
}

int
grub_memcmp (const void *s1, const void *s2, grub_size_t n)
{
  const grub_uint8_t *t1 = s1;
  const grub_uint8_t *t2 = s2;

  while (n--)
    {
      if (*t1 != *t2)
	return (int) *t1 - (int) *t2;

      t1++;
      t2++;
    }

  return 0;
}

int
grub_strcmp (const char *s1, const char *s2)
{
  while (*s1 && *s2)
    {
      if (*s1 != *s2)
	break;

      s1++;
      s2++;
    }

  return (int) (grub_uint8_t) *s1 - (int) (grub_uint8_t) *s2;
}

int
grub_strncmp (const char *s1, const char *s2, grub_size_t n)
{
  if (n == 0)
    return 0;

  while (*s1 && *s2 && --n)
    {
      if (*s1 != *s2)
	break;

      s1++;
      s2++;
    }

  return (int) (grub_uint8_t) *s1 - (int) (grub_uint8_t)  *s2;
}

char *
grub_strchr (const char *s, int c)
{
  do
    {
      if (*s == c)
	return (char *) s;
    }
  while (*s++);

  return 0;
}

char *
grub_strrchr (const char *s, int c)
{
  char *p = NULL;

  do
    {
      if (*s == c)
	p = (char *) s;
    }
  while (*s++);

  return p;
}

char *
grub_strchrnul (const char *s, int c)
{
  do
    {
      if (*s == c)
	break;
    }
  while (*s++);

  return (char *) s;
}

int
grub_strword (const char *haystack, const char *needle)
{
  const char *n_pos = needle;

  while (grub_iswordseparator (*haystack))
    haystack++;

  while (*haystack)
    {
      /* Crawl both the needle and the haystack word we're on.  */
      while(*haystack && !grub_iswordseparator (*haystack)
            && *haystack == *n_pos)
        {
          haystack++;
          n_pos++;
        }

      /* If we reached the end of both words at the same time, the word
      is found. If not, eat everything in the haystack that isn't the
      next word (or the end of string) and "reset" the needle.  */
      if ( (!*haystack || grub_iswordseparator (*haystack))
         && (!*n_pos || grub_iswordseparator (*n_pos)))
        return 1;
      else
        {
          n_pos = needle;
          while (*haystack && !grub_iswordseparator (*haystack))
            haystack++;
          while (grub_iswordseparator (*haystack))
            haystack++;
        }
    }

  return 0;
}

int
grub_isspace (int c)
{
  return (c == '\n' || c == '\r' || c == ' ' || c == '\t');
}

unsigned long
grub_strtoul (const char * restrict str, const char ** const restrict end,
	      int base)
{
  unsigned long long num;

  num = grub_strtoull (str, end, base);
#if GRUB_CPU_SIZEOF_LONG != 8
  if (num > ~0UL)
    {
      grub_error (GRUB_ERR_OUT_OF_RANGE, N_("overflow is detected"));
      return ~0UL;
    }
#endif

  return (unsigned long) num;
}

unsigned long long
grub_strtoull (const char * restrict str, const char ** const restrict end,
	       int base)
{
  unsigned long long num = 0;
  int found = 0;

  /* Skip white spaces.  */
  /* grub_isspace checks that *str != '\0'.  */
  while (grub_isspace (*str))
    str++;

  /* Guess the base, if not specified. The prefix `0x' means 16, and
     the prefix `0' means 8.  */
  if (str[0] == '0')
    {
      if (str[1] == 'x')
	{
	  if (base == 0 || base == 16)
	    {
	      base = 16;
	      str += 2;
	    }
	}
      else if (base == 0 && str[1] >= '0' && str[1] <= '7')
	base = 8;
    }

  if (base == 0)
    base = 10;

  while (*str)
    {
      unsigned long digit;

      digit = grub_tolower (*str) - '0';
      if (digit > 9)
	{
	  digit += '0' - 'a' + 10;
	  /* digit <= 9 check is needed to keep chars larger than
	     '9' but less than 'a' from being read as numbers */
	  if (digit >= (unsigned long) base || digit <= 9)
	    break;
	}
      if (digit >= (unsigned long) base)
	break;

      found = 1;

      /* NUM * BASE + DIGIT > ~0ULL */
      if (num > grub_divmod64 (~0ULL - digit, base, 0))
	{
	  grub_error (GRUB_ERR_OUT_OF_RANGE,
		      N_("overflow is detected"));

          if (end)
            *end = (char *) str;

	  return ~0ULL;
	}

      num = num * base + digit;
      str++;
    }

  if (! found)
    {
      grub_error (GRUB_ERR_BAD_NUMBER,
		  N_("unrecognized number"));

      if (end)
        *end = (char *) str;

      return 0;
    }

  if (end)
    *end = (char *) str;

  return num;
}

char *
grub_strdup (const char *s)
{
  grub_size_t len;
  char *p;

  if (!s)
    return grub_zalloc (1);

  len = grub_strlen (s) + 1;
  p = (char *) grub_malloc (len);
  if (! p)
    return 0;

  return grub_memcpy (p, s, len);
}

char *
grub_strndup (const char *s, grub_size_t n)
{
  grub_size_t len;
  char *p;

  len = grub_strlen (s);
  if (len > n)
    len = n;
  p = (char *) grub_malloc (len + 1);
  if (! p)
    return 0;

  grub_memcpy (p, s, len);
  p[len] = '\0';
  return p;
}

/* clang detects that we're implementing here a memset so it decides to
   optimise and calls memset resulting in infinite recursion. With volatile
   we make it not optimise in this way.  */
#ifdef __clang__
#define VOLATILE_CLANG volatile
#else
#define VOLATILE_CLANG
#endif

void *
grub_memset (void *s, int c, grub_size_t len)
{
  void *p = s;
  grub_uint8_t pattern8 = c;

  if (len >= 3 * sizeof (unsigned long))
    {
      unsigned long patternl = 0;
      grub_size_t i;

      for (i = 0; i < sizeof (unsigned long); i++)
	patternl |= ((unsigned long) pattern8) << (8 * i);

      while (len > 0 && (((grub_addr_t) p) & (sizeof (unsigned long) - 1)))
	{
	  *(VOLATILE_CLANG grub_uint8_t *) p = pattern8;
	  p = (grub_uint8_t *) p + 1;
	  len--;
	}
      while (len >= sizeof (unsigned long))
	{
	  *(VOLATILE_CLANG unsigned long *) p = patternl;
	  p = (unsigned long *) p + 1;
	  len -= sizeof (unsigned long);
	}
    }

  while (len > 0)
    {
      *(VOLATILE_CLANG grub_uint8_t *) p = pattern8;
      p = (grub_uint8_t *) p + 1;
      len--;
    }

  return s;
}

grub_size_t
grub_strlen (const char *s)
{
  const char *p = s;

  while (*p)
    p++;

  return p - s;
}

static const char *
scan_str (const char *s1, const char *s2)
{
  while (*s1)
    {
      const char *p = s2;

      while (*p)
	{
	  if (*s1 == *p)
	    return s1;
	  p++;
	}

      s1++;
    }

  return s1;
}

grub_size_t
grub_strspn (const char *s, const char *accept)
{
  const char *p;
  const char *a;
  grub_size_t count = 0;

  for (p = s; *p != '\0'; ++p)
    {
      for (a = accept; *a != '\0'; ++a)
        if (*p == *a)
          break;
      if (*a == '\0')
        return count;
      else
        ++count;
    }

  return count;
}

int
grub_strcspn (const char *s1, const char *s2)
{
  const char *r;

  r = scan_str (s1, s2);
  return r - s1;
}

char* grub_strtok(char *str, const char *delim)
{
  static char *nxt;
  static int size;
  int i;
  if(str != NULL)
  {
    nxt = str;
    size = grub_strlen(str);
  }
  else if(size > 0)
  {
    nxt++;
    size--;
    str = nxt;
  }
  else
    str = NULL;

  while(*nxt)
  {
    i = grub_strspn(nxt, delim);
    while(i > 1)
    {
        *nxt = '\0';
        nxt++;
        size--;
        i--;
    }
    if(1 == i)
    {
        *nxt = '\0';
        if(size > 1)
        {
            nxt--;
            size++;
        }
    }
    nxt++;
    size--;
  }
  return str;
}

char *
grub_strpbrk (const char *s1, const char *s2)
{
  const char *r;

  r = scan_str (s1, s2);
  return (*r) ? (char *) r : 0;
}

static inline void
grub_reverse (char *str)
{
  char *p = str + grub_strlen (str) - 1;

  while (str < p)
    {
      char tmp;

      tmp = *str;
      *str = *p;
      *p = tmp;
      str++;
      p--;
    }
}

/* Divide N by D, return the quotient, and store the remainder in *R.  */
grub_uint64_t
grub_divmod64 (grub_uint64_t n, grub_uint64_t d, grub_uint64_t *r)
{
  /* This algorithm is typically implemented by hardware. The idea
     is to get the highest bit in N, 64 times, by keeping
     upper(N * 2^i) = (Q * D + M), where upper
     represents the high 64 bits in 128-bits space.  */
  unsigned bits = 64;
  grub_uint64_t q = 0;
  grub_uint64_t m = 0;

  /* ARM and IA64 don't have a fast 32-bit division.
     Using that code would just make us use software division routines, calling
     ourselves indirectly and hence getting infinite recursion.
  */
#if !GRUB_DIVISION_IN_SOFTWARE
  /* Skip the slow computation if 32-bit arithmetic is possible.  */
  if (n < 0xffffffff && d < 0xffffffff)
    {
      if (r)
	*r = ((grub_uint32_t) n) % (grub_uint32_t) d;

      return ((grub_uint32_t) n) / (grub_uint32_t) d;
    }
#endif

  while (bits--)
    {
      m <<= 1;

      if (n & (1ULL << 63))
	m |= 1;

      q <<= 1;
      n <<= 1;

      if (m >= d)
	{
	  q |= 1;
	  m -= d;
	}
    }

  if (r)
    *r = m;

  return q;
}

/* Convert a long long value to a string. This function avoids 64-bit
   modular arithmetic or divisions.  */
static inline char *
grub_lltoa (char *str, int c, unsigned long long n)
{
  unsigned base = ((c == 'x') || (c == 'X')) ? 16 : 10;
  char *p;

  if ((long long) n < 0 && c == 'd')
    {
      n = (unsigned long long) (-((long long) n));
      *str++ = '-';
    }

  p = str;

  if (base == 16)
    do
      {
	unsigned d = (unsigned) (n & 0xf);
	*p++ = (d > 9) ? d + ((c == 'x') ? 'a' : 'A') - 10 : d + '0';
      }
    while (n >>= 4);
  else
    /* BASE == 10 */
    do
      {
	grub_uint64_t m;

	n = grub_divmod64 (n, 10, &m);
	*p++ = m + '0';
      }
    while (n);

  *p = 0;

  grub_reverse (str);
  return p;
}

/*
 * Parse printf() fmt0 string into args arguments.
 *
 * The parsed arguments are either used by a printf() function to format the fmt0
 * string or they are used to compare a format string from an untrusted source
 * against a format string with expected arguments.
 *
 * When the fmt_check is set to !0, e.g. 1, then this function is executed in
 * printf() format check mode. This enforces stricter rules for parsing the
 * fmt0 to limit exposure to possible errors in printf() handling. It also
 * disables positional parameters, "$", because some formats, e.g "%s%1$d",
 * cannot be validated with the current implementation.
 *
 * The max_args allows to set a maximum number of accepted arguments. If the fmt0
 * string defines more arguments than the max_args then the parse_printf_arg_fmt()
 * function returns an error. This is currently used for format check only.
 */
static grub_err_t
parse_printf_arg_fmt (const char *fmt0, struct printf_args *args,
		      int fmt_check, grub_size_t max_args)
{
  const char *fmt;
  char c;
  grub_size_t n = 0;

  args->count = 0;

  COMPILE_TIME_ASSERT (sizeof (int) == sizeof (grub_uint32_t));
  COMPILE_TIME_ASSERT (sizeof (int) <= sizeof (long long));
  COMPILE_TIME_ASSERT (sizeof (long) <= sizeof (long long));
  COMPILE_TIME_ASSERT (sizeof (long long) == sizeof (void *)
		       || sizeof (int) == sizeof (void *));

  fmt = fmt0;
  while ((c = *fmt++) != 0)
    {
      if (c != '%')
	continue;

      if (*fmt =='-')
	fmt++;

      while (grub_isdigit (*fmt))
	fmt++;

      if (*fmt == '$')
	{
	  if (fmt_check)
	    return grub_error (GRUB_ERR_BAD_ARGUMENT,
			       "positional arguments are not supported");
	  fmt++;
	}

      if (*fmt =='-')
	fmt++;

      while (grub_isdigit (*fmt))
	fmt++;

      if (*fmt =='.')
	fmt++;

      while (grub_isdigit (*fmt))
	fmt++;

      if (*fmt == '*') {
	args->count++;
	fmt++;
      }

      c = *fmt++;
      if (c == 'l')
	c = *fmt++;
      if (c == 'l')
	c = *fmt++;

      switch (c)
	{
	case 'p':
	case 'x':
	case 'X':
	case 'u':
	case 'd':
	case 'c':
	case 'C':
	case 's':
	  args->count++;
	  break;
	case '%':
	  /* "%%" is the escape sequence to output "%". */
	  break;
	default:
	  if (fmt_check)
	    return grub_error (GRUB_ERR_BAD_ARGUMENT, "unexpected format");
	  break;
	}
    }

  if (fmt_check && args->count > max_args)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "too many arguments");

  if (args->count <= ARRAY_SIZE (args->prealloc))
    args->ptr = args->prealloc;
  else
    {
      args->ptr = grub_calloc (args->count, sizeof (args->ptr[0]));
      if (!args->ptr)
	{
	  if (fmt_check)
	    return grub_errno;

	  grub_errno = GRUB_ERR_NONE;
	  args->ptr = args->prealloc;
	  args->count = ARRAY_SIZE (args->prealloc);
	}
    }

  grub_memset (args->ptr, 0, args->count * sizeof (args->ptr[0]));

  fmt = fmt0;
  n = 0;
  while ((c = *fmt++) != 0)
    {
      int longfmt = 0;
      grub_size_t curn;
      const char *p;

      if (c != '%')
	continue;

      curn = n++;

      if (*fmt =='-')
	fmt++;

      p = fmt;

      while (grub_isdigit (*fmt))
	fmt++;

      if (*fmt == '$')
	{
	  curn = grub_strtoull (p, 0, 10) - 1;
	  fmt++;
	}

      if (*fmt =='-')
	fmt++;

      while (grub_isdigit (*fmt))
	fmt++;

      if (*fmt =='.')
	fmt++;

      while (grub_isdigit (*fmt))
	fmt++;

      if (*fmt == '*') {
	fmt++;
	args->ptr[curn].type = INT;
	curn = n++;
      }

      c = *fmt++;
      if (c == '%')
	{
	  n--;
	  continue;
	}

      if (c == 'l')
	{
	  c = *fmt++;
	  longfmt = 1;
	}
      if (c == 'l')
	{
	  c = *fmt++;
	  longfmt = 2;
	}
      if (curn >= args->count)
	continue;
      switch (c)
	{
	case 'x':
	case 'X':
	case 'u':
	  args->ptr[curn].type = UNSIGNED_INT + longfmt;
	  break;
	case 'd':
	  args->ptr[curn].type = INT + longfmt;
	  break;
	case 'p':
	  if (sizeof (void *) == sizeof (long long))
	    args->ptr[curn].type = UNSIGNED_LONGLONG;
	  else
	    args->ptr[curn].type = UNSIGNED_INT;
	  break;
	case 's':
	  args->ptr[curn].type = STRING;
	  break;
	case 'C':
	case 'c':
	  args->ptr[curn].type = INT;
	  break;
	}
    }

  return GRUB_ERR_NONE;
}

static void
parse_printf_args (const char *fmt0, struct printf_args *args, va_list args_in)
{
  grub_size_t n;

  parse_printf_arg_fmt (fmt0, args, 0, 0);

  for (n = 0; n < args->count; n++)
    switch (args->ptr[n].type)
      {
      case INT:
	args->ptr[n].ll = va_arg (args_in, int);
	break;
      case LONG:
	args->ptr[n].ll = va_arg (args_in, long);
	break;
      case UNSIGNED_INT:
	args->ptr[n].ll = va_arg (args_in, unsigned int);
	break;
      case UNSIGNED_LONG:
	args->ptr[n].ll = va_arg (args_in, unsigned long);
	break;
      case LONGLONG:
      case UNSIGNED_LONGLONG:
	args->ptr[n].ll = va_arg (args_in, long long);
	break;
      case STRING:
	if (sizeof (void *) == sizeof (long long))
	  args->ptr[n].ll = va_arg (args_in, long long);
	else
	  args->ptr[n].ll = va_arg (args_in, unsigned int);
	break;
      }
}

static inline void __attribute__ ((always_inline))
write_char (char *str, grub_size_t *count, grub_size_t max_len, unsigned char ch)
{
  if (*count < max_len)
    str[*count] = ch;

  (*count)++;
}

static int
grub_vsnprintf_real (char *str, grub_size_t max_len, const char *fmt0,
		     struct printf_args *args)
{
  char c;
  grub_size_t n = 0;
  grub_size_t count = 0;
  const char *fmt;

  fmt = fmt0;

  while ((c = *fmt++) != 0)
    {
      unsigned int format1 = 0;
      unsigned int format2 = ~ 0U;
      char zerofill = ' ';
      char rightfill = 0;
      grub_size_t curn;

      if (c != '%')
	{
	  write_char (str, &count, max_len,c);
	  continue;
	}

      if (*fmt == '%')
	{
	  write_char (str, &count, max_len, '%');
	  fmt++;
	  continue;
	}

      curn = n++;

    rescan:;

      if (*fmt =='-')
	{
	  rightfill = 1;
	  fmt++;
	}

      /* Read formatting parameters.  */
      if (grub_isdigit (*fmt))
	{
	  if (fmt[0] == '0')
	    zerofill = '0';
	  format1 = grub_strtoul (fmt, &fmt, 10);
	}

      if (*fmt == '.')
	fmt++;

      if (grub_isdigit (*fmt))
	format2 = grub_strtoul (fmt, &fmt, 10);

      if (*fmt == '*')
        {
	   fmt++;
	   format1 = (unsigned long) args->ptr[curn].ll;
	   curn++;
	}

      if (*fmt == '$')
	{
	  curn = format1 - 1;
	  fmt++;
	  format1 = 0;
	  format2 = ~ 0U;
	  zerofill = ' ';
	  rightfill = 0;

	  goto rescan;
	}

      c = *fmt++;
      if (c == 'l')
	c = *fmt++;
      if (c == 'l')
	c = *fmt++;

      if (c == '%')
	{
	  write_char (str, &count, max_len,c);
	  n--;
	  continue;
	}

      if (curn >= args->count)
	continue;

      long long curarg = args->ptr[curn].ll;

      switch (c)
	{
	case 'p':
	  write_char (str, &count, max_len, '0');
	  write_char (str, &count, max_len, 'x');
	  c = 'x';
	  /* Fall through. */
	case 'x':
	case 'X':
	case 'u':
	case 'd':
	  {
	    char tmp[32];
	    const char *p = tmp;
	    grub_size_t len;
	    grub_size_t fill;

	    len = grub_lltoa (tmp, c, curarg) - tmp;
	    fill = len < format1 ? format1 - len : 0;
	    if (! rightfill)
	      while (fill--)
		write_char (str, &count, max_len, zerofill);
	    while (*p)
	      write_char (str, &count, max_len, *p++);
	    if (rightfill)
	      while (fill--)
		write_char (str, &count, max_len, zerofill);
	  }
	  break;

	case 'c':
	  write_char (str, &count, max_len,curarg & 0xff);
	  break;

	case 'C':
	  {
	    grub_uint32_t code = curarg;
	    int shift;
	    unsigned mask;

	    if (code <= 0x7f)
	      {
		shift = 0;
		mask = 0;
	      }
	    else if (code <= 0x7ff)
	      {
		shift = 6;
		mask = 0xc0;
	      }
	    else if (code <= 0xffff)
	      {
		shift = 12;
		mask = 0xe0;
	      }
	    else if (code <= 0x10ffff)
	      {
		shift = 18;
		mask = 0xf0;
	      }
	    else
	      {
		code = '?';
		shift = 0;
		mask = 0;
	      }

	    write_char (str, &count, max_len,mask | (code >> shift));

	    for (shift -= 6; shift >= 0; shift -= 6)
	      write_char (str, &count, max_len,0x80 | (0x3f & (code >> shift)));
	  }
	  break;

	case 's':
	  {
	    grub_size_t len = 0;
	    grub_size_t fill;
	    const char *p = ((char *) (grub_addr_t) curarg) ? : "(null)";
	    grub_size_t i;

	    while (len < format2 && p[len])
	      len++;

	    fill = len < format1 ? format1 - len : 0;

	    if (!rightfill)
	      while (fill--)
		write_char (str, &count, max_len, zerofill);

	    for (i = 0; i < len; i++)
	      write_char (str, &count, max_len,*p++);

	    if (rightfill)
	      while (fill--)
		write_char (str, &count, max_len, zerofill);
	  }

	  break;

	default:
	  write_char (str, &count, max_len,c);
	  break;
	}
    }

  if (count < max_len)
    str[count] = '\0';
  else
    str[max_len] = '\0';
  return count;
}

int
grub_vsnprintf (char *str, grub_size_t n, const char *fmt, va_list ap)
{
  grub_size_t ret;
  struct printf_args args;

  if (!n)
    return 0;

  n--;

  parse_printf_args (fmt, &args, ap);

  ret = grub_vsnprintf_real (str, n, fmt, &args);

  free_printf_args (&args);

  return ret < n ? ret : n;
}

int
grub_snprintf (char *str, grub_size_t n, const char *fmt, ...)
{
  va_list ap;
  int ret;

  va_start (ap, fmt);
  ret = grub_vsnprintf (str, n, fmt, ap);
  va_end (ap);

  return ret;
}

char *
grub_xvasprintf (const char *fmt, va_list ap)
{
  grub_size_t s, as = PREALLOC_SIZE;
  char *ret;
  struct printf_args args;

  parse_printf_args (fmt, &args, ap);

  while (1)
    {
      ret = grub_malloc (as + 1);
      if (!ret)
	{
	  free_printf_args (&args);
	  return NULL;
	}

      s = grub_vsnprintf_real (ret, as, fmt, &args);

      if (s <= as)
	{
	  free_printf_args (&args);
	  return ret;
	}

      grub_free (ret);
      as = s;
    }
}

char *
grub_xasprintf (const char *fmt, ...)
{
  va_list ap;
  char *ret;

  va_start (ap, fmt);
  ret = grub_xvasprintf (fmt, ap);
  va_end (ap);

  return ret;
}

/**
 * Build a table of 256, with values 1 or 0, if character is allowed or not.
 * So, if A is allowed, set[65] = 1.
 * If we will have a character, to see if it is matching, just check the table,
 * for it's ascii code.
 * It can be simplified with a bitmap, but.
 */
static int
build_set (const char **fmt, char *set, int len)
{
  unsigned char c, prev, next;
  char neg = 0;
  int i; 

  if (*(*fmt)++ != '[')
    return -1;
  if (**fmt == '^')
  {
    (*fmt)++;
    neg = 1;
  }
  if (neg == 1 && **fmt == ']')
    (*fmt)++;

  grub_memset(set, neg, len);

  for (; **fmt; (*fmt)++)
  {
    c = **fmt;
    // - is allowed at the start and end of the set
    if (c == '-')
    {
      prev = *(*fmt - 1);
      next = *(*fmt + 1); 
      if (prev == '[' || next == ']')
        set[c] = !neg;
      else
        for (i = prev; i <= next; i++) set[i] = !neg;
    }
    else if (c == ']')
      return 0;
    else
      set[c] = !neg;
  }
  return -1;
}

/**
 * Check if the first letter c of a string, can be the start of a valid
 *  integer in base n, having sign or not. 
 */
static int
valid_sint (char c, int base, int sign)
{
  if (base == 2 && c >= '0' && c < '2')
    return 0;
  else if (base == 8 && c >= '0' && c < '8')
    return 0;
  else if (base == 10 &&
           ((c >= '0' && c <= '9') || (sign && (c == '-' || c == '+'))))
    return 0;
  else if (base == 16 && ((c >= '0' && c <= '9') || 
           (c >='a' && c <= 'f') || (c >='A' && c <= 'F')))
    return 0;
  return -1;
}

/**
 * Having a string, consumes width chars from it, and return an integer
 * in base base, having sign or not.
 * Will work for base 2, 8, 10, 16
 * For base 16 will skip 0x infront of number.
 * For base 10, if signed, will consider - infront of number.
 * For base 8, should skip 0
 * I should reimplement this using sets. 
 */
static long long
get_int (const char **str, int width, int base, int sign)
{
  long long n = 0;
  int w = width > 0;
  int neg = 0;
  int xskip = 0;
  char c;

  if (base != 2 && base != 8 && base != 10 && base != 16)
    return 0;

  for (n = 0; **str; (*str)++)
  {
    c = **str;
    if (sign && neg == 0 && base == 10 && c == '-')
    {
      neg = 1;
      if (w) width--;
      continue;
    }
    if (base == 16 && !xskip && (c == 'x' || c == 'X'))
    {
      xskip = 1;
      n = 0;
      continue;
    }
    if (w && width-- == 0)
      break;
    c = c >= 'a' ? c - ('a' - 'A') : c; // to upper
    if (!grub_isdigit(c))
    {
      if (base != 16)
        break;
      else if (c < 'A' || c > 'F')
        break;
    }
    if (base == 2 && c > '1')
      break;
    else if (base == 8 && c > '7')
      break;
    if (base == 16 && c >= 'A')
      c = c - 'A' + 10 + '0';
    n = n * base + c - '0';
  }
  if (neg && n > 0)
    n = -n;
  return n;
}

/**
 * Gets a string from str and puts it into ptr, if skip is not set
 * if ptr is NULL, it will consume the string, but not save it
 * if width is set, it will stop after max width characters.
 * if set[256] is set, it will only accept characters from the set,
 * eg: set['a'] = 1 - it will accept 'a'
 * otherwise it will stop on first space, or end of string.
 * Returns the number of characters matched
 */
static int
get_str (const char **str, char *ptr, char *set, int width)
{
  int n, w, skip;
  unsigned char c;
  w = (width > 0);
  skip = (ptr == NULL);

  for (n = 0; **str; (*str)++, n++)
  {
    c = **str;
    if ((w && width-- == 0) || (!set && grub_isspace(c)))
      break;
    if (set && (set[c] == 0))
      break;
    if (!skip)
      *ptr++ = c;
  }
  if (!skip)
    *ptr = 0;
  return n;
}

/* Flags */
#define F_SKIP   0001   // don't save matched value - '*'
#define F_ALLOC  0002   // Allocate memory for the pointer - 'm'
#define F_SIGNED 0004   // is this a signed number (%d,%i)?

/* Format states */
#define S_DEFAULT   0
#define S_FLAGS     1
#define S_WIDTH     2
#define S_PRECIS    3
#define S_LENGTH    4
#define S_CONV      5

/* Lenght flags */
#define L_CHAR      1
#define L_SHORT     2
#define L_LONG      3
#define L_LLONG     4
#define L_DOUBLE    5

/**
 * Shrinked down, vsscanf implementation.
 *  This will not handle floating numbers (yet), nor allocated (gnu) pointers.
 */
int
grub_vsscanf (const char *str, const char *fmt, va_list ap)
{
  grub_size_t n = 0; // number of matched input items
  char state = S_DEFAULT;
  void *ptr;
  long long num;
  int base, sign, flags = 0, width, lflags;
  char set[256];

  if (!fmt)
    return 0;
  for (; *fmt && *str; fmt++)
  {
    if (state == S_DEFAULT)
    {
      if (*fmt == '%')
      {
        flags = 0;
        state = S_FLAGS;
      }
      else if (grub_isspace(*fmt))
      {
        while (grub_isspace(*str))
          str++;
      }
      else
      {
        if (*fmt != *str++)
          break;
      }
      continue;
    }
    if (state == S_FLAGS)
    {
      switch (*fmt)
      {
        case '*': flags = F_SKIP; break;
        case 'm': if ((flags & F_SKIP) == 0) flags = F_ALLOC; break;
        default: width = 0; state = S_WIDTH;
      }
    }
    if (state == S_WIDTH)
    {
      if (grub_isdigit(*fmt) && *fmt > '0')
        width = get_int(&fmt, 0, 10, 0);
      lflags = 0;
      state = S_LENGTH;
    }
    if (state == S_LENGTH)
    {
      switch (*fmt)
      {
        case 'h': lflags = lflags == L_CHAR ? L_SHORT : L_CHAR; break;
        case 'l': lflags = lflags == L_LONG ? L_LLONG : L_LONG; break;
        case 'L': lflags = L_DOUBLE; break;
        default: state = S_CONV;
      }
    }
    if (state == S_CONV)
    {
      if (grub_strchr("douixXb", *fmt))
      {
        state = S_DEFAULT;
        base = 10;
        sign = 0;
        if (*fmt == 'd' || *fmt == 'i') 
          sign = 1;
        if (*fmt == 'b')
          base = 2;
        else if (*fmt == 'o')
          base = 8;
        else if (*fmt == 'x' || *fmt == 'X')
          base = 16;

        /* Numbers should skip starting spaces "  123l", 
         *  strings, chars not */
        while (grub_isspace(*str)) 
          str++;

        if (valid_sint(*str, base, sign) < 0)
          break;

        num = get_int(&str, width, base, sign);
        if (flags & F_SKIP)
        {
          continue;
        }
        ptr = va_arg(ap, void *);
        switch (lflags)
        {
          case L_DOUBLE:
          case L_LLONG: *(long long *)ptr = num; break;
          case L_LONG: *(long *) ptr = num; break; 
          case L_SHORT: *(short *) ptr = num; break;
          case L_CHAR: *(char *) ptr = num; break;
          default: *(int *) ptr = num;
        }
        n++;
      }
      else if ('c' == *fmt)
      {
        state = S_DEFAULT;
        if (flags & F_SKIP)
        {
          str++;
          continue;
        }
        ptr = va_arg(ap, void *);
        *(char *)ptr = *(str)++;
        n++;
      }
      else if ('s' == *fmt)
      {
        state = S_DEFAULT;
        if (flags & F_SKIP)
        {
          get_str(&str, NULL, NULL, width);
          continue;
        }
        ptr = va_arg(ap, void *);
        get_str(&str, (char *)ptr, NULL, width);
        n++;
      }
      else if ('[' == *fmt)
      {
        state = S_DEFAULT;
        if (build_set(&fmt, set, sizeof(set)) < 0)
          break;
        if (flags & F_SKIP)
        {
          get_str (&str, NULL, set, width);
          continue;
        }
        ptr = va_arg(ap, void *);
        get_str(&str, ptr, set, width);
      }
      else if ('%' == *fmt)
      {
        state = S_DEFAULT;
        if (*str != '%')
          break;
        str++;
      }
      else
      {
        break;
      }
    }
  }
  return n;
}

int
grub_sscanf (const char *str, const char *format, ...)
{
  va_list ap;
  int ret;
  va_start (ap, format);
  ret = grub_vsscanf (str, format, ap);
  va_end (ap);
  return ret;
}

grub_err_t
grub_printf_fmt_check (const char *fmt, const char *fmt_expected)
{
  struct printf_args args_expected, args_fmt;
  grub_err_t ret;
  grub_size_t n;

  if (fmt == NULL || fmt_expected == NULL)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid format");

  ret = parse_printf_arg_fmt (fmt_expected, &args_expected, 1, GRUB_SIZE_MAX);
  if (ret != GRUB_ERR_NONE)
    return ret;

  /* Limit parsing to the number of expected arguments. */
  ret = parse_printf_arg_fmt (fmt, &args_fmt, 1, args_expected.count);
  if (ret != GRUB_ERR_NONE)
    {
      free_printf_args (&args_expected);
      return ret;
    }

  for (n = 0; n < args_fmt.count; n++)
    if (args_fmt.ptr[n].type != args_expected.ptr[n].type)
     {
	ret = grub_error (GRUB_ERR_BAD_ARGUMENT, "arguments types do not match");
	break;
     }

  free_printf_args (&args_expected);
  free_printf_args (&args_fmt);

  return ret;
}


/* Abort GRUB. This function does not return.  */
static void __attribute__ ((noreturn))
grub_abort (void)
{
  grub_printf ("\nAborted.");
  
#ifndef GRUB_UTIL
  if (grub_term_inputs)
#endif
    {
      grub_printf (" Press any key to exit.");
      grub_getkey ();
    }

  grub_exit (1);
}

#if defined (__clang__) && !defined (GRUB_UTIL)
/* clang emits references to abort().  */
void __attribute__ ((noreturn))
abort (void)
{
  grub_abort ();
}
#endif

void
grub_fatal (const char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  grub_vprintf (_(fmt), ap);
  va_end (ap);

  grub_refresh ();

  grub_abort ();
}

#if BOOT_TIME_STATS

#include <grub/time.h>

struct grub_boot_time *grub_boot_time_head;
static struct grub_boot_time **boot_time_last = &grub_boot_time_head;

void
grub_real_boot_time (const char *file,
		     const int line,
		     const char *fmt, ...)
{
  struct grub_boot_time *n;
  va_list args;

  grub_error_push ();
  n = grub_malloc (sizeof (*n));
  if (!n)
    {
      grub_errno = 0;
      grub_error_pop ();
      return;
    }
  n->file = file;
  n->line = line;
  n->tp = grub_get_time_ms ();
  n->next = 0;

  va_start (args, fmt);
  n->msg = grub_xvasprintf (fmt, args);    
  va_end (args);

  *boot_time_last = n;
  boot_time_last = &n->next;

  grub_errno = 0;
  grub_error_pop ();
}
#endif
