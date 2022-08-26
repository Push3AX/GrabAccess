/* obdisk.c - Open Boot disk access. */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019 Free Software Foundation, Inc.
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
#include <grub/env.h>
#include <grub/i18n.h>
#include <grub/kernel.h>
#include <grub/list.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/scsicmd.h>
#include <grub/time.h>
#include <grub/ieee1275/ieee1275.h>
#include <grub/ieee1275/obdisk.h>

#define IEEE1275_DEV        "ieee1275/"
#define IEEE1275_DISK_ALIAS "/disk@"

struct disk_dev
{
  struct disk_dev         *next;
  struct disk_dev         **prev;
  char                    *name;
  char                    *raw_name;
  char                    *grub_devpath;
  char                    *grub_alias_devpath;
  grub_ieee1275_ihandle_t ihandle;
  grub_uint32_t           block_size;
  grub_uint64_t           num_blocks;
  unsigned int            log_sector_size;
  grub_uint32_t           opened;
  grub_uint32_t           valid;
  grub_uint32_t           boot_dev;
};

struct parent_dev
{
  struct parent_dev       *next;
  struct parent_dev       **prev;
  char                    *name;
  char                    *type;
  grub_ieee1275_ihandle_t ihandle;
  grub_uint32_t           address_cells;
};

static struct grub_scsi_test_unit_ready tur =
{
  .opcode    = grub_scsi_cmd_test_unit_ready,
  .lun       = 0,
  .reserved1 = 0,
  .reserved2 = 0,
  .reserved3 = 0,
  .control   = 0,
};

static int disks_enumerated;
static struct disk_dev *disk_devs;
static struct parent_dev *parent_devs;

static const char *block_blacklist[] = {
  /* Requires additional work in grub before being able to be used. */
  "/iscsi-hba",
  /* This block device should never be used by grub. */
  "/reboot-memory@0",
  0
};

#define STRCMP(a, b) ((a) && (b) && (grub_strcmp (a, b) == 0))

static char *
strip_ob_partition (char *path)
{
  char *sptr;

  sptr = grub_strstr (path, ":");

  if (sptr != NULL)
    *sptr = '\0';

  return path;
}

static void
escape_commas (const char *src, char *dest)
{
  const char *iptr;

  for (iptr = src; *iptr; )
    {
      if (*iptr == ',')
	*dest++ ='\\';

      *dest++ = *iptr++;
    }

  *dest = '\0';
}

static int
count_commas (const char *src)
{
  int count = 0;

  for ( ; *src; src++)
    if (*src == ',')
      count++;

  return count;
}


static char *
decode_grub_devname (const char *name)
{
  char *devpath = grub_malloc (grub_strlen (name) + 1);
  char *p, c;

  if (devpath == NULL)
    return NULL;

  /* Un-escape commas. */
  p = devpath;
  while ((c = *name++) != '\0')
    {
      if (c == '\\' && *name == ',')
	{
	  *p++ = ',';
	  name++;
	}
      else
	*p++ = c;
    }

  *p++ = '\0';

  return devpath;
}

static char *
encode_grub_devname (const char *path)
{
  char *encoding, *optr;

  if (path == NULL)
    return NULL;

  encoding = grub_malloc (sizeof (IEEE1275_DEV) + count_commas (path) +
                          grub_strlen (path) + 1);

  if (encoding == NULL)
    {
      grub_print_error ();
      return NULL;
    }

  optr = grub_stpcpy (encoding, IEEE1275_DEV);
  escape_commas (path, optr);
  return encoding;
}

static char *
get_parent_devname (const char *devname)
{
  char *parent, *pptr;

  parent = grub_strdup (devname);

  if (parent == NULL)
    {
      grub_print_error ();
      return NULL;
    }

  pptr = grub_strstr (parent, IEEE1275_DISK_ALIAS);

  if (pptr != NULL)
    *pptr = '\0';

  return parent;
}

static void
free_parent_dev (struct parent_dev *parent)
{
  if (parent != NULL)
    {
      grub_free (parent->name);
      grub_free (parent->type);
      grub_free (parent);
    }
}

static struct parent_dev *
init_parent (const char *parent)
{
  struct parent_dev *op;

  op = grub_zalloc (sizeof (struct parent_dev));

  if (op == NULL)
    {
      grub_print_error ();
      return NULL;
    }

  op->name = grub_strdup (parent);
  op->type = grub_malloc (IEEE1275_MAX_PROP_LEN);

  if ((op->name == NULL) || (op->type == NULL))
    {
      grub_print_error ();
      free_parent_dev (op);
      return NULL;
    }

  return op;
}

static struct parent_dev *
open_new_parent (const char *parent)
{
  struct parent_dev *op = init_parent(parent);
  grub_ieee1275_ihandle_t ihandle;
  grub_ieee1275_phandle_t phandle;
  grub_uint32_t address_cells = 2;

  if (op == NULL)
    return NULL;

  grub_ieee1275_open (parent, &ihandle);

  if (ihandle == 0)
    {
      grub_error (GRUB_ERR_BAD_DEVICE, "unable to open %s", parent);
      grub_print_error ();
      free_parent_dev (op);
      return NULL;
    }

  if (grub_ieee1275_instance_to_package (ihandle, &phandle))
    {
      grub_error (GRUB_ERR_BAD_DEVICE, "unable to get parent %s", parent);
      grub_print_error ();
      free_parent_dev (op);
      return NULL;
    }

  /*
   * IEEE Std 1275-1994 page 110: A missing "address-cells" property
   * signifies that the number of address cells is two. So ignore on error.
   */
  if (grub_ieee1275_get_integer_property (phandle, "#address-cells",
					  &address_cells,
					  sizeof (address_cells), 0) != 0)
    address_cells = 2;

  grub_ieee1275_get_property (phandle, "device_type", op->type,
                              IEEE1275_MAX_PROP_LEN, NULL);

  op->ihandle = ihandle;
  op->address_cells = address_cells;
  return op;
}

static struct parent_dev *
open_parent (const char *parent)
{
  struct parent_dev *op;

  op = grub_named_list_find (GRUB_AS_NAMED_LIST (parent_devs), parent);

  if (op == NULL)
    {
      op = open_new_parent (parent);

      if (op != NULL)
        grub_list_push (GRUB_AS_LIST_P (&parent_devs), GRUB_AS_LIST (op));
    }

  return op;
}

static void
display_parents (void)
{
  struct parent_dev *parent;

  grub_printf ("-------------------- PARENTS --------------------\n");

  FOR_LIST_ELEMENTS (parent, parent_devs)
    {
      grub_printf ("name:         %s\n", parent->name);
      grub_printf ("type:         %s\n", parent->type);
      grub_printf ("address_cells %x\n", parent->address_cells);
    }

  grub_printf ("-------------------------------------------------\n");
}

static char *
canonicalise_4cell_ua (grub_ieee1275_ihandle_t ihandle, char *unit_address)
{
  grub_uint32_t phy_lo, phy_hi, lun_lo, lun_hi;
  int valid_phy = 0;
  grub_size_t size;
  char *canon = NULL;

  valid_phy = grub_ieee1275_decode_unit4 (ihandle, unit_address,
                                          grub_strlen (unit_address), &phy_lo,
                                          &phy_hi, &lun_lo, &lun_hi);

  if ((valid_phy == 0) && (phy_hi != 0xffffffff))
    canon = grub_ieee1275_encode_uint4 (ihandle, phy_lo, phy_hi,
                                        lun_lo, lun_hi, &size);

  return canon;
}

static char *
canonicalise_disk (const char *devname)
{
  char *canon, *parent;
  struct parent_dev *op;

  canon = grub_ieee1275_canonicalise_devname (devname);

  if (canon == NULL)
    {
      /* This should not happen. */
      grub_error (GRUB_ERR_BAD_DEVICE, "canonicalise devname failed");
      grub_print_error ();
      return NULL;
    }

  /* Don't try to open the parent of a virtual device. */
  if (grub_strstr (canon, "virtual-devices"))
    return canon;

  parent = get_parent_devname (canon);

  if (parent == NULL)
    return NULL;

  op = open_parent (parent);

  /*
   * Devices with 4 address cells can have many different types of addressing
   * (phy, wwn, and target lun). Use the parents encode-unit / decode-unit
   * to find the true canonical name.
   */
  if ((op) && (op->address_cells == 4))
    {
      char *unit_address, *real_unit_address, *real_canon;
      grub_size_t real_unit_str_len;

      unit_address = grub_strstr (canon, IEEE1275_DISK_ALIAS);
      unit_address += grub_strlen (IEEE1275_DISK_ALIAS);

      if (unit_address == NULL)
        {
          /*
           * This should not be possible, but return the canonical name for
           * the non-disk block device.
           */
          grub_free (parent);
          return (canon);
        }

      real_unit_address = canonicalise_4cell_ua (op->ihandle, unit_address);

      if (real_unit_address == NULL)
        {
          /*
           * This is not an error, since this function could be called with a devalias
           * containing a drive that isn't installed in the system.
           */
          grub_free (parent);
          return NULL;
        }

      real_unit_str_len = grub_strlen (op->name) + sizeof (IEEE1275_DISK_ALIAS)
                          + grub_strlen (real_unit_address);

      real_canon = grub_malloc (real_unit_str_len);

      grub_snprintf (real_canon, real_unit_str_len, "%s/disk@%s",
                     op->name, real_unit_address);

      grub_free (canon);
      canon = real_canon;
    }

  grub_free (parent);
  return (canon);
}

static struct disk_dev *
add_canon_disk (const char *cname)
{
  struct disk_dev *dev;

  dev = grub_zalloc (sizeof (struct disk_dev));

  if (dev == NULL)
    goto failed;

  if (grub_ieee1275_test_flag (GRUB_IEEE1275_FLAG_RAW_DEVNAMES))
    {
      /*
       * Append :nolabel to the end of all SPARC disks.
       * nolabel is mutually exclusive with all other
       * arguments and allows a client program to open
       * the entire (raw) disk. Any disk label is ignored.
       */
      dev->raw_name = grub_malloc (grub_strlen (cname) + sizeof (":nolabel"));

      if (dev->raw_name == NULL)
        goto failed;

      grub_snprintf (dev->raw_name, grub_strlen (cname) + sizeof (":nolabel"),
                     "%s:nolabel", cname);
    }

  /*
   * Don't use grub_ieee1275_encode_devname here, the devpath in grub.cfg doesn't
   * understand device aliases, which the layer above sometimes sends us.
   */
  dev->grub_devpath = encode_grub_devname(cname);

  if (dev->grub_devpath == NULL)
    goto failed;

  dev->name = grub_strdup (cname);

  if (dev->name == NULL)
    goto failed;

  dev->valid = 1;
  grub_list_push (GRUB_AS_LIST_P (&disk_devs), GRUB_AS_LIST (dev));
  return dev;

 failed:
  grub_print_error ();

  if (dev != NULL)
    {
      grub_free (dev->name);
      grub_free (dev->grub_devpath);
      grub_free (dev->raw_name);
    }

  grub_free (dev);
  return NULL;
}

static grub_err_t
add_disk (const char *path)
{
  grub_err_t ret = GRUB_ERR_NONE;
  struct disk_dev *dev;
  char *canon;

  canon = canonicalise_disk (path);
  dev = grub_named_list_find (GRUB_AS_NAMED_LIST (disk_devs), canon);

  if ((canon != NULL) && (dev == NULL))
    {
      struct disk_dev *ob_device;

      ob_device = add_canon_disk (canon);

      if (ob_device == NULL)
        ret = grub_error (GRUB_ERR_OUT_OF_MEMORY, "failure to add disk");
    }
  else if (dev != NULL)
    dev->valid = 1;

  grub_free (canon);
  return (ret);
}

static grub_err_t
grub_obdisk_read (grub_disk_t disk, grub_disk_addr_t sector,
		  grub_size_t size, char *dest)
{
  grub_err_t ret = GRUB_ERR_NONE;
  struct disk_dev *dev;
  unsigned long long pos;
  grub_ssize_t result = 0;

  if (disk->data == NULL)
    return grub_error (GRUB_ERR_BAD_DEVICE, "invalid disk data");

  dev = (struct disk_dev *)disk->data;
  pos = sector << disk->log_sector_size;
  grub_ieee1275_seek (dev->ihandle, pos, &result);

  if (result < 0)
    {
      dev->opened = 0;
      return grub_error (GRUB_ERR_READ_ERROR, "seek error, can't seek block %llu",
                         (long long) sector);
    }

  grub_ieee1275_read (dev->ihandle, dest, size << disk->log_sector_size,
                      &result);

  if (result != (grub_ssize_t) (size << disk->log_sector_size))
    {
      dev->opened = 0;
      return grub_error (GRUB_ERR_READ_ERROR, N_("failure reading sector 0x%llx "
                                                 "from `%s'"),
                         (unsigned long long) sector,
                         disk->name);
    }
  return ret;
}

static void
grub_obdisk_close (grub_disk_t disk)
{
  grub_memset (disk, 0, sizeof (*disk));
}

static void
scan_usb_disk (const char *parent)
{
  struct parent_dev *op;
  grub_ssize_t result;

  op = open_parent (parent);

  if (op == NULL)
    {
      grub_error (GRUB_ERR_BAD_DEVICE, "unable to open %s", parent);
      grub_print_error ();
      return;
    }

  if ((grub_ieee1275_set_address (op->ihandle, 0, 0) == 0) &&
      (grub_ieee1275_no_data_command (op->ihandle, &tur, &result) == 0) &&
      (result == 0))
    {
      char *buf;

      buf = grub_malloc (IEEE1275_MAX_PATH_LEN);

      if (buf == NULL)
        {
          grub_error (GRUB_ERR_OUT_OF_MEMORY, "disk scan failure");
          grub_print_error ();
          return;
        }

      grub_snprintf (buf, IEEE1275_MAX_PATH_LEN, "%s/disk@0", parent);
      add_disk (buf);
      grub_free (buf);
    }
}

static void
scan_nvme_disk (const char *path)
{
  char *buf;

  buf = grub_malloc (IEEE1275_MAX_PATH_LEN);

  if (buf == NULL)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, "disk scan failure");
      grub_print_error ();
      return;
    }

  grub_snprintf (buf, IEEE1275_MAX_PATH_LEN, "%s/disk@1", path);
  add_disk (buf);
  grub_free (buf);
}

static void
scan_sparc_sas_2cell (struct parent_dev *op)
{
  grub_ssize_t result;
  grub_uint8_t tgt;
  char *buf;

  buf = grub_malloc (IEEE1275_MAX_PATH_LEN);

  if (buf == NULL)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, "disk scan failure");
      grub_print_error ();
      return;
    }

  for (tgt = 0; tgt < 0xf; tgt++)
    {

      if ((grub_ieee1275_set_address(op->ihandle, tgt, 0) == 0) &&
          (grub_ieee1275_no_data_command (op->ihandle, &tur, &result) == 0) &&
          (result == 0))
        {

          grub_snprintf (buf, IEEE1275_MAX_PATH_LEN, "%s/disk@%"
                         PRIxGRUB_UINT32_T, op->name, tgt);

          add_disk (buf);
        }
    }
}

static void
scan_sparc_sas_4cell (struct parent_dev *op)
{
  grub_uint16_t exp;
  grub_uint8_t phy;
  char *buf;

  buf = grub_malloc (IEEE1275_MAX_PATH_LEN);

  if (buf == NULL)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, "disk scan failure");
      grub_print_error ();
      return;
    }

  /*
   * Cycle thru the potential for dual ported SAS disks
   * behind a SAS expander.
   */
  for (exp = 0; exp <= 0x100; exp+=0x100)

    /* The current limit is 32 disks on a phy. */
    for (phy = 0; phy < 0x20; phy++)
      {
        char *canon = NULL;

        grub_snprintf (buf, IEEE1275_MAX_PATH_LEN, "p%" PRIxGRUB_UINT32_T ",0",
                       exp | phy);

        canon = canonicalise_4cell_ua (op->ihandle, buf);

        if (canon != NULL)
          {
            grub_snprintf (buf, IEEE1275_MAX_PATH_LEN, "%s/disk@%s",
                           op->name, canon);

            add_disk (buf);
            grub_free (canon);
          }
      }

  grub_free (buf);
}

static void
scan_sparc_sas_disk (const char *parent)
{
  struct parent_dev *op;

  op = open_parent (parent);

  if ((op != NULL) && (op->address_cells == 4))
    scan_sparc_sas_4cell (op);
  else if ((op != NULL) && (op->address_cells == 2))
    scan_sparc_sas_2cell (op);
}

static void
iterate_devtree (const struct grub_ieee1275_devalias *alias)
{
  struct grub_ieee1275_devalias child;

  if ((grub_strcmp (alias->type, "scsi-2") == 0) ||
      (grub_strcmp (alias->type, "scsi-sas") == 0))
    return scan_sparc_sas_disk (alias->path);

  else if (grub_strcmp (alias->type, "nvme") == 0)
    return scan_nvme_disk (alias->path);

  else if (grub_strcmp (alias->type, "scsi-usb") == 0)
    return scan_usb_disk (alias->path);

  else if (grub_strcmp (alias->type, "block") == 0)
    {
      const char **bl = block_blacklist;

      while (*bl != NULL)
        {
          if (grub_strstr (alias->path, *bl))
            return;
          bl++;
        }

      add_disk (alias->path);
      return;
    }

  FOR_IEEE1275_DEVCHILDREN (alias->path, child)
    iterate_devtree (&child);
}

static void
enumerate_disks (void)
{
  struct grub_ieee1275_devalias alias;

  FOR_IEEE1275_DEVCHILDREN("/", alias)
    iterate_devtree (&alias);
}

static grub_err_t
add_bootpath (void)
{
  struct disk_dev *ob_device;
  grub_err_t ret = GRUB_ERR_NONE;
  char *dev, *alias;
  char *type;

  dev = grub_ieee1275_get_boot_dev ();

  if (dev == NULL)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "failure adding boot device");

  type = grub_ieee1275_get_device_type (dev);

  if (type == NULL)
    {
      grub_free (dev);
      return grub_error (GRUB_ERR_OUT_OF_MEMORY, "failure adding boot device");
    }

  alias = NULL;

  if (grub_strcmp (type, "network") != 0)
    {
      dev = strip_ob_partition (dev);
      ob_device = add_canon_disk (dev);

      if (ob_device == NULL)
        ret =  grub_error (GRUB_ERR_OUT_OF_MEMORY, "failure adding boot device");

      ob_device->valid = 1;

      alias = grub_ieee1275_get_devname (dev);

      if (alias && grub_strcmp (alias, dev) != 0)
        ob_device->grub_alias_devpath = grub_ieee1275_encode_devname (dev);

      ob_device->boot_dev = 1;
    }

  grub_free (type);
  grub_free (dev);
  grub_free (alias);
  return ret;
}

static void
enumerate_aliases (void)
{
  struct grub_ieee1275_devalias alias;

  /*
   * Some block device aliases are not in canonical form
   *
   * For example:
   *
   *   disk3                    /pci@301/pci@1/scsi@0/disk@p3
   *   disk2                    /pci@301/pci@1/scsi@0/disk@p2
   *   disk1                    /pci@301/pci@1/scsi@0/disk@p1
   *   disk                     /pci@301/pci@1/scsi@0/disk@p0
   *   disk0                    /pci@301/pci@1/scsi@0/disk@p0
   *
   * None of these devices are in canonical form.
   *
   * Also, just because there is a devalias, doesn't mean there is a disk
   * at that location.  And a valid boot block device doesn't have to have
   * a devalias at all.
   *
   * At this point, all valid disks have been found in the system
   * and devaliases that point to canonical names are stored in the
   * disk_devs list already.
   */
  FOR_IEEE1275_DEVALIASES (alias)
    {
      struct disk_dev *dev;
      char *canon;

      if (grub_strcmp (alias.type, "block") != 0)
        continue;

      canon = canonicalise_disk (alias.name);

      if (canon == NULL)
        /* This is not an error, a devalias could point to a nonexistent disk. */
        continue;

      dev = grub_named_list_find (GRUB_AS_NAMED_LIST (disk_devs), canon);

      if (dev != NULL)
        {
          /*
           * If more than one alias points to the same device,
           * remove the previous one unless it is the boot dev,
           * since the upper level will use the first one. The reason
           * all the others are redone is in the case of hot-plugging
           * a disk.  If the boot disk gets hot-plugged, it will come
           * thru here with a different name without the boot_dev flag
           * set.
           */
          if ((dev->boot_dev) && (dev->grub_alias_devpath))
            continue;

          grub_free (dev->grub_alias_devpath);
          dev->grub_alias_devpath = grub_ieee1275_encode_devname (alias.path);
        }
      grub_free (canon);
    }
}

static void
display_disks (void)
{
  struct disk_dev *dev;

  grub_printf ("--------------------- DISKS ---------------------\n");

  FOR_LIST_ELEMENTS (dev, disk_devs)
    {
      grub_printf ("name:                 %s\n", dev->name);
      grub_printf ("grub_devpath:         %s\n", dev->grub_devpath);
      grub_printf ("grub_alias_devpath:   %s\n", dev->grub_alias_devpath);
      grub_printf ("valid:                %s\n", (dev->valid) ? "yes" : "no");
      grub_printf ("boot_dev:             %s\n", (dev->boot_dev) ? "yes" : "no");
      grub_printf ("opened:               %s\n", (dev->ihandle) ? "yes" : "no");
      grub_printf ("block size:           %" PRIuGRUB_UINT32_T "\n",
                   dev->block_size);
      grub_printf ("num blocks:           %" PRIuGRUB_UINT64_T "\n",
                   dev->num_blocks);
      grub_printf ("log sector size:      %" PRIuGRUB_UINT32_T "\n",
                   dev->log_sector_size);
      grub_printf ("\n");
    }

  grub_printf ("-------------------------------------------------\n");
}

static void
display_stats (void)
{
  const char *debug = grub_env_get ("debug");

  if (debug == NULL)
    return;

  if (grub_strword (debug, "all") || grub_strword (debug, "obdisk"))
    {
      display_parents ();
      display_disks ();
    }
}

static void
invalidate_all_disks (void)
{
  struct disk_dev *dev = NULL;

  if (disks_enumerated != 0)
    FOR_LIST_ELEMENTS (dev, disk_devs)
      dev->valid = 0;
}

static struct disk_dev *
find_legacy_grub_devpath (const char *name)
{
  struct disk_dev *dev = NULL;
  char *canon, *devpath = NULL;

  devpath = decode_grub_devname (name + sizeof ("ieee1275"));
  canon = canonicalise_disk (devpath);

  if (canon != NULL)
    dev = grub_named_list_find (GRUB_AS_NAMED_LIST (disk_devs), canon);

  grub_free (devpath);
  grub_free (canon);
  return dev;
}

static void
enumerate_devices (void)
{
  invalidate_all_disks ();
  enumerate_disks ();
  enumerate_aliases ();
  disks_enumerated = 1;
  display_stats ();
}

static struct disk_dev *
find_grub_devpath_real (const char *name)
{
  struct disk_dev *dev = NULL;

  FOR_LIST_ELEMENTS (dev, disk_devs)
    {
      if ((STRCMP (dev->grub_devpath, name))
        || (STRCMP (dev->grub_alias_devpath, name)))
        break;
    }

  return dev;
}

static struct disk_dev *
find_grub_devpath (const char *name)
{
  struct disk_dev *dev = NULL;
  int enumerated;

  do {
    enumerated = disks_enumerated;
    dev = find_grub_devpath_real (name);

    if (dev != NULL)
      break;

    dev = find_legacy_grub_devpath (name);

    if (dev != NULL)
      break;

    enumerate_devices ();
  } while (enumerated == 0);

  return dev;
}

static int
grub_obdisk_iterate (grub_disk_dev_iterate_hook_t hook, void *hook_data,
		     grub_disk_pull_t pull)
{
  struct disk_dev *dev;
  const char *name;

  if (pull != GRUB_DISK_PULL_NONE)
    return 0;

  enumerate_devices ();

  FOR_LIST_ELEMENTS (dev, disk_devs)
    {
      if (dev->valid == 1)
	{
	  if (dev->grub_alias_devpath)
	    name = dev->grub_alias_devpath;
          else
	    name = dev->grub_devpath;

          if (hook (name, hook_data))
            return 1;
	}
    }

  return 0;
}

static grub_err_t
grub_obdisk_open (const char *name, grub_disk_t disk)
{
  grub_ieee1275_ihandle_t ihandle = 0;
  struct disk_dev *dev = NULL;

  if (grub_strncmp (name, IEEE1275_DEV, sizeof (IEEE1275_DEV) - 1) != 0)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "not IEEE1275 device");

  dev = find_grub_devpath (name);

  if (dev == NULL)
    {
      grub_printf ("UNKNOWN DEVICE: %s\n", name);
      return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "%s", name);
    }

  if (dev->opened == 0)
    {
      if (dev->raw_name != NULL)
        grub_ieee1275_open (dev->raw_name, &ihandle);
      else
        grub_ieee1275_open (dev->name, &ihandle);

      if (ihandle == 0)
        {
          grub_printf ("Can't open device %s\n", name);
          return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "can't open device %s", name);
        }

      dev->block_size = grub_ieee1275_get_block_size (ihandle);
      dev->num_blocks = grub_ieee1275_num_blocks (ihandle);

      if (dev->num_blocks == 0)
        dev->num_blocks = grub_ieee1275_num_blocks64 (ihandle);

      if (dev->num_blocks == 0)
        dev->num_blocks = GRUB_DISK_SIZE_UNKNOWN;

      if (dev->block_size != 0)
        {
          for (dev->log_sector_size = 0;
               (1U << dev->log_sector_size) < dev->block_size;
               dev->log_sector_size++);
        }
      else
        dev->log_sector_size = 9;

      dev->ihandle = ihandle;
      dev->opened = 1;
    }

  disk->total_sectors = dev->num_blocks;
  disk->id = dev->ihandle;
  disk->data = dev;
  disk->log_sector_size = dev->log_sector_size;
  return GRUB_ERR_NONE;
}


static struct grub_disk_dev grub_obdisk_dev =
  {
    .name    = "obdisk",
    .id      = GRUB_DISK_DEVICE_OBDISK_ID,
    .disk_iterate = grub_obdisk_iterate,
    .disk_open    = grub_obdisk_open,
    .disk_close   = grub_obdisk_close,
    .disk_read    = grub_obdisk_read,
  };

void
grub_obdisk_init (void)
{
  grub_disk_firmware_fini = grub_obdisk_fini;
  add_bootpath ();
  grub_disk_dev_register (&grub_obdisk_dev);
}

void
grub_obdisk_fini (void)
{
  struct disk_dev *dev;

  FOR_LIST_ELEMENTS (dev, disk_devs)
    {
      if (dev->opened != 0)
          grub_ieee1275_close (dev->ihandle);
    }

  grub_disk_dev_unregister (&grub_obdisk_dev);
}
