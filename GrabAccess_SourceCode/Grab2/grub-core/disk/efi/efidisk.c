/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008  Free Software Foundation, Inc.
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

#include <grub/disk.h>
#include <grub/partition.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/err.h>
#include <grub/term.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/disk.h>

/* GUID.  */
static grub_efi_guid_t block_io_guid = GRUB_EFI_BLOCK_IO_GUID;

static struct grub_efidisk_data *fd_devices;
static struct grub_efidisk_data *hd_devices;
static struct grub_efidisk_data *cd_devices;

struct grub_efivdisk_data *grub_efivdisk_list;

static struct grub_efidisk_data *
make_devices (void)
{
  grub_efi_uintn_t num_handles;
  grub_efi_handle_t *handles;
  grub_efi_handle_t *handle;
  struct grub_efidisk_data *devices = 0;

  /* Find handles which support the disk io interface.  */
  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &block_io_guid,
				    0, &num_handles);
  if (! handles)
    return 0;

  /* Make a linked list of devices.  */
  for (handle = handles; num_handles--; handle++)
    {
      grub_efi_device_path_t *dp;
      grub_efi_device_path_t *ldp;
      struct grub_efidisk_data *d;
      grub_efi_block_io_t *bio;

      dp = grub_efi_get_device_path (*handle);
      if (! dp)
	continue;

      ldp = grub_efi_find_last_device_path (dp);
      if (! ldp)
	/* This is empty. Why?  */
	continue;

      bio = grub_efi_open_protocol (*handle, &block_io_guid,
				    GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
      if (! bio)
	/* This should not happen... Why?  */
	continue;

      /* iPXE adds stub Block IO protocol to loaded image device handle. It is
         completely non-functional and simply returns an error for every method.
        So attempt to detect and skip it. Magic number is literal "iPXE" and
        check block size as well */
      /* FIXME: shoud we close it? We do not do it elsewhere */
      if (bio->media && bio->media->media_id == 0x69505845U &&
         bio->media->block_size == 1)
         continue;

      d = grub_malloc (sizeof (*d));
      if (! d)
	{
	  /* Uggh.  */
	  grub_free (handles);
	  while (devices)
	    {
	      d = devices->next;
	      grub_free (devices);
	      devices = d;
	    }
	  return 0;
	}

      d->handle = *handle;
      d->device_path = dp;
      d->last_device_path = ldp;
      d->block_io = bio;
      d->next = devices;
      devices = d;
    }

  grub_free (handles);

  return devices;
}

/* Find the parent device.  */
static struct grub_efidisk_data *
find_parent_device (struct grub_efidisk_data *devices,
		    struct grub_efidisk_data *d)
{
  grub_efi_device_path_t *dp, *ldp;
  struct grub_efidisk_data *parent;

  dp = grub_efi_duplicate_device_path (d->device_path);
  if (! dp)
    return 0;

  ldp = grub_efi_find_last_device_path (dp);
  if (! ldp)
    return 0;

  ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
  ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
  ldp->length = sizeof (*ldp);

  for (parent = devices; parent; parent = parent->next)
    {
      /* Ignore itself.  */
      if (parent == d)
	continue;

      if (grub_efi_compare_device_paths (parent->device_path, dp) == 0)
	break;
    }

  grub_free (dp);
  return parent;
}

static int
is_child (struct grub_efidisk_data *child,
	  struct grub_efidisk_data *parent)
{
  grub_efi_device_path_t *dp, *ldp;
  int ret;

  dp = grub_efi_duplicate_device_path (child->device_path);
  if (! dp)
    return 0;

  ldp = grub_efi_find_last_device_path (dp);
  if (! ldp)
    return 0;

  ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
  ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
  ldp->length = sizeof (*ldp);

  ret = (grub_efi_compare_device_paths (dp, parent->device_path) == 0);
  grub_free (dp);
  return ret;
}

#define FOR_CHILDREN(p, dev) for (p = dev; p; p = p->next) if (is_child (p, d))

/* Add a device into a list of devices in an ascending order.  */
static void
add_device (struct grub_efidisk_data **devices, struct grub_efidisk_data *d)
{
  struct grub_efidisk_data **p;
  struct grub_efidisk_data *n;

  for (p = devices; *p; p = &((*p)->next))
    {
      int ret;

      ret = grub_efi_compare_device_paths (grub_efi_find_last_device_path ((*p)->device_path),
					   grub_efi_find_last_device_path (d->device_path));
      if (ret == 0)
	ret = grub_efi_compare_device_paths ((*p)->device_path,
					     d->device_path);
      if (ret == 0)
	return;
      else if (ret > 0)
	break;
    }

  n = grub_malloc (sizeof (*n));
  if (! n)
    return;

  grub_memcpy (n, d, sizeof (*n));
  n->next = (*p);
  (*p) = n;
}

/* Name the devices.  */
static void
name_devices (struct grub_efidisk_data *devices)
{
  struct grub_efidisk_data *d;

  /* First, identify devices by media device paths.  */
  for (d = devices; d; d = d->next)
    {
      grub_efi_device_path_t *dp;

      dp = d->last_device_path;
      if (! dp)
	continue;

      if (GRUB_EFI_DEVICE_PATH_TYPE (dp) == GRUB_EFI_MEDIA_DEVICE_PATH_TYPE)
	{
	  int is_hard_drive = 0;

	  switch (GRUB_EFI_DEVICE_PATH_SUBTYPE (dp))
	    {
	    case GRUB_EFI_HARD_DRIVE_DEVICE_PATH_SUBTYPE:
	      is_hard_drive = 1;
	      /* Intentionally fall through.  */
	    case GRUB_EFI_CDROM_DEVICE_PATH_SUBTYPE:
	      {
		struct grub_efidisk_data *parent, *parent2;

		parent = find_parent_device (devices, d);
		if (!parent)
		  {
#ifdef DEBUG_NAMES
		    grub_printf ("skipping orphaned partition: ");
		    grub_efi_print_device_path (d->device_path);
#endif
		    break;
		  }
		parent2 = find_parent_device (devices, parent);
		if (parent2)
		  {
#ifdef DEBUG_NAMES
		    grub_printf ("skipping subpartition: ");
		    grub_efi_print_device_path (d->device_path);
#endif
		    /* Mark itself as used.  */
		    d->last_device_path = 0;
		    break;
		  }
		if (!parent->last_device_path)
		  {
		    d->last_device_path = 0;
		    break;
		  }
		if (is_hard_drive)
		  {
#ifdef DEBUG_NAMES
		    grub_printf ("adding a hard drive by a partition: ");
		    grub_efi_print_device_path (parent->device_path);
#endif
		    add_device (&hd_devices, parent);
		  }
		else
		  {
#ifdef DEBUG_NAMES
		    grub_printf ("adding a cdrom by a partition: ");
		    grub_efi_print_device_path (parent->device_path);
#endif
		    add_device (&cd_devices, parent);
		  }

		/* Mark the parent as used.  */
		parent->last_device_path = 0;
	      }
	      /* Mark itself as used.  */
	      d->last_device_path = 0;
	      break;

	    default:
#ifdef DEBUG_NAMES
	      grub_printf ("skipping other type: ");
	      grub_efi_print_device_path (d->device_path);
#endif
	      /* For now, ignore the others.  */
	      break;
	    }
	}
      else
	{
#ifdef DEBUG_NAMES
	  grub_printf ("skipping non-media: ");
	  grub_efi_print_device_path (d->device_path);
#endif
	}
    }

  /* Let's see what can be added more.  */
  for (d = devices; d; d = d->next)
    {
      grub_efi_device_path_t *dp;
      grub_efi_block_io_media_t *m;
      int is_floppy = 0;

      dp = d->last_device_path;
      if (! dp)
	continue;

      /* Ghosts proudly presented by Apple.  */
      if (GRUB_EFI_DEVICE_PATH_TYPE (dp) == GRUB_EFI_MEDIA_DEVICE_PATH_TYPE
	  && GRUB_EFI_DEVICE_PATH_SUBTYPE (dp)
	  == GRUB_EFI_VENDOR_MEDIA_DEVICE_PATH_SUBTYPE)
	{
	  grub_efi_vendor_device_path_t *vendor = (grub_efi_vendor_device_path_t *) dp;
	  const struct grub_efi_guid apple = GRUB_EFI_VENDOR_APPLE_GUID;

	  if (vendor->header.length == sizeof (*vendor)
	      && grub_memcmp (&vendor->vendor_guid, &apple,
			      sizeof (vendor->vendor_guid)) == 0
	      && find_parent_device (devices, d))
	    continue;
	}

      m = d->block_io->media;
      if (GRUB_EFI_DEVICE_PATH_TYPE (dp) == GRUB_EFI_ACPI_DEVICE_PATH_TYPE
	  && GRUB_EFI_DEVICE_PATH_SUBTYPE (dp)
	  == GRUB_EFI_ACPI_DEVICE_PATH_SUBTYPE)
	{
	  grub_efi_acpi_device_path_t *acpi
	    = (grub_efi_acpi_device_path_t *) dp;
	  /* Floppy EISA ID.  */ 
	  if (acpi->hid == 0x60441d0 || acpi->hid == 0x70041d0
	      || acpi->hid == 0x70141d1)
	    is_floppy = 1;
	}
      if (is_floppy)
	{
#ifdef DEBUG_NAMES
	  grub_printf ("adding a floppy: ");
	  grub_efi_print_device_path (d->device_path);
#endif
	  add_device (&fd_devices, d);
	}
      else if (m->read_only && m->block_size > GRUB_DISK_SECTOR_SIZE)
	{
	  /* This check is too heuristic, but assume that this is a
	     CDROM drive.  */
#ifdef DEBUG_NAMES
	  grub_printf ("adding a cdrom by guessing: ");
	  grub_efi_print_device_path (d->device_path);
#endif
	  add_device (&cd_devices, d);
	}
      else
	{
	  /* The default is a hard drive.  */
#ifdef DEBUG_NAMES
	  grub_printf ("adding a hard drive by guessing: ");
	  grub_efi_print_device_path (d->device_path);
#endif
	  add_device (&hd_devices, d);
	}
    }
}

static void
free_devices (struct grub_efidisk_data *devices)
{
  struct grub_efidisk_data *p, *q;

  for (p = devices; p; p = q)
    {
      q = p->next;
      grub_free (p);
    }
}

/* Enumerate all disks to name devices.  */
static void
enumerate_disks (void)
{
  struct grub_efidisk_data *devices;

  devices = make_devices ();
  if (! devices)
    return;

  name_devices (devices);
  free_devices (devices);
}

static int
grub_efidisk_iterate (grub_disk_dev_iterate_hook_t hook, void *hook_data,
		      grub_disk_pull_t pull)
{
  struct grub_efidisk_data *d;
  char buf[16];
  int count;

  switch (pull)
    {
    case GRUB_DISK_PULL_NONE:
      for (d = hd_devices, count = 0; d; d = d->next, count++)
	{
	  grub_snprintf (buf, sizeof (buf), "hd%d", count);
	  grub_dprintf ("efidisk", "iterating %s\n", buf);
	  if (hook (buf, hook_data))
	    return 1;
	}
      break;
    case GRUB_DISK_PULL_REMOVABLE:
      for (d = fd_devices, count = 0; d; d = d->next, count++)
	{
	  grub_snprintf (buf, sizeof (buf), "fd%d", count);
	  grub_dprintf ("efidisk", "iterating %s\n", buf);
	  if (hook (buf, hook_data))
	    return 1;
	}

      for (d = cd_devices, count = 0; d; d = d->next, count++)
	{
	  grub_snprintf (buf, sizeof (buf), "cd%d", count);
	  grub_dprintf ("efidisk", "iterating %s\n", buf);
	  if (hook (buf, hook_data))
	    return 1;
	}
      break;
    default:
      return 0;
    }

  return 0;
}

static int
get_drive_number (const char *name)
{
  unsigned long drive;

  if ((name[0] != 'f' && name[0] != 'h' && name[0] != 'c') || name[1] != 'd')
    goto fail;

  drive = grub_strtoul (name + 2, 0, 10);
  if (grub_errno != GRUB_ERR_NONE)
    goto fail;

  return (int) drive ;

 fail:
  grub_error (GRUB_ERR_UNKNOWN_DEVICE, "not a efidisk");
  return -1;
}

static struct grub_efidisk_data *
get_device (struct grub_efidisk_data *devices, int num)
{
  struct grub_efidisk_data *d;

  for (d = devices; d && num; d = d->next, num--)
    ;

  if (num == 0)
    return d;

  return 0;
}

static grub_err_t
grub_efidisk_open (const char *name, struct grub_disk *disk)
{
  int num;
  struct grub_efidisk_data *d = 0;
  grub_efi_block_io_media_t *m;

  grub_dprintf ("efidisk", "opening %s\n", name);

  num = get_drive_number (name);
  if (num < 0)
    return grub_errno;

  switch (name[0])
    {
    case 'f':
      d = get_device (fd_devices, num);
      break;
    case 'c':
      d = get_device (cd_devices, num);
      break;
    case 'h':
      d = get_device (hd_devices, num);
      break;
    default:
      /* Never reach here.  */
      break;
    }

  if (! d)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "no such device");

  disk->id = ((num << GRUB_CHAR_BIT) | name[0]);
  m = d->block_io->media;
  /* FIXME: Probably it is better to store the block size in the disk,
     and total sectors should be replaced with total blocks.  */
  grub_dprintf ("efidisk",
		"m = %p, last block = %llx, block size = %x, io align = %x\n",
		m, (unsigned long long) m->last_block, m->block_size,
		m->io_align);

  /* Ensure required buffer alignment is a power of two (or is zero). */
  if (m->io_align & (m->io_align - 1))
    return grub_error (GRUB_ERR_IO, "invalid buffer alignment %d", m->io_align);

  disk->total_sectors = m->last_block + 1;
  /* Don't increase this value due to bug in some EFI.  */
  disk->max_agglomerate = 0xa0000 >> (GRUB_DISK_CACHE_BITS + GRUB_DISK_SECTOR_BITS);
  if (m->block_size & (m->block_size - 1) || !m->block_size)
    return grub_error (GRUB_ERR_IO, "invalid sector size %d",
		       m->block_size);
  for (disk->log_sector_size = 0;
       (1U << disk->log_sector_size) < m->block_size;
       disk->log_sector_size++);
  disk->data = d;

  grub_dprintf ("efidisk", "opening %s succeeded\n", name);

  return GRUB_ERR_NONE;
}

static void
grub_efidisk_close (struct grub_disk *disk __attribute__ ((unused)))
{
  /* EFI disks do not allocate extra memory, so nothing to do here.  */
  grub_dprintf ("efidisk", "closing %s\n", disk->name);
}

static grub_efi_status_t
grub_efidisk_readwrite (struct grub_disk *disk, grub_disk_addr_t sector,
			grub_size_t size, char *buf, int wr)
{
  struct grub_efidisk_data *d;
  grub_efi_block_io_t *bio;
  grub_efi_status_t status;
  grub_size_t io_align, num_bytes;
  char *aligned_buf;

  d = disk->data;
  bio = d->block_io;

  /* Set alignment to 1 if 0 specified */
  io_align = bio->media->io_align ? bio->media->io_align : 1;
  num_bytes = size << disk->log_sector_size;

  if ((grub_addr_t) buf & (io_align - 1))
    {
      aligned_buf = grub_memalign (io_align, num_bytes);
      if (! aligned_buf)
	return GRUB_EFI_OUT_OF_RESOURCES;
      if (wr)
	grub_memcpy (aligned_buf, buf, num_bytes);
    }
  else
    {
      aligned_buf = buf;
    }

  status =  efi_call_5 ((wr ? bio->write_blocks : bio->read_blocks), bio,
			bio->media->media_id, (grub_efi_uint64_t) sector,
			(grub_efi_uintn_t) num_bytes, aligned_buf);

  if ((grub_addr_t) buf & (io_align - 1))
    {
      if (!wr)
	grub_memcpy (buf, aligned_buf, num_bytes);
      grub_free (aligned_buf);
    }

  return status;
}

static grub_err_t
grub_efidisk_read (struct grub_disk *disk, grub_disk_addr_t sector,
		   grub_size_t size, char *buf)
{
  grub_efi_status_t status;

  grub_dprintf ("efidisk",
		"reading 0x%lx sectors at the sector 0x%llx from %s\n",
		(unsigned long) size, (unsigned long long) sector, disk->name);

  status = grub_efidisk_readwrite (disk, sector, size, buf, 0);

  if (status == GRUB_EFI_NO_MEDIA)
    return grub_error (GRUB_ERR_OUT_OF_RANGE, N_("no media in `%s'"), disk->name);
  else if (status != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_READ_ERROR,
		       N_("failure reading sector 0x%llx from `%s'"),
		       (unsigned long long) sector,
		       disk->name);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_efidisk_write (struct grub_disk *disk, grub_disk_addr_t sector,
		    grub_size_t size, const char *buf)
{
  grub_efi_status_t status;

  grub_dprintf ("efidisk",
		"writing 0x%lx sectors at the sector 0x%llx to %s\n",
		(unsigned long) size, (unsigned long long) sector, disk->name);

  status = grub_efidisk_readwrite (disk, sector, size, (char *) buf, 1);

  if (status == GRUB_EFI_NO_MEDIA)
    return grub_error (GRUB_ERR_OUT_OF_RANGE, N_("no media in `%s'"), disk->name);
  else if (status != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_WRITE_ERROR,
		       N_("failure writing sector 0x%llx to `%s'"),
		       (unsigned long long) sector, disk->name);

  return GRUB_ERR_NONE;
}

static struct grub_disk_dev grub_efidisk_dev =
  {
    .name = "efidisk",
    .id = GRUB_DISK_DEVICE_EFIDISK_ID,
    .disk_iterate = grub_efidisk_iterate,
    .disk_open = grub_efidisk_open,
    .disk_close = grub_efidisk_close,
    .disk_read = grub_efidisk_read,
    .disk_write = grub_efidisk_write,
    .next = 0
  };

void
grub_efidisk_fini (void)
{
  free_devices (fd_devices);
  free_devices (hd_devices);
  free_devices (cd_devices);
  fd_devices = 0;
  hd_devices = 0;
  cd_devices = 0;
  grub_disk_dev_unregister (&grub_efidisk_dev);
}

void
grub_efidisk_init (void)
{
  grub_disk_firmware_fini = grub_efidisk_fini;

  enumerate_disks ();
  grub_disk_dev_register (&grub_efidisk_dev);
}

/* Some utility functions to map GRUB devices with EFI devices.  */
grub_efi_handle_t
grub_efidisk_get_device_handle (grub_disk_t disk)
{
  struct grub_efidisk_data *d;
  char type;
  struct grub_efidisk_data dummy_data;

  if (disk->dev->id == GRUB_DISK_DEVICE_EFIDISK_ID)
  {
    d = disk->data;
    type = disk->name[0];
  }
  else if (grub_strcmp (disk->dev->name, "efivdisk") == 0)
  {
    struct grub_efivdisk_data *v = disk->data;
    d = &dummy_data;
    dummy_data.handle = v->vdisk.handle;
    dummy_data.device_path = v->vdisk.dp;
    dummy_data.block_io = (grub_efi_block_io_t *)&v->vdisk.block_io;
    if (v->type == CD)
      type = 'c';
    else if (v->type == FD)
      type = 'f';
    else
      type = 'h';
  }
  else
    return 0;

  switch (type)
    {
    case 'f':
      /* This is the simplest case.  */
      return d->handle;

    case 'c':
      /* FIXME: probably this is not correct.  */
      return d->handle;

    case 'h':
      /* If this is the whole disk, just return its own data.  */
      if (! disk->partition)
	return d->handle;

      /* Otherwise, we must query the corresponding device to the firmware.  */
      {
	struct grub_efidisk_data *devices;
	grub_efi_handle_t handle = 0;
	struct grub_efidisk_data *c;

	devices = make_devices ();
	FOR_CHILDREN (c, devices)
	  {
	    grub_efi_hard_drive_device_path_t *hd;

	    hd = (grub_efi_hard_drive_device_path_t *) c->last_device_path;

	    if ((GRUB_EFI_DEVICE_PATH_TYPE (c->last_device_path)
		 == GRUB_EFI_MEDIA_DEVICE_PATH_TYPE)
		&& (GRUB_EFI_DEVICE_PATH_SUBTYPE (c->last_device_path)
		    == GRUB_EFI_HARD_DRIVE_DEVICE_PATH_SUBTYPE)
		&& (grub_partition_get_start (disk->partition) 
		    == (hd->partition_start << (disk->log_sector_size
						- GRUB_DISK_SECTOR_BITS)))
		&& (grub_partition_get_len (disk->partition)
		    == (hd->partition_size << (disk->log_sector_size
					       - GRUB_DISK_SECTOR_BITS))))
	      {
		handle = c->handle;
		break;
	      }
	  }

	free_devices (devices);

	if (handle != 0)
	  return handle;
      }
      break;

    default:
      break;
    }

  return 0;
}

#define NEEDED_BUFLEN sizeof ("XdXXXXXXXXXX")
static inline int
get_diskname_from_path_real (const grub_efi_device_path_t *path,
			     struct grub_efidisk_data *head,
			     char *buf)
{
  int count = 0;
  struct grub_efidisk_data *d;
  for (d = head, count = 0; d; d = d->next, count++)
    if (grub_efi_compare_device_paths (d->device_path, path) == 0)
      {
	grub_snprintf (buf, NEEDED_BUFLEN - 1, "d%d", count);
	return 1;
      }
  return 0;
}

static inline int
get_diskname_from_path (const grub_efi_device_path_t *path,
			char *buf)
{
  if (get_diskname_from_path_real (path, hd_devices, buf + 1))
    {
      buf[0] = 'h';
      return 1;
    }

  if (get_diskname_from_path_real (path, fd_devices, buf + 1))
    {
      buf[0] = 'f';
      return 1;
    }

  if (get_diskname_from_path_real (path, cd_devices, buf + 1))
    {
      buf[0] = 'c';
      return 1;
    }
  return 0;
}

/* Context for grub_efidisk_get_device_name.  */
struct grub_efidisk_get_device_name_ctx
{
  char *partition_name;
  grub_efi_hard_drive_device_path_t *hd;
};

/* Helper for grub_efidisk_get_device_name.
   Find the identical partition.  */
static int
grub_efidisk_get_device_name_iter (grub_disk_t disk,
				   const grub_partition_t part, void *data)
{
  struct grub_efidisk_get_device_name_ctx *ctx = data;

  if (grub_partition_get_start (part)
      == (ctx->hd->partition_start << (disk->log_sector_size
				       - GRUB_DISK_SECTOR_BITS))
      && grub_partition_get_len (part)
      == (ctx->hd->partition_size << (disk->log_sector_size
				      - GRUB_DISK_SECTOR_BITS)))
    {
      ctx->partition_name = grub_partition_get_name (part);
      return 1;
    }

  return 0;
}

char *
grub_efidisk_get_device_name_from_dp (grub_efi_device_path_t *dp)
{
  grub_efi_device_path_t *ldp;
  char device_name[NEEDED_BUFLEN];
  if (! dp)
    return 0;

  ldp = grub_efi_find_last_device_path (dp);
  if (! ldp)
    return 0;

  if (GRUB_EFI_DEVICE_PATH_TYPE (ldp) == GRUB_EFI_MEDIA_DEVICE_PATH_TYPE
      && (GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) == GRUB_EFI_CDROM_DEVICE_PATH_SUBTYPE
	  || GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) == GRUB_EFI_HARD_DRIVE_DEVICE_PATH_SUBTYPE))
    {
      struct grub_efidisk_get_device_name_ctx ctx;
      char *dev_name;
      grub_efi_device_path_t *dup_dp;
      grub_disk_t parent = 0;

      /* It is necessary to duplicate the device path so that GRUB
	 can overwrite it.  */
      dup_dp = grub_efi_duplicate_device_path (dp);
      if (! dup_dp)
	return 0;

      while (1)
	{
	  grub_efi_device_path_t *dup_ldp;
	  dup_ldp = grub_efi_find_last_device_path (dup_dp);
	  if (! dup_ldp)
	    break;

	  if (!(GRUB_EFI_DEVICE_PATH_TYPE (dup_ldp) == GRUB_EFI_MEDIA_DEVICE_PATH_TYPE
		&& (GRUB_EFI_DEVICE_PATH_SUBTYPE (dup_ldp) == GRUB_EFI_CDROM_DEVICE_PATH_SUBTYPE
		    || GRUB_EFI_DEVICE_PATH_SUBTYPE (dup_ldp) == GRUB_EFI_HARD_DRIVE_DEVICE_PATH_SUBTYPE)))
	    break;

	  dup_ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
	  dup_ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
	  dup_ldp->length = sizeof (*dup_ldp);
	}

      if (!get_diskname_from_path (dup_dp, device_name))
	{
	  grub_free (dup_dp);
	  return 0;
	}

      parent = grub_disk_open (device_name);
      grub_free (dup_dp);

      if (! parent)
	return 0;

      /* Find a partition which matches the hard drive device path.  */
      ctx.partition_name = NULL;
      ctx.hd = (grub_efi_hard_drive_device_path_t *) ldp;
      if (ctx.hd->partition_start == 0
	  && (ctx.hd->partition_size << (parent->log_sector_size
					 - GRUB_DISK_SECTOR_BITS))
	  == grub_disk_native_sectors (parent))
	{
	  dev_name = grub_strdup (parent->name);
	}
      else
	{
	  grub_partition_iterate (parent, grub_efidisk_get_device_name_iter,
				  &ctx);

	  if (! ctx.partition_name)
	    {
	      /* No partition found. In most cases partition is embed in
		 the root path anyway, so this is not critical.
		 This happens only if partition is on partmap that GRUB
		 doesn't need to access root.
	       */
	      grub_disk_close (parent);
	      return grub_strdup (device_name);
	    }

	  dev_name = grub_xasprintf ("%s,%s", parent->name,
				     ctx.partition_name);
	  grub_free (ctx.partition_name);
	}
      grub_disk_close (parent);

      return dev_name;
    }
  /* This may be guessed device - floppy, cdrom or entire disk.  */
  if (!get_diskname_from_path (dp, device_name))
    return 0;
  return grub_strdup (device_name);
}

char *
grub_efidisk_get_device_name (grub_efi_handle_t *handle)
{
  grub_efi_device_path_t *dp;
  dp = grub_efi_get_device_path (handle);
  return grub_efidisk_get_device_name_from_dp (dp);
}
