/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2016  Free Software Foundation, Inc.
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

#ifdef URL_TEST

#define _GNU_SOURCE 1

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define N_(x) x

#define grub_malloc(x) malloc(x)
#define grub_free(x) ({if (x) free(x);})
#define grub_error(a, fmt, args...) printf(fmt "\n", ## args)
#define grub_dprintf(a, fmt, args...) printf(a ": " fmt, ## args)
#define grub_strlen(x) strlen(x)
#define grub_strdup(x) strdup(x)
#define grub_strstr(x,y) strstr(x,y)
#define grub_memcpy(x,y,z) memcpy(x,y,z)
#define grub_strcmp(x,y) strcmp(x,y)
#define grub_strncmp(x,y,z) strncmp(x,y,z)
#define grub_strcasecmp(x,y) strcasecmp(x,y)
#define grub_strchrnul(x,y) strchrnul(x,y)
#define grub_strchr(x,y) strchr(x,y)
#define grub_strndup(x,y) strndup(x,y)
#define grub_strtoul(x,y,z) strtoul(x,y,z)
#define grub_memmove(x,y,z) memmove(x,y,z)
#define grub_size_t size_t
#define grub_errno errno

#else
#include <grub/types.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/net/url.h>
#endif

static char *
translate_slashes(char *str)
{
  int i, j;
  if (str == NULL)
    return str;

  for (i = 0, j = 0; str[i] != '\0'; i++, j++)
    {
      if (str[i] == '\\')
	{
	  str[j] = '/';
	  if (str[i+1] == '\\')
	    i++;
	}
    }

  return str;
}

static inline int
hex2int (char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  c |= 0x20;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

static int
url_unescape (char *buf, grub_size_t len)
{
  int c, rc;
  unsigned int i;


  if (len < 3)
    {
      for (i = 0; i < len; i++)
	if (buf[i] == '%')
	  return -1;
      return 0;
    }

  for (i = 0; len > 2 && i < len - 2; i++)
    {
      if (buf[i] == '%')
	{
	  unsigned int j;
	  for (j = i+1; j < i+3; j++)
	    {
	      if (!(buf[j] >= '0' && buf[j] <= '9') &&
		  !(buf[j] >= 'a' && buf[j] <= 'f') &&
		  !(buf[j] >= 'A' && buf[j] <= 'F'))
		return -1;
	    }
	  i += 2;
	}
    }
  if (i == len - 2)
    {
      if (buf[i+1] == '%' || buf[i+2] == '%')
	return -1;
    }
  for (i = 0; i < len - 2; i++)
    {
      if (buf[i] == '%')
	{
	  rc = hex2int (buf[i+1]);
	  if (rc < 0)
	    return -1;
	  c = (rc & 0xf) << 4;
	  rc = hex2int (buf[i+2]);
	  if (rc < 0)
	    return -1;
	  c |= (rc & 0xf);

	  buf[i] = c;
	  grub_memmove (buf+i+1, buf+i+3, len-(i+2));
	  len -= 2;
	}
    }
  return 0;
}

static int
extract_http_url_info (char *url, int ssl,
		       char **userinfo, char **host, int *port,
		       char **file)
{
  char *colon, *slash, *query, *at = NULL, *separator, *auth_end;

  char *userinfo_off = NULL;
  char *userinfo_end;
  char *host_off = NULL;
  char *host_end;
  char *port_off = NULL;
  char *port_end;
  char *file_off = NULL;

  grub_size_t l;
  int c;

  if (!url || !userinfo || !host || !port || !file)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid argument");

  *userinfo = *host = *file = NULL;
  *port = -1;

  userinfo_off = url;

  slash = grub_strchrnul (userinfo_off, '/');
  query = grub_strchrnul (userinfo_off, '?');
  auth_end = slash < query ? slash : query;
  /* auth_end here is one /past/ the last character in the auth section, i.e.
   * it's the : or / or NUL */

  separator = grub_strchrnul (userinfo_off, '@');
  if (separator > auth_end)
    {
      host_off = userinfo_off;
      userinfo_off = NULL;
      userinfo_end = NULL;
    }
  else
    {
      at = separator;
      *separator = '\0';
      userinfo_end = separator;
      host_off = separator + 1;
    }

  if (*host_off == '[')
    {
      separator = grub_strchrnul (host_off, ']');
      if (separator >= auth_end)
	goto fail;

      separator += 1;
      host_end = separator;
    }
  else
    {
      host_end = separator = colon = grub_strchrnul (host_off, ':');

      if (colon > auth_end)
	{
	  separator = NULL;
	  host_end = auth_end;
	}
    }

  if (separator && separator < auth_end)
    {
      if (*separator == ':')
	{
	  port_off = separator + 1;
	  port_end = auth_end;

	  if (auth_end - port_end > 0)
	    goto fail;
	  if (port_end - port_off < 1)
	    goto fail;
	}
      else
	goto fail;
    }

  file_off = auth_end;
  if (port_off)
    {
      unsigned long portul;

      separator = NULL;
      c = *port_end;
      *port_end = '\0';

      portul = grub_strtoul (port_off, (const char **)&separator, 10);
      *port_end = c;
#ifdef URL_TEST
      if (portul == ULONG_MAX && errno == ERANGE)
	goto fail;
#else
      if (grub_errno == GRUB_ERR_OUT_OF_RANGE)
	goto fail;
#endif
      if (portul & ~0xfffful)
	goto fail;
      if (separator != port_end)
	goto fail;

      *port = portul & 0xfffful;
    }
  else if (ssl)
    *port = 443;
  else
    *port = 80;

  if (userinfo_off && *userinfo_off)
    {
      l = userinfo_end - userinfo_off + 1;

      *userinfo = grub_strndup (userinfo_off, l);
      if (!*userinfo)
	goto fail;
      (*userinfo)[l-1]= '\0';
    }

  l = host_end - host_off;

  if (host_end == NULL)
    goto fail;
  else
    c = *host_end;

  *host_end = '\0';
  *host = grub_strndup (host_off, l);
  *host_end = c;
  if (!*host)
    goto fail;
  (*host)[l] = '\0';

  *file = grub_strdup (file_off);
  if (!*file)
    goto fail;

  if (at)
    *at = '@';
  return 0;
fail:
  if (at)
    *at = '@';
  grub_free (*userinfo);
  grub_free (*host);
  grub_free (*file);

  return -1;
}

static int
extract_tftp_url_info (char *url, int ssl, char **host, char **file, int *port)
{
  char *slash, *semi;

  char *host_off = url;
  char *host_end;
  char *file_off;

  int c;

  if (!url || !host || !file || !port)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid argument");

  if (ssl)
    *port = 3713;
  else
    *port = 69;

  slash = grub_strchr (url, '/');
  if (!slash)
    return -1;

  host_end = file_off = slash;

  semi = grub_strchrnul (slash, ';');
  if (!grub_strncmp (semi, ";mode=", 6) && grub_strcmp (semi+6, "octet"))
    {
      grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		  N_("TFTP mode `%s' is not implemented."), semi+6);
      return -1;
    }

  /*
   * Maybe somebody added a new method, I dunno.  Anyway, semi is a reserved
   * character, so if it's there, it's the start of the mode block or it's
   * invalid.  So set it to \0 unconditionally, not just for ;mode=octet
   */
  *semi = '\0';

  c = *host_end;
  *host_end = '\0';
  *host = grub_strdup (host_off);
  *host_end = c;

  *file = grub_strdup (file_off);

  if (!*file || !*host)
    {
      grub_free (*file);
      grub_free (*host);
      return -1;
    }

  return 0;
}

int
extract_url_info (const char *urlbuf, grub_size_t buflen,
		  char **scheme, char **userinfo,
		  char **host, int *port, char **file)
{
  char *url;
  char *colon;

  char *scheme_off;
  char *specific_off;

  int rc;
  int c;

  int https;

  if (!urlbuf || !buflen || !scheme || !userinfo || !host || !port || !file)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid argument");

  *scheme = *userinfo = *host = *file = NULL;
  *port = -1;

  /* make sure we have our own coherent grub_string. */
  url = grub_malloc (buflen + 1);
  if (!url)
    return -1;

  grub_memcpy (url, urlbuf, buflen);
  url[buflen] = '\0';

  grub_dprintf ("net", "dhcpv6 boot-file-url: `%s'\n", url);

  /* get rid of any backslashes */
  url = translate_slashes (url);

  /* find the constituent parts */
  colon = grub_strstr (url, "://");
  if (!colon)
    goto fail;

  scheme_off = url;
  c = *colon;
  *colon = '\0';
  specific_off = colon + 3;

  https = !grub_strcasecmp (scheme_off, "https");

  rc = 0;
  if (!grub_strcasecmp (scheme_off, "tftp"))
    {
      rc = extract_tftp_url_info (specific_off, 0, host, file, port);
    }
#ifdef URL_TEST
  else if (!grub_strcasecmp (scheme_off, "http") || https)
#else
  else if (!grub_strcasecmp (scheme_off, "http"))
#endif
    {
      rc = extract_http_url_info (specific_off,
				  https, userinfo, host, port, file);
    }
#ifdef URL_TEST
  else if (!grub_strcasecmp (scheme_off, "iscsi"))
    {
      grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		  N_("Unimplemented URL scheme `%s'"), scheme_off);
      *colon = c;
      goto fail;
    }
  else if (!grub_strcasecmp (scheme_off, "tftps"))
    {
      rc = extract_tftp_url_info (specific_off, 1, host, file, port);
    }
#endif
  else
    {
      grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		  N_("Unimplemented URL scheme `%s'"), scheme_off);
      *colon = c;
      goto fail;
    }

  if (rc < 0)
    {
      *colon = c;
      goto fail;
    }

  *scheme = grub_strdup (scheme_off);
  *colon = c;
  if (!*scheme)
    goto fail;

  if (*userinfo)
    {
      rc = url_unescape (*userinfo, grub_strlen (*userinfo));
      if (rc < 0)
	goto fail;
    }

  if (*host)
    {
      rc = url_unescape (*host, grub_strlen (*host));
      if (rc < 0)
	goto fail;
    }

  if (*file)
    {
      rc = url_unescape (*file, grub_strlen (*file));
      if (rc < 0)
	goto fail;
    }

  grub_free (url);
  return 0;
fail:
  grub_free (*scheme);
  grub_free (*userinfo);
  grub_free (*host);
  grub_free (*file);

  if (!grub_errno)
    grub_error (GRUB_ERR_NET_BAD_ADDRESS, N_("Invalid boot-file-url `%s'"),
		url);
  grub_free (url);
  return -1;
}

#ifdef URL_TEST

struct test {
    char *url;
    int rc;
    char *scheme;
    char *userinfo;
    char *host;
    int port;
    char *file;
} tests[] = {
  {.url = "http://foo.example.com/",
   .rc = 0,
   .scheme = "http",
   .host = "foo.example.com",
   .port = 80,
   .file = "/",
  },
  {.url = "http://foo.example.com/?foobar",
   .rc = 0,
   .scheme = "http",
   .host = "foo.example.com",
   .port = 80,
   .file = "/?foobar",
  },
  {.url = "http://[foo.example.com/",
   .rc = -1,
  },
  {.url = "http://[foo.example.com/?foobar",
   .rc = -1,
  },
  {.url = "http://foo.example.com:/",
   .rc = -1,
  },
  {.url = "http://foo.example.com:81/",
   .rc = 0,
   .scheme = "http",
   .host = "foo.example.com",
   .port = 81,
   .file = "/",
  },
  {.url = "http://foo.example.com:81/?foobar",
   .rc = 0,
   .scheme = "http",
   .host = "foo.example.com",
   .port = 81,
   .file = "/?foobar",
  },
  {.url = "http://[1234::1]/",
   .rc = 0,
   .scheme = "http",
   .host = "[1234::1]",
   .port = 80,
   .file = "/",
  },
  {.url = "http://[1234::1]/?foobar",
   .rc = 0,
   .scheme = "http",
   .host = "[1234::1]",
   .port = 80,
   .file = "/?foobar",
  },
  {.url = "http://[1234::1]:81/",
   .rc = 0,
   .scheme = "http",
   .host = "[1234::1]",
   .port = 81,
   .file = "/",
  },
  {.url = "http://[1234::1]:81/?foobar",
   .rc = 0,
   .scheme = "http",
   .host = "[1234::1]",
   .port = 81,
   .file = "/?foobar",
  },
  {.url = "http://foo@foo.example.com/",
   .rc = 0,
   .scheme = "http",
   .userinfo = "foo",
   .host = "foo.example.com",
   .port = 80,
   .file = "/",
  },
  {.url = "http://foo@foo.example.com/?foobar",
   .rc = 0,
   .scheme = "http",
   .userinfo = "foo",
   .host = "foo.example.com",
   .port = 80,
   .file = "/?foobar",
  },
  {.url = "http://foo@[foo.example.com/",
   .rc = -1,
  },
  {.url = "http://foo@[foo.example.com/?foobar",
   .rc = -1,
  },
  {.url = "http://foo@foo.example.com:81/",
   .rc = 0,
   .scheme = "http",
   .userinfo = "foo",
   .host = "foo.example.com",
   .port = 81,
   .file = "/",
  },
  {.url = "http://foo@foo.example.com:81/?foobar",
   .rc = 0,
   .scheme = "http",
   .userinfo = "foo",
   .host = "foo.example.com",
   .port = 81,
   .file = "/?foobar",
  },
  {.url = "http://foo@[1234::1]/",
   .rc = 0,
   .scheme = "http",
   .userinfo = "foo",
   .host = "[1234::1]",
   .port = 80,
   .file = "/",
  },
  {.url = "http://foo@[1234::1]/?foobar",
   .rc = 0,
   .scheme = "http",
   .userinfo = "foo",
   .host = "[1234::1]",
   .port = 80,
   .file = "/?foobar",
  },
  {.url = "http://foo@[1234::1]:81/",
   .rc = 0,
   .scheme = "http",
   .userinfo = "foo",
   .host = "[1234::1]",
   .port = 81,
   .file = "/",
  },
  {.url = "http://foo@[1234::1]:81/?foobar",
   .rc = 0,
   .scheme = "http",
   .userinfo = "foo",
   .host = "[1234::1]",
   .port = 81,
   .file = "/?foobar",
  },
  {.url = "https://foo.example.com/",
   .rc = 0,
   .scheme = "https",
   .host = "foo.example.com",
   .port = 443,
   .file = "/",
  },
  {.url = "https://foo.example.com/?foobar",
   .rc = 0,
   .scheme = "https",
   .host = "foo.example.com",
   .port = 443,
   .file = "/?foobar",
  },
  {.url = "https://[foo.example.com/",
   .rc = -1,
  },
  {.url = "https://[foo.example.com/?foobar",
   .rc = -1,
  },
  {.url = "https://foo.example.com:81/",
   .rc = 0,
   .scheme = "https",
   .host = "foo.example.com",
   .port = 81,
   .file = "/",
  },
  {.url = "https://foo.example.com:81/?foobar",
   .rc = 0,
   .scheme = "https",
   .host = "foo.example.com",
   .port = 81,
   .file = "/?foobar",
  },
  {.url = "https://[1234::1]/",
   .rc = 0,
   .scheme = "https",
   .host = "[1234::1]",
   .port = 443,
   .file = "/",
  },
  {.url = "https://[1234::1]/?foobar",
   .rc = 0,
   .scheme = "https",
   .host = "[1234::1]",
   .port = 443,
   .file = "/?foobar",
  },
  {.url = "https://[1234::1]:81/",
   .rc = 0,
   .scheme = "https",
   .host = "[1234::1]",
   .port = 81,
   .file = "/",
  },
  {.url = "https://[1234::1]:81/?foobar",
   .rc = 0,
   .scheme = "https",
   .host = "[1234::1]",
   .port = 81,
   .file = "/?foobar",
  },
  {.url = "https://foo@foo.example.com/",
   .rc = 0,
   .scheme = "https",
   .userinfo = "foo",
   .host = "foo.example.com",
   .port = 443,
   .file = "/",
  },
  {.url = "https://foo@foo.example.com/?foobar",
   .rc = 0,
   .scheme = "https",
   .userinfo = "foo",
   .host = "foo.example.com",
   .port = 443,
   .file = "/?foobar",
  },
  {.url = "https://foo@[foo.example.com/",
   .rc = -1,
  },
  {.url = "https://f%6fo@[foo.example.com/?fooba%72",
   .rc = -1,
  },
  {.url = "https://foo@foo.example.com:81/",
   .rc = 0,
   .scheme = "https",
   .userinfo = "foo",
   .host = "foo.example.com",
   .port = 81,
   .file = "/",
  },
  {.url = "https://foo@foo.example.com:81/?foobar",
   .rc = 0,
   .scheme = "https",
   .userinfo = "foo",
   .host = "foo.example.com",
   .port = 81,
   .file = "/?foobar",
  },
  {.url = "https://foo@[1234::1]/",
   .rc = 0,
   .scheme = "https",
   .userinfo = "foo",
   .host = "[1234::1]",
   .port = 443,
   .file = "/",
  },
  {.url = "https://foo@[1234::1]/?foobar",
   .rc = 0,
   .scheme = "https",
   .userinfo = "foo",
   .host = "[1234::1]",
   .port = 443,
   .file = "/?foobar",
  },
  {.url = "https://f%6fo@[12%334::1]:81/",
   .rc = 0,
   .scheme = "https",
   .userinfo = "foo",
   .host = "[1234::1]",
   .port = 81,
   .file = "/",
  },
  {.url = "https://foo@[1234::1]:81/?foobar",
   .rc = 0,
   .scheme = "https",
   .userinfo = "foo",
   .host = "[1234::1]",
   .port = 81,
   .file = "/?foobar",
  },
  {.url = "tftp://foo.e%78ample.com/foo/bar/b%61%7a",
   .rc = 0,
   .scheme = "tftp",
   .host = "foo.example.com",
   .port = 69,
   .file = "/foo/bar/baz",
  },
  {.url = "tftp://foo.example.com/foo/bar/baz",
   .rc = 0,
   .scheme = "tftp",
   .host = "foo.example.com",
   .port = 69,
   .file = "/foo/bar/baz",
  },
  {.url = "tftps://foo.example.com/foo/bar/baz",
   .rc = 0,
   .scheme = "tftps",
   .host = "foo.example.com",
   .port = 3713,
   .file = "/foo/bar/baz",
  },
  {.url = "tftps://foo.example.com/foo/bar/baz;mode=netascii",
   .rc = -1,
  },
  {.url = "tftps://foo.example.com/foo/bar/baz;mode=octet",
   .rc = 0,
   .scheme = "tftps",
   .host = "foo.example.com",
   .port = 3713,
   .file = "/foo/bar/baz",
  },
  {.url = "tftps://foo.example.com/foo/bar/baz;mode=invalid",
   .rc = -1,
  },
  {.url = "",
  },
};

static int
string_test (char *name, char *a, char *b)
{
  if ((a && !b) || (!a && b))
    {
      printf("expected %s \"%s\", got \"%s\"\n", name, a, b);
      return -1;
    }
  if (a && b && strcmp(a, b))
    {
      printf("expected %s \"%s\", got \"%s\"\n", name, a, b);
      return -1;
    }
  return 0;
}

int
main(void)
{
	unsigned int i;
	int rc;

	for (i = 0; tests[i].url[0] != '\0'; i++)
	{
		char *scheme, *userinfo, *host, *file;
		int port;

		printf("======= url: \"%s\"\n", tests[i].url);
		rc = extract_url_info(tests[i].url, strlen(tests[i].url) + 1,
				      &scheme, &userinfo, &host, &port, &file);
		if (tests[i].rc != rc)
		  {
		    printf("  extract_url_info(...) = %d\n", rc);
		    exit(1);
		  }
		else if (rc >= 0)
		  {
		    if (string_test("scheme", tests[i].scheme, scheme) < 0)
		      exit(1);
		    if (string_test("userinfo", tests[i].userinfo, userinfo) < 0)
		      exit(1);
		    if (string_test("host", tests[i].host, host) < 0)
		      exit(1);
		    if (port != tests[i].port)
		      {
			printf("  bad port \"%d\" should have been \"%d\"\n",
			       port, tests[i].port);
			exit(1);
		      }
		    if (string_test("file", tests[i].file, file) < 0)
		      exit(1);
		  }
		free(scheme);
		free(userinfo);
		free(host);
		free(file);
	}
	printf("everything worked?!?\n");
}
#endif
