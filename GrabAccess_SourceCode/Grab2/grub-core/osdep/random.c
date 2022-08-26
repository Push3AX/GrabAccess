#if defined (_WIN32) || defined (__CYGWIN__)
#include "windows/random.c"
#elif defined (__linux__) || defined (__FreeBSD__) \
  || defined (__FreeBSD_kernel__) || defined (__OpenBSD__) \
  || defined (__GNU__) || defined (__NetBSD__) \
  || defined (__APPLE__) || defined(__sun__) || defined (__HAIKU__)
#include "unix/random.c"
#else
#include "basic/random.c"
#endif
