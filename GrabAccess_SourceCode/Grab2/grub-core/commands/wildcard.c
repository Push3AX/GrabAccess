/* wildcard.c - Wildcard character expansion for GRUB script.  */
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

#include <grub/mm.h>
#include <grub/fs.h>
#include <grub/env.h>
#include <grub/file.h>
#include <grub/device.h>
#include <grub/script_sh.h>
#include <grub/safemath.h>

#include <regex.h>

static inline int isregexop (char ch);
static char ** merge (char **lhs, char **rhs);
static char *make_dir (const char *prefix, const char *start, const char *end);
static int make_regex (const char *regex_start, const char *regex_end,
		       regex_t *regexp);
static void split_path (const char *path, const char **suffix_end, const char **regex_end);
static char ** match_devices (const regex_t *regexp, int noparts);
static char ** match_files (const char *prefix, const char *suffix_start,
			    const char *suffix_end, const regex_t *regexp);

static grub_err_t wildcard_expand (const char *s, char ***strs);

struct grub_script_wildcard_translator grub_filename_translator = {
  .expand = wildcard_expand,
};

static char **
merge (char **dest, char **ps)
{
  int i;
  int j;
  char **p;
  grub_size_t sz;

  if (! dest)
    return ps;

  if (! ps)
    return dest;

  for (i = 0; dest[i]; i++)
    ;
  for (j = 0; ps[j]; j++)
    ;

  if (grub_add (i, j, &sz) ||
      grub_add (sz, 1, &sz) ||
      grub_mul (sz, sizeof (char *), &sz))
    return dest;

  p = grub_realloc (dest, sz);
  if (! p)
    {
      grub_free (dest);
      grub_free (ps);
      return 0;
    }

  dest = p;
  for (j = 0; ps[j]; j++)
    dest[i++] = ps[j];
  dest[i] = 0;

  grub_free (ps);
  return dest;
}

static inline int
isregexop (char ch)
{
  return grub_strchr ("*.\\|+{}[]?", ch) ? 1 : 0;
}

static char *
make_dir (const char *prefix, const char *start, const char *end)
{
  char ch;
  unsigned i;
  unsigned n;
  char *result;

  i = grub_strlen (prefix);
  n = i + end - start;

  result = grub_malloc (n + 1);
  if (! result)
    return 0;

  grub_strcpy (result, prefix);
  while (start < end && (ch = *start++))
    if (ch == '\\' && isregexop (*start))
      result[i++] = *start++;
    else
      result[i++] = ch;

  result[i] = '\0';
  return result;
}

static int
make_regex (const char *start, const char *end, regex_t *regexp)
{
  char ch;
  int i = 0;
  unsigned len = end - start;
  char *buffer;
  grub_size_t sz;

  /* Worst case size is (len * 2 + 2 + 1). */
  if (grub_mul (len, 2, &sz) ||
      grub_add (sz, 3, &sz))
    return 1;

  buffer = grub_malloc (sz);
  if (! buffer)
    return 1;

  buffer[i++] = '^';
  while (start < end)
    {
      /* XXX Only * and ? expansion for now.  */
      switch ((ch = *start++))
	{
	case '\\':
	  buffer[i++] = ch;
	  if (*start != '\0')
	    buffer[i++] = *start++;
	  break;

	case '.':
	case '(':
	case ')':
	case '@':
	case '+':
	case '|':
	case '{':
	case '}':
	case '[':
	case ']':
	  buffer[i++] = '\\';
	  buffer[i++] = ch;
	  break;

	case '*':
	  buffer[i++] = '.';
	  buffer[i++] = '*';
	  break;

	case '?':
	  buffer[i++] = '.';
	  break;

	default:
	  buffer[i++] = ch;
	}
    }
  buffer[i++] = '$';
  buffer[i] = '\0';
  grub_dprintf ("expand", "Regexp is %s\n", buffer);

  if (regcomp (regexp, buffer, RE_SYNTAX_GNU_AWK))
    {
      grub_free (buffer);
      return 1;
    }

  grub_free (buffer);
  return 0;
}

/* Split `str' into two parts: (1) dirname that is regexop free (2)
   dirname that has a regexop.  */
static void
split_path (const char *str, const char **noregexop, const char **regexop)
{
  char ch = 0;
  int regex = 0;

  const char *end;
  const char *split;  /* points till the end of dirnaname that doesn't
			 need expansion.  */

  split = end = str;
  while ((ch = *end))
    {
      if (ch == '\\' && end[1])
	end++;

      else if (ch == '*' || ch == '?')
	regex = 1;

      else if (ch == '/' && ! regex)
	split = end + 1;  /* forward to next regexop-free dirname */

      else if (ch == '/' && regex)
	break;  /* stop at the first dirname with a regexop */

      end++;
    }

  *regexop = end;
  if (! regex)
    *noregexop = end;
  else
    *noregexop = split;
}

/* Context for match_devices.  */
struct match_devices_ctx
{
  const regex_t *regexp;
  int noparts;
  int ndev;
  char **devs;
};

/* Helper for match_devices.  */
static int
match_devices_iter (const char *name, void *data)
{
  struct match_devices_ctx *ctx = data;
  char **t;
  char *buffer;
  grub_size_t sz;

  /* skip partitions if asked to. */
  if (ctx->noparts && grub_strchr (name, ','))
    return 0;

  buffer = grub_xasprintf ("(%s)", name);
  if (! buffer)
    return 1;

  grub_dprintf ("expand", "matching: %s\n", buffer);
  if (regexec (ctx->regexp, buffer, 0, 0, 0))
    {
      grub_dprintf ("expand", "not matched\n");
 fail:
      grub_free (buffer);
      return 0;
    }

  if (grub_add (ctx->ndev, 2, &sz) ||
      grub_mul (sz, sizeof (char *), &sz))
    goto fail;

  t = grub_realloc (ctx->devs, sz);
  if (! t)
    {
      grub_free (buffer);
      return 1;
    }

  ctx->devs = t;
  ctx->devs[ctx->ndev++] = buffer;
  ctx->devs[ctx->ndev] = 0;
  return 0;
}

static char **
match_devices (const regex_t *regexp, int noparts)
{
  struct match_devices_ctx ctx = {
    .regexp = regexp,
    .noparts = noparts,
    .ndev = 0,
    .devs = 0
  };
  int i;

  if (grub_device_iterate (match_devices_iter, &ctx))
    goto fail;

  return ctx.devs;

 fail:

  for (i = 0; ctx.devs && ctx.devs[i]; i++)
    grub_free (ctx.devs[i]);

  grub_free (ctx.devs);

  return 0;
}

/* Context for match_files.  */
struct match_files_ctx
{
  const regex_t *regexp;
  char **files;
  unsigned nfile;
  char *dir;
};

/* Helper for match_files.  */
static int
match_files_iter (const char *name,
		  const struct grub_dirhook_info *info __attribute__((unused)),
		  void *data)
{
  struct match_files_ctx *ctx = data;
  char **t;
  char *buffer;
  grub_size_t sz;

  /* skip . and .. names */
  if (grub_strcmp(".", name) == 0 || grub_strcmp("..", name) == 0)
    return 0;

  grub_dprintf ("expand", "matching: %s in %s\n", name, ctx->dir);
  if (regexec (ctx->regexp, name, 0, 0, 0))
    return 0;

  grub_dprintf ("expand", "matched\n");

  buffer = grub_xasprintf ("%s%s", ctx->dir, name);
  if (! buffer)
    return 1;

  if (grub_add (ctx->nfile, 2, &sz) ||
      grub_mul (sz, sizeof (char *), &sz))
    goto fail;

  t = grub_realloc (ctx->files, sz);
  if (!t)
    {
 fail:
      grub_free (buffer);
      return 1;
    }

  ctx->files = t;
  ctx->files[ctx->nfile++] = buffer;
  ctx->files[ctx->nfile] = 0;
  return 0;
}

static char **
match_files (const char *prefix, const char *suffix, const char *end,
	     const regex_t *regexp)
{
  struct match_files_ctx ctx = {
    .regexp = regexp,
    .nfile = 0,
    .files = 0
  };
  int i;
  const char *path;
  char *device_name;
  grub_fs_t fs;
  grub_device_t dev;

  dev = 0;
  device_name = 0;
  grub_error_push ();

  ctx.dir = make_dir (prefix, suffix, end);
  if (! ctx.dir)
    goto fail;

  device_name = grub_file_get_device_name (ctx.dir);
  dev = grub_device_open (device_name);
  if (! dev)
    goto fail;

  fs = grub_fs_probe (dev);
  if (! fs)
    goto fail;

  if (ctx.dir[0] == '(')
    {
      path = grub_strchr (ctx.dir, ')');
      if (!path)
	goto fail;
      path++;
    }
  else
    path = ctx.dir;

  if (fs->fs_dir (dev, path, match_files_iter, &ctx))
    goto fail;

  grub_free (ctx.dir);
  grub_device_close (dev);
  grub_free (device_name);
  grub_error_pop ();
  return ctx.files;

 fail:

  grub_free (ctx.dir);

  for (i = 0; ctx.files && ctx.files[i]; i++)
    grub_free (ctx.files[i]);

  grub_free (ctx.files);

  if (dev)
    grub_device_close (dev);

  grub_free (device_name);

  grub_error_pop ();
  return 0;
}

/* Context for check_file.  */
struct check_file_ctx
{
  const char *basename;
  int found;
};

/* Helper for check_file.  */
static int
check_file_iter (const char *name, const struct grub_dirhook_info *info,
		 void *data)
{
  struct check_file_ctx *ctx = data;

  if (ctx->basename[0] == 0
      || (info->case_insensitive ? grub_strcasecmp (name, ctx->basename) == 0
	  : grub_strcmp (name, ctx->basename) == 0))
    {
      ctx->found = 1;
      return 1;
    }
  
  return 0;
}

static int
check_file (const char *dir, const char *basename)
{
  struct check_file_ctx ctx = {
    .basename = basename,
    .found = 0
  };
  grub_fs_t fs;
  grub_device_t dev;
  const char *device_name, *path;

  device_name = grub_file_get_device_name (dir);
  dev = grub_device_open (device_name);
  if (! dev)
    goto fail;

  fs = grub_fs_probe (dev);
  if (! fs)
    goto fail;

  if (dir[0] == '(')
    {
      path = grub_strchr (dir, ')');
      if (!path)
	goto fail;
      path++;
    }
  else
    path = dir;

  fs->fs_dir (dev, path[0] ? path : "/", check_file_iter, &ctx);
  if (grub_errno == 0 && basename[0] == 0)
    ctx.found = 1;

 fail:
  grub_errno = 0;

  return ctx.found;
}

static void
unescape (char *out, const char *in, const char *end)
{
  char *optr;
  const char *iptr;

  for (optr = out, iptr = in; iptr < end;)
    {
      if (*iptr == '\\' && iptr + 3 < end && iptr[1] == 'x' && grub_ishex(iptr[2]) && grub_ishex(iptr[3]))
	{
	  *optr++ = grub_tohex (iptr[2]) * 16 + grub_tohex (iptr[3]);
	  iptr += 4;
	  continue;
	}
      else if (*iptr == '\\' && iptr + 1 < end)
	{
	  *optr++ = iptr[1];
	  iptr += 2;
	  continue;
	}
      if (*iptr == '\\')
	break;
      *optr++ = *iptr++;
    }
  *optr = 0;
}

static grub_err_t
wildcard_expand (const char *s, char ***strs)
{
  const char *start;
  const char *regexop;
  const char *noregexop;
  char **paths = 0;
  int had_regexp = 0;

  unsigned i;
  regex_t regexp;

  *strs = 0;
  if (s[0] != '/' && s[0] != '(' && s[0] != '*')
    return 0;

  start = s;
  while (*start)
    {
      split_path (start, &noregexop, &regexop);

      if (noregexop == regexop)
	{
	  grub_dprintf ("expand", "no expansion needed\n");
	  if (paths == 0)
	    {
	      paths = grub_malloc (sizeof (char *) * 2);
	      if (!paths)
		goto fail;
	      paths[0] = grub_malloc (regexop - start + 1);
	      if (!paths[0])
		goto fail;
	      unescape (paths[0], start, regexop);
	      paths[1] = 0;
	    }
	  else
	    {
	      int j = 0;
	      for (i = 0; paths[i]; i++)
		{
		  char *o, *oend;
		  char *n;
		  char *p;
		  o = paths[i];
		  oend = o + grub_strlen (o);
		  n = grub_malloc ((oend - o) + (regexop - start) + 1);
		  if (!n)
		    goto fail;
		  grub_memcpy (n, o, oend - o);

		  unescape (n + (oend - o), start, regexop);
		  if (had_regexp)
		    p = grub_strrchr (n, '/');
		  else
		    p = 0;
		  if (!p)
		    {
		      grub_free (o);
		      paths[j++] = n;
		      continue;
		    }
		  *p = 0;
		  if (!check_file (n, p + 1))
		    {
		      grub_dprintf ("expand", "file <%s> in <%s> not found\n",
				    p + 1, n);
		      grub_free (o);
		      grub_free (n);
			      continue;
		    }
		  *p = '/';
		  grub_free (o);
		  paths[j++] = n;
		}
	      if (j == 0)
		{
		  grub_free (paths);
		  paths = 0;
		  goto done;
		}
	      paths[j] = 0;
	    }
	  grub_dprintf ("expand", "paths[0] = `%s'\n", paths[0]);
	  start = regexop;
	  continue;
	}

      if (make_regex (noregexop, regexop, &regexp))
	goto fail;

      had_regexp = 1;

      if (paths == 0)
	{
	  if (start == noregexop) /* device part has regexop */
	    paths = match_devices (&regexp, *start != '(');

	  else  /* device part explicit wo regexop */
	    paths = match_files ("", start, noregexop, &regexp);
	}
      else
	{
	  char **r = 0;

	  for (i = 0; paths[i]; i++)
	    {
	      char **p;

	      p = match_files (paths[i], start, noregexop, &regexp);
	      grub_free (paths[i]);
	      if (! p)
		continue;

	      r = merge (r, p);
	      if (! r)
		goto fail;
	    }
	  grub_free (paths);
	  paths = r;
	}

      regfree (&regexp);
      if (! paths)
	goto done;

      start = regexop;
    }

 done:

  *strs = paths;
  return 0;

 fail:

  for (i = 0; paths && paths[i]; i++)
    grub_free (paths[i]);
  grub_free (paths);
  regfree (&regexp);
  return grub_errno;
}
