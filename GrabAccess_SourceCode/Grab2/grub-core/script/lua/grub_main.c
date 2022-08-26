/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
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

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "grub_lib.h"

#include <grub/dl.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/normal.h>
#include <grub/lua.h>
#include <grub/command.h>
#include <grub/extcmd.h>

GRUB_MOD_LICENSE("GPLv3+");

lua_State *grub_lua_global_state = NULL;

/* Call `grub_error' to report a Lua error.  The error message string must be
   on the top of the Lua stack (at index -1).  The error message is popped off
   the Lua stack before this function returns.  */
static void
handle_lua_error (const char *error_type)
{
  const char *error_msg;
  error_msg = lua_tostring (grub_lua_global_state, -1);
  if (error_msg == NULL)
    error_msg = "(error message not a string)";
  grub_error (GRUB_ERR_BAD_ARGUMENT, "%s: %s", error_type, error_msg);
  /* Pop the error message.  */
  lua_pop (grub_lua_global_state, 1);
}

/* Taken from lua.c */
static int
incomplete (lua_State * L, int status)
{
  if (status == LUA_ERRSYNTAX)
    {
      size_t lmsg;
      const char *msg = lua_tolstring (L, -1, &lmsg);
      const char *tp = msg + lmsg - (sizeof (LUA_QL ("<eof>")) - 1);
      if (strstr (msg, LUA_QL ("<eof>")) == tp)
	{
	  lua_pop (L, 1);
	  return 1;
	}
    }
  return 0;			/* else... */
}

static grub_err_t
interactive (void)
{
  const char *ps1 = "lua> ";
  const char *ps2 = "lua>> ";
  const char *prompt = ps1;
  char *line;
  char *chunk = NULL;
  size_t len;
  size_t oldlen = 0;
  int r;

  grub_printf ("%s", N_ ("Welcome to lua, press the escape key to exit."));

  while ((line = grub_cmdline_get (prompt)) != NULL)
    {
      /* len = lenght of chunk + line + newline character */
      len = oldlen + grub_strlen (line) + 1;
      chunk = grub_realloc (chunk, len + 1);
      grub_strcpy (chunk + oldlen , line);
      chunk[len - 1] = '\n';
      chunk[len] = '\0';
      grub_free (line);

      r = luaL_loadbuffer (grub_lua_global_state, chunk, len, "stdin");
      if (!r)
	{
	  /* No error: Execute this chunk and prepare to read another */
	  r = lua_pcall (grub_lua_global_state, 0, 0, 0);
	  if (r)
	    {
	      handle_lua_error ("Lua");
	      grub_print_error ();
	    }

	  grub_free (chunk);
	  chunk = NULL;
	  len = 0;
	  prompt = ps1;
	}
      else if (incomplete (grub_lua_global_state, r))
	{
	  /* Chunk is incomplete, try reading another line */
	  prompt = ps2;
	}
      else if (r == LUA_ERRSYNTAX)
	{
	  handle_lua_error ("Lua");
	  grub_print_error ();

	  /* This chunk is garbage, try starting another one */
	  grub_free (chunk);
	  chunk = NULL;
	  len = 0;
	  prompt = ps1;
	}
      else
	{
	  /* Handle errors other than syntax errors (out of memory, etc.) */
	  grub_free (chunk);
	  handle_lua_error ("Lua parser failed");
	  return grub_errno;
	}

      oldlen = len;
    }

  grub_free (chunk);
  lua_gc (grub_lua_global_state, LUA_GCCOLLECT, 0);

  return grub_errno;
}

static void print_version (void)
{
  grub_printf (LUA_RELEASE "  " LUA_COPYRIGHT);
}

static const struct grub_arg_option options[] =
{
  {"execute", 'e', 0, N_("Execute string."), 0, 0},
  {"load", 'l', 0, N_("Load library."), N_("NAME"), ARG_TYPE_STRING},
  {"interactive", 'i', 0, N_("Enter interactive mode after executing script."), 0, 0},
  {"version", 'v', 0, N_("Show version information."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

enum options
{
  GRUB_LUA_EXE,
  GRUB_LUA_LOA,
  GRUB_LUA_INT,
  GRUB_LUA_VER,
};

static grub_err_t
grub_cmd_lua (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *opt = ctxt->state;
  if (opt[GRUB_LUA_LOA].set)
  {
    lua_getglobal(grub_lua_global_state, "require");
    lua_pushstring(grub_lua_global_state, opt[GRUB_LUA_LOA].arg);
  }
  if (argc == 1)
  {
    if (opt[GRUB_LUA_EXE].set)
    {
      if (!luaL_loadbuffer (grub_lua_global_state, args[0], strlen(args[0]), "stdin"))
        lua_pcall (grub_lua_global_state, 0, 0, 0);
      handle_lua_error ("Lua");
    }
    else if (luaL_loadfile (grub_lua_global_state, args[0]))
    {
      handle_lua_error ("Lua");
    }
    else if (lua_pcall (grub_lua_global_state, 0, 0, 0))
    {
      handle_lua_error ("Lua");
    }
    if (opt[GRUB_LUA_INT].set)
      return interactive ();
  }
  else if (argc == 0)
  {
    if (opt[GRUB_LUA_VER].set)
      print_version ();
    else
      return interactive ();
  }
  else
  {
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "1 or 0 arguments expected");
  }

  return grub_errno;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT (lua)
{
  grub_lua_global_state = lua_open ();
  if (grub_lua_global_state)
  {
    lua_gc (grub_lua_global_state, LUA_GCSTOP, 0);
    luaL_openlibs (grub_lua_global_state);
    luaL_register (grub_lua_global_state, "grub", grub_lua_lib);
    lua_gc (grub_lua_global_state, LUA_GCRESTART, 0);
    cmd = grub_register_extcmd ("lua", grub_cmd_lua, 0, N_("[OPTIONS] [FILE]"),
              N_ ("Run lua script FILE or start interactive lua shell"), options);
  }
}

GRUB_MOD_FINI (lua)
{
  if (grub_lua_global_state)
  {
    lua_close (grub_lua_global_state);
    grub_unregister_extcmd (cmd);
  }
}
