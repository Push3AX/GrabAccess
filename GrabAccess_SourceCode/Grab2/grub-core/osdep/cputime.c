#ifdef __MINGW32__
#include "windows/cputime.c"
#else
#include "unix/cputime.c"
#endif
