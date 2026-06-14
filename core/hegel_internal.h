/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Internal shared definitions for the pure-C hegel-c core.
**
** === Architecture ===
**
** The HOST side owns the engine: libhegel (dlopen'd, see
** hegel_engine.h) runs the generate/shrink loop in-process.  The
** TEST BODY runs either:
**
**   - in the same process (nofork mode) — draws call the engine
**     directly; failures escape via longjmp back to the run loop; or
**   - in a forked child (fork mode, the default) — draws are framed
**     request/response messages over a pipe pair; the parent serves
**     them by calling the engine.  A crash in the child is caught by
**     waitpid and reported as an interesting (failing) test case.
**
** The message protocol below is deliberately transport-shaped: fixed
** little-header framing, no shared memory, no fd passing.  The same
** message set can later run over a serial link / TCP socket to a
** remote target (embedded board) — the parent's serve loop doesn't
** care what is on the other end.
*/

#ifndef HEGEL_INTERNAL_H
#define HEGEL_INTERNAL_H

#include "hegel_engine.h"
#include "../hegel_c.h"

#include <setjmp.h>
#include <stdint.h>

/* ---- Escape codes (longjmp values / terminal outcomes) ---- */

#define HEGEL_ESC_FAIL    1   /* property failed (hegel_fail / HEGEL_ASSERT) */
#define HEGEL_ESC_ASSUME  2   /* assume(false) — discard the case            */
#define HEGEL_ESC_STOP    3   /* engine choice budget exhausted — overrun    */
#define HEGEL_ESC_HEALTH  4   /* hegel_health_fail — abort the whole run     */

/* ---- Wire protocol (child -> parent requests) ----
**
** Request frame:  { uint8 op; uint8 pad[3]; uint32 len; uint8 payload[len] }
** Response frame: { uint8 status; uint8 pad[7]; payload }
**   status 0 = OK (payload depends on op)
**   status 1 = STOP — engine budget exhausted, abort the body
**
** Scalar payloads are 8-byte little-struct fields in native byte
** order (parent and child are the same machine; a future remote
** transport would pin endianness at the framing layer). */

#define HEGEL_OP_DRAW_INT    1   /* req: i64 min, i64 max        resp: i64    */
#define HEGEL_OP_DRAW_U64    2   /* req: u64 min, u64 max        resp: u64    */
#define HEGEL_OP_DRAW_DOUBLE 3   /* req: f64 min, f64 max, u64 width  resp: f64 */
#define HEGEL_OP_DRAW_TEXT   4   /* req: i64 min, i64 max        resp: u32 len + bytes */
#define HEGEL_OP_DRAW_BYTES  5   /* req: i64 min, i64 max        resp: u32 len + bytes */
#define HEGEL_OP_DRAW_REGEX  6   /* req: pattern bytes           resp: u32 len + bytes */
#define HEGEL_OP_DRAW_BOOL   7   /* req: (empty)                 resp: u64 0/1 */
#define HEGEL_OP_SPAN_START  8   /* req: u64 label               resp: ack   */
#define HEGEL_OP_SPAN_STOP   9   /* req: u64 discard             resp: ack   */
#define HEGEL_OP_TARGET     10   /* req: f64 value + label bytes resp: ack   */
/* Terminal messages — no response; the child exits after sending. */
#define HEGEL_OP_DONE       20   /* body completed: VALID                    */
#define HEGEL_OP_STOPPED    21   /* body aborted after STOP: OVERRUN         */
#define HEGEL_OP_ASSUME     22   /* assume(false): INVALID                   */
#define HEGEL_OP_FAIL       23   /* u32 olen + origin + u32 mlen + msg: INTERESTING */
#define HEGEL_OP_HEALTH     24   /* msg bytes: abort run, health failure     */

typedef struct {
  uint8_t  op;
  uint8_t  pad[3];
  uint32_t len;
} hegel_msg_header;

#define HEGEL_RESP_OK     0
#define HEGEL_RESP_STOP   1
#define HEGEL_RESP_ASSUME 2   /* engine rejected the draw — discard the case */

/* ---- The test case handle (public opaque type hegel_testcase) ---- */

typedef enum {
  HEGEL_TC_INPROC = 0,  /* nofork, or the future remote-host driver */
  HEGEL_TC_CHILD  = 1   /* fork-mode child: draws go over the pipe  */
} hegel_tc_mode;

struct HegelTestCase {
  hegel_tc_mode     mode;

  /* inproc */
  const hg_engine * eng;
  hg_tc           * htc;

  /* child */
  int               req_fd;    /* child writes requests here   */
  int               resp_fd;   /* child reads responses here   */

  /* shared */
  int               final_replay;  /* engine's final replay of the minimal example */
  long              draw_count;    /* user-visible draws, for "Draw N:" traces     */
  int               silent;        /* internal draws (stateful bookkeeping) — no trace */
  int               stop_seen;     /* a draw returned STOP; case must end as OVERRUN  */

  /* Case-level escape: set by the nofork run loop (fork children
  ** terminate by sending a message and _exit instead).  Non-NULL only
  ** in inproc mode. */
  jmp_buf         * escape;

  /* Rule-level escape: set around stateful rule bodies in BOTH modes
  ** so ASSUME / STOP inside a rule returns control to the stateful
  ** loop instead of ending the case.  FAIL never uses this — it
  ** always propagates to the case level. */
  jmp_buf         * rule_escape;

  /* Per-case cleanup hook, run by the case driver on EVERY exit path
  ** (normal return or escape) — the C analogue of stateful.rs's RAII
  ** teardown guard.  Cleared after invocation. */
  void           (* cleanup)(void * arg);
  void            * cleanup_arg;
};

/* ---- Cross-file internals (defined in hegel_runtime.c) ---- */

/* The test case currently being driven — used by hegel_fail (whose
** public signature has no tc parameter).  Single active case per
** process by design. */
extern hegel_testcase * hegel__current_tc;

/* Pending failure message + origin, filled in by the fail path before
** escaping, read by the case driver. */
extern char hegel__fail_msg[1024];
extern char hegel__fail_origin[512];

/* Registered per-case setup hook (hegel_set_case_setup). */
extern void (*hegel__case_setup)(void);

/* Terminal-outcome helpers shared by runtime + stateful. */
void hegel__escape (hegel_testcase * tc, int code);          /* longjmp or send+_exit */
void hegel__child_send_terminal (hegel_testcase * tc, int op);

/* Internal draws shared by the combinator generators / stateful loop. */
int  hegel__draw_bool (hegel_testcase * tc);

/* Nofork run driver with a custom body (stateful). */
int  hegel__run_property_nofork (void (*test_fn)(hegel_testcase *), uint64_t n_cases);

/* Robust full-buffer pipe IO (EINTR-safe).  Return 0 on success,
** -1 on EOF/error. */
int hegel__read_full  (int fd, void * buf, size_t n);
int hegel__write_full (int fd, const void * buf, size_t n);

/* Engine-call layer used by both the inproc draw path and the fork
** parent's serve loop.  Each returns 0 on success, 1 on STOP (choice
** budget exhausted), 2 on ASSUME (the engine rejected the draw — the
** case must be discarded as INVALID), and aborts the process on hard
** engine errors (invalid schema/range — a schema authoring error,
** mirroring hegel_gen.c's abort policy). */
int hegel__eng_draw_int    (const hg_engine * e, hg_tc * t, int64_t min, int64_t max, int64_t * out);
int hegel__eng_draw_u64    (const hg_engine * e, hg_tc * t, uint64_t min, uint64_t max, uint64_t * out);
int hegel__eng_draw_double (const hg_engine * e, hg_tc * t, double min, double max, int width,
                            double * out);
int hegel__eng_draw_bool   (const hg_engine * e, hg_tc * t, int * out);
/* Text/bytes/regex write at most cap-1 chars + NUL (text/regex) or cap
** raw bytes (bytes); *out_len is the (possibly capped) length. */
int hegel__eng_draw_text   (const hg_engine * e, hg_tc * t, int64_t min, int64_t max,
                            char * buf, int cap, int * out_len);
int hegel__eng_draw_bytes  (const hg_engine * e, hg_tc * t, int64_t min, int64_t max,
                            uint8_t * buf, int cap, int * out_len);
int hegel__eng_draw_regex  (const hg_engine * e, hg_tc * t, const char * pattern,
                            char * buf, int cap, int * out_len);

/* Raw variants used by the fork parent's serve loop: *out points into
** the engine's value buffer and is invalidated by the next engine
** call on this test case — forward it before drawing again. */
int hegel__eng_draw_text_raw  (const hg_engine * e, hg_tc * t, int64_t min, int64_t max,
                               const uint8_t ** out, size_t * out_len);
int hegel__eng_draw_bytes_raw (const hg_engine * e, hg_tc * t, int64_t min, int64_t max,
                               const uint8_t ** out, size_t * out_len);
int hegel__eng_draw_regex_raw (const hg_engine * e, hg_tc * t, const char * pattern,
                               const uint8_t ** out, size_t * out_len);

#endif /* HEGEL_INTERNAL_H */
