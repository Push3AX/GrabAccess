/* regexp.c -- The regexp command.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2007  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/env.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/script_sh.h>
#include <regex.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
  {
    { "set", 's', GRUB_ARG_OPTION_REPEATABLE,
      /* TRANSLATORS: in regexp you can mark some
	 groups with parentheses. These groups are
	 then numbered and you can save some of
	 them in variables. In other programs
	 those components aree often referenced with
	 back slash, e.g. \1. Compare
	 sed -e 's,\([a-z][a-z]*\),lowercase=\1,g'
	 The whole matching component is saved in VARNAME, not its number.
       */
      N_("Store matched component NUMBER in VARNAME."),
      N_("[NUMBER:]VARNAME"), ARG_TYPE_STRING },
    { 0, 0, 0, 0, 0, 0 }
  };

static grub_err_t
setvar (char *str, char *v, regmatch_t *m)
{
  char ch;
  grub_err_t err;
  ch = str[m->rm_eo];
  str[m->rm_eo] = '\0';
  err = grub_env_set (v, str + m->rm_so);
  str[m->rm_eo] = ch;
  return err;
}

static grub_err_t
set_matches (char **varnames, char *str, grub_size_t nmatches,
	     regmatch_t *matches)
{
  int i;
  char *p;
  const char * q;
  grub_err_t err;
  unsigned long j;

  for (i = 0; varnames && varnames[i]; i++)
    {
      err = GRUB_ERR_NONE;
      p = grub_strchr (varnames[i], ':');
      if (! p)
	{
	  /* varname w/o index defaults to 1 */
	  if (nmatches < 2 || matches[1].rm_so == -1)
	    grub_env_unset (varnames[i]);
	  else
	    err = setvar (str, varnames[i], &matches[1]);
	}
      else
	{
	  j = grub_strtoul (varnames[i], &q, 10);
	  if (q != p)
	    return grub_error (GRUB_ERR_BAD_ARGUMENT,
			       "invalid variable name format %s", varnames[i]);

	  if (nmatches <= j || matches[j].rm_so == -1)
	    grub_env_unset (p + 1);
	  else
	    err = setvar (str, p + 1, &matches[j]);
	}

      if (err != GRUB_ERR_NONE)
	return err;
    }
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_regexp (grub_extcmd_context_t ctxt, int argc, char **args)
{
  regex_t regex;
  int ret;
  grub_size_t s;
  char *comperr;
  grub_err_t err;
  regmatch_t *matches = 0;

  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("two arguments expected"));

  ret = regcomp (&regex, args[0], REG_EXTENDED);
  if (ret)
    goto fail;

  matches = grub_calloc (regex.re_nsub + 1, sizeof (*matches));
  if (! matches)
    goto fail;

  ret = regexec (&regex, args[1], regex.re_nsub + 1, matches, 0);
  if (!ret)
    {
      err = set_matches (ctxt->state[0].args, args[1],
			 regex.re_nsub + 1, matches);
      regfree (&regex);
      grub_free (matches);
      return err;
    }

 fail:
  grub_free (matches);
  s = regerror (ret, &regex, 0, 0);
  comperr = grub_malloc (s);
  if (!comperr)
    {
      regfree (&regex);
      return grub_errno;
    }
  regerror (ret, &regex, comperr, s);
  err = grub_error (GRUB_ERR_TEST_FAILURE, "%s", comperr);
  regfree (&regex);
  grub_free (comperr);
  return err;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(regexp)
{
  cmd = grub_register_extcmd ("regexp", grub_cmd_regexp, 0,
			      /* TRANSLATORS: This are two arguments. So it's
				 two separate units to translate and pay
				 attention not to reverse them.  */
			      N_("REGEXP STRING"),
			      N_("Test if REGEXP matches STRING."), options);

  /* Setup GRUB script wildcard translator.  */
  grub_wildcard_translator = &grub_filename_translator;
}

GRUB_MOD_FINI(regexp)
{
  grub_unregister_extcmd (cmd);
  grub_wildcard_translator = 0;
}
