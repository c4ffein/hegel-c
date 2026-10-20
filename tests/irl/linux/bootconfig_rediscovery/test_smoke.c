/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Smoke test: snapshotted bootconfig.c links and parses a trivial valid
** config.  Confirms the build chain only. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/bootconfig.h>

int
main (void)
{
  char buf[1024];
  strcpy (buf, "foo = bar\nbaz {\n  qux = quux\n}\n");
  size_t orig_len = strlen (buf);
  const char *msg = NULL;
  int pos = 0;
  int ret = xbc_init (buf, orig_len, &msg, &pos);
  printf ("smoke: xbc_init -> %d msg=%s pos=%d\n",
          ret, msg ? msg : "(null)", pos);
  if (ret < 0) return 1;
  xbc_exit ();
  printf ("smoke OK\n");
  return 0;
}
