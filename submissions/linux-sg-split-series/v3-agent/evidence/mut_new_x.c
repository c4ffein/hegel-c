/* tools/mutate.py driver for test_x_new.c (150 cases per property). */
#define main new_main
#include "test_x_new.c"
#undef main
#include "mut_guard.h"
int main (void) { mut_guard (); char * a[] = { "mut_new_x", "150", 0 }; return new_main (2, a); }
