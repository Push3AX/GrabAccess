/*
** $Id: loslib.c,v 1.19.1.3 2008/01/18 16:38:18 roberto Exp $
** Standard Operating System library
** See Copyright Notice in lua.h
*/


#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define loslib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

#include <grub/time.h>
#include <grub/datetime.h>
#include <grub/script_sh.h>

/*
static int os_pushresult (lua_State *L, int i, const char *filename) {
  int en = errno;
  if (i) {
    lua_pushboolean(L, 1);
    return 1;
  }
  else {
    lua_pushnil(L);
    lua_pushfstring(L, "%s: %s", filename, strerror(en));
    lua_pushinteger(L, en);
    return 3;
  }
}
*/

static int os_execute (lua_State *L) {
  lua_pushinteger(L, grub_script_execute_sourcecode (luaL_optstring(L, 1, NULL)));
  return 1;
}

/*
static int os_remove (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  return os_pushresult(L, remove(filename) == 0, filename);
}

static int os_rename (lua_State *L) {
  const char *fromname = luaL_checkstring(L, 1);
  const char *toname = luaL_checkstring(L, 2);
  return os_pushresult(L, rename(fromname, toname) == 0, fromname);
}

static int os_tmpname (lua_State *L) {
  char buff[LUA_TMPNAMBUFSIZE];
  int err;
  lua_tmpnam(buff, err);
  if (err)
    return luaL_error(L, "unable to generate a unique filename");
  lua_pushstring(L, buff);
  return 1;
}
*/

static int os_getenv (lua_State *L) {
  lua_pushstring(L, getenv(luaL_checkstring(L, 1)));  /* if NULL push nil */
  return 1;
}

static int os_clock (lua_State *L) {
  lua_pushnumber(L, grub_get_time_ms());
  return 1;
}

/*
** {======================================================
** Time/Date operations
** { year=%Y, month=%m, day=%d, hour=%H, min=%M, sec=%S,
**   wday=%w+1, yday=%j, isdst=? }
** =======================================================
*/

static void setfield (lua_State *L, const char *key, int value) {
  lua_pushinteger(L, value);
  lua_setfield(L, -2, key);
}

static void setboolfield (lua_State *L, const char *key, int value) {
  if (value < 0)  /* undefined? */
    return;  /* does not set field */
  lua_pushboolean(L, value);
  lua_setfield(L, -2, key);
}

/*
static int getboolfield (lua_State *L, const char *key) {
  int res;
  lua_getfield(L, -1, key);
  res = lua_isnil(L, -1) ? -1 : lua_toboolean(L, -1);
  lua_pop(L, 1);
  return res;
}
*/

static int getfield (lua_State *L, const char *key, int d) {
  int res;
  lua_getfield(L, -1, key);
  if (lua_isnumber(L, -1))
    res = (int)lua_tointeger(L, -1);
  else {
    if (d < 0)
      return luaL_error(L, "field " LUA_QS " missing in date table", key);
    res = d;
  }
  lua_pop(L, 1);
  return res;
}


static int os_date (lua_State *L) {
  const char *s = luaL_optstring(L, 1, "%c");
  struct grub_datetime t;
  if (lua_isnoneornil(L, 2))  /* called without args? */
    grub_get_datetime (&t);  /* get current time */
  else {
    grub_unixtime2datetime (luaL_checkinteger (L, 2), &t);
  }

  if (*s == '!') {  /* UTC? */
    s++;  /* skip `!' */
  }

  if (strcmp(s, "*t") == 0) {
    lua_createtable(L, 0, 9);  /* 9 = number of fields */
    setfield(L, "sec", t.second);
    setfield(L, "min", t.minute);
    setfield(L, "hour", t.hour);
    setfield(L, "day", t.day);
    setfield(L, "month", t.month);
    setfield(L, "year", t.year);
    setfield(L, "wday", grub_get_weekday (&t));
    setfield(L, "yday", 0);
    setboolfield(L, "isdst", 0);
  }
  else {
    lua_pushnil(L);
  }
  return 1;
}


static int os_time (lua_State *L) {
  struct grub_datetime t;
  if (lua_isnoneornil(L, 1))  /* called without args? */
    grub_get_datetime (&t);  /* get current time */
  else {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);  /* make sure table is at the top */
    t.second = getfield(L, "sec", 0);
    t.minute = getfield(L, "min", 0);
    t.hour = getfield(L, "hour", 12);
    t.day = getfield(L, "day", -1);
    t.month = getfield(L, "month", -1);
    t.year = getfield(L, "year", -1);
  }
  int r;
  grub_int64_t nix;
  r = grub_datetime2unixtime (&t, &nix);
  if (r == 0)
    lua_pushnil(L);
  else
    lua_pushnumber(L, nix);
  return 1;
}

/*
static int os_difftime (lua_State *L) {
  lua_pushnumber(L, difftime((time_t)(luaL_checknumber(L, 1)),
                             (time_t)(luaL_optnumber(L, 2, 0))));
  return 1;
}
*/

/* }====================================================== */

/*
static int os_setlocale (lua_State *L) {
  static const int cat[] = {LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY,
                      LC_NUMERIC, LC_TIME};
  static const char *const catnames[] = {"all", "collate", "ctype", "monetary",
     "numeric", "time", NULL};
  const char *l = luaL_optstring(L, 1, NULL);
  int op = luaL_checkoption(L, 2, "all", catnames);
  lua_pushstring(L, setlocale(cat[op], l));
  return 1;
}
*/

static int os_exit (lua_State *L __attribute__ ((unused))) {
  exit(luaL_optint(L, 1, 0));
}

static const luaL_Reg syslib[] = {
  {"clock",     os_clock},
  {"date",      os_date},
  //{"difftime",  os_difftime},
  {"execute",   os_execute},
  {"exit",      os_exit},
  {"getenv",    os_getenv},
  //{"remove",    os_remove},
  //{"rename",    os_rename},
  //{"setlocale", os_setlocale},
  {"time",      os_time},
  //{"tmpname",   os_tmpname},
  {NULL, NULL}
};

/* }====================================================== */



LUALIB_API int luaopen_os (lua_State *L) {
  luaL_register(L, LUA_OSLIBNAME, syslib);
  return 1;
}

