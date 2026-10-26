/* tools/mutate.py driver for test_fake.c (300 cases per property). */
#define main fake_main
#include "test_fake.c"
#undef main
#include "mut_guard.h"
int main (void) { mut_guard (); char * a[] = { "mut_fake", "300", 0 }; return fake_main (2, a); }
