/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019,2020  Free Software Foundation, Inc.
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
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/env.h>
#include <unistd.h>
#include <stdlib.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
{
  {"set", 's', 0, N_("Store the result in a variable."),
    N_("VARNAME"), ARG_TYPE_STRING},
  {"unsigned", 'u', 0, N_("Calculate unsigned values."), 0, 0},
  {"hex", 'x', 0, N_("Display the result in hexadecimal form."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

enum options
{
  EXPR_SET,
  EXPR_U64,
  EXPR_HEX,
};

static char *suppr_spaces (char *str)
{
  uint64_t i = 0, j = 0;
  char *str2 = NULL;

  str2 = malloc (sizeof (str) + 1);
  if (!str2)
    return NULL;
  while (str[i] != '\0')
  {
    if (str[i] != ' ')
    {
      str2[j] = str[i];
      j = j + 1;
    }
    i = i + 1;
  }
  str2[j] = '\0';
  return (str2);
}

/* unsigned */
#define EXPR_INT64 uint64_t
#define SUFFIX(x) x
#include "exprXX.c"

/* signed */
#undef EXPR_INT64
#undef SUFFIX
#define EXPR_INT64 int64_t
#define SUFFIX(x) x ## s
#include "exprXX.c"

static grub_err_t
grub_cmd_expr (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  uint64_t ret;
  char str[32];

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("expression expected"));
  if (state[EXPR_U64].set)
  {
    ret = eval_expr (args[0]);
    grub_snprintf (str, 32, "%llu", (unsigned long long)ret);
  }
  else
  {
    ret = eval_exprs (args[0]);
    grub_snprintf (str, 32, "%lld", (long long)ret);
  }

  if (state[EXPR_HEX].set)
    grub_snprintf (str, 32, "0x%llx", (unsigned long long)ret);

  if (state[EXPR_SET].set)
    grub_env_set (state[EXPR_SET].arg, str);
  else
    grub_printf ("%s\n", str);

  return 0;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(expr)
{
  cmd = grub_register_extcmd ("expr", grub_cmd_expr, 0,
                              N_("[OPTIONS] EXPRESSION"),
                              N_("Evaluate math expressions."),
                              options);
}

GRUB_MOD_FINI(expr)
{
  grub_unregister_extcmd (cmd);
}
