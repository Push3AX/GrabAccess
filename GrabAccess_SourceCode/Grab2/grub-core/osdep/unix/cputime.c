#include <config.h>
#include <config-util.h>

#include <sys/times.h>
#include <unistd.h>
#include <grub/emu/misc.h>

grub_uint64_t
grub_util_get_cpu_time_ms (void)
{
  struct tms tm;
  static long sc_clk_tck;
  if (!sc_clk_tck)
    {
      sc_clk_tck = sysconf(_SC_CLK_TCK);
      if (sc_clk_tck <= 0)
	sc_clk_tck = 1000;
    }

  times (&tm); 
  return (tm.tms_utime * 1000ULL) / sc_clk_tck;
}
