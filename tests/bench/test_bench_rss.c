/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* RSS probe: reports the parent's resident set size after hegeltest
** and the hegel-core server connection are fully warmed.  This is a
** lower bound on what fork(2) would duplicate for every test case in
** fork mode — the page tables are copied-on-write, but every dirty
** page still counts against the fork's wall-clock cost (page-table
** walk scales with RSS).
**
** Runs a single nofork case first so the measurement happens in the
** parent process, after:
**   - hegeltest's OnceLock<HegelSession> has been populated,
**   - the hegel-core Python subprocess has been spawned,
**   - the stdio pipes and CBOR buffers have been allocated,
**   - the first draw has round-tripped through the server.
**
** Output is two machine-readable key=value lines so the Makefile can
** grep/awk them without regex pain. */

#include "hegel_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void warm (hegel_testcase * tc) {
  (void) hegel_draw_int (tc, 0, 100);
}

int main (void) {
  hegel_run_test_nofork (warm);

  FILE * f = fopen ("/proc/self/statm", "r");
  if (!f) { perror ("open /proc/self/statm"); return 1; }
  long size_pages = 0, rss_pages = 0;
  if (fscanf (f, "%ld %ld", &size_pages, &rss_pages) != 2) {
    fprintf (stderr, "parse /proc/self/statm failed\n");
    fclose (f);
    return 1;
  }
  fclose (f);

  long pagesize = sysconf (_SC_PAGESIZE);
  if (pagesize <= 0) pagesize = 4096;

  printf ("RSS_KB=%ld\n", (rss_pages  * pagesize) / 1024);
  printf ("VSZ_KB=%ld\n", (size_pages * pagesize) / 1024);
  return 0;
}
