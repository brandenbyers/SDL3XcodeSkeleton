#ifndef main_h
#define main_h

#include <stdio.h>

/* Include signal handling for tvOS */
#if defined(__APPLE__) && TARGET_OS_TV
#include <signal.h>
#endif

#endif /* main_h */
