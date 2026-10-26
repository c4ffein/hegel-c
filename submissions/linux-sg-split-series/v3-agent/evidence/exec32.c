/* 32-bit side of P-ABI32 (test_fake.c): reads one fdesc on stdin, runs sg_split()
** on fake pages, prints the canonical output.  Built -m32 -static against the same
** sg_split.c as the 64-bit test (see build32.sh). */
#include "sgfake.h"

int main (void)
{
  fdesc d;
  fbuilt B;
  fresult r;
  static char out[1 << 15];
  if (fdesc_scan (&d, stdin)) { fprintf (stderr, "exec32: bad description\n"); return 2; }
  fbuild (&d, &B);
  fcall (&d, &B, &r);
  fcanon (&d, &r, out, sizeof out);
  fputs (out, stdout);
  return 0;
}
