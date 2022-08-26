#include <config.h>
#include <grub/util/install.h>
#include <grub/util/misc.h>

int 
grub_install_compress_gzip (const char *src, const char *dest)
{
  grub_util_error (_("no compression is available for your platform"));
}

int 
grub_install_compress_xz (const char *src, const char *dest)
{
  grub_util_error (_("no compression is available for your platform"));
}

int 
grub_install_compress_lzop (const char *src, const char *dest)
{
  grub_util_error (_("no compression is available for your platform"));
}
