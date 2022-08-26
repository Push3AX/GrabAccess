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

#include <grub/disk.h>
#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/err.h>
#include <grub/term.h>
#include <grub/i18n.h>
#include <grub/xen.h>
#include <grub/time.h>
#include <xen/io/blkif.h>

struct virtdisk
{
  int handle;
  char *fullname;
  char *backend_dir;
  char *frontend_dir;
  struct blkif_sring *shared_page;
  struct blkif_front_ring ring;
  grub_xen_grant_t grant;
  grub_xen_evtchn_t evtchn;
  void *dma_page;
  grub_xen_grant_t dma_grant;
  struct virtdisk *compat_next;
};

#define xen_wmb() mb()
#define xen_mb() mb()

static struct virtdisk *virtdisks;
static grub_size_t vdiskcnt;
struct virtdisk *compat_head;

static int
grub_virtdisk_iterate (grub_disk_dev_iterate_hook_t hook, void *hook_data,
		       grub_disk_pull_t pull)
{
  grub_size_t i;

  if (pull != GRUB_DISK_PULL_NONE)
    return 0;

  for (i = 0; i < vdiskcnt; i++)
    if (hook (virtdisks[i].fullname, hook_data))
      return 1;
  return 0;
}

static grub_err_t
grub_virtdisk_open (const char *name, grub_disk_t disk)
{
  int i;
  grub_uint32_t secsize;
  char fdir[200];
  char *buf;
  int num = -1;
  struct virtdisk *vd;

  /* For compatibility with pv-grub legacy menu.lst accept hdX as disk name */
  if (name[0] == 'h' && name[1] == 'd' && name[2])
    {
      num = grub_strtoul (name + 2, 0, 10);
      if (grub_errno)
	{
	  grub_errno = 0;
	  num = -1;
	}
    }
  for (i = 0, vd = compat_head; vd; vd = vd->compat_next, i++)
    if (i == num || grub_strcmp (name, vd->fullname) == 0)
      break;
  if (!vd)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "not a virtdisk");
  disk->data = vd;
  disk->id = vd - virtdisks;

  grub_snprintf (fdir, sizeof (fdir), "%s/sectors", vd->backend_dir);
  buf = grub_xenstore_get_file (fdir, NULL);
  if (!buf)
    return grub_errno;
  disk->total_sectors = grub_strtoull (buf, 0, 10);
  if (grub_errno)
    return grub_errno;

  grub_snprintf (fdir, sizeof (fdir), "%s/sector-size", vd->backend_dir);
  buf = grub_xenstore_get_file (fdir, NULL);
  if (!buf)
    return grub_errno;
  secsize = grub_strtoull (buf, 0, 10);
  if (grub_errno)
    return grub_errno;

  if ((secsize & (secsize - 1)) || !secsize || secsize < 512
      || secsize > GRUB_XEN_PAGE_SIZE)
    return grub_error (GRUB_ERR_IO, "unsupported sector size %d", secsize);

  for (disk->log_sector_size = 0;
       (1U << disk->log_sector_size) < secsize; disk->log_sector_size++);

  disk->total_sectors >>= disk->log_sector_size - 9;

  return GRUB_ERR_NONE;
}

static void
grub_virtdisk_close (grub_disk_t disk __attribute__ ((unused)))
{
}

static grub_err_t
grub_virtdisk_read (grub_disk_t disk, grub_disk_addr_t sector,
		    grub_size_t size, char *buf)
{
  struct virtdisk *data = disk->data;

  while (size)
    {
      grub_size_t cur;
      struct blkif_request *req;
      struct blkif_response *resp;
      int sta = 0;
      struct evtchn_send send;
      cur = size;
      if (cur > (unsigned) (GRUB_XEN_PAGE_SIZE >> disk->log_sector_size))
	cur = GRUB_XEN_PAGE_SIZE >> disk->log_sector_size;
      while (RING_FULL (&data->ring))
	grub_xen_sched_op (SCHEDOP_yield, 0);
      req = RING_GET_REQUEST (&data->ring, data->ring.req_prod_pvt);
      req->operation = BLKIF_OP_READ;
      req->nr_segments = 1;
      req->handle = data->handle;
      req->id = 0;
      req->sector_number = sector << (disk->log_sector_size - 9);
      req->seg[0].gref = data->dma_grant;
      req->seg[0].first_sect = 0;
      req->seg[0].last_sect = (cur << (disk->log_sector_size - 9)) - 1;
      data->ring.req_prod_pvt++;
      RING_PUSH_REQUESTS (&data->ring);
      mb ();
      send.port = data->evtchn;
      grub_xen_event_channel_op (EVTCHNOP_send, &send);

      while (!RING_HAS_UNCONSUMED_RESPONSES (&data->ring))
	{
	  grub_xen_sched_op (SCHEDOP_yield, 0);
	  mb ();
	}
      while (1)
	{
	  int wtd;
	  RING_FINAL_CHECK_FOR_RESPONSES (&data->ring, wtd);
	  if (!wtd)
	    break;
	  resp = RING_GET_RESPONSE (&data->ring, data->ring.rsp_cons);
	  data->ring.rsp_cons++;
	  if (resp->status)
	    sta = resp->status;
	}
      if (sta)
	return grub_error (GRUB_ERR_IO, "read failed");
      grub_memcpy (buf, data->dma_page, cur << disk->log_sector_size);
      size -= cur;
      sector += cur;
      buf += cur << disk->log_sector_size;
    }
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_virtdisk_write (grub_disk_t disk, grub_disk_addr_t sector,
		     grub_size_t size, const char *buf)
{
  struct virtdisk *data = disk->data;

  while (size)
    {
      grub_size_t cur;
      struct blkif_request *req;
      struct blkif_response *resp;
      int sta = 0;
      struct evtchn_send send;
      cur = size;
      if (cur > (unsigned) (GRUB_XEN_PAGE_SIZE >> disk->log_sector_size))
	cur = GRUB_XEN_PAGE_SIZE >> disk->log_sector_size;

      grub_memcpy (data->dma_page, buf, cur << disk->log_sector_size);

      while (RING_FULL (&data->ring))
	grub_xen_sched_op (SCHEDOP_yield, 0);
      req = RING_GET_REQUEST (&data->ring, data->ring.req_prod_pvt);
      req->operation = BLKIF_OP_WRITE;
      req->nr_segments = 1;
      req->handle = data->handle;
      req->id = 0;
      req->sector_number = sector << (disk->log_sector_size - 9);
      req->seg[0].gref = data->dma_grant;
      req->seg[0].first_sect = 0;
      req->seg[0].last_sect = (cur << (disk->log_sector_size - 9)) - 1;
      data->ring.req_prod_pvt++;
      RING_PUSH_REQUESTS (&data->ring);
      mb ();
      send.port = data->evtchn;
      grub_xen_event_channel_op (EVTCHNOP_send, &send);

      while (!RING_HAS_UNCONSUMED_RESPONSES (&data->ring))
	{
	  grub_xen_sched_op (SCHEDOP_yield, 0);
	  mb ();
	}
      while (1)
	{
	  int wtd;
	  RING_FINAL_CHECK_FOR_RESPONSES (&data->ring, wtd);
	  if (!wtd)
	    break;
	  resp = RING_GET_RESPONSE (&data->ring, data->ring.rsp_cons);
	  data->ring.rsp_cons++;
	  if (resp->status)
	    sta = resp->status;
	}
      if (sta)
	return grub_error (GRUB_ERR_IO, "write failed");
      size -= cur;
      sector += cur;
      buf += cur << disk->log_sector_size;
    }
  return GRUB_ERR_NONE;
}

static struct grub_disk_dev grub_virtdisk_dev = {
  .name = "xen",
  .id = GRUB_DISK_DEVICE_XEN,
  .disk_iterate = grub_virtdisk_iterate,
  .disk_open = grub_virtdisk_open,
  .disk_close = grub_virtdisk_close,
  .disk_read = grub_virtdisk_read,
  .disk_write = grub_virtdisk_write,
  .next = 0
};

static int
count (const char *dir __attribute__ ((unused)), void *data)
{
  grub_size_t *ctr = data;
  (*ctr)++;

  return 0;
}

static int
fill (const char *dir, void *data)
{
  grub_size_t *ctr = data;
  domid_t dom;
  /* "dir" is just a number, at most 19 characters. */
  char fdir[200];
  char num[20];
  grub_err_t err;
  void *buf;
  struct evtchn_alloc_unbound alloc_unbound;
  struct virtdisk **prev = &compat_head, *vd = compat_head;

  /* Shouldn't happen unles some hotplug happened.  */
  if (vdiskcnt >= *ctr)
    return 1;
  virtdisks[vdiskcnt].handle = grub_strtoul (dir, 0, 10);
  if (grub_errno)
    {
      grub_errno = 0;
      return 0;
    }
  virtdisks[vdiskcnt].fullname = 0;
  virtdisks[vdiskcnt].backend_dir = 0;

  grub_snprintf (fdir, sizeof (fdir), "device/vbd/%s/backend", dir);
  virtdisks[vdiskcnt].backend_dir = grub_xenstore_get_file (fdir, NULL);
  if (!virtdisks[vdiskcnt].backend_dir)
    goto out_fail_1;

  grub_snprintf (fdir, sizeof (fdir), "%s/dev",
		 virtdisks[vdiskcnt].backend_dir);
  buf = grub_xenstore_get_file (fdir, NULL);
  if (!buf)
    {
      grub_errno = 0;
      virtdisks[vdiskcnt].fullname = grub_xasprintf ("xenid/%s", dir);
    }
  else
    {
      virtdisks[vdiskcnt].fullname = grub_xasprintf ("xen/%s", (char *) buf);
      grub_free (buf);
    }
  if (!virtdisks[vdiskcnt].fullname)
    goto out_fail_1;

  grub_snprintf (fdir, sizeof (fdir), "device/vbd/%s/backend-id", dir);
  buf = grub_xenstore_get_file (fdir, NULL);
  if (!buf)
    goto out_fail_1;

  dom = grub_strtoul (buf, 0, 10);
  grub_free (buf);
  if (grub_errno)
    goto out_fail_1;

  virtdisks[vdiskcnt].shared_page =
    grub_xen_alloc_shared_page (dom, &virtdisks[vdiskcnt].grant);
  if (!virtdisks[vdiskcnt].shared_page)
    goto out_fail_1;

  virtdisks[vdiskcnt].dma_page =
    grub_xen_alloc_shared_page (dom, &virtdisks[vdiskcnt].dma_grant);
  if (!virtdisks[vdiskcnt].dma_page)
    goto out_fail_2;

  alloc_unbound.dom = DOMID_SELF;
  alloc_unbound.remote_dom = dom;

  grub_xen_event_channel_op (EVTCHNOP_alloc_unbound, &alloc_unbound);
  virtdisks[vdiskcnt].evtchn = alloc_unbound.port;

  SHARED_RING_INIT (virtdisks[vdiskcnt].shared_page);
  FRONT_RING_INIT (&virtdisks[vdiskcnt].ring, virtdisks[vdiskcnt].shared_page,
		   GRUB_XEN_PAGE_SIZE);

  grub_snprintf (fdir, sizeof (fdir), "device/vbd/%s/ring-ref", dir);
  grub_snprintf (num, sizeof (num), "%u", virtdisks[vdiskcnt].grant);
  err = grub_xenstore_write_file (fdir, num, grub_strlen (num));
  if (err)
    goto out_fail_3;

  grub_snprintf (fdir, sizeof (fdir), "device/vbd/%s/event-channel", dir);
  grub_snprintf (num, sizeof (num), "%u", virtdisks[vdiskcnt].evtchn);
  err = grub_xenstore_write_file (fdir, num, grub_strlen (num));
  if (err)
    goto out_fail_3;

  grub_snprintf (fdir, sizeof (fdir), "device/vbd/%s/protocol", dir);
  err = grub_xenstore_write_file (fdir, XEN_IO_PROTO_ABI_NATIVE,
				  grub_strlen (XEN_IO_PROTO_ABI_NATIVE));
  if (err)
    goto out_fail_3;

  struct gnttab_dump_table dt;
  dt.dom = DOMID_SELF;
  grub_xen_grant_table_op (GNTTABOP_dump_table, (void *) &dt, 1);

  grub_snprintf (fdir, sizeof (fdir), "device/vbd/%s/state", dir);
  err = grub_xenstore_write_file (fdir, "3", 1);
  if (err)
    goto out_fail_3;

  while (1)
    {
      grub_snprintf (fdir, sizeof (fdir), "%s/state",
		     virtdisks[vdiskcnt].backend_dir);
      buf = grub_xenstore_get_file (fdir, NULL);
      if (!buf)
	goto out_fail_3;
      if (grub_strcmp (buf, "2") != 0)
	break;
      grub_free (buf);
      grub_xen_sched_op (SCHEDOP_yield, 0);
    }
  grub_dprintf ("xen", "state=%s\n", (char *) buf);
  grub_free (buf);

  grub_snprintf (fdir, sizeof (fdir), "device/vbd/%s", dir);

  virtdisks[vdiskcnt].frontend_dir = grub_strdup (fdir);

  /* For compatibility with pv-grub maintain linked list sorted by handle
     value in increasing order. This allows mapping of (hdX) disk names
     from legacy menu.lst */
  while (vd)
    {
      if (vd->handle > virtdisks[vdiskcnt].handle)
	break;
      prev = &vd->compat_next;
      vd = vd->compat_next;
    }
  virtdisks[vdiskcnt].compat_next = vd;
  *prev = &virtdisks[vdiskcnt];

  vdiskcnt++;
  return 0;

out_fail_3:
  grub_xen_free_shared_page (virtdisks[vdiskcnt].dma_page);
out_fail_2:
  grub_xen_free_shared_page (virtdisks[vdiskcnt].shared_page);
out_fail_1:
  grub_free (virtdisks[vdiskcnt].backend_dir);
  grub_free (virtdisks[vdiskcnt].fullname);

  grub_errno = 0;
  return 0;
}

void
grub_xendisk_init (void)
{
  grub_size_t ctr = 0;
  if (grub_xenstore_dir ("device/vbd", count, &ctr))
    grub_errno = 0;

  if (!ctr)
    return;

  virtdisks = grub_calloc (ctr, sizeof (virtdisks[0]));
  if (!virtdisks)
    return;
  if (grub_xenstore_dir ("device/vbd", fill, &ctr))
    grub_errno = 0;

  grub_disk_dev_register (&grub_virtdisk_dev);
}

void
grub_xendisk_fini (void)
{
  char fdir[200];
  unsigned i;

  for (i = 0; i < vdiskcnt; i++)
    {
      char *buf;
      struct evtchn_close close_op = {.port = virtdisks[i].evtchn };

      grub_snprintf (fdir, sizeof (fdir), "%s/state",
		     virtdisks[i].frontend_dir);
      grub_xenstore_write_file (fdir, "6", 1);

      while (1)
	{
	  grub_snprintf (fdir, sizeof (fdir), "%s/state",
			 virtdisks[i].backend_dir);
	  buf = grub_xenstore_get_file (fdir, NULL);
	  grub_dprintf ("xen", "state=%s\n", (char *) buf);

	  if (!buf || grub_strcmp (buf, "6") == 0)
	    break;
	  grub_free (buf);
	  grub_xen_sched_op (SCHEDOP_yield, 0);
	}
      grub_free (buf);

      grub_snprintf (fdir, sizeof (fdir), "%s/ring-ref",
		     virtdisks[i].frontend_dir);
      grub_xenstore_write_file (fdir, NULL, 0);

      grub_snprintf (fdir, sizeof (fdir), "%s/event-channel",
		     virtdisks[i].frontend_dir);
      grub_xenstore_write_file (fdir, NULL, 0);

      grub_xen_free_shared_page (virtdisks[i].dma_page);
      grub_xen_free_shared_page (virtdisks[i].shared_page);

      grub_xen_event_channel_op (EVTCHNOP_close, &close_op);

      /* Prepare for handoff.  */
      grub_snprintf (fdir, sizeof (fdir), "%s/state",
		     virtdisks[i].frontend_dir);
      grub_xenstore_write_file (fdir, "1", 1);
    }
}
