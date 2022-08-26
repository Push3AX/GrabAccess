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

static grub_uint32_t
tsc2ms (grub_uint64_t tsc)
{
  grub_uint64_t ah = tsc >> 32;
  grub_uint64_t al = tsc & 0xffffffff;

  return ((al * grub_tsc_rate) >> 32) + ah * grub_tsc_rate;
}

static const char *descs[] = {
  [1] = "romstage",
  [2] = "before RAM init",
  [3] = "after RAM init",
  [4] = "end of romstage",
  [5] = "start of verified boot",
  [6] = "end of verified boot",
  [8] = "start of RAM copy",
  [9] = "end of RAM copy",
  [10] = "start of ramstage",
  [11] = "start of bootblock",
  [12] = "end of bootblock",
  [13] = "starting to load romstage",
  [14] = "finished loading romstage",
  [15] = "starting LZMA decompress (ignore for x86)",
  [16] = "finished LZMA decompress (ignore for x86)",
  [30] = "device enumerate",
  [40] = "device configure",
  [50] = "device enable",
  [60] = "device initialize",
  [70] = "device done",
  [75] = "CBMEM POST",
  [80] = "writing tables",
  [90] = "loading payload",
  [98] = "wake jump",
  [99] = "selfboot jump",
};

static int
iterate_linuxbios_table (grub_linuxbios_table_item_t table_item,
			 void *data)
{
  int *available = data;
  grub_uint64_t last_tsc = 0;
  struct grub_linuxbios_timestamp_table *ts_table;
  unsigned i;

  if (table_item->tag != GRUB_LINUXBIOS_MEMBER_TIMESTAMPS)
    return 0;

  *available = 1;
  ts_table = (struct grub_linuxbios_timestamp_table *) (grub_addr_t)
    *(grub_uint64_t *) (table_item + 1);

  for (i = 0; i < ts_table->used; i++)
    {
      grub_uint32_t tmabs = tsc2ms (ts_table->entries[i].tsc);
      grub_uint32_t tmrel = tsc2ms (ts_table->entries[i].tsc - last_tsc);
      last_tsc = ts_table->entries[i].tsc;

      grub_printf ("%3d.%03ds %2d.%03ds %02d %s\n", 
		   tmabs / 1000, tmabs % 1000, tmrel / 1000, tmrel % 1000,
		   ts_table->entries[i].id,
		   (ts_table->entries[i].id < ARRAY_SIZE (descs)
		    && descs[ts_table->entries[i].id])
		   ? descs[ts_table->entries[i].id] : "");
    }
  return 1;
}


static grub_err_t
grub_cmd_coreboot_boottime (struct grub_command *cmd __attribute__ ((unused)),
			    int argc __attribute__ ((unused)),
			    char *argv[] __attribute__ ((unused)))
{
  int available = 0;

  grub_linuxbios_table_iterate (iterate_linuxbios_table, &available);
  if (!available)
    {
      grub_puts_ (N_("No boot time statistics is available\n"));
      return 0;
    }
 return 0;
}

static grub_command_t cmd_boottime;

GRUB_MOD_INIT(cbtime)
{
  cmd_boottime =
    grub_register_command ("coreboot_boottime", grub_cmd_coreboot_boottime,
			   0, N_("Show coreboot boot time statistics."));
}

GRUB_MOD_FINI(cbtime)
{
  grub_unregister_command (cmd_boottime);
}
