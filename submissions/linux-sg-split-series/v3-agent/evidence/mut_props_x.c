/* tools/mutate.py driver for test_props_x.c (150 cases per property). */
#define main props_main
#include "test_props_x.c"
#undef main
#include "mut_guard.h"
int main (void) { mut_guard (); char * a[] = { "mut_props_x", "150", 0 }; return props_main (2, a); }
