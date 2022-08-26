#if defined (__MINGW32__) && !defined (__CYGWIN__)
#include "windows/config.c"
#elif defined (__AROS__)
#include "aros/config.c"
#else
#include "unix/config.c"
#endif
