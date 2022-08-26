/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010  Free Software Foundation, Inc.
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

#include <grub/loader.h>
#include <grub/file.h>
#include <grub/err.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/partition.h>
#include <grub/msdos_partition.h>
#include <grub/scsi.h>
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/video.h>
#include <grub/mm.h>
#include <grub/cpu/relocator.h>
#include <grub/extcmd.h>
#include <grub/verify.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_dl_t my_mod;
static struct grub_relocator *rel;
static grub_uint32_t eip = 0xffffffff;

#define GRUB_PLAN9_TARGET            0x100000
#define GRUB_PLAN9_ALIGN             4096
#define GRUB_PLAN9_CONFIG_ADDR       0x001200
#define GRUB_PLAN9_CONFIG_PATH_SIZE  0x000040
#define GRUB_PLAN9_CONFIG_MAGIC      "ZORT 0\r\n"

static const struct grub_arg_option options[] =
  {
    {"map", 'm', GRUB_ARG_OPTION_REPEATABLE,
     /* TRANSLATORS: it's about guessing which GRUB disk
	is which Plan9 disk. If your language has no
	word "mapping" you can use another word which
	means that the GRUBDEVICE and PLAN9DEVICE are
	actually the same device, just named differently
	in OS and GRUB.  */
     N_("Override guessed mapping of Plan9 devices."), 
     N_("GRUBDEVICE=PLAN9DEVICE"),
     ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
  };

struct grub_plan9_header
{
  grub_uint32_t magic;
#define GRUB_PLAN9_MAGIC 0x1eb
  grub_uint32_t text_size;
  grub_uint32_t data_size;
  grub_uint32_t bss_size;
  grub_uint32_t sectiona;
  grub_uint32_t entry_addr;
  grub_uint32_t zero;
  grub_uint32_t sectionb;
};

static grub_err_t
grub_plan9_boot (void)
{
  struct grub_relocator32_state state = { 
    .eax = 0,
    .eip = eip,
    .ebx = 0,
    .ecx = 0,
    .edx = 0,
    .edi = 0,
    .esp = 0,
    .ebp = 0,
    .esi = 0
  };
  grub_video_set_mode ("text", 0, 0);

  return grub_relocator32_boot (rel, state, 0);
}

static grub_err_t
grub_plan9_unload (void)
{
  grub_relocator_unload (rel);
  rel = NULL;
  grub_dl_unref (my_mod);
  return GRUB_ERR_NONE;
}

/* Context for grub_cmd_plan9.  */
struct grub_cmd_plan9_ctx
{
  grub_extcmd_context_t ctxt;
  grub_file_t file;
  char *pmap;
  grub_size_t pmapalloc;
  grub_size_t pmapptr;
  int noslash;
  int prefixescnt[5];
  char *bootdisk, *bootpart;
};

static const char prefixes[5][10] = {
  "dos", "plan9", "ntfs", "linux", "linuxswap"
};

#include <grub/err.h>

static inline grub_err_t 
grub_extend_alloc (grub_size_t sz, grub_size_t *allocated, char **ptr)
{
  void *n;
  if (sz < *allocated)
    return GRUB_ERR_NONE;

  *allocated = 2 * sz;
  n = grub_realloc (*ptr, *allocated);
  if (!n)
    return grub_errno;
  *ptr = n;
  return GRUB_ERR_NONE;
}

/* Helper for grub_cmd_plan9.  */
static int
fill_partition (grub_disk_t disk, const grub_partition_t partition, void *data)
{
  struct grub_cmd_plan9_ctx *fill_ctx = data;
  int file_disk = 0;
  int pstart, pend;

  if (!fill_ctx->noslash)
    {
      if (grub_extend_alloc (fill_ctx->pmapptr + 1, &fill_ctx->pmapalloc,
			     &fill_ctx->pmap))
	return 1;
      fill_ctx->pmap[fill_ctx->pmapptr++] = '/';
    }
  fill_ctx->noslash = 0;

  file_disk = fill_ctx->file->device->disk
    && disk->id == fill_ctx->file->device->disk->id
    && disk->dev->id == fill_ctx->file->device->disk->dev->id;

  pstart = fill_ctx->pmapptr;
  if (grub_strcmp (partition->partmap->name, "plan") == 0)
    {
      unsigned ptr = partition->index + sizeof ("part ") - 1;
      grub_err_t err;
      disk->partition = partition->parent;
      do
	{
	  if (grub_extend_alloc (fill_ctx->pmapptr + 1, &fill_ctx->pmapalloc,
				 &fill_ctx->pmap))
	    return 1;
	  err = grub_disk_read (disk, 1, ptr, 1,
				fill_ctx->pmap + fill_ctx->pmapptr);
	  if (err)
	    {
	      disk->partition = 0;
	      return err;
	    }
	  ptr++;
	  fill_ctx->pmapptr++;
	}
      while (grub_isalpha (fill_ctx->pmap[fill_ctx->pmapptr - 1])
	     || grub_isdigit (fill_ctx->pmap[fill_ctx->pmapptr - 1]));
      fill_ctx->pmapptr--;
    }
  else
    {
      char name[50];
      int c = 0;
      if (grub_strcmp (partition->partmap->name, "msdos") == 0)
	{
	  switch (partition->msdostype)
	    {
	    case GRUB_PC_PARTITION_TYPE_PLAN9:
	      c = 1;
	      break;
	    case GRUB_PC_PARTITION_TYPE_NTFS:
	      c = 2;
	      break;
	    case GRUB_PC_PARTITION_TYPE_MINIX:
	    case GRUB_PC_PARTITION_TYPE_LINUX_MINIX:
	    case GRUB_PC_PARTITION_TYPE_EXT2FS:
	      c = 3;
	      break;
	    case GRUB_PC_PARTITION_TYPE_LINUX_SWAP:
	      c = 4;
	      break;
	    }
	}

      if (fill_ctx->prefixescnt[c] == 0)
	grub_strcpy (name, prefixes[c]);
      else
	grub_snprintf (name, sizeof (name), "%s.%d", prefixes[c],
		       fill_ctx->prefixescnt[c]);
      fill_ctx->prefixescnt[c]++;
      if (grub_extend_alloc (fill_ctx->pmapptr + grub_strlen (name) + 1,
			     &fill_ctx->pmapalloc, &fill_ctx->pmap))
	return 1;
      grub_strcpy (fill_ctx->pmap + fill_ctx->pmapptr, name);
      fill_ctx->pmapptr += grub_strlen (name);
    }
  pend = fill_ctx->pmapptr;
  if (grub_extend_alloc (fill_ctx->pmapptr + 2 + 25 + 5 + 25,
			 &fill_ctx->pmapalloc, &fill_ctx->pmap))
    return 1;
  fill_ctx->pmap[fill_ctx->pmapptr++] = ' ';
  grub_snprintf (fill_ctx->pmap + fill_ctx->pmapptr, 25 + 5 + 25,
		 "%" PRIuGRUB_UINT64_T " %" PRIuGRUB_UINT64_T,
		 grub_partition_get_start (partition),
		 grub_partition_get_start (partition)
		 + grub_partition_get_len (partition));
  if (file_disk && grub_partition_get_start (partition)
      == grub_partition_get_start (fill_ctx->file->device->disk->partition)
      && grub_partition_get_len (partition)
      == grub_partition_get_len (fill_ctx->file->device->disk->partition))
    {
      grub_free (fill_ctx->bootpart);
      fill_ctx->bootpart = grub_strndup (fill_ctx->pmap + pstart,
					 pend - pstart);
    }

  fill_ctx->pmapptr += grub_strlen (fill_ctx->pmap + fill_ctx->pmapptr);
  return 0;
}

/* Helper for grub_cmd_plan9.  */
static int
fill_disk (const char *name, void *data)
{
  struct grub_cmd_plan9_ctx *fill_ctx = data;
  grub_device_t dev;
  char *plan9name = NULL;
  unsigned i;
  int file_disk = 0;

  dev = grub_device_open (name);
  if (!dev)
    {
      grub_print_error ();
      return 0;
    }
  if (!dev->disk)
    {
      grub_device_close (dev);
      return 0;
    }
  file_disk = fill_ctx->file->device->disk
    && dev->disk->id == fill_ctx->file->device->disk->id
    && dev->disk->dev->id == fill_ctx->file->device->disk->dev->id;
  for (i = 0;
       fill_ctx->ctxt->state[0].args && fill_ctx->ctxt->state[0].args[i]; i++)
    if (grub_strncmp (name, fill_ctx->ctxt->state[0].args[i],
		      grub_strlen (name)) == 0
	&& fill_ctx->ctxt->state[0].args[i][grub_strlen (name)] == '=')
      break;
  if (fill_ctx->ctxt->state[0].args && fill_ctx->ctxt->state[0].args[i])
    plan9name = grub_strdup (fill_ctx->ctxt->state[0].args[i]
			     + grub_strlen (name) + 1);
  else
    switch (dev->disk->dev->id)
      {
      case GRUB_DISK_DEVICE_BIOSDISK_ID:
	if (dev->disk->id & 0x80)
	  plan9name = grub_xasprintf ("sdB%u",
				      (unsigned) (dev->disk->id & 0x7f));
	else
	  plan9name = grub_xasprintf ("fd%u",
				      (unsigned) (dev->disk->id & 0x7f));
	break;
	/* Shouldn't happen as Plan9 doesn't work on these platforms.  */
      case GRUB_DISK_DEVICE_OFDISK_ID:
      case GRUB_DISK_DEVICE_EFIDISK_ID:

	/* Plan9 doesn't see those.  */
      default:

	/* Not sure how to handle those. */
      case GRUB_DISK_DEVICE_NAND_ID:
	if (!file_disk)
	  {
	    grub_device_close (dev);
	    return 0;
	  }
	
	/* if it's the disk the kernel is loaded from we need to name
	   it nevertheless.  */
	plan9name = grub_strdup ("sdZ0");
	break;

      case GRUB_DISK_DEVICE_ATA_ID:
	{
	  unsigned unit;
	  if (grub_strlen (dev->disk->name) < sizeof ("ata0") - 1)
	    unit = 0;
	  else
	    unit = grub_strtoul (dev->disk->name + sizeof ("ata0") - 1, 0, 0);
	  plan9name = grub_xasprintf ("sd%c%d", 'C' + unit / 2, unit % 2);
	}
	break;
      case GRUB_DISK_DEVICE_SCSI_ID:
	if (((dev->disk->id >> GRUB_SCSI_ID_SUBSYSTEM_SHIFT) & 0xff)
	    == GRUB_SCSI_SUBSYSTEM_PATA)
	  {
	    unsigned unit;
	    if (grub_strlen (dev->disk->name) < sizeof ("ata0") - 1)
	      unit = 0;
	    else
	      unit = grub_strtoul (dev->disk->name + sizeof ("ata0") - 1,
				   0, 0);
	    plan9name = grub_xasprintf ("sd%c%d", 'C' + unit / 2, unit % 2);
	    break;
	  }
	
	/* FIXME: how does Plan9 number controllers?
	   We probably need save the SCSI devices and sort them  */
	plan9name
	  = grub_xasprintf ("sd0%u", (unsigned)
			    ((dev->disk->id >> GRUB_SCSI_ID_BUS_SHIFT)
			     & 0xf));
	break;
      }
  if (!plan9name)
    {
      grub_print_error ();
      grub_device_close (dev);
      return 0;
    }
  if (grub_extend_alloc (fill_ctx->pmapptr + grub_strlen (plan9name)
			 + sizeof ("part="), &fill_ctx->pmapalloc,
			 &fill_ctx->pmap))
    {
      grub_free (plan9name);
      grub_device_close (dev);
      return 1;
    }
  grub_strcpy (fill_ctx->pmap + fill_ctx->pmapptr, plan9name);
  fill_ctx->pmapptr += grub_strlen (plan9name);
  if (!file_disk)
    grub_free (plan9name);
  else
    {
      grub_free (fill_ctx->bootdisk);
      fill_ctx->bootdisk = plan9name;
    }
  grub_strcpy (fill_ctx->pmap + fill_ctx->pmapptr, "part=");
  fill_ctx->pmapptr += sizeof ("part=") - 1;

  fill_ctx->noslash = 1;
  grub_memset (fill_ctx->prefixescnt, 0, sizeof (fill_ctx->prefixescnt));
  if (grub_partition_iterate (dev->disk, fill_partition, fill_ctx))
    {
      grub_device_close (dev);
      return 1;
    }
  if (grub_extend_alloc (fill_ctx->pmapptr + 1, &fill_ctx->pmapalloc,
			 &fill_ctx->pmap))
    {
      grub_device_close (dev);
      return 1;
    }
  fill_ctx->pmap[fill_ctx->pmapptr++] = '\n';

  grub_device_close (dev);
  return 0;
}

static grub_err_t
grub_cmd_plan9 (grub_extcmd_context_t ctxt, int argc, char *argv[])
{
  struct grub_cmd_plan9_ctx fill_ctx = {
    .ctxt = ctxt,
    .file = 0,
    .pmap = NULL,
    .pmapalloc = 256,
    .pmapptr = 0,
    .noslash = 1,
    .bootdisk = NULL,
    .bootpart = NULL
  };
  void *mem;
  grub_size_t memsize, padsize;
  struct grub_plan9_header hdr;
  char *config, *configptr;
  grub_size_t configsize;
  char *bootpath = NULL;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  grub_dl_ref (my_mod);

  rel = grub_relocator_new ();
  if (!rel)
    goto fail;

  fill_ctx.file = grub_file_open (argv[0], GRUB_FILE_TYPE_PLAN9_KERNEL);
  if (! fill_ctx.file)
    goto fail;

  fill_ctx.pmap = grub_malloc (fill_ctx.pmapalloc);
  if (!fill_ctx.pmap)
    goto fail;

  if (grub_disk_dev_iterate (fill_disk, &fill_ctx))
    goto fail;

  if (grub_extend_alloc (fill_ctx.pmapptr + 1, &fill_ctx.pmapalloc,
			 &fill_ctx.pmap))
    goto fail;
  fill_ctx.pmap[fill_ctx.pmapptr] = 0;

  {
    char *file_name = grub_strchr (argv[0], ')');
    if (file_name)
      file_name++;
    else
      file_name = argv[0];
    if (*file_name)
      file_name++;

    if (fill_ctx.bootpart)
      bootpath = grub_xasprintf ("%s!%s!%s", fill_ctx.bootdisk,
				 fill_ctx.bootpart, file_name);
    else
      bootpath = grub_xasprintf ("%s!%s", fill_ctx.bootdisk, file_name);
    grub_free (fill_ctx.bootdisk);
    grub_free (fill_ctx.bootpart);
  }
  if (!bootpath)
    goto fail;

  if (grub_file_read (fill_ctx.file, &hdr,
		      sizeof (hdr)) != (grub_ssize_t) sizeof (hdr))
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		    argv[0]);
      goto fail;
    }

  if (grub_be_to_cpu32 (hdr.magic) != GRUB_PLAN9_MAGIC
      || hdr.zero)
    {
      grub_error (GRUB_ERR_BAD_OS, "unsupported Plan9");
      goto fail;
    }

  memsize = ALIGN_UP (grub_be_to_cpu32 (hdr.text_size) + sizeof (hdr),
		       GRUB_PLAN9_ALIGN);
  memsize += ALIGN_UP (grub_be_to_cpu32 (hdr.data_size), GRUB_PLAN9_ALIGN);
  memsize += ALIGN_UP(grub_be_to_cpu32 (hdr.bss_size), GRUB_PLAN9_ALIGN);
  eip = grub_be_to_cpu32 (hdr.entry_addr) & 0xfffffff;

  /* path */
  configsize = GRUB_PLAN9_CONFIG_PATH_SIZE;
  /* magic */
  configsize += sizeof (GRUB_PLAN9_CONFIG_MAGIC) - 1;
  {
    int i;
    for (i = 1; i < argc; i++)
      configsize += grub_strlen (argv[i]) + 1;
  }
  configsize += (sizeof ("bootfile=") - 1) + grub_strlen (bootpath) + 1;
  configsize += fill_ctx.pmapptr;
  /* Terminating \0.  */
  configsize++;

  {
    grub_relocator_chunk_t ch;
    grub_err_t err;
    err = grub_relocator_alloc_chunk_addr (rel, &ch, GRUB_PLAN9_CONFIG_ADDR,
					   configsize);
    if (err)
      goto fail;
    config = get_virtual_current_address (ch);
  }

  grub_memset (config, 0, GRUB_PLAN9_CONFIG_PATH_SIZE);
  grub_strncpy (config, bootpath, GRUB_PLAN9_CONFIG_PATH_SIZE - 1);

  configptr = config + GRUB_PLAN9_CONFIG_PATH_SIZE;
  grub_memcpy (configptr, GRUB_PLAN9_CONFIG_MAGIC,
	       sizeof (GRUB_PLAN9_CONFIG_MAGIC) - 1);
  configptr += sizeof (GRUB_PLAN9_CONFIG_MAGIC) - 1;
  configptr = grub_stpcpy (configptr, "bootfile=");
  configptr = grub_stpcpy (configptr, bootpath);
  *configptr++ = '\n';
  char *cmdline = configptr;
  {
    int i;
    for (i = 1; i < argc; i++)
      {
	configptr = grub_stpcpy (configptr, argv[i]);
	*configptr++ = '\n';
      }
  }

  {
    grub_err_t err;
    *configptr = '\0';
    err = grub_verify_string (cmdline, GRUB_VERIFY_KERNEL_CMDLINE);
    if (err)
      goto fail;
  }

  configptr = grub_stpcpy (configptr, fill_ctx.pmap);

  {
    grub_relocator_chunk_t ch;
    grub_err_t err;

    err = grub_relocator_alloc_chunk_addr (rel, &ch, GRUB_PLAN9_TARGET,
					   memsize);
    if (err)
      goto fail;
    mem = get_virtual_current_address (ch);
  }

  {
    grub_uint8_t *ptr;
    ptr = mem;
    grub_memcpy (ptr, &hdr, sizeof (hdr));
    ptr += sizeof (hdr);

    if (grub_file_read (fill_ctx.file, ptr, grub_be_to_cpu32 (hdr.text_size))
	!= (grub_ssize_t) grub_be_to_cpu32 (hdr.text_size))
      {
	if (!grub_errno)
	  grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		      argv[0]);
	goto fail;
      }
    ptr += grub_be_to_cpu32 (hdr.text_size);
    padsize = ALIGN_UP (grub_be_to_cpu32 (hdr.text_size) + sizeof (hdr),
			GRUB_PLAN9_ALIGN) - grub_be_to_cpu32 (hdr.text_size)
      - sizeof (hdr);

    grub_memset (ptr, 0, padsize);
    ptr += padsize;

    if (grub_file_read (fill_ctx.file, ptr, grub_be_to_cpu32 (hdr.data_size))
	!= (grub_ssize_t) grub_be_to_cpu32 (hdr.data_size))
      {
	if (!grub_errno)
	  grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		      argv[0]);
	goto fail;
      }
    ptr += grub_be_to_cpu32 (hdr.data_size);
    padsize = ALIGN_UP (grub_be_to_cpu32 (hdr.data_size), GRUB_PLAN9_ALIGN)
      - grub_be_to_cpu32 (hdr.data_size);

    grub_memset (ptr, 0, padsize);
    ptr += padsize;
    grub_memset (ptr, 0, ALIGN_UP(grub_be_to_cpu32 (hdr.bss_size),
				  GRUB_PLAN9_ALIGN));
  }
  grub_loader_set (grub_plan9_boot, grub_plan9_unload, 1);
  return GRUB_ERR_NONE;

 fail:
  grub_free (fill_ctx.pmap);

  if (fill_ctx.file)
    grub_file_close (fill_ctx.file);

  grub_plan9_unload ();

  return grub_errno;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(plan9)
{
  cmd = grub_register_extcmd ("plan9", grub_cmd_plan9,
			      GRUB_COMMAND_OPTIONS_AT_START,
			      N_("KERNEL ARGS"), N_("Load Plan9 kernel."),
			      options);
  my_mod = mod;
}

GRUB_MOD_FINI(plan9)
{
  grub_unregister_extcmd (cmd);
}
