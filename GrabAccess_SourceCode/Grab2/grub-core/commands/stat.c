/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2007  Free Software Foundation, Inc.
 *  Copyright (C) 2003  NIIBE Yutaka <gniibe@m17n.org>
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

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/memory.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/extcmd.h>
#include <grub/env.h>
#include <grub/file.h>
#include <grub/fs.h>
#include <grub/normal.h>
#include <grub/partition.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
{
  {"set", 's', 0, N_("Set a variable to return value."), N_("VAR"), ARG_TYPE_STRING},
  {"size", 'z', 0, N_("Display file size."), 0, 0},
  {"human", 'm', 0, N_("Display file size in a human readable format."), 0, 0},
  {"offset", 'o', 0, N_("Display file offset on disk."), 0, 0},
  {"contig", 'c', 0, N_("Check if the file is contiguous or not."), 0, 0},
  {"fs", 'f', 0, N_("Display filesystem information."), 0, 0},
  {"ram", 'r', 0, N_("Display RAM size in MiB."), 0, 0},
  {"quiet", 'q', 0, N_("Don't print strings."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

enum options
{
  STAT_SET,
  STAT_SIZE,
  STAT_HUMAN,
  STAT_OFFSET,
  STAT_CONTIG,
  STAT_FS,
  STAT_RAM,
  STAT_QUIET,
};

static void
read_block_start (grub_disk_addr_t sector,
                  unsigned offset __attribute ((unused)),
                  unsigned length, void *data)
{
  grub_disk_addr_t *start = data;
  *start = sector + 1 - (length >> GRUB_DISK_SECTOR_BITS);
}

static grub_err_t
grub_cmd_stat (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  grub_file_t file = 0;
  grub_off_t size = 0;
  const char *human_size = NULL;
  char *str = NULL;
  grub_disk_addr_t start = 0;

  str = grub_malloc (GRUB_DISK_SECTOR_SIZE);
  if (!str)
  {
    grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
    goto fail;
  }

  if (state[STAT_RAM].set)
  {
    grub_uint64_t total_mem = grub_get_total_mem_size ();
    grub_snprintf (str, GRUB_DISK_SECTOR_SIZE, "%" PRIuGRUB_UINT64_T,
                   total_mem >> 20);
    if (!state[STAT_QUIET].set)
      grub_printf ("%s\n", str);
    goto fail;
  }

  if (argc != 1)
  {
    grub_error (GRUB_ERR_BAD_ARGUMENT, "bad argument");
    goto fail;
  }
  file = grub_file_open (args[0], GRUB_FILE_TYPE_CAT | GRUB_FILE_TYPE_NO_DECOMPRESS);
  if (!file)
  {
    grub_error (GRUB_ERR_BAD_FILENAME, N_("failed to open %s"), args[0]);
    goto fail;
  }

  size = grub_file_size (file);

  int namelen = grub_strlen (args[0]);
  if (args[0][0] == '(' && args[0][namelen - 1] == ')')
  {
    args[0][namelen - 1] = 0;
    grub_disk_t disk = 0;
    disk = grub_disk_open (&args[0][1]);
    if (!disk)
      goto fail;
    size = grub_disk_native_sectors (disk) << GRUB_DISK_SECTOR_BITS;
    grub_disk_close (disk);
  }

  human_size = grub_get_human_size (size, GRUB_HUMAN_SIZE_SHORT);

  if (state[STAT_CONTIG].set)
  {
    int num = 0;
    if (file->device && file->device->disk)
    {
      num = grub_blocklist_convert (file);
      if (!state[STAT_QUIET].set)
        grub_printf ("File is%scontiguous.\nNumber of fragments: %d\n",
                     (num > 1)? " NOT ":" ", num);
    }
    grub_snprintf (str, GRUB_DISK_SECTOR_SIZE, "%d", num);
    goto fail;
  }

  if (file->device && file->device->disk)
  {
    file->read_hook = read_block_start;
    file->read_hook_data = &start;
    grub_file_read (file, str, GRUB_DISK_SECTOR_SIZE);
    grub_memset (str, 0, GRUB_DISK_SECTOR_SIZE);
  }

  if (state[STAT_SIZE].set)
  {
    grub_snprintf (str, GRUB_DISK_SECTOR_SIZE, "%" PRIuGRUB_UINT64_T, size);
    if (!state[STAT_QUIET].set)
      grub_printf ("%s\n", str);
  }
  else if (state[STAT_HUMAN].set)
  {
    grub_strncpy (str, human_size, GRUB_DISK_SECTOR_SIZE);
    if (!state[STAT_QUIET].set)
      grub_printf ("%s\n", str);
  }
  else if (state[STAT_OFFSET].set)
  {
    grub_snprintf (str, GRUB_DISK_SECTOR_SIZE, "%" PRIuGRUB_UINT64_T, start);
    if (!state[STAT_QUIET].set)
      grub_printf ("%s\n", str);
  }
  else if (state[STAT_FS].set)
  {
    if (!file->fs || !file->device || !file->device->disk)
      goto fail;
    char *label = NULL;
    char partinfo[64];
    if (file->fs->fs_label)
      file->fs->fs_label (file->device, &label);

    if (!state[STAT_QUIET].set)
    {
      grub_printf ("Filesystem: %s\n", file->fs->name);
      if (label)
        grub_printf ("Label: [%s]\n", label);
      grub_printf ("Disk: %s\n", file->device->disk->name);
      grub_printf ("Total sectors: %" PRIuGRUB_UINT64_T "\n",
                    file->device->disk->total_sectors);
    }
    if (file->device->disk->partition)
    {
      grub_snprintf (partinfo, 64,
                     "%s %d %" PRIuGRUB_UINT64_T
                     " %" PRIuGRUB_UINT64_T " %d %" PRIuGRUB_UINT64_T,
                     file->device->disk->partition->partmap->name,
                     file->device->disk->partition->number,
                     file->device->disk->partition->start,
                     file->device->disk->partition->len,
                     file->device->disk->partition->index,
                     file->device->disk->partition->flag);
      if (!state[STAT_QUIET].set)
        grub_printf ("Partition information: \n%s\n", partinfo);
    }
    else
      grub_strncpy (partinfo, "no_part", 64);
    grub_snprintf (str, GRUB_DISK_SECTOR_SIZE,
                   "%s [%s] %s %" PRIuGRUB_UINT64_T " %s",
                   file->fs->name, label? label : "",
                   file->device->disk->name,
                   file->device->disk->total_sectors,
                   partinfo);
    if (label)
      grub_free (label);
  }
  else
  {
    if (!state[STAT_QUIET].set)
      grub_printf ("File: %s\nSize: %s\nSeekable: %d\nOffset on disk: %"
                   PRIuGRUB_UINT64_T "\n",
                   file->name, human_size,
                   !file->not_easily_seekable, start);
    grub_snprintf (str, GRUB_DISK_SECTOR_SIZE, "%s %d %" PRIuGRUB_UINT64_T,
                   human_size, !file->not_easily_seekable, start);
  }

fail:
  if (state[STAT_SET].set)
    grub_env_set (state[STAT_SET].arg, str);
  if (str)
    grub_free (str);
  if (file)
    grub_file_close (file);
  return grub_errno;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(stat)
{
  cmd = grub_register_extcmd ("stat", grub_cmd_stat, 0, N_("[OPTIONS] FILE"),
                              N_("Display file and filesystem information."),
                              options);
}

GRUB_MOD_FINI(stat)
{
  grub_unregister_extcmd (cmd);
}
