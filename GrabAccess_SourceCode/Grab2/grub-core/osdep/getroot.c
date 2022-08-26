#ifdef __linux__
#include "linux/getroot.c"
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include "freebsd/getroot.c"
#elif defined(__NetBSD__) || defined(__OpenBSD__)
#include "bsd/getroot.c"
#elif defined(__APPLE__)
#include "apple/getroot.c"
#elif defined(__sun__)
#include "sun/getroot.c"
#elif defined(__GNU__)
#include "hurd/getroot.c"
#elif defined(__CYGWIN__) || defined (__MINGW32__)
#include "windows/getroot.c"
#elif defined(__AROS__)
#include "aros/getroot.c"
#elif defined (__HAIKU__)
#include "haiku/getroot.c"
#else
# warning "No getroot OS-specific functions is available for your system. Device detection may not work properly."
#include "basic/getroot.c"
#endif
