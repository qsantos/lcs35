#ifndef TIME_H
#define TIME_H

// C90
#include <stddef.h>

extern double real_clock(void);

extern int human_time_relative(char* s, size_t n, double secs);
extern int human_time_absolute(char* s, size_t n, double secs);
extern size_t human_time_both(char* s, size_t n, double secs);

#endif
