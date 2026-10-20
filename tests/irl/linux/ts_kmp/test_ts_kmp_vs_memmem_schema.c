/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Schema-API variant of test_ts_kmp_vs_memmem.c.  Same property,
** different input-drawing surface — `HEGEL_STRUCT` + `HEGEL_ARR_OF`
** + `HEGEL_LET`/`HEGEL_USE` instead of per-byte `hegel_draw_int`
** loops.  Lets us A/B compare:
**   - test_ts_kmp_vs_memmem.c           — primitive draws (~80 LOC body)
**   - test_ts_kmp_vs_memmem_schema.c    — schema draws (this file)
**
** The expected wins from schema:
**   1. Each byte array is one logical span — shrinker can drop the
**      whole array as a unit, not just shrink bytes individually.
**   2. The ARR_OF + LET binding makes "length-then-N-elements" a
**      one-liner; the ptr/count struct field discipline is checked
**      at schema-build time.
**   3. Failing-case traces show "haystack: [arr of N bytes]" as a
**      structured node, not 1024 anonymous draws.
**
** Skipping the biased-3sym test: a non-contiguous alphabet
** ({0x00,'a','b'}) doesn't fit any builtin schema element kind today
** without going through HEGEL_MAP_INT — that's a documented friction
** rather than a workaround in scope here. */

#define _GNU_SOURCE     /* memmem */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "hegel_c.h"
#include "hegel_gen.h"

extern unsigned int ts_kmp_search (const void *pattern, unsigned int plen,
                                   const void *data, unsigned int dlen);

#define MAX_HAY 1024
#define MAX_NDL  32

/* The shape we want hegel-c to fill in: pointer first, count second.
** Same Bag pattern as test_schema_array_of_ints.c. */
typedef struct {
  uint8_t * bytes;
  int       n;
} Bytes;

static unsigned int
oracle_memmem (const unsigned char *hay, size_t hlen,
               const unsigned char *ndl, size_t nlen)
{
  if (nlen == 0) return 0;
  if (nlen > hlen) return UINT_MAX;
  const unsigned char *p = memmem (hay, hlen, ndl, nlen);
  if (!p) return UINT_MAX;
  return (unsigned int) (p - hay);
}

/* Schemas built once at startup, reused across cases. */
static hegel_schema_t  hay_schema_random;
static hegel_schema_t  ndl_schema_random;
static hegel_schema_t  hay_schema_ab;
static hegel_schema_t  ndl_schema_ab;

static void
test_random_bytes_schema (hegel_testcase * tc)
{
  Bytes *hay, *ndl;
  hegel_shape *sh_hay = hegel_schema_draw (tc, hay_schema_random, (void **) &hay);
  hegel_shape *sh_ndl = hegel_schema_draw (tc, ndl_schema_random, (void **) &ndl);

  unsigned int kmp_off = ts_kmp_search (ndl->bytes, ndl->n, hay->bytes, hay->n);
  unsigned int mem_off = oracle_memmem  (hay->bytes, hay->n, ndl->bytes, ndl->n);

  HEGEL_ASSERT (kmp_off == mem_off,
                "kmp_off=%u mem_off=%u hlen=%d nlen=%d",
                kmp_off, mem_off, hay->n, ndl->n);

  hegel_shape_free (sh_hay);
  hegel_shape_free (sh_ndl);
}

static void
test_biased_ab_schema (hegel_testcase * tc)
{
  Bytes *hay, *ndl;
  hegel_shape *sh_hay = hegel_schema_draw (tc, hay_schema_ab, (void **) &hay);
  hegel_shape *sh_ndl = hegel_schema_draw (tc, ndl_schema_ab, (void **) &ndl);

  unsigned int kmp_off = ts_kmp_search (ndl->bytes, ndl->n, hay->bytes, hay->n);
  unsigned int mem_off = oracle_memmem  (hay->bytes, hay->n, ndl->bytes, ndl->n);

  HEGEL_ASSERT (kmp_off == mem_off,
                "kmp_off=%u mem_off=%u hlen=%d nlen=%d (alphabet=ab)",
                kmp_off, mem_off, hay->n, ndl->n);

  hegel_shape_free (sh_hay);
  hegel_shape_free (sh_ndl);
}

int
main (void)
{
  /* Random-byte alphabet (full 0..255 range). */
  {
    HEGEL_BINDING (hlen);
    hay_schema_random = HEGEL_STRUCT (Bytes,
        HEGEL_LET    (hlen, HEGEL_INT (1, MAX_HAY)),
        HEGEL_ARR_OF (HEGEL_USE (hlen), HEGEL_U8 (0, 255)),
        HEGEL_USE    (hlen));
  }
  {
    HEGEL_BINDING (nlen);
    ndl_schema_random = HEGEL_STRUCT (Bytes,
        HEGEL_LET    (nlen, HEGEL_INT (1, MAX_NDL)),
        HEGEL_ARR_OF (HEGEL_USE (nlen), HEGEL_U8 (0, 255)),
        HEGEL_USE    (nlen));
  }

  /* 'a','b' biased alphabet — 2 symbols stress KMP failure-table. */
  {
    HEGEL_BINDING (hlen);
    hay_schema_ab = HEGEL_STRUCT (Bytes,
        HEGEL_LET    (hlen, HEGEL_INT (1, MAX_HAY)),
        HEGEL_ARR_OF (HEGEL_USE (hlen), HEGEL_U8 (0x61, 0x62)),
        HEGEL_USE    (hlen));
  }
  {
    HEGEL_BINDING (nlen);
    ndl_schema_ab = HEGEL_STRUCT (Bytes,
        HEGEL_LET    (nlen, HEGEL_INT (1, MAX_NDL)),
        HEGEL_ARR_OF (HEGEL_USE (nlen), HEGEL_U8 (0x61, 0x62)),
        HEGEL_USE    (nlen));
  }

  hegel_suite *s = hegel_suite_new ();
  hegel_suite_add (s, "ts_kmp_vs_memmem_random_bytes_schema", test_random_bytes_schema);
  hegel_suite_add (s, "ts_kmp_vs_memmem_biased_ab_schema",    test_biased_ab_schema);
  int rc = hegel_suite_run (s);
  hegel_suite_free (s);

  hegel_schema_free (hay_schema_random);
  hegel_schema_free (ndl_schema_random);
  hegel_schema_free (hay_schema_ab);
  hegel_schema_free (ndl_schema_ab);
  return rc;
}
