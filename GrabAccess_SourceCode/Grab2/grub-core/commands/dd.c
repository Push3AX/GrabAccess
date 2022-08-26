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
/*
 *  BURG - Brand-new Universal loadeR from GRUB
 *  Copyright 2009 Bean Lee - All Rights Reserved
 *
 *  BURG is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/partition.h>
#include <grub/msdos_partition.h>
#include <grub/gpt_partition.h>
#include <grub/fs.h>
#include <grub/extcmd.h>
#include <grub/normal.h>
#include <grub/i18n.h>
#include <grub/lua.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
{
  {"if", 'i', 0, N_("Specify input file."), "FILE", ARG_TYPE_STRING},
  {"str", 's', 0, N_("Specify input string."), "STRING", ARG_TYPE_STRING},
  {"hex", 'x', 0, N_("Specify input hex string."), "HEX", ARG_TYPE_STRING},
  {"of", 'o', 0, N_("Specify output file."), "FILE", ARG_TYPE_STRING},
  {"bs", 'b', 0, N_("Specify block size (1~4096)."), "BYTES", ARG_TYPE_INT},
  {"count", 'c', 0, N_("Number of blocks to copy."), "BLOCKS", ARG_TYPE_INT},
  {"skip", 'k', 0, N_("Skip N bytes at input."), "BYTES", ARG_TYPE_INT},
  {"seek", 'e', 0, N_("Skip N bytes at output."), "BYTES", ARG_TYPE_INT},
  {0, 0, 0, 0, 0, 0}
};

enum options
{
  DD_IF,
  DD_STR,
  DD_HEX,
  DD_OF,
  DD_BS,
  DD_COUNT,
  DD_SKIP,
  DD_SEEK,
};

static grub_err_t
grub_cmd_dd (grub_extcmd_context_t ctxt, int argc __attribute__ ((unused)),
             char **args __attribute__ ((unused)))
{
  struct grub_arg_list *state = ctxt->state;
  grub_uint8_t data[4096];
  /* input */
  char *str = NULL;
  char *hexstr = NULL;
  grub_file_t in_file = NULL;
  grub_off_t in_size = 0;
  /* output */
  grub_file_t out_file = NULL;
  grub_off_t out_size = 0;
  /* block size */
  grub_uint32_t bs = 1;
  /* skip & seek */
  grub_off_t skip = 0;
  grub_off_t seek = 0;
  /* count */
  grub_off_t count = 0;

  if (state[DD_IF].set)
  {
    in_file = grub_file_open (state[DD_IF].arg,
                              GRUB_FILE_TYPE_HEXCAT |
                              GRUB_FILE_TYPE_NO_DECOMPRESS);
    if (! in_file)
      return grub_error (GRUB_ERR_BAD_FILENAME, N_("failed to open %s"),
                         state[DD_IF].arg);
    in_size = grub_file_size (in_file);
  }

  if (state[DD_STR].set)
  {
    str = state[DD_STR].arg;
    in_size = grub_strlen (str);
  }

  if (state[DD_HEX].set)
  {
    int i, size;
    str = state[DD_HEX].arg;
    size = grub_strlen (str) / 2;
    if (size == 0)
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid hex string");

    hexstr = grub_zalloc (size);
    if (! hexstr)
      return grub_errno;

    for (i = 0; i < size * 2; i++)
    {
      int c;
      if ((str[i] >= '0') && (str[i] <= '9'))
        c = str[i] - '0';
      else if ((str[i] >= 'A') && (str[i] <= 'F'))
        c = str[i] - 'A' + 10;
      else if ((str[i] >= 'a') && (str[i] <= 'f'))
        c = str[i] - 'a' + 10;
      else
      {
        grub_free (hexstr);
        return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid hex string");
      }
      if ((i & 1) == 0)
        c <<= 4;
      hexstr[i >> 1] |= c;
    }
    str = hexstr;
    in_size = size;
  }

  if (state[DD_OF].set)
  {
    out_file = grub_file_open (state[DD_OF].arg,
                               GRUB_FILE_TYPE_HEXCAT |
                               GRUB_FILE_TYPE_NO_DECOMPRESS);
    if (! out_file)
      return grub_error (GRUB_ERR_BAD_FILENAME, N_("failed to open %s"),
                         state[DD_OF].arg);
    out_size = grub_file_size (out_file);
    grub_blocklist_convert (out_file);
  }

  if (((! in_file) && (! str)) || (! out_file))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "no input or output file");

  if (state[DD_BS].set)
  {
    bs = grub_strtoul (state[DD_BS].arg, 0, 0);
    if (! bs || bs > 4096)
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid block size");
  }

  if (state[DD_COUNT].set)
  {
    count = grub_strtoull (state[DD_COUNT].arg, 0, 0);
    if (! count)
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid count");
  }

  if (state[DD_SKIP].set)
    skip = grub_strtoull (state[DD_SKIP].arg, 0, 0);

  if (state[DD_SEEK].set)
    seek = grub_strtoull (state[DD_SEEK].arg, 0, 0);

  count *= bs;

  if ((skip >= in_size) || (seek >= out_size))
  {
    grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid skip/seek");
    goto fail;
  }

  if (!count)
    count = in_size - skip;

  if (skip + count > in_size)
  {
    grub_printf ("WARNING: skip + count > input_size\n");
    count = in_size - skip;
  }

  if (seek + count > out_size)
  {
    grub_printf ("WARNING: seek + count > output_size\n");
    count = out_size - seek;
  }

  while (count)
  {
    grub_uint32_t copy_bs;
    copy_bs = (bs > count) ? count : bs;
    /* read */
    if (in_file)
    {
      grub_file_seek (in_file, skip);
      grub_file_read (in_file, data, copy_bs);
      if (grub_errno)
        break;
    }
    else
    {
      grub_memcpy (data, str + skip, copy_bs);
    }
    /* write */
    grub_file_seek (out_file, seek);
    grub_blocklist_write (out_file, (char *)data, copy_bs);
    if (grub_errno)
      break;

    skip += copy_bs;
    seek += copy_bs;
    count -= copy_bs;
  }

fail:
  if (hexstr)
    grub_free (hexstr);
  if (in_file)
    grub_file_close (in_file);
  if (out_file)
    grub_file_close (out_file);
  return grub_errno;
}

static grub_extcmd_t cmd;

static int
lua_disk_open (lua_State *state)
{
  grub_disk_t disk = 0;
  const char *name;
  char *str = NULL;

  name = luaL_checkstring (state, 1);
  str = grub_strdup (name);
  if (!str)
    return 0;
  if (str[0] == '(')
  {
    str[grub_strlen (str) - 1] = 0;
    disk = grub_disk_open (&str[1]);
  }
  else
    disk = grub_disk_open (str);
  save_errno (state);
  grub_free (str);
  if (!disk)
    return 0;
  lua_pushlightuserdata (state, disk);
  return 1;
}

static int
lua_disk_close (lua_State *state)
{
  grub_disk_t disk;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  disk = lua_touserdata (state, 1);
  grub_disk_close (disk);

  return push_result (state);
}

/* disk, sector, offset, len */
static int
lua_disk_read (lua_State *state)
{
  grub_disk_t disk;
  char *b = NULL;
  int len, sec, ofs;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  disk = lua_touserdata (state, 1);
  sec = luaL_checkinteger (state, 2);
  ofs = luaL_checkinteger (state, 3);
  len = luaL_checkinteger (state, 4);
  b = grub_malloc (len);
  if (! b)
    return 0;
  grub_disk_read (disk, sec, ofs, len, b);
  save_errno (state);
  lua_pushstring (state, b);
  grub_free (b);
  return 1;
}

/* disk, sector, offset, size, buf */
static int
lua_disk_write (lua_State *state)
{
  grub_disk_t disk;
  const char *buf = NULL;
  int len, sec, ofs;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  disk = lua_touserdata (state, 1);
  sec = luaL_checkinteger (state, 2);
  ofs = luaL_checkinteger (state, 3);
  len = luaL_checkinteger (state, 4);
  buf = luaL_checkstring (state, 5);
  grub_disk_write (disk, sec, ofs, len, buf);
  save_errno (state);
  return 0;
}

static int
lua_disk_partmap (lua_State *state)
{
  grub_disk_t disk;
  const char *buf = "none";
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  disk = lua_touserdata (state, 1);
  if (disk->partition && disk->partition->partmap)
    buf = disk->partition->partmap->name;
  lua_pushstring (state, buf);
  return 1;
}

static int
lua_disk_driver (lua_State *state)
{
  grub_disk_t disk;
  const char *buf = "none";
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  disk = lua_touserdata (state, 1);
  if (disk->dev)
    buf = disk->dev->name;
  lua_pushstring (state, buf);
  return 1;
}

static int
lua_disk_fs (lua_State *state)
{
  grub_disk_t disk;
  struct grub_device dev = {NULL, NULL};
  grub_fs_t fs;
  const char *buf = "none";
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  disk = lua_touserdata (state, 1);
  dev.disk = disk;
  fs = grub_fs_probe (&dev);
  if (fs)
    buf = fs->name;
  lua_pushstring (state, buf);
  return 1;
}

static int
lua_disk_fsuuid (lua_State *state)
{
  grub_disk_t disk;
  struct grub_device dev = {NULL, NULL};
  grub_fs_t fs;
  char *buf = NULL;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  disk = lua_touserdata (state, 1);
  dev.disk = disk;
  fs = grub_fs_probe (&dev);
  if (fs && fs->fs_uuid)
    fs->fs_uuid (&dev, &buf);
  if (buf)
  {
    lua_pushstring (state, buf);
    grub_free (buf);
  }
  else
    lua_pushstring (state, "");
  return 1;
}

static int
lua_disk_label (lua_State *state)
{
  grub_disk_t disk;
  struct grub_device dev = {NULL, NULL};
  grub_fs_t fs;
  char *buf = NULL;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  disk = lua_touserdata (state, 1);
  dev.disk = disk;
  fs = grub_fs_probe (&dev);
  if (fs && fs->fs_label)
    fs->fs_label (&dev, &buf);
  if (buf)
  {
    lua_pushstring (state, buf);
    grub_free (buf);
  }
  else
    lua_pushstring (state, "");
  return 1;
}

static int
lua_disk_size (lua_State *state)
{
  grub_disk_t disk;
  char buf[32];
  unsigned long long size;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  disk = lua_touserdata (state, 1);
  size = grub_disk_native_sectors (disk) << GRUB_DISK_SECTOR_BITS;
  if (lua_gettop (state) > 1)
    grub_snprintf (buf, 32, "%s", grub_get_human_size (size, GRUB_HUMAN_SIZE_SHORT));
  else
    grub_snprintf (buf, 32, "%llu", size);
  lua_pushstring (state, buf);
  return 1;
}

static int
lua_disk_bootable (lua_State *state)
{
  grub_disk_t disk;
  int boot = 0;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  disk = lua_touserdata (state, 1);
  if (disk->partition &&
      disk->partition->msdostype != GRUB_PC_PARTITION_TYPE_GPT_DISK &&
      grub_strcmp (disk->partition->partmap->name, "msdos") == 0)
  {
    if (disk->partition->flag & 0x80)
      boot = 1;
  }
  else if (disk->partition &&
           grub_strcmp (disk->partition->partmap->name, "gpt") == 0)
  {
    grub_packed_guid_t EFI_GUID = GRUB_GPT_PARTITION_TYPE_EFI_SYSTEM;
    if (grub_memcmp (&disk->partition->gpttype,
                     &EFI_GUID, sizeof (grub_packed_guid_t)) == 0)
      boot = 1;
  }
  lua_pushboolean (state, boot);
  return 1;
}

static luaL_Reg disklib[] =
{
  {"open", lua_disk_open},
  {"close", lua_disk_close},
  {"read", lua_disk_read},
  {"write", lua_disk_write},
  {"partmap", lua_disk_partmap},
  {"driver", lua_disk_driver},
  {"fs", lua_disk_fs},
  {"fsuuid", lua_disk_fsuuid},
  {"label", lua_disk_label},
  {"size", lua_disk_size},
  {"bootable", lua_disk_bootable},
  {0, 0}
};

GRUB_MOD_INIT(dd)
{
  cmd = grub_register_extcmd ("dd", grub_cmd_dd, 0,
                              N_("[OPTIONS]"), N_("Copy data."), options);
  if (grub_lua_global_state)
  {
    lua_gc (grub_lua_global_state, LUA_GCSTOP, 0);
    luaL_register (grub_lua_global_state, "disk", disklib);
    lua_gc (grub_lua_global_state, LUA_GCRESTART, 0);
  }
}

GRUB_MOD_FINI(dd)
{
  grub_unregister_extcmd (cmd);
}
