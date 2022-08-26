/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2017  Free Software Foundation, Inc.
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
#include <grub/term.h>
#include <grub/env.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/lua.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
{
  {0, 'n', 0, N_("grub_getkey_noblock"), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

static grub_err_t
grub_cmd_getkey (grub_extcmd_context_t ctxt, int argc, char **args)

{
  struct grub_arg_list *state = ctxt->state;
  int key;
  char keyenv[20];
  if (state[0].set)
    key = grub_getkey_noblock ();
  else
    key = grub_getkey ();

  if (argc == 1)
  {
    grub_snprintf (keyenv, 20, "%d", key);
    grub_env_set (args[0], keyenv);
  }
  else
  {
    grub_printf ("0x%08x\n", key);
  }
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd;

static int
lua_input_read (lua_State *state)
{
  int hide;
  hide = (lua_gettop (state) > 0) ? luaL_checkinteger (state, 1) : 0;
  char *line = grub_getline (hide);
  if (! line)
    lua_pushnil(state);
  else
    lua_pushstring (state, line);

  grub_free (line);
  grub_printf ("\n");
  return 1;
}

/* Lua function: input.getkey() : returns { ASCII char, scan code }.  */
static int
lua_input_getkey (lua_State *state)
{
  int c = grub_getkey();
  lua_pushinteger (state, c & 0xFF);          /* Push ASCII character code.  */
  lua_pushinteger (state, (c >> 8) & 0xFF);   /* Push the scan code.  */
  return 2;
}
static int
lua_input_getkey_noblock (lua_State *state __attribute__ ((unused)))
{
  int c = grub_getkey_noblock ();
  lua_pushinteger (state, c & 0xFF);          /* Push ASCII character code.  */
  lua_pushinteger (state, (c >> 8) & 0xFF);   /* Push the scan code.  */
  return 2;
}

static luaL_Reg inputlib[] =
{
  {"getkey", lua_input_getkey},
  {"getkey_noblock", lua_input_getkey_noblock},
  {"read", lua_input_read},
  {0, 0}
};

GRUB_MOD_INIT(getkey)
{
  cmd = grub_register_extcmd ("getkey", grub_cmd_getkey, 0,
                              N_("[-n] [VARNAME]"),
                              N_("Return the value of the pressed key. "),
                              options);
  if (grub_lua_global_state)
  {
    lua_gc (grub_lua_global_state, LUA_GCSTOP, 0);
    luaL_register (grub_lua_global_state, "input", inputlib);
    lua_gc (grub_lua_global_state, LUA_GCRESTART, 0);
  }
}

GRUB_MOD_FINI(getkey)
{
  grub_unregister_extcmd (cmd);
}
