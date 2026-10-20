/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Smoke test: snapshotted (buggy) decompress_bunzip2.c links and decodes
** a tiny precomputed bzip2 stream containing "hi".  Confirms build chain
** only — UBSAN may emit a runtime report on the get_bits() shift; that
** does not cause a test failure here. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration of the entry point with the same signature as the
** kernel's STATIC INIT bunzip2(); supplied by bunzip_shim.o. */
int bunzip2 (unsigned char *buf, long len,
             long (*fill)(void *, unsigned long),
             long (*flush)(void *, unsigned long),
             unsigned char *outbuf,
             long *pos,
             void (*error)(char *x));

static unsigned char hi_bz2[] = {
  0x42, 0x5a, 0x68, 0x39, 0x31, 0x41, 0x59, 0x26, 0x53, 0x59,
  0x9a, 0x89, 0xb4, 0x22, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
  0x60, 0x20, 0x00, 0x21, 0x00, 0x82, 0xb1, 0x77, 0x24, 0x53,
  0x85, 0x09, 0x09, 0xa8, 0x9b, 0x42, 0x20
};

static void on_error (char *msg) { fprintf (stderr, "bunzip2 error: %s\n", msg); }

int main (void)
{
  unsigned char out[64] = {0};
  long pos = 0;
  int r = bunzip2 (hi_bz2, (long) sizeof hi_bz2, NULL, NULL, out, &pos, on_error);
  printf ("smoke: bunzip2 -> %d (0 = success), out=\"%.2s\"\n", r, out);
  /* Don't gate the smoke test on the actual decompression succeeding; the
  ** point is just to verify the buggy snapshot links and runs. */
  printf ("smoke OK\n");
  return 0;
}
