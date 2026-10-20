/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Stub for objagg's tracepoint events — userspace harness has no
** ftrace machinery, so make all trace_objagg_* calls into no-ops. */

#ifndef HEGEL_OBJAGG_TRACE_STUB_H
#define HEGEL_OBJAGG_TRACE_STUB_H

#define trace_objagg_create(o)                              do { (void)(o); } while (0)
#define trace_objagg_destroy(o)                             do { (void)(o); } while (0)
#define trace_objagg_obj_create(o, x)                       do { (void)(o); (void)(x); } while (0)
#define trace_objagg_obj_destroy(o, x)                      do { (void)(o); (void)(x); } while (0)
#define trace_objagg_obj_get(o, x, r)                       do { (void)(o); (void)(x); (void)(r); } while (0)
#define trace_objagg_obj_put(o, x, r)                       do { (void)(o); (void)(x); (void)(r); } while (0)
#define trace_objagg_obj_root_create(o, x)                  do { (void)(o); (void)(x); } while (0)
#define trace_objagg_obj_root_destroy(o, x)                 do { (void)(o); (void)(x); } while (0)
#define trace_objagg_obj_parent_assign(o, x, p, r)          do { (void)(o); (void)(x); (void)(p); (void)(r); } while (0)
#define trace_objagg_obj_parent_unassign(o, x, p, r)        do { (void)(o); (void)(x); (void)(p); (void)(r); } while (0)

#endif
