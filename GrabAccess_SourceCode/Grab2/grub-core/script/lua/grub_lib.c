/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009,2018,2019,2020  Free Software Foundation, Inc.
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
#include <grub/env.h>
#include <grub/parser.h>
#include <grub/command.h>
#include <grub/normal.h>
#include <grub/term.h>
#include <grub/file.h>
#include <grub/menu.h>
#include <grub/memory.h>
#include <grub/misc.h>
#include <grub/device.h>
#include <grub/partition.h>
#include <grub/i18n.h>
#include <grub/time.h>
#include <grub/script_sh.h>
#include <grub/lua.h>

/* Updates the globals grub_errno and grub_msg, leaving their values on the 
   top of the stack, and clears grub_errno. When grub_errno is zero, grub_msg
   is not left on the stack. The value returned is the number of values left on
   the stack. */
int
push_result (lua_State *state)
{
  int saved_errno;
  int num_results;

  saved_errno = grub_errno;
  grub_errno = 0;

  /* Push once for setfield, and again to leave on the stack */
  lua_pushinteger (state, saved_errno);
  lua_pushinteger (state, saved_errno);
  lua_setfield (state, LUA_GLOBALSINDEX, "grub_errno");

  if (saved_errno)
  {
    /* Push once for setfield, and again to leave on the stack */
    lua_pushstring (state, grub_errmsg);
    lua_pushstring (state, grub_errmsg);
    num_results = 2;
  }
  else
  {
    lua_pushnil (state);
    num_results = 1;
  }

  lua_setfield (state, LUA_GLOBALSINDEX, "grub_errmsg");

  return num_results;
}

/* Updates the globals grub_errno and grub_msg ( without leaving them on the
   stack ), clears grub_errno,  and returns the value of grub_errno before it
   was cleared. */
int
save_errno (lua_State *state)
{
  int saved_errno;

  saved_errno = grub_errno;
  lua_pop(state, push_result(state));

  return saved_errno;
}

static int
grub_lua_run (lua_State *state)
{
  int n;
  char **args;
  const char *s;

  s = luaL_checkstring (state, 1);
  if ((! grub_parser_split_cmdline (s, 0, 0, &n, &args))
      && (n >= 0))
    {
      grub_command_t cmd;

      cmd = grub_command_find (args[0]);
      if (cmd)
    (cmd->func) (cmd, n-1, &args[1]);
      else
    grub_error (GRUB_ERR_FILE_NOT_FOUND, "command not found");

      grub_free (args[0]);
      grub_free (args);
    }

  return push_result (state);
}

static int
grub_lua_script (lua_State *state)
{
  const char *s;

  s = luaL_checkstring (state, 1);
  grub_err_t err;
  err = grub_script_execute_sourcecode (s);
  if (!err)
    grub_error (GRUB_ERR_UNKNOWN_COMMAND, "ERROR");

  return push_result (state);
}

static int
grub_lua_getenv (lua_State *state)
{
  int n, i;

  n = lua_gettop (state);
  for (i = 1; i <= n; i++)
    {
      const char *name, *value;

      name = luaL_checkstring (state, i);
      value = grub_env_get (name);
      if (value)
    lua_pushstring (state, value);
      else
    lua_pushnil (state);
    }

  return n;
}

static int
grub_lua_setenv (lua_State *state)
{
  const char *name, *value;

  name = luaL_checkstring (state, 1);
  value = luaL_checkstring (state, 2);

  if (name[0])
    grub_env_set (name, value);

  return 0;
}

static int
grub_lua_exportenv (lua_State *state)
{
  const char *name, *value;

  name = luaL_checkstring (state, 1);
  value = luaL_checkstring (state, 2);

  if (name[0])
    {
    grub_env_export (name);
    if (value[0])
      grub_env_set (name, value);
    }

  return 0;
}
/* Helper for grub_lua_enum_device.  */
static int
grub_lua_enum_device_iter (const char *name, void *data)
{
  lua_State *state = data;
  int result;
  grub_device_t dev;

  result = 0;
  dev = grub_device_open (name);
  if (dev)
  {
    grub_fs_t fs;

    fs = grub_fs_probe (dev);
    if (fs)
    {
      lua_pushvalue (state, 1);
      lua_pushstring (state, name);
      lua_pushstring (state, fs->name);
      if (! fs->fs_uuid)
        lua_pushnil (state);
      else
      {
        int err;
        char *uuid = NULL;
        err = fs->fs_uuid (dev, &uuid);
        if (err || !uuid)
        {
          grub_errno = 0;
          lua_pushnil (state);
        }
        else
        {
          lua_pushstring (state, uuid);
          grub_free (uuid);
        }
      }
      if (! fs->fs_label)
        lua_pushnil (state);
      else
      {
        int err;
        char *label = NULL;
        err = fs->fs_label (dev, &label);
        if (err || !label)
        {
          grub_errno = 0;
          lua_pushnil (state);
        }
        else
        {
          lua_pushstring (state, label);
          grub_free (label);
        }
      }
      if (!dev->disk)
        lua_pushnil (state);
      else
      {
        const char *human_size = NULL;
        human_size = grub_get_human_size (grub_disk_native_sectors (dev->disk)
              << GRUB_DISK_SECTOR_BITS, GRUB_HUMAN_SIZE_SHORT);
        if (human_size)
          lua_pushstring (state, human_size);
        else
          lua_pushnil (state);
      }
      lua_call (state, 5, 1);
      result = lua_tointeger (state, -1);
      lua_pop (state, 1);
    }
    else
      grub_errno = 0;
    grub_device_close (dev);
  }
  else
    grub_errno = 0;

  return result;
}

static int
grub_lua_enum_device (lua_State *state)
{
  luaL_checktype (state, 1, LUA_TFUNCTION);
  grub_device_iterate (grub_lua_enum_device_iter, state);
  return push_result (state);
}

static int
enum_file (const char *name, const struct grub_dirhook_info *info,
       void *data)
{
  int result;
  lua_State *state = data;
  if (grub_strcmp (name, ".") == 0 || grub_strcmp (name, "..") == 0 ||
      grub_strcmp (name, "System Volume Information") == 0)
    return 0;

  lua_pushvalue (state, 1);
  lua_pushstring (state, name);
  lua_pushinteger (state, info->dir != 0);
  lua_call (state, 2, 1);
  result = lua_tointeger (state, -1);
  lua_pop (state, 1);

  return result;
}

static int
grub_lua_enum_file (lua_State *state)
{
  char *device_name;
  const char *arg;
  grub_device_t dev;

  luaL_checktype (state, 1, LUA_TFUNCTION);
  arg = luaL_checkstring (state, 2);
  device_name = grub_file_get_device_name (arg);
  dev = grub_device_open (device_name);
  if (dev)
  {
    grub_fs_t fs;
    const char *path;

    fs = grub_fs_probe (dev);
    path = grub_strchr (arg, ')');
    if (! path)
      path = arg;
    else
      path++;
    if ((!path && !device_name) || !*path)
      grub_printf ("invalid path\n");
    if (fs)
      (fs->fs_dir) (dev, path, enum_file, state);

    grub_device_close (dev);
  }

  grub_free (device_name);

  return push_result (state);
}

static int
grub_lua_file_open (lua_State *state)
{
  grub_file_t file;
  const char *name;
  const char *flag;

  name = luaL_checkstring (state, 1);
  flag = (lua_gettop (state) > 1) ? luaL_checkstring (state, 2) : 0;
  file = grub_file_open (name, GRUB_FILE_TYPE_SKIP_SIGNATURE);
  save_errno (state);

  if (! file)
    return 0;

  if (grub_strchr (flag, 'w'))
    grub_blocklist_convert (file);

  lua_pushlightuserdata (state, file);
  return 1;
}

static int
grub_lua_file_close (lua_State *state)
{
  grub_file_t file;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);
  grub_file_close (file);

  return push_result (state);
}

static int
grub_lua_file_seek (lua_State *state)
{
  grub_file_t file;
  grub_off_t offset;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);
  offset = luaL_checkinteger (state, 2);

  offset = grub_file_seek (file, offset);
  save_errno (state);

  lua_pushinteger (state, offset);
  return 1;
}

static int
grub_lua_file_read (lua_State *state)
{
  grub_file_t file;
  luaL_Buffer b;
  int n;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);
  n = luaL_checkinteger (state, 2);

  luaL_buffinit (state, &b);
  while (n)
    {
      char *p;
      int nr;

      nr = (n > LUAL_BUFFERSIZE) ? LUAL_BUFFERSIZE : n;
      p = luaL_prepbuffer (&b);

      nr = grub_file_read (file, p, nr);
      if (nr <= 0)
    break;

      luaL_addsize (&b, nr);
      n -= nr;
    }

  save_errno (state);
  luaL_pushresult (&b);
  return 1;
}

static int
grub_lua_file_write (lua_State *state)
{
  grub_file_t file;
  grub_ssize_t ret;
  grub_size_t len;
  const char *buf;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);
  buf = lua_tolstring (state, 2, &len);
  ret = grub_blocklist_write (file, buf, len);
  if (ret > 0)
    file->offset += ret;

  save_errno (state);
  lua_pushinteger (state, ret);
  return 1;
}

static int
grub_lua_file_getline (lua_State *state)
{
  grub_file_t file;
  char *line;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);

  line = grub_file_getline (file);
  save_errno (state);

  if (! line)
    return 0;

  lua_pushstring (state, line);
  grub_free (line);
  return 1;
}

static int
grub_lua_file_getsize (lua_State *state)
{
  grub_file_t file;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);

  lua_pushinteger (state, file->size);
  return 1;
}

static int
grub_lua_file_getpos (lua_State *state)
{
  grub_file_t file;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);

  lua_pushinteger (state, file->offset);
  return 1;
}

static int
grub_lua_file_eof (lua_State *state)
{
  grub_file_t file;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);

  lua_pushboolean (state, file->offset >= file->size);
  return 1;
}

static int
grub_lua_file_exist (lua_State *state)
{
  grub_file_t file;
  const char *name;
  int result;

  result = 0;
  name = luaL_checkstring (state, 1);
  file = grub_file_open (name, GRUB_FILE_TYPE_FS_SEARCH);
  if (file)
    {
      result++;
      grub_file_close (file);
    }
  else
    grub_errno = 0;

  lua_pushboolean (state, result);
  return 1;
}

//file, skip, length
static int
grub_lua_hexdump (lua_State *state)
{
  grub_file_t file;
  grub_size_t len, i;
  grub_disk_addr_t skip;
  char *var_buf = NULL;
  char *var_hex = NULL;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);
  if (!file)
    return 0;

  skip = luaL_checkinteger (state, 2);
  len = luaL_checkinteger (state, 3);
  if (skip > file->size)
    return 0;
  if (skip + len > file->size)
    len = file->size - skip;

  var_buf = (char *) grub_zalloc (len + 1);
  var_hex = (char *) grub_zalloc (2 * len + 1);
  if (!var_buf || !var_hex)
    return 0;

  file->offset = skip;
  grub_file_read (file, var_buf, len);

  for (i = 0; i < len; i++)
  {
    grub_snprintf (var_hex + 2 * i, 3, "%02x", (unsigned char)var_buf[i]);
    var_buf[i] = ((var_buf[i] >= 32) && (var_buf[i] < 127)) ? var_buf[i] : '.';
  }
  lua_pushstring (state, var_buf);
  lua_pushstring (state, var_hex);
  grub_free (var_buf);
  grub_free (var_hex);
  return 2;
}

static int
grub_lua_add_menu (lua_State *state)
{
  int n;
  const char *source;

  source = luaL_checklstring (state, 1, 0);
  n = lua_gettop (state) - 1;
  if (n > 0)
  {
    const char **args;
    char *p;
    int i;

    args = grub_malloc (n * sizeof (args[0]));
    if (!args)
      return push_result (state);
    for (i = 0; i < n; i++)
      args[i] = luaL_checkstring (state, 2 + i);

    p = grub_strdup (source);
    if (! p)
      return push_result (state);

    grub_normal_add_menu_entry (n, args, NULL, NULL, NULL, NULL, NULL, p, NULL,
                                0, NULL, NULL);
    grub_free (p);
    grub_free (args);
  }
  else
  {
    lua_pushstring (state, "not enough parameter");
    lua_error (state);
  }

  return push_result (state);
}

static int
grub_lua_clear_menu (lua_State *state __attribute__ ((unused)))
{
  grub_normal_clear_menu ();
  return 0;
}

static int
grub_lua_add_icon_menu (lua_State *state)
{
  int n;
  const char *source;
  source = luaL_checklstring (state, 2, 0);
  n = lua_gettop (state) - 2;
  if (n > 0)
  {
    const char **args;
    char *p;
    int i;
    char **class = NULL;
    class = grub_malloc (2 * sizeof (class[0]));
    class[0] = grub_strdup (luaL_checklstring (state, 1, 0));
    class[1] = NULL;
    args = grub_malloc (n * sizeof (args[0]));
    if (!args)
      return push_result (state);
    for (i = 0; i < n; i++)
      args[i] = luaL_checkstring (state, 3 + i);

    p = grub_strdup (source);
    if (! p)
      return push_result (state);
    grub_normal_add_menu_entry (n, args, class, NULL, NULL, NULL, NULL, p, NULL,
                                0, NULL, NULL);
    grub_free (p);
    grub_free (args);
  }
  else
  {
    lua_pushstring (state, "not enough parameter");
    lua_error (state);
  }
  return push_result (state);
}

static int
grub_lua_add_hidden_menu (lua_State *state)
{
  int n;
  const char *source;
  source = luaL_checklstring (state, 2, 0);
  n = lua_gettop (state) - 2;
  if (n > 0)
  {
    const char **args;
    char *p;
    int i;
    const char *hotkey;
    args = grub_malloc (n * sizeof (args[0]));
    if (!args)
      return push_result (state);
    for (i = 0; i < n; i++)
      args[i] = luaL_checkstring (state, 3 + i);

    p = grub_strdup (source);
    if (! p)
      return push_result (state);
    hotkey = grub_strdup (luaL_checklstring (state, 1, 0));
    grub_normal_add_menu_entry (n, args, NULL, NULL, NULL, hotkey, NULL, p, NULL,
                                0x02, NULL, NULL);
    grub_free (p);
    grub_free (args);
  }
  else
  {
    lua_pushstring (state, "not enough parameter");
    lua_error (state);
  }
  return push_result (state);
}

static int
grub_lua_gettext (lua_State *state)
{
  const char *translation;
  translation = luaL_checkstring (state, 1);
  lua_pushstring (state, grub_gettext (translation));
  return 1;
}

static int
grub_lua_random (lua_State *state)
{
  uint16_t r = grub_get_time_ms ();
  uint16_t m;
  m = luaL_checkinteger (state, 1);
  r = ((r * 7621) + 1) % 32768;
  lua_pushinteger (state, r % m);
  return 1;
}

/* Helper for grub_lua_enum_block.  */
static int
grub_lua_enum_block_iter (unsigned long long offset,
                          unsigned long long length,
                          unsigned long long start, void *data)
{
  lua_State *state = data;
  int result;
  char str[255];

  lua_pushvalue (state, 1);
  grub_snprintf (str, 255, "%llu+%llu",
                 (offset >> GRUB_DISK_SECTOR_BITS) + start,
                 length >> GRUB_DISK_SECTOR_BITS);
  lua_pushstring (state, str);

  lua_call (state, 1, 1);
  result = lua_tointeger (state, -1);
  lua_pop (state, 1);

  return result;
}

static int
grub_lua_enum_block (lua_State *state)
{
  int num, i;
  const char *name;
  grub_file_t file = 0;
  struct grub_fs_block *p;
  grub_disk_addr_t start = 0;
  luaL_checktype (state, 1, LUA_TFUNCTION);
  name = luaL_checkstring (state, 2);
  file = grub_file_open (name, GRUB_FILE_TYPE_PRINT_BLOCKLIST
                              | GRUB_FILE_TYPE_NO_DECOMPRESS);
  if (!file)
    return 0;
  if (! file->device->disk)
    return 0;

  num = grub_blocklist_convert (file);
  start = (lua_gettop (state) > 2) ?
            grub_partition_get_start (file->device->disk->partition) : 0;
  p = file->data;
  for (i = 0; i < num; i++)
  {
    grub_lua_enum_block_iter (p->offset, p->length, start, state);
    p++;
  }
  grub_file_close (file);
  return push_result (state);
}

static int
grub_lua_cls (lua_State *state __attribute__ ((unused)))
{
  grub_cls ();
  return 0;
}

static int
grub_lua_setcolorstate (lua_State *state)
{
  grub_setcolorstate (luaL_checkinteger (state, 1));
  return 0;
}

static int
grub_lua_refresh (lua_State *state __attribute__ ((unused)))
{
  grub_refresh ();
  return 0;
}

static int
grub_lua_getmem (lua_State *state __attribute__ ((unused)))
{
  const char *str;
  grub_uint64_t total_mem = grub_get_total_mem_size ();
  str = grub_get_human_size (total_mem, GRUB_HUMAN_SIZE_SHORT);
  lua_pushstring (state, str);
  return 1;
}

luaL_Reg grub_lua_lib[] =
{
  {"run", grub_lua_run},
  {"script", grub_lua_script},
  {"getenv", grub_lua_getenv},
  {"setenv", grub_lua_setenv},
  {"exportenv", grub_lua_exportenv},
  {"enum_device", grub_lua_enum_device},
  {"enum_file", grub_lua_enum_file},

  {"file_open", grub_lua_file_open},
  {"file_close", grub_lua_file_close},
  {"file_seek", grub_lua_file_seek},
  {"file_read", grub_lua_file_read},
  {"file_write", grub_lua_file_write},
  {"file_getline", grub_lua_file_getline},
  {"file_getsize", grub_lua_file_getsize},
  {"file_getpos", grub_lua_file_getpos},
  {"file_eof", grub_lua_file_eof},
  {"file_exist", grub_lua_file_exist},

  {"hexdump", grub_lua_hexdump},
  {"add_menu", grub_lua_add_menu},
  {"add_icon_menu", grub_lua_add_icon_menu},
  {"add_hidden_menu", grub_lua_add_hidden_menu},
  {"clear_menu", grub_lua_clear_menu},

  {"gettext", grub_lua_gettext},
  {"random", grub_lua_random},
  {"enum_block", grub_lua_enum_block},

  {"cls", grub_lua_cls},
  {"setcolorstate", grub_lua_setcolorstate},
  {"refresh", grub_lua_refresh},

  {"getmem", grub_lua_getmem},
  {0, 0}
};
