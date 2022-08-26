/* uuid.c - Command to generate UUID  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2010  Free Software Foundation, Inc.
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
#include <grub/env.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/term.h>
#include <grub/time.h>
#include <grub/uuid.h>

GRUB_MOD_LICENSE ("GPLv3+");

#pragma GCC diagnostic ignored "-Wcast-align"

/* rand() and srand() */
static grub_uint32_t next = 1;

grub_uint32_t
grub_rand (void)
{
  next = next * 1103515245 + 12345;
  return (next << 16) | ((next >> 16) & 0xFFFF);
}

void
grub_srand (grub_uint32_t seed)
{
  next = seed;
}

static const struct grub_arg_option options_rand[] =
{
  {"from", 'f', 0, N_("from"), N_("XXX"), ARG_TYPE_INT},
  {"to", 't', 0, N_("to"), N_("XXX"), ARG_TYPE_INT},
  {0, 0, 0, 0, 0, 0}
};

enum options_rand
{
  RAND_FROM,
  RAND_TO
};

static grub_err_t
grub_cmd_rand (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  grub_uint32_t r, f = 0, t = GRUB_UINT_MAX;
  grub_srand (grub_get_time_ms());
  r = grub_rand ();
  if (state[RAND_FROM].set)
    f = grub_strtoul (state[RAND_FROM].arg, NULL, 0);
  if (state[RAND_TO].set)
    t = grub_strtoul (state[RAND_TO].arg, NULL, 0);
  if (t < f + 1)
    t = GRUB_UINT_MAX;
  r = r % (t - f) + f;
  if (argc == 0)
    grub_printf ("%u\n", r);
  else
  {
    char str[11]; /* 4294967295 + \0 */
    grub_snprintf (str, 11, "%u", r);
    grub_env_set (args[0], str);
  }
  return GRUB_ERR_NONE;
}

grub_uint32_t guid[4];

static grub_uint64_t
xorshift128plus (grub_uint64_t *s)
{
  grub_uint64_t s1 = s[0];
  const grub_uint64_t s0 = s[1];
  s[0] = s0;
  s1 ^= s1 << 23;
  s[1] = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5);
  return s[1] + s0;
}

static void
uuid_init (void)
{
  grub_srand (grub_get_time_ms());
  int i;
  for (i = 0; i < 4; i++)
  {
    guid[i] = grub_rand ();
  }
}

static void
uuid_generate (char *dst)
{
  static const char *template = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
  static const char *chars = "0123456789abcdef";
  union
  {
    unsigned char b[16];
    grub_uint64_t word[2];
  } s;
  const char *p;
  int i, n;
  /* get random */
  s.word[0] = xorshift128plus((grub_uint64_t *) guid);
  s.word[1] = xorshift128plus((grub_uint64_t *) guid);
  /* build string */
  p = template;
  i = 0;
  while (*p)
  {
    n = s.b[i >> 1];
    n = (i & 1) ? (n >> 4) : (n & 0xf);
    switch (*p)
    {
      case 'x'  : *dst = chars[n];              i++;  break;
      case 'y'  : *dst = chars[(n & 0x3) + 8];  i++;  break;
      default   : *dst = *p;
    }
    dst++, p++;
  }
  *dst = '\0';
}

static grub_err_t
grub_cmd_uuid4 (grub_extcmd_context_t ctxt __attribute__((unused)),
               int argc, char **args)
{
  char buf[GUID_LEN];
  uuid_init ();
  uuid_generate (buf);
  if (argc == 0)
    grub_printf ("%s\n", buf);
  else
    grub_env_set (args[0], buf);
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd_rand, cmd_uuid4;

GRUB_MOD_INIT(uuid)
{
  cmd_rand = grub_register_extcmd ("rand", grub_cmd_rand, 0,
                                   N_("[--from XXX] [--to XXX] VAR"),
                                   N_("Generate a random number."),
                                   options_rand);
  cmd_uuid4 = grub_register_extcmd ("uuid4", grub_cmd_uuid4, 0, N_("VAR"),
                              N_("Generate a uuid4 string."), 0);
}

GRUB_MOD_FINI(uuid)
{
  grub_unregister_extcmd (cmd_rand);
  grub_unregister_extcmd (cmd_uuid4);
}
