#if defined (__MINGW32__) || defined (__CYGWIN__)
#include "windows/relpath.c"
#elif defined (__AROS__)
#include "aros/relpath.c"
#else
#include "unix/relpath.c"
#endif
