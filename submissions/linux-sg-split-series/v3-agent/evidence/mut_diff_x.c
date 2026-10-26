/* tools/mutate.py driver for test_diff_x.c (600 cases per property). */
#define main diff_main
#include "test_diff_x.c"
#undef main
#include "mut_guard.h"
int main (void) { mut_guard (); char * a[] = { "mut_diff_x", "600", 0 }; return diff_main (2, a); }
