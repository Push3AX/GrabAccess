#if defined (__MINGW32__) && !defined (__CYGWIN__)
#include "windows/password.c"
#else
#include "unix/password.c"
#endif
