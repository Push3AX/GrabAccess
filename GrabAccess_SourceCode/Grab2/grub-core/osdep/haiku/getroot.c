#include <config.h>
#include <config-util.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <grub/util/misc.h>
#include <grub/emu/hostdisk.h>
#include <grub/emu/getroot.h>
#include <fs_info.h>
#include <Drivers.h>
#include <StorageDefs.h>

enum grub_dev_abstraction_types
grub_util_get_dev_abstraction_os (const char *os_dev __attribute__((unused)))
{
  return GRUB_DEV_ABSTRACTION_NONE;
}

int
grub_util_pull_device_os (const char *os_dev __attribute__ ((unused)),
			  enum grub_dev_abstraction_types ab __attribute__ ((unused)))
{
  return 0;
}

char *
grub_util_get_grub_dev_os (const char *os_dev __attribute__ ((unused)))
{
  return NULL;
}

char **
grub_guess_root_devices (const char *dir_in)
{
  dev_t dv = dev_for_path (dir_in);
  fs_info inf;
  char **ret;
  if (fs_stat_dev (dv, &inf) != B_OK)
    return NULL;
  ret = xmalloc (2 * sizeof (ret[0]));
  ret[0] = xstrdup (inf.device_name);
  ret[1] = NULL;
  return ret;
}

grub_disk_addr_t
grub_util_find_partition_start_os (const char *dev)
{
  partition_info part;
  grub_disk_addr_t ret;
  int fd = open (dev, O_RDONLY);
  if (fd < 0)
    return 0;
  if (ioctl (fd, B_GET_PARTITION_INFO, &part, sizeof (part)) < 0)
    {
      close (fd);
      return 0;
    }
  ret = part.offset;
  close (fd);
  fd = open (part.device, O_RDONLY);

  device_geometry geo;
  if (ioctl (fd, B_GET_GEOMETRY, &geo, sizeof (geo)) < 0)
    return 0;
  ret /= geo.bytes_per_sector ? : 512;
  close (fd);  
  return ret;
}

char *
grub_util_part_to_disk (const char *os_dev,
			struct stat *st __attribute__ ((unused)),
			int *is_part)
{
  char *ret;
  partition_info part;
  int fd = open (os_dev, O_RDONLY);
  *is_part = 0;

  if (fd < 0)
    return xstrdup (os_dev);
  if (ioctl (fd, B_GET_PARTITION_INFO, &part, sizeof (part)) < 0)
    {
      close (fd);
      return xstrdup (os_dev);
    }
  ret = xstrdup (part.device);
  close (fd);
  *is_part=1;
  return ret;
}

int
grub_util_biosdisk_is_floppy (grub_disk_t disk)
{
  const char *dname;

  dname = grub_util_biosdisk_get_osdev (disk);

  return (strncmp (dname, "/dev/disk/floppy/", sizeof ("/dev/disk/floppy/") - 1)
	  == 0);
}
