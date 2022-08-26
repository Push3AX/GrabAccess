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

static EXPR_INT64 SUFFIX(eval_expr_0) (char **ps);

static EXPR_INT64 SUFFIX(parse_sgn) (int32_t sign, EXPR_INT64 num)
{
  if (sign > 0)
    return num;
  else
    return (0 - num);
}

static EXPR_INT64 SUFFIX(div64) (EXPR_INT64 n, EXPR_INT64 d)
{
  if (!d)
  {
    grub_printf ("ERROR: division by zero.\n");
    return 1;
  }
  return SUFFIX(grub_divmod64) (n, d, NULL);
}

static EXPR_INT64 SUFFIX(mod64) (EXPR_INT64 n, EXPR_INT64 d)
{
  EXPR_INT64 r;
  if (!d)
  {
    grub_printf ("ERROR: division by zero.\n");
    return 1;
  }
  SUFFIX(grub_divmod64) (n, d, &r);
  return r;
}

static EXPR_INT64 SUFFIX(do_op) (EXPR_INT64 lhs, EXPR_INT64 rhs, char op)
{
  if (op == '+')
    return (lhs + rhs);
  else if (op == '-')
    return (lhs - rhs);
  else if (op == '*')
    return (lhs * rhs);
  else if (op == '/')
    return SUFFIX(div64) (lhs, rhs);
  else if (op == '%')
    return SUFFIX(mod64) (lhs, rhs);
  else if (op == '&')
    return lhs & rhs;
  else if (op == '|')
    return lhs | rhs;
  else if (op == '^')
    return lhs ^ rhs;
  else if (op == '<')
    return lhs << rhs;
  else if (op == '>')
    return lhs >> rhs;
  else
    return 0;
}

static EXPR_INT64 SUFFIX(parse_nbr) (char **ps)
{
  EXPR_INT64 nbr;
  int32_t sign;

  nbr = 0;
  sign = 1;
  if ((*ps)[0] == '+' || (*ps)[0] == '-')
  {
    if ((*ps)[0] == '-')
      sign = -1;
    *ps = *ps + 1;
  }
  if ((*ps)[0] == '(')
  {
    *ps = *ps + 1;
    nbr = SUFFIX(eval_expr_0) (ps);
    if ((*ps)[0] == ')')
      *ps = *ps + 1;
    return SUFFIX(parse_sgn) (sign, nbr);
  }
  nbr = grub_strtoull (*ps, (const char **)ps, 0);
  return SUFFIX(parse_sgn) (sign, nbr);
}

static EXPR_INT64 SUFFIX(eval_expr_1) (char **ps)
{
  EXPR_INT64 lhs;
  EXPR_INT64 rhs;
  char op;

  lhs = SUFFIX(parse_nbr) (ps);
  while ((*ps)[0] == '*' || (*ps)[0] == '/' || (*ps)[0] == '%')
  {
    op = (*ps)[0];
    *ps = *ps + 1;
    rhs = SUFFIX(parse_nbr) (ps);
    lhs = SUFFIX(do_op) (lhs, rhs, op);
  }
  return (lhs);
}

static EXPR_INT64 SUFFIX(eval_expr_0) (char **ps)
{
  EXPR_INT64 lhs;
  EXPR_INT64 rhs;
  char op;

  lhs = SUFFIX(parse_nbr) (ps);
  while ((*ps)[0] != '\0' && (*ps)[0] != ')')
  {
    op = (*ps)[0];
    *ps = *ps + 1;
    if (op == '+' || op == '-')
      rhs = SUFFIX(eval_expr_1) (ps);
    else
      rhs = SUFFIX(parse_nbr) (ps);
    lhs = SUFFIX(do_op) (lhs, rhs, op);
  }
  return (lhs);
}

static EXPR_INT64 SUFFIX(eval_expr) (char *str)
{
  EXPR_INT64 ret;
  char *str2 = NULL;
  str2 = suppr_spaces (str);
  if (!str2)
    return 0;
  str = str2;
  ret = SUFFIX(eval_expr_0) (&str);
  free (str2);
  return ret;
}
