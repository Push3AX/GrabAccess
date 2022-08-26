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

#include <grub/dl.h>
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/normal.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/disk.h>
#include <grub/file.h>
#include <grub/partition.h>
#include <grub/datetime.h>
#include <grub/lua.h>

#include "ff.h"
#include "diskio.h"

GRUB_MOD_LICENSE ("GPLv3+");

static inline int
label_isdigit (int c)
{
  return (c >= '1' && c <= '9');
}

static grub_err_t
grub_cmd_mount (grub_command_t cmd __attribute__ ((unused)),
                int argc, char **args)

{
  unsigned int num = 0;
  int namelen;
  grub_disk_t disk = 0;
  if (argc == 1 && grub_strcmp (args[0], "status") == 0)
  {
    int i;
    for (i = 1; i < 10; i++)
    {
      if (!fat_stat[i].disk)
        continue;
      if (!fat_stat[i].disk->partition)
        grub_printf ("%s -> %s:\n", fat_stat[i].disk->name, fat_stat[i].name);
      else
        grub_printf ("%s,%d -> %s:\n", fat_stat[i].disk->name,
                     fat_stat[i].disk->partition->number + 1,
                     fat_stat[i].name);
    }
    return GRUB_ERR_NONE;
  }
  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "bad argument");
  num = grub_strtoul (args[1], NULL, 10);
  if (num > 9 || num == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid number");
  namelen = grub_strlen (args[0]);
  if ((args[0][0] == '(') && (args[0][namelen - 1] == ')'))
  {
    args[0][namelen - 1] = 0;
    disk = grub_disk_open (&args[0][1]);
  }
  else
    disk = grub_disk_open (args[0]);
  if (!disk)
    return grub_errno;

  if (fat_stat[num].disk)
  {
    grub_disk_close (disk);
    return grub_error (GRUB_ERR_BAD_DEVICE, "disk number in use");
  }
  fat_stat[num].present = 1;
  grub_snprintf (fat_stat[num].name, 2, "%u", num);
  fat_stat[num].disk = disk;
  fat_stat[num].total_sectors = disk->total_sectors;

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_umount (grub_command_t cmd __attribute__ ((unused)),
                int argc, char **args)

{
  unsigned int num = 0;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "bad argument");
  num = grub_strtoul (args[0], NULL, 10);
  if (num > 9 || num == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid number");

  if (fat_stat[num].disk)
    grub_disk_close (fat_stat[num].disk);
  fat_stat[num].disk = 0;
  fat_stat[num].present = 0;
  fat_stat[num].total_sectors = 0;

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_mkdir (grub_command_t cmd __attribute__ ((unused)),
                int argc, char **args)

{
  char dev[3] = "1:";
  FATFS fs;
  FRESULT res;
  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "bad argument");
  if (label_isdigit (args[0][0]))
    dev[0] = args[0][0];

  f_mount (&fs, dev, 0);
  res = f_mkdir (args[0]);
  if (res)
    return grub_error (GRUB_ERR_WRITE_ERROR, "mkdir failed %d", res);
  f_mount(0, dev, 0);
  return GRUB_ERR_NONE;
}

static FRESULT
copy_file (char *in_name, char *out_name)
{
  FRESULT res;
  BYTE buffer[4096];
  UINT br, bw;
  FIL in, out;
  res = f_open (&in, in_name, FA_READ);
  if (res)
  {
    grub_error (GRUB_ERR_BAD_FILENAME, "src open failed %d", res);
    goto fail;
  }
  res = f_open (&out, out_name, FA_WRITE | FA_CREATE_ALWAYS);
  if (res)
  {
    grub_error (GRUB_ERR_BAD_FILENAME, "dst open failed %d", res);
    goto fail;
  }

  for (;;)
  {
    res = f_read (&in, buffer, sizeof (buffer), &br);
    if (res || br == 0)
      break; /* error or eof */
    res = f_write (&out, buffer, br, &bw);
    if (res || bw < br)
      break; /* error or disk full */
  }
  f_close(&in);
  f_close(&out);
fail:
  return res;
}

static FRESULT
copy_grub_file (char *in_name, char *out_name)
{
  FRESULT res;
  BYTE buffer[4096];
  UINT br, bw;
  FIL out;
  grub_file_t file = 0;
  file = grub_file_open (in_name, GRUB_FILE_TYPE_HEXCAT
                         | GRUB_FILE_TYPE_NO_DECOMPRESS);
  if (!file)
  {
    grub_error (GRUB_ERR_BAD_FILENAME, "src open failed");
    return FR_NO_FILE;
  }
  res = f_open (&out, out_name, FA_WRITE | FA_CREATE_ALWAYS);
  if (res)
  {
    grub_error (GRUB_ERR_BAD_FILENAME, "dst open failed %d", res);
    return res;
  }
  br = sizeof (buffer);
  for (;;)
  {
    if (file->offset >= file->size)
      break; /* eof */
    if (file->offset + br > file->size)
      br = file->size - file->offset;
    grub_file_read (file, buffer, br);
    res = f_write (&out, buffer, br, &bw);
    if (res || bw < br)
      break; /* error or disk full */
  }
  grub_file_close (file);
  f_close(&out);

  return res;
}

static grub_err_t
grub_cmd_cp (grub_command_t cmd __attribute__ ((unused)),
             int argc, char **args)

{
  char in_dev[3] = "1:";
  char out_dev[3] = "1:";
  FATFS in_fs, out_fs;
  FRESULT res;

  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "bad argument");

  if (label_isdigit (args[0][0]))
    in_dev[0] = args[0][0];
  if (label_isdigit (args[1][0]))
    out_dev[0] = args[1][0];

  if (label_isdigit (args[0][0]))
    f_mount (&in_fs, in_dev, 0);
  f_mount (&out_fs, out_dev, 0);

  if (label_isdigit (args[0][0]))
    res = copy_file (args[0], args[1]);
  else
    res = copy_grub_file (args[0], args[1]);

  if (label_isdigit (args[0][0]))
    f_mount(0, in_dev, 0);
  f_mount(0, out_dev, 0);
  if (res)
    return grub_error (GRUB_ERR_WRITE_ERROR, "copy failed %d", res);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_rename (grub_command_t cmd __attribute__ ((unused)),
                 int argc, char **args)

{
  char dev[3] = "1:";
  FATFS fs;
  FRESULT res;
  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "bad argument");
  if (label_isdigit (args[0][0]))
    dev[0] = args[0][0];
  if (label_isdigit (args[1][0]) && args[1][0] != dev[0])
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "dst drive error");

  f_mount (&fs, dev, 0);
  res = f_rename (args[0], args[1]);
  if (res)
    return grub_error (GRUB_ERR_WRITE_ERROR, "rename failed %d", res);
  f_mount(0, dev, 0);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_rm (grub_command_t cmd __attribute__ ((unused)),
             int argc, char **args)

{
  char dev[3] = "1:";
  FATFS fs;
  FRESULT res;
  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "bad argument");
  if (label_isdigit (args[0][0]))
    dev[0] = args[0][0];

  f_mount (&fs, dev, 0);
  res = f_unlink (args[0]);
  if (res)
    return grub_error (GRUB_ERR_WRITE_ERROR, "unlink failed %d", res);
  f_mount(0, dev, 0);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_mv (grub_command_t cmd __attribute__ ((unused)),
             int argc, char **args)

{
  char in_dev[3] = "1:";
  char out_dev[3] = "1:";
  FATFS in_fs, out_fs;
  FRESULT res;

  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "bad argument");

  if (label_isdigit (args[0][0]))
    in_dev[0] = args[0][0];
  if (label_isdigit (args[1][0]))
    out_dev[0] = args[1][0];

  f_mount (&in_fs, in_dev, 0);
  if (in_dev[0] == out_dev[0])
  {
    /* rename */
    res = f_rename (args[0], args[1]);
    if (res)
      return grub_error (GRUB_ERR_WRITE_ERROR, "mv failed %d", res);
    f_mount(0, in_dev, 0);
    return GRUB_ERR_NONE;
  }

  f_mount (&out_fs, out_dev, 0);
  res = copy_file (args[0], args[1]);
  if (res)
    grub_error (GRUB_ERR_WRITE_ERROR, "copy failed %d", res);
  else
    res = f_unlink (args[0]);

  f_mount(0, in_dev, 0);
  f_mount(0, out_dev, 0);
  if (res)
    return grub_error (GRUB_ERR_WRITE_ERROR, "rm failed %d", res);
  return GRUB_ERR_NONE;
}

static FRESULT
set_timestamp (char *name, struct grub_datetime *tm)
{
    FILINFO info;
    info.fdate = (WORD)(((tm->year - 1980) * 512U) | tm->month * 32U | tm->day);
    info.ftime = (WORD)(tm->hour * 2048U | tm->minute * 32U | tm->second / 2U);
    return f_utime(name, &info);
}

static grub_err_t
grub_cmd_touch (grub_command_t cmd __attribute__ ((unused)),
                int argc, char **args)

{
  char dev[3] = "1:";
  struct grub_datetime tm = { 2020, 1, 1, 0, 0, 0};
  FATFS fs;
  FRESULT res;
  FILINFO info;
  FIL file;
  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "bad argument");

  grub_get_datetime (&tm);
  if (argc > 1)
    tm.year = grub_strtol (args[1], NULL, 10);
  if (argc > 2)
    tm.month = grub_strtol (args[2], NULL, 10);
  if (argc > 3)
    tm.day = grub_strtol (args[3], NULL, 10);
  if (argc > 4)
    tm.hour = grub_strtol (args[4], NULL, 10);
  if (argc > 5)
    tm.minute = grub_strtol (args[5], NULL, 10);
  if (argc > 6)
    tm.second = grub_strtol (args[6], NULL, 10);

  if (label_isdigit (args[0][0]))
    dev[0] = args[0][0];

  f_mount (&fs, dev, 0);
  res = f_stat (args[0], &info);
  switch (res)
  {
    case FR_OK:
      res = set_timestamp (args[0], &tm);
      break;
    case FR_NO_FILE:
      res = f_open (&file, args[0], FA_WRITE | FA_CREATE_ALWAYS);
      if (res)
        grub_error (GRUB_ERR_WRITE_ERROR, "file create failed %d", res);
      else
      {
        f_close(&file);
        res = set_timestamp (args[0], &tm);
      }
      break;
    default:
      grub_error (GRUB_ERR_BAD_FILENAME, "stat failed %d", res);
  }
  f_mount(0, dev, 0);
  if (res)
    return grub_error (GRUB_ERR_WRITE_ERROR, "utime failed %d", res);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_write_file (grub_command_t cmd __attribute__ ((unused)),
                     int argc, char **args)

{
  char dev[3] = "1:";
  FATFS fs;
  FRESULT res;
  FIL file;
  FSIZE_t offset = 0;
  UINT bw;
  if (argc < 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "bad argument");
  if (argc == 3)
    offset = grub_strtoul (args[2], NULL, 0);
  bw = grub_strlen (args[1]);

  if (label_isdigit (args[0][0]))
    dev[0] = args[0][0];

  f_mount (&fs, dev, 0);
  res = f_open (&file, args[0], FA_WRITE | FA_OPEN_EXISTING);
  if (res)
    return grub_error (GRUB_ERR_WRITE_ERROR, "file open failed %d", res);
  res = f_lseek (&file, offset);
  if (!res)
    res = f_write (&file, args[1], bw, &bw);

  f_close(&file);
  f_mount(0, dev, 0);
  if (res)
    return grub_error (GRUB_ERR_WRITE_ERROR, "write failed %d", res);
  return GRUB_ERR_NONE;
}


static grub_command_t cmd_mount, cmd_umount, cmd_mkdir;
static grub_command_t cmd_cp, cmd_rename, cmd_rm;
static grub_command_t cmd_mv, cmd_touch, cmd_write;

static FATFS fatfs_list[10];

/* fat.mount hdx,y disknum */
static int
fat_mount (lua_State *state)
{
  int num = 0;
  char dev[3] = "1:";
  grub_disk_t disk = 0;
  const char *name = NULL;

  name = luaL_checkstring (state, 1);
  num = luaL_checkinteger (state, 2);
  if (num > 9 || num <= 0)
    return 0;
  disk = grub_disk_open (name);
  if (!disk)
    return 0;
  if (fat_stat[num].disk)
  {
    grub_disk_close (disk);
    grub_printf ("disk number in use\n");
    return 0;
  }
  fat_stat[num].present = 1;
  grub_snprintf (fat_stat[num].name, 2, "%d", num);
  fat_stat[num].disk = disk;
  fat_stat[num].total_sectors = disk->total_sectors;
  /* f_mount */
  grub_snprintf (dev, 3, "%d:", num);
  f_mount (&fatfs_list[num], dev, 0);
  return 0;
}

/* fat.umount disknum */
static int
fat_umount (lua_State *state)
{
  int num = 0;
  char dev[3] = "1:";
  num = luaL_checkinteger (state, 1);
  if (num > 9 || num <= 0)
    return 0;
  if (fat_stat[num].disk)
    grub_disk_close (fat_stat[num].disk);
  fat_stat[num].disk = 0;
  fat_stat[num].present = 0;
  fat_stat[num].total_sectors = 0;
  /* f_mount */
  grub_snprintf (dev, 3, "%d:", num);
  f_mount(0, dev, 0);
  grub_memset (&fatfs_list[num], 0, sizeof (FATFS));
  return 0;
}

/* fat.disk_status disknum @ret: grub_disk_t */
static int
fat_disk_status (lua_State *state)
{
  int num = 0;
  num = luaL_checkinteger (state, 1);
  if (num > 9 || num <= 0)
    return 0;
  if (!fat_stat[num].disk)
    return 0;
  lua_pushlightuserdata (state, fat_stat[num].disk);
  return 1;
}

/* fat.get_label disknum @ret:label */
static int
fat_get_label (lua_State *state)
{
  char label[35];
  int num = 0;
  char dev[3] = "1:";
  num = luaL_checkinteger (state, 1);
  if (num > 9 || num <= 0)
    return 0;
  grub_snprintf (dev, 3, "%d:", num);
  f_getlabel(dev, label, 0);
  lua_pushstring (state, label);
  return 1;
}

/* fat.set_label disknum label */
static int
fat_set_label (lua_State *state)
{
  const char *label;
  int num = 0;
  char dev[40] = "1:";
  num = luaL_checkinteger (state, 1);
  if (num > 9 || num <= 0)
    return 0;
  label = luaL_checkstring (state, 2);
  if (grub_strlen (label) > 34)
    return 0;
  grub_snprintf (dev, 40, "%d:%s", num, label);
  f_setlabel(dev);
  return 0;
}

/* fat.mkdir path */
static int
fat_mkdir (lua_State *state)
{
  const char *path;
  path = luaL_checkstring (state, 1);
  f_mkdir (path);
  return 0;
}

/* fat.rename path1 path2 */
static int
fat_rename (lua_State *state)
{
  const char *path1, *path2;
  path1 = luaL_checkstring (state, 1);
  path2 = luaL_checkstring (state, 2);
  f_rename (path1, path2);
  return 0;
}

/* fat.unlink path */
static int
fat_unlink (lua_State *state)
{
  const char *path;
  path = luaL_checkstring (state, 1);
  f_unlink (path);
  return 0;
}

/* fat.open path mode @ret:FIL */
static int
fat_open (lua_State *state)
{
  FIL *file = NULL;
  const char *name;
  BYTE flag = 0;

  file = grub_malloc (sizeof (FIL));
  if (! file)
    return 0;
  name = luaL_checkstring (state, 1);
  flag = (lua_gettop (state) > 1) ? luaL_checkinteger (state, 2) : 0;
  f_open (file, name, flag);
  lua_pushlightuserdata (state, file);
  return 1;
}

/* fat.close FIL */
static int
fat_close (lua_State *state)
{
  FIL *file = NULL;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);
  f_close (file);
  grub_free (file);
  return 0;
}

/* fat.read FIL size @ret:buf */
static int
fat_read (lua_State *state)
{
  FIL *file = NULL;
  luaL_Buffer b;
  int n;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);
  n = luaL_checkinteger (state, 2);
  luaL_buffinit (state, &b);
  while (n)
  {
    char *p;
    UINT nr;

    nr = (n > LUAL_BUFFERSIZE) ? LUAL_BUFFERSIZE : n;
    p = luaL_prepbuffer (&b);

    f_read (file, p, nr, &nr);
    if (nr == 0)
      break;

    luaL_addsize (&b, nr);
    n -= nr;
  }
  luaL_pushresult (&b);
  return 1;
}

/* fat.write FIL buf @ret:size */
static int
fat_write (lua_State *state)
{
  FIL *file;
  UINT ret;
  grub_size_t len;
  const char *buf;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);
  buf = lua_tolstring (state, 2, &len);
  f_write (file, buf, len, &ret);

  lua_pushinteger (state, ret);
  return 1;
}

/* fat.lseek FIL ofs */
static int
fat_lseek (lua_State *state)
{
  FIL *file;
  int ofs;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);
  ofs = luaL_checkinteger (state, 2);
  f_lseek (file, ofs);
  return 0;
}

/* fat.tell FIL @ret:ofs */
static int
fat_tell (lua_State *state)
{
  FIL *file;
  int ret;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);
  ret = f_tell (file);
  lua_pushinteger (state, ret);
  return 1;
}

/* fat.eof FIL @ret:num */
static int
fat_eof (lua_State *state)
{
  FIL *file;
  int ret;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);
  ret = f_eof (file);
  lua_pushinteger (state, ret);
  return 1;
}

/* fat.size FIL @ret:size */
static int
fat_size (lua_State *state)
{
  FIL *file;
  int ret;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);
  ret = f_size (file);
  lua_pushinteger (state, ret);
  return 1;
}

/* fat.truncate FIL */
static int
fat_truncate (lua_State *state)
{
  FIL *file;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  file = lua_touserdata (state, 1);
  f_truncate (file);
  return 0;
}

static luaL_Reg fatlib[] =
{
  {"mount", fat_mount},
  {"umount", fat_umount},
  {"disk_status", fat_disk_status},
  {"get_label", fat_get_label},
  {"set_label", fat_set_label},
  {"mkdir", fat_mkdir},
  {"rename", fat_rename},
  {"unlink", fat_unlink},
  {"open", fat_open},
  {"close", fat_close},
  {"read", fat_read},
  {"write", fat_write},
  {"lseek", fat_lseek},
  {"tell", fat_tell},
  {"eof", fat_eof},
  {"size", fat_size},
  {"truncate", fat_truncate},
  {0, 0}
};

GRUB_MOD_INIT(fatfs)
{
  cmd_mount = grub_register_command ("mount", grub_cmd_mount,
                                      N_("status | DISK NUM[1-9]"),
                                      N_("Mount FAT partition."));
  cmd_umount = grub_register_command ("umount", grub_cmd_umount,
                                      N_("NUM[1-9]"),
                                      N_("Unmount FAT partition."));
  cmd_mkdir = grub_register_command ("mkdir", grub_cmd_mkdir,
                                      N_("PATH"),
                                      N_("Create new directory."));
  cmd_cp = grub_register_command ("cp", grub_cmd_cp,
                                      N_("FILE1 FILE2"),
                                      N_("Copy file."));
  cmd_rename = grub_register_command ("rename", grub_cmd_rename,
                        N_("FILE FILE_NAME"),
                        N_("Rename file/directory or move to other directory"));
  cmd_rm = grub_register_command ("rm", grub_cmd_rm,
                        N_("FILE | DIR"),
                        N_("Remove a file or empty directory."));
  cmd_mv = grub_register_command ("mv", grub_cmd_mv,
                        N_("FILE1 FILE2"),
                        N_("Move or rename file."));
  cmd_touch = grub_register_command ("touch", grub_cmd_touch,
                        N_("FILE [YEAR MONTH DAY HOUR MINUTE SECOND]"),
                        N_("Change the timestamp of a file or directory."));
  cmd_write = grub_register_command ("write_file", grub_cmd_write_file,
                        N_("FILE STRING [OFFSET]"),
                        N_("Write strings to file."));
  if (grub_lua_global_state)
  {
    lua_gc (grub_lua_global_state, LUA_GCSTOP, 0);
    luaL_register (grub_lua_global_state, "fat", fatlib);
    lua_gc (grub_lua_global_state, LUA_GCRESTART, 0);
  }
}

GRUB_MOD_FINI(fatfs)
{
  grub_unregister_command (cmd_mount);
  grub_unregister_command (cmd_umount);
  grub_unregister_command (cmd_mkdir);
  grub_unregister_command (cmd_cp);
  grub_unregister_command (cmd_rename);
  grub_unregister_command (cmd_rm);
  grub_unregister_command (cmd_mv);
  grub_unregister_command (cmd_touch);
  grub_unregister_command (cmd_write);
}
