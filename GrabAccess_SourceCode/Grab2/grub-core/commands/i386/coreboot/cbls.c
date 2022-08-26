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

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/coreboot/lbio.h>
#include <grub/i386/tsc.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const char *console_descs[] = {
  "8250 UART",
  "VGA",
  "BTEXT",
  "log buffer console",
  "SROM",
  "EHCI debug",
  "memory-mapped 8250 UART"
};

static const char *descs[] = {
  [GRUB_LINUXBIOS_MEMBER_MEMORY] = "memory map (`lsmmap' to list)",
  [GRUB_LINUXBIOS_MEMBER_MAINBOARD] = "mainboard",
  [4] = "version",
  [5] = "extra version",
  [6] = "build",
  [7] = "compile time",
  [8] = "compile by",
  [9] = "compile host",
  [0xa] = "compile domain",
  [0xb] = "compiler",
  [0xc] = "linker",
  [0xd] = "assembler",
  [0xf] = "serial",
  [GRUB_LINUXBIOS_MEMBER_CONSOLE] = "console",
  [GRUB_LINUXBIOS_MEMBER_FRAMEBUFFER] = "framebuffer",
  [0x13] = "GPIO",
  [0x15] = "VDAT",
  [GRUB_LINUXBIOS_MEMBER_TIMESTAMPS] = "timestamps (`coreboot_boottime' to list)",
  [GRUB_LINUXBIOS_MEMBER_CBMEMC] = "CBMEM console (`cbmemc' to list)",
  [0x18] = "MRC cache",
  [0x19] = "VBNV",
  [0xc8] = "CMOS option table",
  [0xc9] = "CMOS option",
  [0xca] = "CMOS option enum",
  [0xcb] = "CMOS option defaults",
  [0xcc] = "CMOS checksum",
};

static int
iterate_linuxbios_table (grub_linuxbios_table_item_t table_item,
			 void *data __attribute__ ((unused)))
{
  if (table_item->tag < ARRAY_SIZE (descs) && descs[table_item->tag])
    grub_printf ("tag=%02x size=%02x %s",
		 table_item->tag, table_item->size, descs[table_item->tag]);
  else
    grub_printf ("tag=%02x size=%02x",
		 table_item->tag, table_item->size);

  switch (table_item->tag)
    {
    case GRUB_LINUXBIOS_MEMBER_FRAMEBUFFER:
      {
	struct grub_linuxbios_table_framebuffer *fb;
	fb = (struct grub_linuxbios_table_framebuffer *) (table_item + 1);

	grub_printf (": %dx%dx%d pitch=%d lfb=0x%llx %d/%d/%d/%d %d/%d/%d/%d",
		     fb->width, fb->height,
		     fb->bpp, fb->pitch, 
		     (unsigned long long) fb->lfb,
		     fb->red_mask_size, fb->green_mask_size,
		     fb->blue_mask_size, fb->reserved_mask_size,
		     fb->red_field_pos, fb->green_field_pos,
		     fb->blue_field_pos, fb->reserved_field_pos);
	break;
      }
    case GRUB_LINUXBIOS_MEMBER_MAINBOARD:
      {
	struct grub_linuxbios_mainboard *mb;
	mb = (struct grub_linuxbios_mainboard *) (table_item + 1);
	grub_printf (": vendor=`%s' part_number=`%s'",
		     mb->strings + mb->vendor,
		     mb->strings + mb->part_number);
	break;
      }
    case 0x04 ... 0x0d:
      grub_printf (": `%s'", (char *) (table_item + 1));
      break;
    case GRUB_LINUXBIOS_MEMBER_CONSOLE:
      {
	grub_uint16_t *val = (grub_uint16_t *) (table_item + 1);
	grub_printf (": id=%d", *val);
	if (*val < ARRAY_SIZE (console_descs)
	    && console_descs[*val])
	  grub_printf (" %s", console_descs[*val]);
      }
    }
  grub_printf ("\n");

  return 0;
}


static grub_err_t
grub_cmd_lscoreboot (struct grub_command *cmd __attribute__ ((unused)),
			    int argc __attribute__ ((unused)),
			    char *argv[] __attribute__ ((unused)))
{
  grub_linuxbios_table_iterate (iterate_linuxbios_table, 0);
  return 0;
}

static grub_command_t cmd;

GRUB_MOD_INIT(cbls)
{
  cmd =
    grub_register_command ("lscoreboot", grub_cmd_lscoreboot,
			   0, N_("List coreboot tables."));
}

GRUB_MOD_FINI(cbls)
{
  grub_unregister_command (cmd);
}
