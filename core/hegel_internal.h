/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Internal shared definitions for the pure-C hegel-c core.
**
** === Architecture ===
**
** The HOST side owns the engine: libhegel (dlopen'd, see
** hegel_engine.h) runs the generate/shrink loop in-process.  Every
** engine operation a test body performs is a framed REQUEST executed
** by one server function, hegel__serve_op (hegel_server.c), against
** the host's per-case state.  The test body reaches that server through
** a TRANSPORT selected by tc->mode:
**
**   INPROC — nofork mode: the request is served by a direct function
**            call; failures escape via longjmp back to the run loop.
**   CHILD  — fork mode (the default): the body runs in a forked child
**            and requests travel over a pipe pair; the parent serves
**            them.  A crash in the child is caught by waitpid and
**            reported as an interesting (failing) test case.
**   ISO    — the forked child of a HEGEL_ISOLATED block (nofork mode):
**            may only send notes and a terminal verdict; drawing there
**            is an authoring error.
**
** Because both real transports go through the same message set, the
** protocol is the binding's one engine surface: fixed little-header
** framing, no shared memory, no fd passing.  The same messages can
** later run over a serial link / TCP socket to a remote target
** (embedded board) — the server doesn't care what is on the other end.
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

/* ---- Wire protocol ----
**
** Request frame:  { uint8 op; uint8 pad[3]; uint32 len; uint8 payload[len] }
** Response frame: { uint8 status; uint8 pad[3]; uint32 len; uint8 payload[len] }
**
** Payload fields are native-endian, packed back to back in the order
** listed (parent and child are the same machine; a remote transport
** would pin endianness at the framing layer).  "id" is a server-side
** handle id (collection / recursion / pool / state machine).  "str" is
** raw bytes running to the end of the payload. */

/* Draws */
#define HEGEL_OP_DRAW_INT      1   /* i64 min, i64 max                 -> i64     */
#define HEGEL_OP_DRAW_U64      2   /* u64 min, u64 max                 -> u64     */
#define HEGEL_OP_DRAW_FLOAT    3   /* f64 min, f64 max, u32 width      -> f64     */
#define HEGEL_OP_DRAW_BOOL     4   /* f64 p                            -> u8      */
#define HEGEL_OP_DRAW_TEXT     5   /* u64 min, u64 max                 -> str     */
#define HEGEL_OP_DRAW_BYTES    6   /* u64 min, u64 max                 -> str     */
#define HEGEL_OP_DRAW_REGEX    7   /* str pattern (full match)         -> str     */
/* Structure */
#define HEGEL_OP_SPAN_START   10   /* u64 label                                   */
#define HEGEL_OP_SPAN_STOP    11   /* u8 discard                                  */
#define HEGEL_OP_COLL_NEW     12   /* u64 min, u64 max                 -> i64 id  */
#define HEGEL_OP_COLL_MORE    13   /* i64 id                           -> u8      */
#define HEGEL_OP_COLL_REJECT  14   /* i64 id                                      */
#define HEGEL_OP_COLL_FREE    15   /* i64 id                                      */
#define HEGEL_OP_REC_NEW      16   /* u64 max_depth, u64 max_leaves    -> i64 id  */
#define HEGEL_OP_REC_BRANCH   17   /* i64 id, u64 depth                -> u8      */
#define HEGEL_OP_REC_LEAF     18   /* i64 id                     (OK | RETRY)     */
#define HEGEL_OP_REC_RETRY    19   /* i64 id                     (OK | ASSUME)    */
#define HEGEL_OP_REC_FINISH   20   /* i64 id                     (OK | RETRY)     */
#define HEGEL_OP_REC_FREE     21   /* i64 id                                      */
/* Stateful */
#define HEGEL_OP_POOL_NEW     30   /*                                  -> i64 id  */
#define HEGEL_OP_POOL_ADD     31   /* i64 id                           -> i64 var */
#define HEGEL_OP_POOL_DRAW    32   /* i64 id, u8 consume               -> i64 var */
#define HEGEL_OP_POOL_RESTORE 33   /* i64 id, i64 var                             */
#define HEGEL_OP_POOL_FREE    34   /* i64 id                                      */
#define HEGEL_OP_SM_NEW       35   /* u32 nrules, u32 ninvs, i64 steps,
                                   ** then nrules+ninvs x (u32 len, bytes) names
                                   **                                  -> i64 id  */
#define HEGEL_OP_SM_GROUP     36   /* i64 id                           -> i64     */
#define HEGEL_OP_SM_RULE      37   /* i64 id                           -> i64     */
#define HEGEL_OP_SM_REJECTED  38   /* i64 id                                      */
#define HEGEL_OP_SM_CHECK_INV 39   /* i64 id, i64 inv                  -> u8      */
#define HEGEL_OP_SM_FREE      40   /* i64 id                                      */
/* Reporting */
#define HEGEL_OP_TARGET       50   /* f64 value, str label                        */
#define HEGEL_OP_NOTE         51   /* str line — NO response (fire-and-forget).
                                   ** Captured lines (notes, draw traces) live in
                                   ** the SERVER, so they survive a child crash: a
                                   ** pipe frame is durable once write() returns,
                                   ** child stdio buffers are not. */
#define HEGEL_OP_ISO_SITE     52   /* str site ("" = left the block) — NO response.
                                   ** Lets the server attribute a crash to the
                                   ** HEGEL_ISOLATED block it happened in. */
/* Terminal messages — no response; the sender exits after sending. */
#define HEGEL_OP_DONE         60   /* body completed: VALID                       */
#define HEGEL_OP_STOPPED      61   /* body aborted after STOP: OVERRUN            */
#define HEGEL_OP_ASSUME       62   /* assume(false): INVALID                      */
#define HEGEL_OP_FAIL         63   /* u32 olen, origin, msg: INTERESTING          */
#define HEGEL_OP_HEALTH       64   /* str msg: abort run, health failure          */

typedef struct {
  uint8_t  op;
  uint8_t  pad[3];
  uint32_t len;
} hegel_msg_header;

#define HEGEL_RESP_OK     0
#define HEGEL_RESP_STOP   1   /* engine budget exhausted — abort the body     */
#define HEGEL_RESP_ASSUME 2   /* engine rejected the case — discard it         */
#define HEGEL_RESP_RETRY  3   /* recursion: restart the value from the root    */
#define HEGEL_RESP_ERROR  4   /* misuse (bad bounds, bad pattern...): payload =
                              ** message; the body aborts the run as a health
                              ** failure — the test setup is broken. */

/* ---- Growable byte buffer (request/response payloads) ---- */

typedef struct {
  uint8_t * data;
  size_t    len;
  size_t    cap;
  uint8_t   small[256];
} hegel_buf;

void hegel__buf_init  (hegel_buf * b);
void hegel__buf_free  (hegel_buf * b);
void hegel__buf_put   (hegel_buf * b, const void * p, size_t n);
#define HEGEL_BUF_PUT(b, v) hegel__buf_put ((b), &(v), sizeof (v))

/* ---- Server side (hegel_server.c) ---- */

typedef struct hegel_case_srv hegel_case_srv;

/* Outcome of one test case, assembled by the server from terminal
** messages (and by the runner from a crash). */
typedef struct {
  int  status;        /* HG_STATUS_*, or -1 = no terminal message (crash?) */
  int  health;        /* hegel_health_fail fired — abort the whole run     */
  char origin[512];
  char msg[1024];
} hegel_outcome;

/* Per-test-case server state: the engine's test case, the handle table
** for engine objects the body created, and the captured output. */
hegel_case_srv * hegel__srv_case_new  (const hg_engine * e, hg_tc * htc, int capture);
void             hegel__srv_case_free (hegel_case_srv * c);  /* frees leaked handles too */
int              hegel__srv_capture   (const hegel_case_srv * c);
int              hegel__srv_stop_seen (const hegel_case_srv * c);
const char *     hegel__srv_iso_site  (const hegel_case_srv * c);
/* Captured lines, '\n'-separated; ownership moves to the caller. */
char *           hegel__srv_take_lines (hegel_case_srv * c);
void             hegel__srv_add_line   (hegel_case_srv * c, const char * line, size_t len);

/* Serve one request.  Fills `resp_status` / `resp` for ops that answer.
** Returns 0 = answered, 1 = no response (fire-and-forget), 2 = terminal
** message (outcome filled in). */
int hegel__serve_op (hegel_case_srv * c, uint8_t op, const uint8_t * payload, uint32_t len,
                     uint8_t * resp_status, hegel_buf * resp, hegel_outcome * out);

/* Release run-scoped caches (string generators). */
void hegel__srv_run_end (void);

/* ---- The test case handle (public opaque type hegel_testcase) ---- */

typedef enum {
  HEGEL_TC_INPROC = 0,  /* nofork: requests served by direct call       */
  HEGEL_TC_CHILD  = 1,  /* fork-mode child: requests go over the pipe    */
  HEGEL_TC_ISO    = 2   /* HEGEL_ISOLATED child: notes + verdict only    */
} hegel_tc_mode;

struct HegelTestCase {
  hegel_tc_mode     mode;

  /* inproc */
  hegel_case_srv  * srv;

  /* child / iso */
  int               req_fd;    /* body writes requests here    */
  int               resp_fd;   /* body reads responses here    */

  /* shared */
  int               capture;       /* engine stamped this case for capture */
  long              draw_count;    /* user-visible draws, for "Draw N:" traces */
  int               silent;        /* internal draws (stateful bookkeeping) — no trace */
  int               stop_seen;     /* a draw returned STOP; case must end as OVERRUN  */
  const char      * iso_site;      /* inside a HEGEL_ISOLATED block (file:line) */

  /* Case-level escape: set by the nofork run loop (fork children
  ** terminate by sending a message and _exit instead).  Non-NULL only
  ** in inproc mode. */
  jmp_buf         * escape;

  /* Rule-level escape: set around stateful rule bodies in BOTH modes
  ** so ASSUME inside a rule returns control to the stateful loop
  ** instead of ending the case.  FAIL never uses this — it always
  ** propagates to the case level. */
  jmp_buf         * rule_escape;

  /* Per-case cleanup hook, run by the case driver on EVERY exit path
  ** (normal return or escape).  Cleared after invocation. */
  void           (* cleanup)(void * arg);
  void            * cleanup_arg;
};

/* ---- Cross-file internals (hegel_runtime.c) ---- */

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

/* Terminal-outcome helpers shared by runtime + runner + stateful. */
void hegel__escape (hegel_testcase * tc, int code);          /* longjmp or send+_exit */
void hegel__send_terminal (int fd, int op);
void hegel__emit_line (hegel_testcase * tc, const char * s); /* captured output line */

/* Send a request and read its response (any transport).  Returns the
** response status; RESP_STOP / RESP_ASSUME / RESP_ERROR escape the body
** instead of returning unless `raw` is set.  `resp` may be NULL for
** ops without a payload. */
int hegel__call (hegel_testcase * tc, uint8_t op, const void * payload, uint32_t len,
                 hegel_buf * resp, int raw);
/* Fire-and-forget (NOTE / ISO_SITE). */
void hegel__send (hegel_testcase * tc, uint8_t op, const void * payload, uint32_t len);

/* Robust full-buffer pipe IO (EINTR-safe).  Return 0 on success,
** -1 on EOF/error. */
int hegel__read_full  (int fd, void * buf, size_t n);
int hegel__write_full (int fd, const void * buf, size_t n);

/* Run drivers (hegel_runner.c) with a custom body (stateful). */
int hegel__run_property (void (*test_fn)(hegel_testcase *), uint64_t n_cases, int use_fork);

#endif /* HEGEL_INTERNAL_H */
