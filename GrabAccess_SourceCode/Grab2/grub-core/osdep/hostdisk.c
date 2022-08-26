#ifdef __linux__
#include "linux/hostdisk.c"
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include "freebsd/hostdisk.c"
#elif defined(__NetBSD__) || defined(__OpenBSD__)
#include "bsd/hostdisk.c"
#elif defined(__APPLE__)
#include "apple/hostdisk.c"
#elif defined(__sun__)
#include "sun/hostdisk.c"
#elif defined(__GNU__)
#include "hurd/hostdisk.c"
#elif defined(__CYGWIN__) || defined(__MINGW32__)
#include "windows/hostdisk.c"
#elif defined(__AROS__)
#include "aros/hostdisk.c"
#elif defined (__HAIKU__)
#include "haiku/hostdisk.c"
#else
# warning "No hostdisk OS-specific functions is available for your system. Device detection may not work properly."
#include "basic/hostdisk.c"
#endif
