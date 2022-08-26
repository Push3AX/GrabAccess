/* gdb.c - gdb remote stub module */
/*
 *  Copyright (C) 2003  Free Software Foundation, Inc.
 *  Copyright (C) 2006  Lubomir Kundrak
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/normal.h>
#include <grub/term.h>
#include <grub/cpu/gdb.h>
#include <grub/gdb.h>
#include <grub/serial.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_cmd_gdbstub (struct grub_command *cmd __attribute__ ((unused)),
		  int argc, char **args)
{
  struct grub_serial_port *port;
  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "port required");
  port = grub_serial_find (args[0]);
  if (!port)
    return grub_errno;
  grub_gdb_port = port;
  /* TRANSLATORS: at this position GRUB waits for the user to do an action
     in remote debugger, namely to tell it to establish connection.  */
  grub_puts_ (N_("Now connect the remote debugger, please."));
  grub_gdb_breakpoint ();
  return 0;
}

static grub_err_t
grub_cmd_gdbstop (struct grub_command *cmd __attribute__ ((unused)),
		  int argc __attribute__ ((unused)),
		  char **args __attribute__ ((unused)))
{
  grub_gdb_port = NULL;
  return 0;
}

static grub_err_t
grub_cmd_gdb_break (struct grub_command *cmd __attribute__ ((unused)),
		    int argc __attribute__ ((unused)),
		    char **args __attribute__ ((unused)))
{
  if (!grub_gdb_port)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "No GDB stub is running");
  grub_gdb_breakpoint ();
  return 0;
}

static grub_command_t cmd, cmd_stop, cmd_break;

GRUB_MOD_INIT (gdb)
{
  grub_gdb_idtinit ();
  cmd = grub_register_command ("gdbstub", grub_cmd_gdbstub,
			       N_("PORT"), 
			       /* TRANSLATORS: GDB stub is a small part of
				  GDB functionality running on local host
				  which allows remote debugger to
				  connect to it.  */
			       N_("Start GDB stub on given port"));
  cmd_break = grub_register_command ("gdbstub_break", grub_cmd_gdb_break,
				     /* TRANSLATORS: this refers to triggering
					a breakpoint so that the user will land
					into GDB.  */
				     0, N_("Break into GDB"));
  cmd_stop = grub_register_command ("gdbstub_stop", grub_cmd_gdbstop,
				    0, N_("Stop GDB stub"));
}

GRUB_MOD_FINI (gdb)
{
  grub_unregister_command (cmd);
  grub_unregister_command (cmd_stop);
  grub_gdb_idtrestore ();
}

