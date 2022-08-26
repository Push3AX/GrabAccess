#if defined (__MINGW32__) && !defined (__CYGWIN__)
#include "windows/emuconsole.c"
#else
#include "unix/emuconsole.c"
#endif
