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
#include <grub/cpu/io.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_extcmd_t cmd_read_byte, cmd_read_word, cmd_read_dword;
static grub_command_t cmd_write_byte, cmd_write_word, cmd_write_dword;

static const struct grub_arg_option options[] =
  {
    {0, 'v', 0, N_("Save read value into variable VARNAME."),
     N_("VARNAME"), ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
  };


static grub_err_t
grub_cmd_read (grub_extcmd_context_t ctxt, int argc, char **argv)
{
  grub_port_t addr;
  grub_uint32_t value = 0;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));

  addr = grub_strtoul (argv[0], 0, 0);
  switch (ctxt->extcmd->cmd->name[sizeof ("in") - 1])
    {
    case 'l':
      value = grub_inl (addr);
      break;

    case 'w':
      value = grub_inw (addr);
      break;

    case 'b':
      value = grub_inb (addr);
      break;
    }

  if (ctxt->state[0].set)
    {
      char buf[sizeof ("XXXXXXXX")];
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
  grub_port_t addr;
  grub_uint32_t value;
  grub_uint32_t mask = 0xffffffff;

  if (argc != 2 && argc != 3)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("two arguments expected"));

  addr = grub_strtoul (argv[0], 0, 0);
  value = grub_strtoul (argv[1], 0, 0);
  if (argc == 3)
    mask = grub_strtoul (argv[2], 0, 0);
  value &= mask;
  switch (cmd->name[sizeof ("out") - 1])
    {
    case 'l':
      if (mask != 0xffffffff)
	grub_outl ((grub_inl (addr) & ~mask) | value, addr);
      else
	grub_outl (value, addr);
      break;

    case 'w':
      if ((mask & 0xffff) != 0xffff)
	grub_outw ((grub_inw (addr) & ~mask) | value, addr);
      else
	grub_outw (value, addr);
      break;

    case 'b':
      if ((mask & 0xff) != 0xff)
	grub_outb ((grub_inb (addr) & ~mask) | value, addr);
      else
	grub_outb (value, addr);
      break;
    }

  return 0;
}

GRUB_MOD_INIT(memrw)
{
  cmd_read_byte =
    grub_register_extcmd ("inb", grub_cmd_read, 0,
			  N_("PORT"), N_("Read 8-bit value from PORT."),
			  options);
  cmd_read_word =
    grub_register_extcmd ("inw", grub_cmd_read, 0,
			  N_("PORT"), N_("Read 16-bit value from PORT."),
			  options);
  cmd_read_dword =
    grub_register_extcmd ("inl", grub_cmd_read, 0,
			  N_("PORT"), N_("Read 32-bit value from PORT."),
			  options);
  cmd_write_byte =
    grub_register_command ("outb", grub_cmd_write,
			   N_("PORT VALUE [MASK]"),
			   N_("Write 8-bit VALUE to PORT."));
  cmd_write_word =
    grub_register_command ("outw", grub_cmd_write,
			   N_("PORT VALUE [MASK]"),
			   N_("Write 16-bit VALUE to PORT."));
  cmd_write_dword =
    grub_register_command ("outl", grub_cmd_write,
			   N_("ADDR VALUE [MASK]"),
			   N_("Write 32-bit VALUE to PORT."));
}

GRUB_MOD_FINI(memrw)
{
  grub_unregister_extcmd (cmd_read_byte);
  grub_unregister_extcmd (cmd_read_word);
  grub_unregister_extcmd (cmd_read_dword);
  grub_unregister_command (cmd_write_byte);
  grub_unregister_command (cmd_write_word);
  grub_unregister_command (cmd_write_dword);
}
