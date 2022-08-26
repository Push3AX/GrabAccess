#ifdef __linux__
#include "linux/blocklist.c"
#elif defined (__MINGW32__) || defined (__CYGWIN__)
#include "windows/blocklist.c"
#else
#include "generic/blocklist.c"
#endif
