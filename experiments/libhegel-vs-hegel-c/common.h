/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Measurement hook shared by both sides (not counted as test code):
** count_exec () appends one byte to $LVH_COUNT per execution of the
** property body.  O_APPEND, so it also counts executions that run in a
** forked child (hegel-c's fork mode, a hand-rolled fork).
*/
#ifndef LVH_COMMON_H
#define LVH_COMMON_H

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

static inline void count_exec (void)
{
  const char * path = getenv ("LVH_COUNT");
  if (!path || !*path) return;
  int fd = open (path, O_WRONLY | O_APPEND | O_CREAT, 0644);
  if (fd < 0) return;
  if (write (fd, "x", 1) < 0) { /* best effort */ }
  close (fd);
}

#endif
