/* memrw.c - command to read / write physical memory  */
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
#include <grub/misc.h>
#include <grub/extcmd.h>
#include <grub/env.h>
#include <grub/i18n.h>
#include <grub/lua.h>
#include <grub/file.h>
#ifdef GRUB_MACHINE_EFI
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#else
#include <grub/relocator.h>
#endif

GRUB_MOD_LICENSE ("GPLv3+");

static grub_extcmd_t cmd_read_byte, cmd_read_word, cmd_read_dword;
static grub_command_t cmd_write_byte, cmd_write_word, cmd_write_dword;
static grub_command_t cmd_write_bytes;

static const struct grub_arg_option options[] =
  {
    {0, 'v', 0, N_("Save read value into variable VARNAME."),
     N_("VARNAME"), ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
  };


static grub_err_t
grub_cmd_read (grub_extcmd_context_t ctxt, int argc, char **argv)
{
  grub_addr_t addr;
  grub_uint32_t value = 0;
  char buf[sizeof ("XXXXXXXX")];

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));

  addr = grub_strtoul (argv[0], 0, 0);
  switch (ctxt->extcmd->cmd->name[sizeof ("read_") - 1])
    {
    case 'd':
      value = *((volatile grub_uint32_t *) addr);
      break;

    case 'w':
      value = *((volatile grub_uint16_t *) addr);
      break;

    case 'b':
      value = *((volatile grub_uint8_t *) addr);
      break;
    }

  if (ctxt->state[0].set)
    {
      grub_snprintf (buf, sizeof (buf), "%x", value);
      grub_env_set (ctxt->state[0].arg, buf);
    }
  else
    grub_printf ("0x%x\n", value);

  return 0;
}

static grub_err_t
grub_cmd_write (grub_command_t cmd, int argc, char **argv)
{
  grub_addr_t addr;
  grub_uint32_t value;
  grub_uint32_t mask = 0xffffffff;

  if (argc != 2 && argc != 3)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("two arguments expected"));

  addr = grub_strtoul (argv[0], 0, 0);
  value = grub_strtoul (argv[1], 0, 0);
  if (argc == 3)
    mask = grub_strtoul (argv[2], 0, 0);
  value &= mask;
  switch (cmd->name[sizeof ("write_") - 1])
    {
    case 'd':
      if (mask != 0xffffffff)
	*((volatile grub_uint32_t *) addr)
	  = (*((volatile grub_uint32_t *) addr) & ~mask) | value;
      else
	*((volatile grub_uint32_t *) addr) = value;
      break;

    case 'w':
      if ((mask & 0xffff) != 0xffff)
	*((volatile grub_uint16_t *) addr)
	  = (*((volatile grub_uint16_t *) addr) & ~mask) | value;
      else
	*((volatile grub_uint16_t *) addr) = value;
      break;

    case 'b':
      if ((mask & 0xff) != 0xff)
	*((volatile grub_uint8_t *) addr)
	  = (*((volatile grub_uint8_t *) addr) & ~mask) | value;
      else
	*((volatile grub_uint8_t *) addr) = value;
      break;
    }

  return 0;
}

static grub_err_t
grub_cmd_write_bytes (grub_command_t cmd __attribute__ ((unused)),
  int argc, char **argv)
{
  grub_addr_t addr;
  grub_uint32_t value;

  if (argc < 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
      N_("at least two arguments expected"));

  addr = grub_strtoul (argv[0], 0, 0);
  for (int i = 1; i < argc; i++)
    {
      value = grub_strtoul (argv[i], 0, 0) & 0xFF;
      *((volatile grub_uint8_t *) addr + (i-1)) = value;
    }

  return 0;
}

static const struct grub_arg_option options_load[] =
{
  {"skip", 'k', 0, N_("Skip N bytes from file."), "N", ARG_TYPE_INT},
  {"length", 'l', 0, N_("Read only N bytes."), "N", ARG_TYPE_INT},
  {"addr", 'a', 0, N_("Specify memory address."), "ADDR", ARG_TYPE_INT},
  {"nodecompress", 'n', 0, N_("Don't decompress the file."), 0, 0},
  {"set", 's', 0, N_("Store the file name in a variable."),
    "VARNAME", ARG_TYPE_STRING},
  {0, 0, 0, 0, 0, 0}
};

enum options_load
{
  LOAD_SKIP,
  LOAD_LEN,
  LOAD_ADDR,
  LOAD_NODECOMP,
  LOAD_SET,
};

static grub_err_t
grub_cmd_loadfile (grub_extcmd_context_t ctxt, int argc, char** argv)
{
  struct grub_arg_list* state = ctxt->state;
  grub_file_t file;
  unsigned long long skip = 0, size, len;
  enum grub_file_type type = GRUB_FILE_TYPE_LOOPBACK;
  void *data = NULL;
  char path[64];

  if(argc != 1)
    return grub_error(GRUB_ERR_BAD_ARGUMENT, "file name required");

  if (state[LOAD_SKIP].set)
    skip = grub_strtoull (state[LOAD_SKIP].arg, NULL, 0);
  if (state[LOAD_NODECOMP].set)
    type |= GRUB_FILE_TYPE_NO_DECOMPRESS;

  file = grub_file_open (argv[0], type);
  if (!file)
    return grub_errno;

  size = grub_file_size (file);
  if (skip >= size)
    return grub_error(GRUB_ERR_BAD_ARGUMENT, "skip >= file_size");
  if (state[LOAD_LEN].set)
    len = grub_strtoull (state[LOAD_LEN].arg, NULL, 0);
  else
    len = size;

  if (skip + len > size)
    len = size - skip;

  if (state[LOAD_ADDR].set)
  {
#ifdef GRUB_MACHINE_EFI
    data = grub_efi_allocate_fixed ((grub_efi_physical_address_t)
              grub_strtoull (state[LOAD_ADDR].arg, 0, 0),
              ((grub_efi_uintn_t) len + ((1 << 12) - 1)) >> 12);
#else
    struct grub_relocator *rel = NULL;
    grub_relocator_chunk_t ch;
    rel = grub_relocator_new ();
    if (!rel)
      goto fail;
    if (grub_relocator_alloc_chunk_addr (rel, &ch, (grub_phys_addr_t)
              grub_strtoull (state[LOAD_ADDR].arg, 0, 0), len))
    {
      grub_relocator_unload (rel);
      goto fail;
    }
    data = get_virtual_current_address (ch);
#endif
  }
  else
    data = grub_malloc (len);
  if (!data)
  {
    grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory");
    goto fail;
  }

  grub_file_seek (file, skip);

  grub_file_read (file, data, len);
  grub_snprintf (path, 64, "(mem)[%p]+[%llu]", data, len);
  if (state[LOAD_SET].set)
    grub_env_set (state[LOAD_SET].arg, path);
  else
    grub_printf ("File: %s\n", path);

fail:
  if (file)
    grub_file_close (file);
  return grub_errno;
}

static grub_extcmd_t cmd_load;

static int
lua_read_byte (lua_State *state)
{
  grub_addr_t addr;
  addr = luaL_checkinteger (state, 1);
  lua_pushinteger (state, *((grub_uint8_t *) addr));
  return 1;
}

static int
lua_read_word (lua_State *state)
{
  grub_addr_t addr;
  addr = luaL_checkinteger (state, 1);
  lua_pushinteger (state, *((grub_uint16_t *) addr));
  return 1;
}

static int
lua_read_dword (lua_State *state)
{
  grub_addr_t addr;
  addr = luaL_checkinteger (state, 1);
  lua_pushinteger (state, *((grub_uint32_t *) addr));
  return 1;
}

static int
lua_write_byte (lua_State *state)
{
  grub_addr_t addr;
  addr = luaL_checkinteger (state, 1);
  *((grub_uint8_t *) addr) = luaL_checkinteger (state, 2);
  return 1;
}

static int
lua_write_word (lua_State *state)
{
  grub_addr_t addr;
  addr = luaL_checkinteger (state, 1);
  *((grub_uint16_t *) addr) = luaL_checkinteger (state, 2);
  return 1;
}

static int
lua_write_dword (lua_State *state)
{
  grub_addr_t addr;
  addr = luaL_checkinteger (state, 1);
  *((grub_uint32_t *) addr) = luaL_checkinteger (state, 2);
  return 1;
}

static luaL_Reg memlib[] =
{
  {"read_byte", lua_read_byte},
  {"read_word", lua_read_word},
  {"read_dword", lua_read_dword},
  {"write_byte", lua_write_byte},
  {"write_word", lua_write_word},
  {"write_dword", lua_write_dword},
  {0, 0}
};

GRUB_MOD_INIT(memrw)
{
  cmd_read_byte =
    grub_register_extcmd ("read_byte", grub_cmd_read, 0,
			  N_("ADDR"), N_("Read 8-bit value from ADDR."),
			  options);
  cmd_read_word =
    grub_register_extcmd ("read_word", grub_cmd_read, 0,
			  N_("ADDR"), N_("Read 16-bit value from ADDR."),
			  options);
  cmd_read_dword =
    grub_register_extcmd ("read_dword", grub_cmd_read, 0,
			  N_("ADDR"), N_("Read 32-bit value from ADDR."),
			  options);
  cmd_write_byte =
    grub_register_command ("write_byte", grub_cmd_write,
			   N_("ADDR VALUE [MASK]"),
			   N_("Write 8-bit VALUE to ADDR."));
  cmd_write_word =
    grub_register_command ("write_word", grub_cmd_write,
			   N_("ADDR VALUE [MASK]"),
			   N_("Write 16-bit VALUE to ADDR."));
  cmd_write_dword =
    grub_register_command ("write_dword", grub_cmd_write,
			   N_("ADDR VALUE [MASK]"),
			   N_("Write 32-bit VALUE to ADDR."));
  cmd_write_bytes =
    grub_register_command ("write_bytes", grub_cmd_write_bytes,
			   N_("ADDR VALUE1 [VALUE2 [VALUE3 ...]]"),
			   N_("Write sequences of 8-bit VALUES to ADDR."));
  cmd_load = grub_register_extcmd ("loadfile", grub_cmd_loadfile, 0,
                                   N_("[OPTIONS] FILE"),
                                   N_("Load a file to memory."), options_load);
  if (grub_lua_global_state)
  {
    lua_gc (grub_lua_global_state, LUA_GCSTOP, 0);
    luaL_register (grub_lua_global_state, "memrw", memlib);
    lua_gc (grub_lua_global_state, LUA_GCRESTART, 0);
  }
}

GRUB_MOD_FINI(memrw)
{
  grub_unregister_extcmd (cmd_read_byte);
  grub_unregister_extcmd (cmd_read_word);
  grub_unregister_extcmd (cmd_read_dword);
  grub_unregister_command (cmd_write_byte);
  grub_unregister_command (cmd_write_word);
  grub_unregister_command (cmd_write_dword);
  grub_unregister_command (cmd_write_bytes);
  grub_unregister_extcmd (cmd_load);
}
