/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
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

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/lua.h>

#include "ini.h"

GRUB_MOD_LICENSE ("GPLv3+");
GRUB_MOD_DUAL_LICENSE ("MIT");

static const struct grub_arg_option options_get[] =
{
  {"set", 's', 0,
   N_("Set a variable to return value."), N_("VARNAME"), ARG_TYPE_STRING},
  {0, 0, 0, 0, 0, 0}
};

enum options_get
{
  INIGET_SET,
};

static grub_err_t
grub_cmd_ini_get (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "string required");

  ini_t *config = ini_load (args[0]);
  char *input = NULL;
  char *section = NULL;
  char *key = NULL;
  const char *name = NULL;

  if (!config)
  {
    grub_printf ("cannot parse file: %s\n", args[0]);
    return 0;
  }

  input = grub_strdup (args[1]);
  key = grub_strchr (input, ':');
  if (!key)
    key = input;
  else
  {
    section = input;
    *key = '\0';
    key++;
  }
  name = ini_get(config, section, key);
  if (name)
  {
    if (state[INIGET_SET].set)
      grub_env_set (state[INIGET_SET].arg, name);
    else
      grub_printf("%s%s%s = %s\n", section?:"", section?":":"", key, name);
  }

  if (input)
    grub_free (input);
  ini_free(config);
  return 0;
}

static grub_extcmd_t cmd_get;

static int
lua_ini_load (lua_State *state)
{
  ini_t *config = NULL;
  const char *name;
  name = luaL_checkstring (state, 1);
  config = ini_load (name);
  if (! config)
    return 0;
  lua_pushlightuserdata (state, config);
  return 1;
}

static int
lua_ini_free (lua_State *state)
{
  ini_t *config = NULL;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  config = lua_touserdata (state, 1);
  ini_free (config);
  return push_result (state);
}

static int
lua_ini_get (lua_State *state)
{
  ini_t *config = NULL;
  const char *section = NULL;
  const char *key = NULL;
  const char *val = NULL;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  config = lua_touserdata (state, 1);
  section = (lua_gettop (state) > 2) ?
             luaL_checkstring (state, 2) : NULL;
  key = (lua_gettop (state) > 2) ?
         luaL_checkstring (state, 3) : luaL_checkstring (state, 2);
  val = ini_get (config, section, key);
  if (!val)
    return 0;
  lua_pushstring (state, val);
  return 1;
}

static luaL_Reg inilib[] =
{
  {"load", lua_ini_load},
  {"free", lua_ini_free},
  {"get", lua_ini_get},
  {0, 0}
};

GRUB_MOD_INIT(ini)
{
  cmd_get = grub_register_extcmd ("ini_get", grub_cmd_ini_get, 0,
                  N_("[--set=VARNAME] FILE [SECTION:]KEY"),
                  N_("Get value from ini files."), options_get);
  if (grub_lua_global_state)
  {
    lua_gc (grub_lua_global_state, LUA_GCSTOP, 0);
    luaL_register (grub_lua_global_state, "ini", inilib);
    lua_gc (grub_lua_global_state, LUA_GCRESTART, 0);
  }
}

GRUB_MOD_FINI(ini)
{
  grub_unregister_extcmd (cmd_get);
}
