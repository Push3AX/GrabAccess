/* date.c - command to display/set current datetime.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008  Free Software Foundation, Inc.
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
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/datetime.h>
#include <grub/command.h>
#include <grub/extcmd.h>
#include <grub/env.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define GRUB_DATETIME_SET_YEAR		1
#define GRUB_DATETIME_SET_MONTH		2
#define GRUB_DATETIME_SET_DAY		4
#define GRUB_DATETIME_SET_HOUR		8
#define GRUB_DATETIME_SET_MINUTE	16
#define GRUB_DATETIME_SET_SECOND	32

static const struct grub_arg_option options[] =
{
  {"human", 'm', 0, N_("Store date in a human readable format."), 0, 0},
  {"set", 's', 0, N_("Store date in a variable."), N_("VARNAME"), ARG_TYPE_STRING},
  {0, 0, 0, 0, 0, 0}
};

enum options
{
  DATE_HUMAN,
  DATE_SET,
};

static grub_err_t
grub_cmd_date (grub_extcmd_context_t ctxt,
               int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  struct grub_datetime datetime;
  int limit[6][2] = {{1980, 2079}, {1, 12}, {1, 31}, {0, 23}, {0, 59}, {0, 59}};
  int value[6], mask;

  if (state[DATE_SET].set)
    {
      char *str = NULL;
      if (grub_get_datetime (&datetime))
        return grub_errno;
      if (state[DATE_HUMAN].set)
        str = grub_xasprintf ("%d-%02d-%02d %02d:%02d:%02d %s",
                        datetime.year, datetime.month, datetime.day,
                        datetime.hour, datetime.minute, datetime.second,
                        grub_get_weekday_name (&datetime));
      else
        str = grub_xasprintf ("%d%02d%02d%02d%02d%02d",
                   datetime.year, datetime.month, datetime.day,
                   datetime.hour, datetime.minute, datetime.second);
      if (!str)
        return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
      grub_env_set (state[DATE_SET].arg, str);
      grub_free (str);
      return 0;
    }

  if (argc == 0)
    {
      if (grub_get_datetime (&datetime))
        return grub_errno;

      grub_printf ("%d-%02d-%02d %02d:%02d:%02d %s\n",
                   datetime.year, datetime.month, datetime.day,
                   datetime.hour, datetime.minute, datetime.second,
                   grub_get_weekday_name (&datetime));

      return 0;
    }

  grub_memset (&value, 0, sizeof (value));
  mask = 0;

  for (; argc; argc--, args++)
    {
      const char *p;
      char c;
      int m1, ofs, n, cur_mask;

      p = args[0];
      m1 = grub_strtoul (p, &p, 10);

      c = *p;
      if (c == '-')
        ofs = 0;
      else if (c == ':')
        ofs = 3;
      else
        goto fail;

      value[ofs] = m1;
      cur_mask = (1 << ofs);
      mask &= ~(cur_mask * (1 + 2 + 4));

      for (n = 1; (n < 3) && (*p); n++)
        {
          if (*p != c)
            goto fail;

          value[ofs + n] = grub_strtoul (p + 1, &p, 10);
          cur_mask |= (1 << (ofs + n));
        }

      if (*p)
        goto fail;

      if ((ofs == 0) && (n == 2))
        {
          value[ofs + 2] = value[ofs + 1];
          value[ofs + 1] = value[ofs];
          ofs++;
          cur_mask <<= 1;
        }

      for (; n; n--, ofs++)
        if ((value [ofs] < limit[ofs][0]) ||
            (value [ofs] > limit[ofs][1]))
          goto fail;

      mask |= cur_mask;
    }

  if (grub_get_datetime (&datetime))
    return grub_errno;

  if (mask & GRUB_DATETIME_SET_YEAR)
    datetime.year = value[0];

  if (mask & GRUB_DATETIME_SET_MONTH)
    datetime.month = value[1];

  if (mask & GRUB_DATETIME_SET_DAY)
    datetime.day = value[2];

  if (mask & GRUB_DATETIME_SET_HOUR)
    datetime.hour = value[3];

  if (mask & GRUB_DATETIME_SET_MINUTE)
    datetime.minute = value[4];

  if (mask & GRUB_DATETIME_SET_SECOND)
    datetime.second = value[5];

  return grub_set_datetime (&datetime);

fail:
  return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid datetime");
}

static int
safe_parse_maxint (char **str_ptr, unsigned long long *myint_ptr)
{
  char *ptr = *str_ptr;
  unsigned long long myint = 0;
  unsigned long long mult = 10;
  int found = 0;
  int negative = 0;

  /*
   *  The decimal numbers can be positive or negative, ranging from
   *  0x80000000(the minimal int) to 0x7fffffff(the maximal int).
   *  The hex numbers are not checked.
   */

  if (*ptr == '-') /* check whether or not the negative sign exists */
  {
    ptr++;
    negative = 1;
  }

  /*
   *  Is this a hex number?
   */
  if (*ptr == '0' && grub_tolower (*(ptr + 1)) == 'x')
  {
    ptr += 2;
    mult = 16;
  }

  while (1)
  {
    /* A bit tricky. This below makes use of the equivalence:
       (A >= B && A <= C) <=> ((A - B) <= (C - B))
       when C > B and A is unsigned.  */
    unsigned int digit;

    digit = grub_tolower (*ptr) - '0';
    if (digit > 9)
    {
      digit -= 'a' - '0';
      if (mult == 10 || digit > 5)
        break;
      digit += 10;
    }

    found = 1;
    /* we do not check for hex or negative */
    if (mult == 10 && ! negative)
    /* 0xFFFFFFFFFFFFFFFF == 18446744073709551615ULL */
    if (myint > 1844674407370955161ULL ||
        (myint == 1844674407370955161ULL && digit > 5))
    {
      grub_error (GRUB_ERR_OUT_OF_RANGE, "number overflow.");
      return 0;
    }
    myint *= mult;
    myint += digit;
    ptr++;
  }

  if (!found)
  {
    grub_error (GRUB_ERR_BAD_NUMBER, "ERR_NUMBER_PARSING.");
    return 0;
  }

  *str_ptr = ptr;
  *myint_ptr = negative ? -myint : myint;

  return 1;
}

static void
checktime_help (void)
{
  grub_printf ("Usage: \n");
  grub_printf ("  checktime MINUTE  HOUR   DAY   MONTH  DAY\n");
  grub_printf ("                         (month)       (week)\n");
  grub_printf ("           \\*    any value\n");
  grub_printf ("            ,    value list separator\n");
  grub_printf ("            -    range of values\n");
  grub_printf ("            /    step values\n");
}

static grub_err_t
grub_cmd_checktime (grub_command_t ctxt __attribute__ ((unused)),
               int argc, char **args)
{
  struct grub_datetime datetime;
  unsigned int limit[5][2] = {{0, 59}, {0, 23}, {1, 31}, {1, 12}, {0, 7}};
  unsigned int field[5];
  unsigned int i;

  if (grub_get_datetime (&datetime))
    return grub_errno;
  if (argc != 5)
  {
    checktime_help ();
    return GRUB_ERR_NONE;
  }

  field[0] = datetime.minute;
  field[1] = datetime.hour;
  field[2] = datetime.day;
  field[3] = datetime.month;
  field[4] = grub_get_weekday (&datetime);

  for (i = 0; i < 5; i++)
  {
    char *p;
    int ok = 0;

    p = args[i];
    while (1)
    {
      unsigned long long m1, m2, m3;
      unsigned int j;
      if (*p == '*')
      {
        m1 = limit[i][0];
        m2 = limit[i][1];
        p++;
      }
      else
      {
        if (! safe_parse_maxint (&p, &m1))
          return 0;
        if (*p == '-')
        {
          p++;
          if (! safe_parse_maxint (&p, &m2))
            return 0;
        }
        else
          m2 = m1;
      }

      if ((m1 < limit[i][0]) || (m2 > limit[i][1]) || (m1 > m2))
        return 0;
      if (*p == '/')
      {
        p++;
        if (! safe_parse_maxint (&p, &m3))
          return 0;
      }
      else
        m3 = 1;
      for (j = m1; j <= m2; j+= m3)
      {
        if (j == field[i])
        {
          ok = 1;
          break;
        }
      }

      if (ok)
        break;
      if (*p == ',')
        p++;
      else
        break;
    }
    if (!ok)
      break;
  }
  return (i == 5) ? GRUB_ERR_NONE
    : grub_error (GRUB_ERR_TEST_FAILURE, N_("false"));
}

static grub_extcmd_t cmd_date;
static grub_command_t cmd_checktime;

GRUB_MOD_INIT(date)
{
  cmd_date = grub_register_extcmd ("date", grub_cmd_date, 0,
                      N_("[[year-]month-day] [hour:minute[:second]]"),
                      N_("Display/set current datetime."), options);

  cmd_checktime = grub_register_command ("checktime", grub_cmd_checktime,
                      N_("min hour dom month dow"),
                      N_("Check current date and time."));
}

GRUB_MOD_FINI(date)
{
  grub_unregister_extcmd (cmd_date);
  grub_unregister_command (cmd_checktime);
}
