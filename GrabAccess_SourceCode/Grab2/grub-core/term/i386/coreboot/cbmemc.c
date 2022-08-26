/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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

#include <grub/term.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/time.h>
#include <grub/terminfo.h>
#include <grub/dl.h>
#include <grub/coreboot/lbio.h>
#include <grub/command.h>
#include <grub/normal.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define CURSOR_MASK ((1 << 28) - 1)
#define OVERFLOW (1 << 31)

struct grub_linuxbios_cbmemc
{
  grub_uint32_t size;
  grub_uint32_t cursor;
  char body[0];
};

static struct grub_linuxbios_cbmemc *cbmemc;

static void
put (struct grub_term_output *term __attribute__ ((unused)), const int c)
{
  grub_uint32_t flags, cursor;
  if (!cbmemc)
    return;
  flags = cbmemc->cursor & ~CURSOR_MASK;
  cursor = cbmemc->cursor & CURSOR_MASK;
  if (cursor >= cbmemc->size)
    return;
  cbmemc->body[cursor++] = c;
  if (cursor >= cbmemc->size)
    {
      cursor = 0;
      flags |= OVERFLOW;
    }
  cbmemc->cursor = flags | cursor;
}

struct grub_terminfo_output_state grub_cbmemc_terminfo_output =
  {
    .put = put,
    .size = { 80, 24 }
  };

static struct grub_term_output grub_cbmemc_term_output =
  {
    .name = "cbmemc",
    .init = grub_terminfo_output_init,
    .fini = 0,
    .putchar = grub_terminfo_putchar,
    .getxy = grub_terminfo_getxy,
    .getwh = grub_terminfo_getwh,
    .gotoxy = grub_terminfo_gotoxy,
    .cls = grub_terminfo_cls,
    .setcolorstate = grub_terminfo_setcolorstate,
    .setcursor = grub_terminfo_setcursor,
    .flags = GRUB_TERM_CODE_TYPE_ASCII,
    .data = &grub_cbmemc_terminfo_output,
    .progress_update_divisor = GRUB_PROGRESS_NO_UPDATE
  };

static int
iterate_linuxbios_table (grub_linuxbios_table_item_t table_item,
			 void *data __attribute__ ((unused)))
{
  if (table_item->tag != GRUB_LINUXBIOS_MEMBER_CBMEMC)
    return 0;
  cbmemc = (struct grub_linuxbios_cbmemc *) (grub_addr_t)
    *(grub_uint64_t *) (table_item + 1);
  return 1;
}

static grub_err_t
grub_cmd_cbmemc (struct grub_command *cmd __attribute__ ((unused)),
		 int argc __attribute__ ((unused)),
		 char *argv[] __attribute__ ((unused)))
{
  grub_size_t size, cursor;
  struct grub_linuxbios_cbmemc *real_cbmemc;

  if (!cbmemc)
    return grub_error (GRUB_ERR_IO, "no CBMEM console found");

  real_cbmemc = cbmemc;
  cbmemc = 0;
  cursor = real_cbmemc->cursor & CURSOR_MASK;
  if (!(real_cbmemc->cursor & OVERFLOW) && cursor < real_cbmemc->size)
    size = cursor;
  else
    size = real_cbmemc->size;
  if (real_cbmemc->cursor & OVERFLOW)
    {
      if (cursor > size)
        cursor = 0;
      grub_xnputs(real_cbmemc->body + cursor, size - cursor);
      grub_xnputs(real_cbmemc->body, cursor);
    }
  else
    grub_xnputs(real_cbmemc->body, size);
  cbmemc = real_cbmemc;
  return 0;
}

static grub_command_t cmd;

GRUB_MOD_INIT (cbmemc)
{
  grub_linuxbios_table_iterate (iterate_linuxbios_table, 0);

  if (cbmemc)
    grub_term_register_output ("cbmemc", &grub_cbmemc_term_output);

  cmd =
    grub_register_command ("cbmemc", grub_cmd_cbmemc,
			   0, N_("Show CBMEM console content."));
}


GRUB_MOD_FINI (cbmemc)
{
  grub_term_unregister_output (&grub_cbmemc_term_output);
  grub_unregister_command (cmd);
}
