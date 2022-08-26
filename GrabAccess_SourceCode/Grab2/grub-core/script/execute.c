/* execute.c -- Execute a GRUB script.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2007,2008,2009,2010  Free Software Foundation, Inc.
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
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/script_sh.h>
#include <grub/command.h>
#include <grub/menu.h>
#include <grub/lib/arg.h>
#include <grub/normal.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/verify.h>
#ifdef GRUB_MACHINE_IEEE1275
#include <grub/ieee1275/ieee1275.h>
#endif

/* Max digits for a char is 3 (0xFF is 255), similarly for an int it
   is sizeof (int) * 3, and one extra for a possible -ve sign.  */
#define ERRNO_DIGITS_MAX  (sizeof (int) * 3 + 1)

static unsigned long is_continue;
static unsigned long active_loops;
static unsigned long active_breaks;
static unsigned long function_return;

#define GRUB_SCRIPT_SCOPE_MALLOCED      1
#define GRUB_SCRIPT_SCOPE_ARGS_MALLOCED 2

/* Scope for grub script functions.  */
struct grub_script_scope
{
  unsigned flags;
  unsigned shifts;
  struct grub_script_argv argv;
};
static struct grub_script_scope *scope = 0;

/* Wildcard translator for GRUB script.  */
struct grub_script_wildcard_translator *grub_wildcard_translator;

static char*
wildcard_escape (const char *s)
{
  int i;
  int len;
  char ch;
  char *p;

  len = grub_strlen (s);
  p = grub_malloc (len * 2 + 1);
  if (! p)
    return NULL;

  i = 0;
  while ((ch = *s++))
    {
      if (ch == '\\' && s[0] == 'x' && grub_ishex(s[1]) && grub_ishex(s[2]))
	{
	  p[i++] = grub_tohex (s[1]) * 16 + grub_tohex (s[2]);
	  s += 3;
	  continue;
	}
      else if (ch == '*' || ch == '\\' || ch == '?')
	p[i++] = '\\';
      p[i++] = ch;
    }
  p[i] = '\0';
  return p;
}

static char*
wildcard_unescape (const char *s)
{
  int i;
  int len;
  char ch;
  char *p;

  len = grub_strlen (s);
  p = grub_malloc (len + 1);
  if (! p)
    return NULL;

  i = 0;
  while ((ch = *s++))
    {
      if (ch == '\\' && s[0] == 'x' && grub_ishex(s[1]) && grub_ishex(s[2]))
	{
	  p[i++] = grub_tohex (s[1]) * 16 + grub_tohex (s[2]);
	  s += 3;
	}
      else if (ch == '\\')
	p[i++] = *s++;
      else
	p[i++] = ch;
    }
  p[i] = '\0';
  return p;
}

static void
replace_scope (struct grub_script_scope *new_scope)
{
  if (scope)
    {
      scope->argv.argc += scope->shifts;
      scope->argv.args -= scope->shifts;

      if (scope->flags & GRUB_SCRIPT_SCOPE_ARGS_MALLOCED)
	grub_script_argv_free (&scope->argv);

      if (scope->flags & GRUB_SCRIPT_SCOPE_MALLOCED)
	grub_free (scope);
    }
  scope = new_scope;
}

grub_err_t
grub_script_break (grub_command_t cmd, int argc, char *argv[])
{
  const char *p = NULL;
  unsigned long count;

  if (argc == 0)
    count = 1;
  else if (argc > 1)
    return  grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));
  else
    {
      count = grub_strtoul (argv[0], &p, 10);
      if (grub_errno)
	return grub_errno;
      if (*p != '\0')
	return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("unrecognized number"));
      if (count == 0)
	/* TRANSLATORS: 0 is a quantifier. "break" (similar to bash)
	   can be used e.g. to break 3 loops at once.
	   But asking it to break 0 loops makes no sense. */
	return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("can't break 0 loops"));
    }

  is_continue = grub_strcmp (cmd->name, "break") ? 1 : 0;
  active_breaks = count;
  if (active_breaks > active_loops)
    active_breaks = active_loops;
  return GRUB_ERR_NONE;
}

grub_err_t
grub_script_shift (grub_command_t cmd __attribute__((unused)),
		   int argc, char *argv[])
{
  const char *p = NULL;
  unsigned long n = 0;

  if (! scope)
    return GRUB_ERR_NONE;

  if (argc == 0)
    n = 1;

  else if (argc > 1)
    return GRUB_ERR_BAD_ARGUMENT;

  else
    {
      n = grub_strtoul (argv[0], &p, 10);
      if (*p != '\0')
	return GRUB_ERR_BAD_ARGUMENT;
    }

  if (n > scope->argv.argc)
    return GRUB_ERR_BAD_ARGUMENT;

  scope->shifts += n;
  scope->argv.argc -= n;
  scope->argv.args += n;
  return GRUB_ERR_NONE;
}

grub_err_t
grub_script_setparams (grub_command_t cmd __attribute__((unused)),
		       int argc, char **args)
{
  struct grub_script_scope *new_scope;
  struct grub_script_argv argv = { 0, 0, 0 };

  if (! scope)
    return GRUB_ERR_INVALID_COMMAND;

  new_scope = grub_malloc (sizeof (*new_scope));
  if (! new_scope)
    return grub_errno;

  if (grub_script_argv_make (&argv, argc, args))
    {
      grub_free (new_scope);
      return grub_errno;
    }

  new_scope->shifts = 0;
  new_scope->argv = argv;
  new_scope->flags = GRUB_SCRIPT_SCOPE_MALLOCED |
    GRUB_SCRIPT_SCOPE_ARGS_MALLOCED;

  replace_scope (new_scope);
  return GRUB_ERR_NONE;
}

grub_err_t
grub_script_return (grub_command_t cmd __attribute__((unused)),
		    int argc, char *argv[])
{
  const char *p = NULL;
  unsigned long n;

  if (! scope || argc > 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       /* TRANSLATORS: It's about not being
			  inside a function. "return" can be used only
			  in a function and this error occurs if it's used
			  anywhere else.  */
		       N_("not in function body"));

  if (argc == 0)
    {
      const char *t;
      function_return = 1;
      t = grub_env_get ("?");
      if (!t)
	return GRUB_ERR_NONE;
      return grub_strtoul (t, NULL, 10);
    }

  n = grub_strtoul (argv[0], &p, 10);
  if (grub_errno)
    return grub_errno;
  if (*p != '\0')
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("unrecognized number"));

  function_return = 1;
  return n ? grub_error (n, N_("false")) : GRUB_ERR_NONE;
}

static int
grub_env_special (const char *name)
{
  if (grub_isdigit (name[0]) ||
      grub_strcmp (name, "#") == 0 ||
      grub_strcmp (name, "*") == 0 ||
      grub_strcmp (name, "@") == 0)
    return 1;
  return 0;
}

static char **
grub_script_env_get (const char *name, grub_script_arg_type_t type)
{
  unsigned i;
  struct grub_script_argv result = { 0, 0, 0 };

  if (grub_script_argv_next (&result))
    goto fail;

  if (! grub_env_special (name))
    {
      const char *v = grub_env_get (name);
      if (v && v[0])
	{
	  if (type == GRUB_SCRIPT_ARG_TYPE_VAR)
	    {
	      if (grub_script_argv_split_append (&result, v))
		goto fail;
	    }
	  else
	    if (grub_script_argv_append (&result, v, grub_strlen (v)))
	      goto fail;
	}
    }
  else if (! scope)
    {
      if (grub_script_argv_append (&result, 0, 0))
	goto fail;
    }
  else if (grub_strcmp (name, "#") == 0)
    {
      char buffer[ERRNO_DIGITS_MAX + 1];
      grub_snprintf (buffer, sizeof (buffer), "%u", scope->argv.argc);
      if (grub_script_argv_append (&result, buffer, grub_strlen (buffer)))
	goto fail;
    }
  else if (grub_strcmp (name, "*") == 0)
    {
      for (i = 0; i < scope->argv.argc; i++)
	if (type == GRUB_SCRIPT_ARG_TYPE_VAR)
	  {
	    if (i != 0 && grub_script_argv_next (&result))
	      goto fail;

	    if (grub_script_argv_split_append (&result, scope->argv.args[i]))
	      goto fail;
	  }
	else
	  {
	    if (i != 0 && grub_script_argv_append (&result, " ", 1))
	      goto fail;

	    if (grub_script_argv_append (&result, scope->argv.args[i],
					 grub_strlen (scope->argv.args[i])))
	      goto fail;
	  }
    }
  else if (grub_strcmp (name, "@") == 0)
    {
      for (i = 0; i < scope->argv.argc; i++)
	{
	  if (i != 0 && grub_script_argv_next (&result))
	    goto fail;

	  if (type == GRUB_SCRIPT_ARG_TYPE_VAR)
	    {
	      if (grub_script_argv_split_append (&result, scope->argv.args[i]))
		goto fail;
	    }
	  else
	    if (grub_script_argv_append (&result, scope->argv.args[i],
					 grub_strlen (scope->argv.args[i])))
	      goto fail;
	}
    }
  else
    {
      unsigned long num = grub_strtoul (name, 0, 10);
      if (num == 0)
	; /* XXX no file name, for now.  */

      else if (num <= scope->argv.argc)
	{
	  if (type == GRUB_SCRIPT_ARG_TYPE_VAR)
	    {
	      if (grub_script_argv_split_append (&result,
						 scope->argv.args[num - 1]))
		goto fail;
	    }
	  else
	    if (grub_script_argv_append (&result, scope->argv.args[num - 1],
					 grub_strlen (scope->argv.args[num - 1])
					 ))
	      goto fail;
	}
    }

  return result.args;

 fail:

  grub_script_argv_free (&result);
  return 0;
}

static grub_err_t
grub_script_env_set (const char *name, const char *val)
{
  if (grub_env_special (name))
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("invalid variable name `%s'"), name);

  return grub_env_set (name, val);
}

struct gettext_context
{
  char **allowed_strings;
  grub_size_t nallowed_strings;
  grub_size_t additional_len;
};

static int
parse_string (const char *str,
	      int (*hook) (const char *var, grub_size_t varlen,
			   char **ptr, struct gettext_context *ctx),
	      struct gettext_context *ctx,
	      char *put)
{
  const char *ptr;
  int escaped = 0;
  const char *optr;

  for (ptr = str; ptr && *ptr; )
    switch (*ptr)
      {
      case '\\':
	if (!escaped && put && *(ptr+1) == 'x' && grub_ishex(*(ptr+2)) && grub_ishex(*(ptr+3)))
	  {
	    *put++ = grub_tohex (ptr[2]) * 16 + grub_tohex (ptr[3]);
	    ptr += 4;
	  }
	else
	  {
	    escaped = !escaped;
	    if (!escaped && put)
	      *(put++) = '\\';
	    ptr++;
	  }
	break;
      case '$':
	if (escaped)
	  {
	    escaped = 0;
	    if (put)
	      *(put++) = *ptr;
	    ptr++;
	    break;
	  }

	ptr++;
	switch (*ptr)
	  {
	  case '{':
	    {
	      optr = ptr + 1;
	      ptr = grub_strchr (optr, '}');
	      if (!ptr)
		break;
	      if (hook (optr, ptr - optr, &put, ctx))
		return 1;
	      ptr++;
	      break;
	    }
	  case '0' ... '9':
	    optr = ptr;
	    while (*ptr >= '0' && *ptr <= '9')
	      ptr++;
	    if (hook (optr, ptr - optr, &put, ctx))
	      return 1;
	    break;
	  case 'a' ... 'z':
	  case 'A' ... 'Z':
	  case '_':
	    optr = ptr;
	    while ((*ptr >= '0' && *ptr <= '9')
		   || (*ptr >= 'a' && *ptr <= 'z')
		   || (*ptr >= 'A' && *ptr <= 'Z')
		   || *ptr == '_')
	      ptr++;
	    if (hook (optr, ptr - optr, &put, ctx))
	      return 1;
	    break;
	  case '?':
	  case '#':
	    if (hook (ptr, 1, &put, ctx))
	      return 1;
	    ptr++;
	    break;
	  default:
	    if (put)
	      *(put++) = '$';
	  }
	break;
      default:
	if (escaped && put)
	  *(put++) = '\\';
	escaped = 0;
	if (put)
	  *(put++) = *ptr;
	ptr++;
	break;
      }
  if (put)
    *(put++) = 0;
  return 0;
}

static int
gettext_putvar (const char *str, grub_size_t len,
		char **ptr, struct gettext_context *ctx)
{
  const char *var;
  grub_size_t i;

  for (i = 0; i < ctx->nallowed_strings; i++)
    if (grub_strncmp (ctx->allowed_strings[i], str, len) == 0
	&& ctx->allowed_strings[i][len] == 0)
      {
	break;
      }
  if (i == ctx->nallowed_strings)
    return 0;

  /* Enough for any number.  */
  if (len == 1 && str[0] == '#' && scope != NULL)
    {
      grub_snprintf (*ptr, 30, "%u", scope->argv.argc);
      *ptr += grub_strlen (*ptr);
      return 0;
    }
  var = grub_env_get (ctx->allowed_strings[i]);
  if (var)
    *ptr = grub_stpcpy (*ptr, var);
  return 0;
}

static int
gettext_save_allow (const char *str, grub_size_t len,
		    char **ptr __attribute__ ((unused)),
		    struct gettext_context *ctx)
{
  ctx->allowed_strings[ctx->nallowed_strings++] = grub_strndup (str, len);
  if (!ctx->allowed_strings[ctx->nallowed_strings - 1])
    return 1;
  return 0;
}

static int
gettext_getlen (const char *str, grub_size_t len,
		char **ptr __attribute__ ((unused)),
		struct gettext_context *ctx)
{
  const char *var;
  grub_size_t i;

  for (i = 0; i < ctx->nallowed_strings; i++)
    if (grub_strncmp (ctx->allowed_strings[i], str, len) == 0
	&& ctx->allowed_strings[i][len] == 0)
      break;
  if (i == ctx->nallowed_strings)
    return 0;

  /* Enough for any number.  */
  if (len == 1 && str[0] == '#')
    {
      ctx->additional_len += 30;
      return 0;
    }
  var = grub_env_get (ctx->allowed_strings[i]);
  if (var)
    ctx->additional_len += grub_strlen (var);
  return 0;
}

static int
gettext_append (struct grub_script_argv *result, const char *orig_str)
{
  const char *template;
  char *res = 0;
  struct gettext_context ctx = {
    .allowed_strings = 0,
    .nallowed_strings = 0,
    .additional_len = 1
  };
  int rval = 1;
  const char *iptr;

  grub_size_t dollar_cnt = 0;

  for (iptr = orig_str; *iptr; iptr++)
    if (*iptr == '$')
      dollar_cnt++;
  ctx.allowed_strings = grub_calloc (dollar_cnt, sizeof (ctx.allowed_strings[0]));

  if (parse_string (orig_str, gettext_save_allow, &ctx, 0))
    goto fail;

  template = _(orig_str);

  if (parse_string (template, gettext_getlen, &ctx, 0))
    goto fail;

  res = grub_malloc (grub_strlen (template) + ctx.additional_len);
  if (!res)
    goto fail;

  if (parse_string (template, gettext_putvar, &ctx, res))
    goto fail;

  char *escaped = 0;
  escaped = wildcard_escape (res);
  if (grub_script_argv_append (result, escaped, grub_strlen (escaped)))
    {
      grub_free (escaped);
      goto fail;
    }
  grub_free (escaped);

  rval = 0;
 fail:
  grub_free (res);
  {
    grub_size_t i;
    for (i = 0; i < ctx.nallowed_strings; i++)
      grub_free (ctx.allowed_strings[i]);
  }
  grub_free (ctx.allowed_strings);
  return rval;
}

static int
append (struct grub_script_argv *result,
	const char *s, int escape_type)
{
  int r;
  char *p = 0;

  if (escape_type == 0)
    return grub_script_argv_append (result, s, grub_strlen (s));

  if (escape_type > 0)
    p = wildcard_escape (s);
  else if (escape_type < 0)
    p = wildcard_unescape (s);

  if (! p)
    return 1;

  r = grub_script_argv_append (result, p, grub_strlen (p));
  grub_free (p);
  return r;
}

/* Convert arguments in ARGLIST into ARGV form.  */
static int
grub_script_arglist_to_argv (struct grub_script_arglist *arglist,
			     struct grub_script_argv *argv)
{
  int i;
  char **values = 0;
  struct grub_script_arg *arg = 0;
  struct grub_script_argv result = { 0, 0, 0 };

  if (arglist == NULL)
    return 1;

  for (; arglist && arglist->arg; arglist = arglist->next)
    {
      if (grub_script_argv_next (&result))
	goto fail;

      arg = arglist->arg;
      while (arg)
	{
	  switch (arg->type)
	    {
	    case GRUB_SCRIPT_ARG_TYPE_VAR:
	    case GRUB_SCRIPT_ARG_TYPE_DQVAR:
	      {
		int need_cleanup = 0;

		values = grub_script_env_get (arg->str, arg->type);
		for (i = 0; values && values[i]; i++)
		  {
		    if (!need_cleanup)
		      {
			if (i != 0 && grub_script_argv_next (&result))
			  {
			    need_cleanup = 1;
			    goto cleanup;
			  }

			if (arg->type == GRUB_SCRIPT_ARG_TYPE_VAR)
			  {
			    int len;
			    char ch;
			    char *p;
			    char *op;
			    const char *s = values[i];

			    len = grub_strlen (values[i]);
			    /* \? -> \\\? */
			    /* \* -> \\\* */
			    /* \ -> \\ */
			    p = grub_malloc (len * 2 + 1);
			    if (! p)
			      {
				need_cleanup = 1;
				goto cleanup;
			      }

			    op = p;
			    while ((ch = *s++))
			      {
				if (ch == '\\')
				  {
				    *op++ = '\\';
				    if (*s == '?' || *s == '*')
				      *op++ = '\\';
				  }
				*op++ = ch;
			      }
			    *op = '\0';

			    need_cleanup = grub_script_argv_append (&result, p, op - p);
			    grub_free (p);
			    /* Fall through to cleanup */
			  }
			else
			  {
			    need_cleanup = append (&result, values[i], 1);
			    /* Fall through to cleanup */
			  }
		      }

cleanup:
		    grub_free (values[i]);
		  }
		grub_free (values);

		if (need_cleanup)
		  goto fail;

		break;
	      }

	    case GRUB_SCRIPT_ARG_TYPE_BLOCK:
	      {
		char *p;
		if (grub_script_argv_append (&result, "{", 1))
		  goto fail;
		p = wildcard_escape (arg->str);
		if (!p)
		  goto fail;
		if (grub_script_argv_append (&result, p,
					     grub_strlen (p)))
		  {
		    grub_free (p);
		    goto fail;
		  }
		grub_free (p);
		if (grub_script_argv_append (&result, "}", 1))
		  goto fail;
	      }
	      result.script = arg->script;
	      break;

	    case GRUB_SCRIPT_ARG_TYPE_TEXT:
	      if (arg->str[0] &&
		  grub_script_argv_append (&result, arg->str,
					   grub_strlen (arg->str)))
		goto fail;
	      break;

	    case GRUB_SCRIPT_ARG_TYPE_GETTEXT:
	      {
		if (gettext_append (&result, arg->str))
		  goto fail;
	      }
	      break;

	    case GRUB_SCRIPT_ARG_TYPE_DQSTR:
	    case GRUB_SCRIPT_ARG_TYPE_SQSTR:
	      if (append (&result, arg->str, 1))
		goto fail;
	      break;
	    }
	  arg = arg->next;
	}
    }

  if (! result.args[result.argc - 1])
    result.argc--;

  /* Perform wildcard expansion.  */

  int j;
  int failed = 0;
  struct grub_script_argv unexpanded = result;

  result.argc = 0;
  result.args = 0;
  for (i = 0; unexpanded.args[i]; i++)
    {
      char **expansions = 0;
      if (grub_wildcard_translator
	  && grub_wildcard_translator->expand (unexpanded.args[i],
					       &expansions))
	{
	  grub_script_argv_free (&unexpanded);
	  goto fail;
	}

      if (! expansions)
	{
	  grub_script_argv_next (&result);
	  append (&result, unexpanded.args[i], -1);
	}
      else
	{
	  for (j = 0; expansions[j]; j++)
	    {
	      failed = (failed || grub_script_argv_next (&result) ||
			append (&result, expansions[j], 0));
	      grub_free (expansions[j]);
	    }
	  grub_free (expansions);
	  
	  if (failed)
	    {
	      grub_script_argv_free (&unexpanded);
	      goto fail;
	    }
	}
    }
  grub_script_argv_free (&unexpanded);

  *argv = result;
  return 0;

 fail:

  grub_script_argv_free (&result);
  return 1;
}

static grub_err_t
grub_script_execute_cmd (struct grub_script_cmd *cmd)
{
  int ret;
  char errnobuf[ERRNO_DIGITS_MAX + 1];

  if (cmd == 0)
    return 0;

  ret = cmd->exec (cmd);

  grub_snprintf (errnobuf, sizeof (errnobuf), "%d", ret);
  grub_env_set ("?", errnobuf);
  return ret;
}

/* Execute a function call.  */
grub_err_t
grub_script_function_call (grub_script_function_t func, int argc, char **args)
{
  grub_err_t ret = 0;
  unsigned long loops = active_loops;
  struct grub_script_scope *old_scope;
  struct grub_script_scope new_scope;

  active_loops = 0;
  new_scope.flags = 0;
  new_scope.shifts = 0;
  new_scope.argv.argc = argc;
  new_scope.argv.args = args;

  old_scope = scope;
  scope = &new_scope;

  func->executing++;
  ret = grub_script_execute (func->func);
  func->executing--;

  function_return = 0;
  active_loops = loops;
  replace_scope (old_scope); /* free any scopes by setparams */
  return ret;
}

/* Helper for grub_script_execute_sourcecode.  */
static grub_err_t
grub_script_execute_sourcecode_getline (char **line,
					int cont __attribute__ ((unused)),
					void *data)
{
  const char **source = data;
  const char *p;

  if (! *source)
    {
      *line = 0;
      return 0;
    }

  p = grub_strchr (*source, '\n');

  if (p)
    *line = grub_strndup (*source, p - *source);
  else
    *line = grub_strdup (*source);
  *source = p ? p + 1 : 0;
  return 0;
}

/* Execute a source script.  */
grub_err_t
grub_script_execute_sourcecode (const char *source)
{
  grub_err_t ret = 0;
  struct grub_script *parsed_script;

#ifdef GRUB_MACHINE_IEEE1275
  grub_ieee1275_set_boot_last_label (source);
#endif

  while (source)
    {
      char *line;

      grub_script_execute_sourcecode_getline (&line, 0, &source);
      parsed_script = grub_script_parse
	(line, grub_script_execute_sourcecode_getline, &source);
      if (! parsed_script)
	{
	  ret = grub_errno;
	  grub_free (line);
	  break;
	}

      ret = grub_script_execute (parsed_script);
      grub_script_free (parsed_script);
      grub_free (line);
    }

  return ret;
}

/* Execute a source script in new scope.  */
grub_err_t
grub_script_execute_new_scope (const char *source, int argc, char **args)
{
  grub_err_t ret = 0;
  struct grub_script_scope new_scope;
  struct grub_script_scope *old_scope;

  new_scope.argv.argc = argc;
  new_scope.argv.args = args;
  new_scope.flags = 0;

  old_scope = scope;
  scope = &new_scope;

  ret = grub_script_execute_sourcecode (source);

  scope = old_scope;
  return ret;
}

/* Execute a single command line.  */
grub_err_t
grub_script_execute_cmdline (struct grub_script_cmd *cmd)
{
  struct grub_script_cmdline *cmdline = (struct grub_script_cmdline *) cmd;
  grub_command_t grubcmd;
  grub_err_t ret = 0;
  grub_script_function_t func = 0;
  char errnobuf[18];
  char *cmdname, *cmdstring;
  int argc, offset = 0, cmdlen = 0;
  unsigned int i;
  char **args;
  int invert;
  struct grub_script_argv argv = { 0, 0, 0 };

  /* Lookup the command.  */
  if (grub_script_arglist_to_argv (cmdline->arglist, &argv) || ! argv.args || ! argv.args[0])
    return grub_errno;

  for (i = 0; i < argv.argc; i++)
    {
      cmdlen += grub_strlen (argv.args[i]) + 1;
    }

  cmdstring = grub_malloc (cmdlen);
  if (!cmdstring)
    {
      return grub_error (GRUB_ERR_OUT_OF_MEMORY,
			 N_("cannot allocate command buffer"));
    }

  for (i = 0; i < argv.argc; i++)
    {
      offset += grub_snprintf (cmdstring + offset, cmdlen - offset, "%s ",
			       argv.args[i]);
    }
  cmdstring[cmdlen - 1] = '\0';
  grub_verify_string (cmdstring, GRUB_VERIFY_COMMAND);
  grub_free (cmdstring);
  invert = 0;
  argc = argv.argc - 1;
  args = argv.args + 1;
  cmdname = argv.args[0];
  if (grub_strcmp (cmdname, "!") == 0)
    {
      if (argv.argc < 2 || ! argv.args[1])
	{
	  grub_script_argv_free (&argv);
	  return grub_error (GRUB_ERR_BAD_ARGUMENT,
			     N_("no command is specified"));
	}

      invert = 1;
      argc = argv.argc - 2;
      args = argv.args + 2;
      cmdname = argv.args[1];
    }
  grubcmd = grub_command_find (cmdname);
  if (! grubcmd)
    {
      grub_errno = GRUB_ERR_NONE;

      /* It's not a GRUB command, try all functions.  */
      func = grub_script_function_find (cmdname);
      if (! func)
	{
	  /* As a last resort, try if it is an assignment.  */
	  char *assign = grub_strdup (cmdname);
	  char *eq = grub_strchr (assign, '=');

	  if (eq)
	    {
	      /* This was set because the command was not found.  */
	      grub_errno = GRUB_ERR_NONE;

	      /* Create two strings and set the variable.  */
	      *eq = '\0';
	      eq++;
	      grub_script_env_set (assign, eq);
	    }
	  grub_free (assign);

	  grub_snprintf (errnobuf, sizeof (errnobuf), "%d", grub_errno);
	  grub_script_env_set ("?", errnobuf);

	  grub_script_argv_free (&argv);
	  grub_print_error ();

	  return 0;
	}
    }

  /* Execute the GRUB command or function.  */
  if (grubcmd)
    {
      if (grub_extractor_level && !(grubcmd->flags
				    & GRUB_COMMAND_FLAG_EXTRACTOR))
	ret = grub_error (GRUB_ERR_EXTRACTOR,
			  "%s isn't allowed to execute in an extractor",
			  cmdname);
      else if ((grubcmd->flags & GRUB_COMMAND_FLAG_BLOCKS) &&
	       (grubcmd->flags & GRUB_COMMAND_FLAG_EXTCMD))
	ret = grub_extcmd_dispatcher (grubcmd, argc, args, argv.script);
      else
	ret = (grubcmd->func) (grubcmd, argc, args);
    }
  else
    ret = grub_script_function_call (func, argc, args);

  if (invert)
    {
      if (ret == GRUB_ERR_TEST_FAILURE)
	grub_errno = ret = GRUB_ERR_NONE;
      else if (ret == GRUB_ERR_NONE)
	ret = grub_error (GRUB_ERR_TEST_FAILURE, N_("false"));
      else
	{
	  grub_print_error ();
	  ret = GRUB_ERR_NONE;
	}
    }

  /* Free arguments.  */
  grub_script_argv_free (&argv);

  if (grub_errno == GRUB_ERR_TEST_FAILURE)
    grub_errno = GRUB_ERR_NONE;

  grub_print_error ();

  grub_snprintf (errnobuf, sizeof (errnobuf), "%d", ret);
  grub_env_set ("?", errnobuf);

  return ret;
}

/* Execute a block of one or more commands.  */
grub_err_t
grub_script_execute_cmdlist (struct grub_script_cmd *list)
{
  int ret = 0;
  struct grub_script_cmd *cmd;

  /* Loop over every command and execute it.  */
  for (cmd = list->next; cmd; cmd = cmd->next)
    {
      if (active_breaks)
	break;

      ret = grub_script_execute_cmd (cmd);

      if (function_return)
	break;
    }

  return ret;
}

/* Execute an if statement.  */
grub_err_t
grub_script_execute_cmdif (struct grub_script_cmd *cmd)
{
  int ret;
  const char *result;
  struct grub_script_cmdif *cmdif = (struct grub_script_cmdif *) cmd;

  /* Check if the commands results in a true or a false.  The value is
     read from the env variable `?'.  */
  ret = grub_script_execute_cmd (cmdif->exec_to_evaluate);
  if (function_return)
    return ret;

  result = grub_env_get ("?");
  grub_errno = GRUB_ERR_NONE;

  /* Execute the `if' or the `else' part depending on the value of
     `?'.  */
  if (result && ! grub_strcmp (result, "0"))
    return grub_script_execute_cmd (cmdif->exec_on_true);
  else
    return grub_script_execute_cmd (cmdif->exec_on_false);
}

/* Execute a for statement.  */
grub_err_t
grub_script_execute_cmdfor (struct grub_script_cmd *cmd)
{
  unsigned i;
  grub_err_t result;
  struct grub_script_argv argv = { 0, 0, 0 };
  struct grub_script_cmdfor *cmdfor = (struct grub_script_cmdfor *) cmd;

  if (grub_script_arglist_to_argv (cmdfor->words, &argv))
    return grub_errno;

  active_loops++;
  result = 0;
  for (i = 0; i < argv.argc; i++)
    {
      if (is_continue && active_breaks == 1)
	active_breaks = 0;

      if (! active_breaks)
	{
	  grub_script_env_set (cmdfor->name->str, argv.args[i]);
	  result = grub_script_execute_cmd (cmdfor->list);
	  if (function_return)
	    break;
	}
    }

  if (active_breaks)
    active_breaks--;

  active_loops--;
  grub_script_argv_free (&argv);
  return result;
}

/* Execute a "while" or "until" command.  */
grub_err_t
grub_script_execute_cmdwhile (struct grub_script_cmd *cmd)
{
  int result;
  struct grub_script_cmdwhile *cmdwhile = (struct grub_script_cmdwhile *) cmd;

  active_loops++;
  do {
    result = grub_script_execute_cmd (cmdwhile->cond);
    if (function_return)
      break;

    if (cmdwhile->until ? !result : result)
      break;

    result = grub_script_execute_cmd (cmdwhile->list);
    if (function_return)
      break;

    if (active_breaks == 1 && is_continue)
      active_breaks = 0;

    if (active_breaks)
      break;

  } while (1); /* XXX Put a check for ^C here */

  if (active_breaks)
    active_breaks--;

  active_loops--;
  return result;
}

/* Execute any GRUB pre-parsed command or script.  */
grub_err_t
grub_script_execute (struct grub_script *script)
{
  if (script == 0)
    return 0;

  return grub_script_execute_cmd (script->cmd);
}
