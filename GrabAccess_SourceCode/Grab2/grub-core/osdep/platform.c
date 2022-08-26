#ifdef __linux__
#include "linux/platform.c"
#elif defined (__MINGW32__) || defined (__CYGWIN__)
#include "windows/platform.c"
#elif defined (__MINGW32__) || defined (__CYGWIN__) || defined (__AROS__)
#include "basic/no_platform.c"
#else
#include "basic/platform.c"
#endif
