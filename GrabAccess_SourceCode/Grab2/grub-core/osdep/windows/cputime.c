#include <config.h>
#include <config-util.h>

#include <grub/emu/misc.h>
#include <windows.h>

grub_uint64_t
grub_util_get_cpu_time_ms (void)
{
  FILETIME cr, ex, ke, us;
  ULARGE_INTEGER us_ul;

  GetProcessTimes (GetCurrentProcess (), &cr, &ex, &ke, &us);
  us_ul.LowPart = us.dwLowDateTime;
  us_ul.HighPart = us.dwHighDateTime;

  return us_ul.QuadPart / 10000;
}

