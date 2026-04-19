// SPDX-License-Identifier: MIT
// Copyright (c) 2026 c4ffein
// Part of hegel-c — see hegel/LICENSE for terms.

use hegel::generators as gs;
use hegel::{Hegel, TestCase};
use std::ffi::CStr;
use std::mem::ManuallyDrop;
use std::os::raw::{c_char, c_int};
use std::panic::{catch_unwind, AssertUnwindSafe};

/// Opaque handle passed to C test functions.
pub struct HegelTestCase {
    tc: TestCase,
}

/// C test function signature: void test_fn(hegel_testcase *tc).
/// Uses C-unwind so that Rust panics (from hegel_fail, hegel_assume,
/// or internal hegel StopTest) can unwind back through the C frame.
type CTestFn = unsafe extern "C-unwind" fn(*mut HegelTestCase);

/// Optional per-test-case setup callback, registered via hegel_set_case_setup.
/// Called before each test case (in both fork and nofork modes).
/// Use this to reset global state in the library under test (e.g., RNG seeds).
static mut CASE_SETUP_FN: Option<unsafe extern "C-unwind" fn()> = None;

/// Call the registered setup callback, if any.
fn call_case_setup() {
    unsafe {
        if let Some(f) = CASE_SETUP_FN {
            f();
        }
    }
}

/*
** Fork-child IPC protocol.
**
** Each test case runs in a forked child process. Draws are proxied
** back to the parent (which owns the hegel server connection) via
** a pair of pipes. This isolates crashes (SIGSEGV, SIGABRT, etc.)
** while preserving hegel's ability to shrink failing test cases.
**
** Message types (child -> parent on req_pipe):
*/
const MSG_DRAW_INT:    u8 = 1; /* payload: min(4) + max(4)   ; response: val(4)  */
const MSG_DRAW_I64:    u8 = 2; /* payload: min(8) + max(8)   ; response: val(8)  */
const MSG_DRAW_U64:    u8 = 3; /* payload: min(8) + max(8)   ; response: val(8)  */
const MSG_DRAW_USIZE:  u8 = 4; /* payload: min(8) + max(8)   ; response: val(8)  */
const MSG_ASSUME:      u8 = 5; /* no payload, no response     ; child _exit(77)   */
const MSG_FAIL:        u8 = 6; /* payload: len(4) + msg(len)  ; child _exit(1)    */
const MSG_OK:          u8 = 7; /* no payload, no response     ; child _exit(0)    */
const MSG_DRAW_DOUBLE: u8 = 9;  /* payload: min(8) + max(8)   ; response: val(8)  */
const MSG_DRAW_FLOAT:  u8 = 10; /* payload: min(4) + max(4)   ; response: val(4)  */
const MSG_DRAW_TEXT:   u8 = 11; /* payload: min(4) + max(4)   ; response: len(4) + bytes(len) */
const MSG_DRAW_REGEX:  u8 = 12; /* payload: plen(4) + pat(plen) ; response: len(4) + bytes(len) */
const MSG_START_SPAN:  u8 = 13; /* payload: label(8)           ; no response       */
const MSG_STOP_SPAN:   u8 = 14; /* payload: discard(1)         ; no response       */


/*
** Fork-child global state.
**
** Safety: these globals are only accessed by the hegel API functions
** (hegel_draw_*, hegel_fail, hegel_assume), which are always called
** from the test's main thread.  The code under test may freely spawn
** threads or fork — those threads/children never call back into the
** hegel API, so the globals remain uncontested.
**
** The hegel server connection is sequential and one test case runs
** at a time.
*/
static mut IN_FORK_CHILD: bool = false;
static mut FORK_REQ_WR: c_int = -1;  /* child writes requests here  */
static mut FORK_RESP_RD: c_int = -1; /* child reads responses here  */

/*
** Pipe fds are created without O_CLOEXEC.  This is fine: we fork()
** but never exec(), so there is no risk of leaking fds into an
** unrelated process.  If fork-exec were ever needed, switch to
** pipe2(O_CLOEXEC).
*/
fn pipe_pair() -> (c_int, c_int) {
    let mut fds: [c_int; 2] = [0; 2];
    assert!(unsafe { libc::pipe(fds.as_mut_ptr()) } == 0, "pipe() failed");
    (fds[0], fds[1])
}

/*
** Write errors (EPIPE, etc.) are silently ignored.  This function is
** called from the forked test process to send draw requests and
** results back to the hegel runner (the parent process that owns the
** server connection).  If the parent dies or closes its pipe end, the
** entire test run is already lost — the forked process will _exit()
** moments later regardless.
*/
fn pipe_write_all(fd: c_int, data: &[u8]) {
    let mut off = 0;
    while off < data.len() {
        let n = unsafe {
            libc::write(fd, data[off..].as_ptr() as *const libc::c_void,
                        data.len() - off)
        };
        if n <= 0 { break; }
        off += n as usize;
    }
}

fn pipe_read_exact(fd: c_int, buf: &mut [u8]) -> bool {
    let mut off = 0;
    while off < buf.len() {
        let n = unsafe {
            libc::read(fd, buf[off..].as_mut_ptr() as *mut libc::c_void,
                       buf.len() - off)
        };
        if n <= 0 { return false; }
        off += n as usize;
    }
    true
}

/// Called from inside a forked child when its pipe to the parent is
/// dead (parent panicked, closed pipes, or died).  Exit cleanly so
/// the parent's waitpid reaps us promptly.  We do NOT attempt to run
/// any more of the test — every draw would return garbage and the
/// test body could do something unpredictable with it.
fn child_abandoned() -> ! {
    unsafe { libc::_exit(0) };
}

/// Result from serving the child's requests.
enum ChildMsg {
    Ok,
    Fail(String),
    Assume,
    Eof, /* child crashed or exited without a message */
}

/// Optional verbose tracing.  When `HEGEL_VERBOSE_TRACE=1` is set
/// in the environment, hegel-c prints each draw and each case
/// boundary to stderr from the parent process.  Useful for
/// watching the shrinker narrow down on a failing case — every
/// shrink attempt is one cluster of `[hegel] draw_*` lines bracketed
/// by `[hegel] case_start` / `[hegel] case_end`.
///
/// Off by default — opting in adds one line per primitive draw,
/// which can be a lot for schema-API tests with array fields.
fn verbose_trace_enabled() -> bool {
    static ENABLED: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *ENABLED.get_or_init(|| std::env::var("HEGEL_VERBOSE_TRACE").is_ok())
}

/// Monotonic case counter for verbose-trace output.
fn next_trace_case_id() -> u64 {
    static N: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
    N.fetch_add(1, std::sync::atomic::Ordering::SeqCst) + 1
}

/// Parent: loop reading draw requests from the child, forwarding
/// them to hegel via `tc.draw()`, and sending results back.
fn parent_serve(tc: &TestCase, req_rd: c_int, resp_wr: c_int) -> ChildMsg {
    let trace = verbose_trace_enabled();
    loop {
        let mut tag = [0u8; 1];
        if !pipe_read_exact(req_rd, &mut tag) {
            return ChildMsg::Eof;
        }

        match tag[0] {
            MSG_DRAW_INT => {
                let mut buf = [0u8; 8];
                if !pipe_read_exact(req_rd, &mut buf) { return ChildMsg::Eof; }
                let min_val = c_int::from_le_bytes(buf[0..4].try_into().unwrap());
                let max_val = c_int::from_le_bytes(buf[4..8].try_into().unwrap());
                let val: c_int = tc.draw(
                    gs::integers::<c_int>().min_value(min_val).max_value(max_val));
                if trace { eprintln!("[hegel]   draw_int({},{}) -> {}", min_val, max_val, val); }
                pipe_write_all(resp_wr, &val.to_le_bytes());
            }
            MSG_DRAW_I64 => {
                let mut buf = [0u8; 16];
                if !pipe_read_exact(req_rd, &mut buf) { return ChildMsg::Eof; }
                let min_val = i64::from_le_bytes(buf[0..8].try_into().unwrap());
                let max_val = i64::from_le_bytes(buf[8..16].try_into().unwrap());
                let val: i64 = tc.draw(
                    gs::integers::<i64>().min_value(min_val).max_value(max_val));
                if trace { eprintln!("[hegel]   draw_i64({},{}) -> {}", min_val, max_val, val); }
                pipe_write_all(resp_wr, &val.to_le_bytes());
            }
            MSG_DRAW_U64 => {
                let mut buf = [0u8; 16];
                if !pipe_read_exact(req_rd, &mut buf) { return ChildMsg::Eof; }
                let min_val = u64::from_le_bytes(buf[0..8].try_into().unwrap());
                let max_val = u64::from_le_bytes(buf[8..16].try_into().unwrap());
                let val: u64 = tc.draw(
                    gs::integers::<u64>().min_value(min_val).max_value(max_val));
                if trace { eprintln!("[hegel]   draw_u64({},{}) -> {}", min_val, max_val, val); }
                pipe_write_all(resp_wr, &val.to_le_bytes());
            }
            MSG_DRAW_USIZE => {
                let mut buf = [0u8; 16];
                if !pipe_read_exact(req_rd, &mut buf) { return ChildMsg::Eof; }
                let min_val = usize::from_le_bytes(buf[0..8].try_into().unwrap());
                let max_val = usize::from_le_bytes(buf[8..16].try_into().unwrap());
                let val: usize = tc.draw(
                    gs::integers::<usize>().min_value(min_val).max_value(max_val));
                if trace { eprintln!("[hegel]   draw_usize({},{}) -> {}", min_val, max_val, val); }
                pipe_write_all(resp_wr, &val.to_le_bytes());
            }
            MSG_DRAW_DOUBLE => {
                let mut buf = [0u8; 16];
                if !pipe_read_exact(req_rd, &mut buf) { return ChildMsg::Eof; }
                let min_val = f64::from_le_bytes(buf[0..8].try_into().unwrap());
                let max_val = f64::from_le_bytes(buf[8..16].try_into().unwrap());
                let val: f64 = tc.draw(
                    gs::floats::<f64>().min_value(min_val).max_value(max_val));
                if trace { eprintln!("[hegel]   draw_double({},{}) -> {}", min_val, max_val, val); }
                pipe_write_all(resp_wr, &val.to_le_bytes());
            }
            MSG_DRAW_FLOAT => {
                let mut buf = [0u8; 8];
                if !pipe_read_exact(req_rd, &mut buf) { return ChildMsg::Eof; }
                let min_val = f32::from_le_bytes(buf[0..4].try_into().unwrap());
                let max_val = f32::from_le_bytes(buf[4..8].try_into().unwrap());
                let val: f32 = tc.draw(
                    gs::floats::<f32>().min_value(min_val).max_value(max_val));
                if trace { eprintln!("[hegel]   draw_float({},{}) -> {}", min_val, max_val, val); }
                pipe_write_all(resp_wr, &val.to_le_bytes());
            }
            MSG_DRAW_TEXT => {
                let mut buf = [0u8; 8];
                if !pipe_read_exact(req_rd, &mut buf) { return ChildMsg::Eof; }
                let min_size = u32::from_le_bytes(buf[0..4].try_into().unwrap()) as usize;
                let max_size = u32::from_le_bytes(buf[4..8].try_into().unwrap()) as usize;
                let val: String = tc.draw(
                    gs::text().min_size(min_size).max_size(max_size));
                if trace { eprintln!("[hegel]   draw_text({},{}) -> {:?}", min_size, max_size, val); }
                let bytes = val.as_bytes();
                pipe_write_all(resp_wr, &(bytes.len() as u32).to_le_bytes());
                pipe_write_all(resp_wr, bytes);
            }
            MSG_DRAW_REGEX => {
                let mut len_buf = [0u8; 4];
                if !pipe_read_exact(req_rd, &mut len_buf) { return ChildMsg::Eof; }
                let pat_len = u32::from_le_bytes(len_buf) as usize;
                let mut pat_buf = vec![0u8; pat_len];
                if pat_len > 0 && !pipe_read_exact(req_rd, &mut pat_buf) {
                    return ChildMsg::Eof;
                }
                let pattern = String::from_utf8_lossy(&pat_buf);
                let val: String = tc.draw(gs::from_regex(&pattern));
                if trace { eprintln!("[hegel]   draw_regex({:?}) -> {:?}", pattern, val); }
                let bytes = val.as_bytes();
                pipe_write_all(resp_wr, &(bytes.len() as u32).to_le_bytes());
                pipe_write_all(resp_wr, bytes);
            }
            MSG_START_SPAN => {
                let mut buf = [0u8; 8];
                if !pipe_read_exact(req_rd, &mut buf) { return ChildMsg::Eof; }
                let label = u64::from_le_bytes(buf);
                if trace { eprintln!("[hegel]   start_span({})", label); }
                tc.start_span(label);
            }
            MSG_STOP_SPAN => {
                let mut buf = [0u8; 1];
                if !pipe_read_exact(req_rd, &mut buf) { return ChildMsg::Eof; }
                if trace { eprintln!("[hegel]   stop_span(discard={})", buf[0] != 0); }
                tc.stop_span(buf[0] != 0);
            }
            MSG_ASSUME => {
                return ChildMsg::Assume;
            }
            MSG_FAIL => {
                let mut len_buf = [0u8; 4];
                if !pipe_read_exact(req_rd, &mut len_buf) { return ChildMsg::Eof; }
                let len = u32::from_le_bytes(len_buf) as usize;
                let mut msg_buf = vec![0u8; len];
                if len > 0 && !pipe_read_exact(req_rd, &mut msg_buf) {
                    return ChildMsg::Eof;
                }
                return ChildMsg::Fail(String::from_utf8_lossy(&msg_buf).into_owned());
            }
            MSG_OK => {
                return ChildMsg::Ok;
            }
            _ => {
                return ChildMsg::Eof;
            }
        }
    }
}

/// Run a single test case in a forked child process.
///
/// The child runs `test_fn` with draws proxied back to the parent
/// via pipes. The parent forwards draws to hegel (so shrinking works)
/// and waits for the child. If the child crashes, the parent reports
/// it as a panic — which hegel catches and shrinks.
fn run_forked(test_fn: CTestFn, tc: TestCase) {
    let trace_id = if verbose_trace_enabled() {
        let id = next_trace_case_id();
        eprintln!("[hegel] case_start #{}", id);
        Some(id)
    } else {
        None
    };

    call_case_setup();

    /* Ignore SIGPIPE — a crashed child can close the pipe mid-write. */
    unsafe { libc::signal(libc::SIGPIPE, libc::SIG_IGN); }

    let (req_rd, req_wr) = pipe_pair();
    let (resp_rd, resp_wr) = pipe_pair();

    let tc = ManuallyDrop::new(tc);

    let pid = unsafe { libc::fork() };
    if pid < 0 {
        unsafe {
            libc::close(req_rd); libc::close(req_wr);
            libc::close(resp_rd); libc::close(resp_wr);
        }
        panic!("fork() failed");
    }

    if pid == 0 {
        /* Child: close parent ends, set up fork-child mode. */
        unsafe {
            libc::close(req_rd);
            libc::close(resp_wr);
            IN_FORK_CHILD = true;
            FORK_REQ_WR = req_wr;
            FORK_RESP_RD = resp_rd;
        }

        /* Create HegelTestCase from the forked copy of tc.
        ** The child never calls tc.draw() directly — all draws
        ** go through the pipe via hegel_draw_* checks. */
        let tc_child = unsafe { std::ptr::read(&*tc) };
        let mut htc = HegelTestCase { tc: tc_child };

        let result = catch_unwind(AssertUnwindSafe(|| {
            unsafe { test_fn(&mut htc) };
        }));

        match result {
            Ok(()) => {
                pipe_write_all(unsafe { FORK_REQ_WR }, &[MSG_OK]);
                unsafe { libc::_exit(0) };
            }
            Err(info) => {
                /* hegel_fail already sent MSG_FAIL and called _exit,
                ** so we only get here from unexpected panics. */
                let msg = if let Some(s) = info.downcast_ref::<String>() {
                    s.clone()
                } else if let Some(s) = info.downcast_ref::<&str>() {
                    s.to_string()
                } else {
                    "unexpected panic".to_string()
                };
                let msg_bytes = msg.as_bytes();
                let len = msg_bytes.len() as u32;
                pipe_write_all(unsafe { FORK_REQ_WR }, &[MSG_FAIL]);
                pipe_write_all(unsafe { FORK_REQ_WR }, &len.to_le_bytes());
                pipe_write_all(unsafe { FORK_REQ_WR }, msg_bytes);
                unsafe { libc::_exit(1) };
            }
        }
    }

    /* Parent: close child ends, serve draw requests. */
    unsafe {
        libc::close(req_wr);
        libc::close(resp_rd);
    }

    /* Take tc back — safe because child has its own copy via fork. */
    let tc = ManuallyDrop::into_inner(tc);

    /* parent_serve can panic if hegel's tc.draw() aborts the test case
    ** (e.g. __HEGEL_STOP_TEST sentinel when the engine decides to discard
    ** mid-generation). If we let that panic propagate freely, the child
    ** is left blocked on a read waiting for a response that never comes,
    ** and we never waitpid it. Catch the panic here, unconditionally
    ** reap the child and close the pipes, then re-raise.
    **
    ** Closing the parent's pipe ends unblocks the child: its next
    ** hegel_draw_* call sees pipe EOF and _exit(0)s itself (see the
    ** IN_FORK_CHILD branch in hegel_draw_int and siblings). */
    let serve_result = catch_unwind(AssertUnwindSafe(|| parent_serve(&tc, req_rd, resp_wr)));

    unsafe {
        libc::close(req_rd);
        libc::close(resp_wr);
    }

    let mut status: c_int = 0;
    unsafe { libc::waitpid(pid, &mut status, 0); }

    let result = match serve_result {
        Ok(r) => r,
        Err(panic_info) => {
            if let Some(id) = trace_id {
                let msg = if let Some(s) = panic_info.downcast_ref::<String>() {
                    s.as_str()
                } else if let Some(s) = panic_info.downcast_ref::<&str>() {
                    s
                } else {
                    "<panic>"
                };
                let kind = if msg.contains("__HEGEL_STOP_TEST") || msg.contains("__HEGEL_ASSUME_FAIL") {
                    "discard"
                } else {
                    "panic"
                };
                eprintln!("[hegel] case_end #{} {} ({:.60})", id, kind, msg);
            }
            /* About to re-raise parent_serve's panic via resume_unwind below.
            ** We're in normal control flow here because catch_unwind above
            ** converted the panic into Err(panic_info); the unwind is suspended.
            **
            ** resume_unwind never returns and restarts unwinding. As the stack
            ** unwinds this frame, any still-live local gets dropped — and tc is
            ** the only non-trivial one. If tc's destructor panicked during
            ** unwind, Rust would abort the process (double-panic).
            **
            ** So drop tc here, before resume_unwind, to keep a hypothetical
            ** drop-panic in normal control flow where it'd propagate as a
            ** regular panic instead of aborting. In hegeltest 0.4.3 this is
            ** defense in depth: TestCase has no Drop impl, and the transitive
            ** Stream::drop just calls Connection::unregister_stream (mutex +
            ** HashMap::remove, no I/O). Arc<Connection>'s refcount doesn't hit
            ** zero here because the process-wide HegelSession singleton holds
            ** another Arc. Re-check this block on hegeltest upgrades. */
            drop(tc);
            std::panic::resume_unwind(panic_info);
        }
    };

    if let Some(id) = trace_id {
        let kind = match &result {
            ChildMsg::Ok => "ok",
            ChildMsg::Fail(_) => "fail",
            ChildMsg::Assume => "assume",
            ChildMsg::Eof => "eof",
        };
        eprintln!("[hegel] case_end #{} {}", id, kind);
    }

    /* Handle assume before dropping tc — tc.assume() panics with a
    ** sentinel type that hegel recognizes as "discard this test case". */
    if let ChildMsg::Assume = result {
        tc.assume(false); /* panics, hegel catches and discards */
    }

    /* Drop tc before the panic! below, same rationale as the
    ** resume_unwind site above: keeps any hypothetical drop-panic in
    ** normal control flow instead of mid-unwind. With hegeltest 0.4.3
    ** this is defense in depth — TestCase has no Drop impl. */
    let fail_msg = match result {
        ChildMsg::Ok => None,
        ChildMsg::Fail(msg) => Some(msg),
        ChildMsg::Assume => unreachable!(),
        ChildMsg::Eof => {
            if libc::WIFSIGNALED(status) {
                Some(format!("crashed (signal {})", libc::WTERMSIG(status)))
            } else if libc::WIFEXITED(status) {
                Some(format!("child exited unexpectedly (status {})", libc::WEXITSTATUS(status)))
            } else {
                Some("child lost".to_string())
            }
        }
    };
    drop(tc);

    if let Some(msg) = fail_msg {
        panic!("{}", msg);
    }
}

/// Run a hegel property test that calls a C function for each test case.
/// Each test case runs in a forked child process so that crashes
/// (SIGSEGV, SIGABRT, etc.) are caught and can be shrunk by hegel.
///
/// # Safety
/// `test_fn` must be a valid C function pointer.
/// Catch hegel's final "Property test failed" panic and convert it
/// to a clean process exit.  Without this, a double-panic can occur
/// when hegel's panic hook interacts with the fork machinery.
fn run_hegel<F: FnMut(TestCase) + Send + Sync>(h: Hegel<F>) {
    if run_hegel_result(h) != 0 {
        std::process::exit(1);
    }
}

/// Like run_hegel, but returns 0 on success and 1 on failure instead of
/// calling exit().  Allows callers to continue after a property failure.
fn run_hegel_result<F: FnMut(TestCase) + Send + Sync>(h: Hegel<F>) -> c_int {
    let result = catch_unwind(AssertUnwindSafe(|| h.run()));
    match result {
        Ok(()) => 0,
        Err(e) => {
            let msg: String = if let Some(s) = e.downcast_ref::<String>() {
                s.clone()
            } else if let Some(s) = e.downcast_ref::<&str>() {
                s.to_string()
            } else {
                "test failed".to_string()
            };
            eprintln!("{}", msg);
            1
        }
    }
}

/// Register a function to be called before each test case.
///
/// Use this to reset global state in the library under test
/// (e.g., RNG seeds, caches). The callback is called in both
/// fork and nofork modes, before the test function runs.
///
/// Pass NULL to clear a previously registered callback.
///
/// # Safety
/// `setup_fn` must be a valid function pointer or NULL.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_set_case_setup(
    setup_fn: Option<unsafe extern "C-unwind" fn()>,
) {
    unsafe { CASE_SETUP_FN = setup_fn; }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_run_test(test_fn: CTestFn) {
    run_hegel(Hegel::new(move |tc: TestCase| {
        run_forked(test_fn, tc);
    }));
}

/// Run a hegel property test with a custom number of test cases.
/// Each test case runs in a forked child process.
///
/// # Safety
/// `test_fn` must be a valid C function pointer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_run_test_n(test_fn: CTestFn, n_cases: u64) {
    run_hegel(Hegel::new(move |tc: TestCase| {
        run_forked(test_fn, tc);
    })
    .settings(hegel::Settings::new().test_cases(n_cases)));
}

/// Like hegel_run_test, but returns 0 on success, 1 on failure instead
/// of calling exit(1).  Allows multiple tests in one binary.
///
/// # Safety
/// `test_fn` must be a valid C function pointer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_run_test_result(test_fn: CTestFn) -> c_int {
    run_hegel_result(Hegel::new(move |tc: TestCase| {
        run_forked(test_fn, tc);
    }))
}

/// Like hegel_run_test_n, but returns 0/1 instead of exiting.
///
/// # Safety
/// `test_fn` must be a valid C function pointer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_run_test_result_n(test_fn: CTestFn, n_cases: u64) -> c_int {
    run_hegel_result(Hegel::new(move |tc: TestCase| {
        run_forked(test_fn, tc);
    })
    .settings(hegel::Settings::new().test_cases(n_cases)))
}

/// Run a hegel property test WITHOUT fork isolation.
///
/// **Not recommended for general use.** A crash (SIGSEGV, SIGABRT)
/// kills the entire process — no shrinking, no recovery.
/// Provided for benchmarking fork overhead.
///
/// # Safety
/// `test_fn` must be a valid C function pointer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_run_test_nofork(test_fn: CTestFn) {
    run_hegel(Hegel::new(move |tc: TestCase| {
        call_case_setup();
        let mut htc = HegelTestCase { tc };
        unsafe { test_fn(&mut htc) };
    }));
}

/// Run a hegel property test WITHOUT fork isolation, custom case count.
///
/// **Not recommended for general use.** See `hegel_run_test_nofork`.
///
/// # Safety
/// `test_fn` must be a valid C function pointer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_run_test_nofork_n(test_fn: CTestFn, n_cases: u64) {
    run_hegel(Hegel::new(move |tc: TestCase| {
        call_case_setup();
        let mut htc = HegelTestCase { tc };
        unsafe { test_fn(&mut htc) };
    })
    .settings(hegel::Settings::new().test_cases(n_cases)));
}

/**************************************/
/*                                    */
/* Test suite API.                    */
/*                                    */
/**************************************/

/// A test suite that runs multiple tests in one binary, sharing a single
/// Hegel server process.  Uses hegel_run_test_result internally.
pub struct HegelSuite {
    tests: Vec<(String, CTestFn)>,
}

/// Create a new empty test suite.
#[unsafe(no_mangle)]
pub extern "C" fn hegel_suite_new() -> *mut HegelSuite {
    Box::into_raw(Box::new(HegelSuite { tests: Vec::new() }))
}

/// Add a named test to the suite.
///
/// # Safety
/// `suite` must be a valid pointer from hegel_suite_new.
/// `name` must be a valid null-terminated C string.
/// `test_fn` must be a valid C function pointer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_suite_add(
    suite: *mut HegelSuite,
    name: *const c_char,
    test_fn: CTestFn,
) {
    let s = unsafe { &mut *suite };
    let n = if name.is_null() {
        "<unnamed>".to_string()
    } else {
        unsafe { CStr::from_ptr(name) }
            .to_string_lossy()
            .into_owned()
    };
    s.tests.push((n, test_fn));
}

/// Run all tests in the suite.  Prints results to stderr.
/// Returns 0 if all passed, 1 if any failed.
///
/// # Safety
/// `suite` must be a valid pointer from hegel_suite_new.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_suite_run(suite: *mut HegelSuite) -> c_int {
    let s = unsafe { &*suite };
    let total = s.tests.len();
    let mut passed = 0usize;
    let mut failed = 0usize;

    eprintln!();
    eprintln!("==== Running {} test(s) ====", total);
    for (name, test_fn) in &s.tests {
        eprint!("  {:<35}", name);
        let rc = run_hegel_result(Hegel::new({
            let f = *test_fn;
            move |tc: TestCase| { run_forked(f, tc); }
        }));
        if rc == 0 {
            eprintln!("OK");
            passed += 1;
        } else {
            eprintln!("FAIL");
            failed += 1;
        }
    }
    eprintln!();
    eprintln!("==== Results: {}/{} passed, {} failed ====", passed, total, failed);
    if failed > 0 { 1 } else { 0 }
}

/// Free a test suite.  Safe to call with NULL.
///
/// # Safety
/// `suite` must be a valid pointer from hegel_suite_new, or NULL.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_suite_free(suite: *mut HegelSuite) {
    if !suite.is_null() {
        unsafe { drop(Box::from_raw(suite)); }
    }
}

/// Fail the current test case with a message. Triggers hegel shrinking.
///
/// In fork-child mode: sends the failure message to the parent via pipe
/// and calls `_exit(1)`. In direct mode: panics (for non-forked use).
///
/// # Safety
/// `msg` must be a valid null-terminated C string, or NULL.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_fail(msg: *const c_char) {
    let message = if msg.is_null() {
        "hegel_fail called".to_string()
    } else {
        unsafe { CStr::from_ptr(msg) }
            .to_string_lossy()
            .into_owned()
    };

    if unsafe { IN_FORK_CHILD } {
        let msg_bytes = message.as_bytes();
        let len = msg_bytes.len() as u32;
        let fd = unsafe { FORK_REQ_WR };
        pipe_write_all(fd, &[MSG_FAIL]);
        pipe_write_all(fd, &len.to_le_bytes());
        pipe_write_all(fd, msg_bytes);
        unsafe { libc::_exit(1); }
    }

    panic!("{}", message);
}

/// Assert a condition. If false, fail with a message (triggers shrinking).
///
/// # Safety
/// `msg` must be a valid null-terminated C string, or NULL.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_assert(
    condition: c_int,
    msg: *const c_char,
) {
    if condition == 0 {
        unsafe { hegel_fail(msg) };
    }
}

/// Emit a *health-check* failure — distinct from hegel_fail.
///
/// Semantics:
///   hegel_fail         → the CODE UNDER TEST is broken; shrink and
///                        show the user a minimal counterexample.
///   hegel_health_fail  → the TEST SETUP is broken (e.g. the schema's
///                        recursion probabilities mean every draw hits
///                        the depth bound); don't shrink — just report.
///
/// LIMITATION (hegel-c v0, Rust-bound via hegeltest 0.4.3):
///
/// hegeltest 0.4.3 has **no public API** for emitting a "stop, don't
/// shrink" signal from a test function.  The two internal sentinel
/// strings that would suppress shrinking (`ASSUME_FAIL_STRING`,
/// `STOP_TEST_STRING`) are `pub(crate)` and cause cases to be
/// *discarded* rather than *failed* — wrong semantics for a health
/// check, which must halt the test run.
///
/// So this function currently takes the pragmatic path:
///
///   1. Prefix the message with `"Health check failure: "`.
///   2. Route through the same mechanism as hegel_fail (MSG_FAIL in
///      fork mode; panic in nofork mode).
///   3. Shrinking DOES run — but because every shrunk case also trips
///      the same health-fail path, shrinking converges on the same
///      message.  The final user-visible output is correct, and
///      downstream tooling (e.g. the Makefile TESTS_HEALTH category)
///      grepping stderr for `"Health check failure"` matches as
///      expected.  Cost: some wasted shrinking cycles.
///
/// A proper fix requires one of:
///   - hegeltest exposing a public `fail_health_check` API upstream,
///   - or a pure-C hegel binding that speaks the hegeltest wire
///     protocol directly (skips the Rust crate altogether).  In
///     either case the C surface `hegel_health_fail` stays identical
///     — only the implementation changes.
///
/// # Safety
/// `msg` must be a valid null-terminated C string, or NULL.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_health_fail(msg: *const c_char) {
    let message = if msg.is_null() {
        "hegel_health_fail called".to_string()
    } else {
        unsafe { CStr::from_ptr(msg) }
            .to_string_lossy()
            .into_owned()
    };
    let prefixed = format!("Health check failure: {}", message);

    if unsafe { IN_FORK_CHILD } {
        let msg_bytes = prefixed.as_bytes();
        let len = msg_bytes.len() as u32;
        let fd = unsafe { FORK_REQ_WR };
        pipe_write_all(fd, &[MSG_FAIL]);
        pipe_write_all(fd, &len.to_le_bytes());
        pipe_write_all(fd, msg_bytes);
        unsafe { libc::_exit(1); }
    }

    panic!("{}", prefixed);
}

/// Draw a random integer in [min_val, max_val].
///
/// # Safety
/// `tc` must be a valid pointer obtained from a hegel test callback.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_draw_int(
    tc: *mut HegelTestCase,
    min_val: c_int,
    max_val: c_int,
) -> c_int {
    if unsafe { IN_FORK_CHILD } {
        let fd_w = unsafe { FORK_REQ_WR };
        let fd_r = unsafe { FORK_RESP_RD };
        let mut req = [0u8; 9];
        req[0] = MSG_DRAW_INT;
        req[1..5].copy_from_slice(&min_val.to_le_bytes());
        req[5..9].copy_from_slice(&max_val.to_le_bytes());
        pipe_write_all(fd_w, &req);
        let mut resp = [0u8; 4];
        if !pipe_read_exact(fd_r, &mut resp) { child_abandoned(); }
        return c_int::from_le_bytes(resp);
    }
    let htc = unsafe { &*tc };
    htc.tc.draw(gs::integers::<c_int>().min_value(min_val).max_value(max_val))
}

/// Draw a random i64 in [min_val, max_val].
///
/// # Safety
/// `tc` must be a valid pointer obtained from a hegel test callback.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_draw_i64(
    tc: *mut HegelTestCase,
    min_val: i64,
    max_val: i64,
) -> i64 {
    if unsafe { IN_FORK_CHILD } {
        let fd_w = unsafe { FORK_REQ_WR };
        let fd_r = unsafe { FORK_RESP_RD };
        let mut req = [0u8; 17];
        req[0] = MSG_DRAW_I64;
        req[1..9].copy_from_slice(&min_val.to_le_bytes());
        req[9..17].copy_from_slice(&max_val.to_le_bytes());
        pipe_write_all(fd_w, &req);
        let mut resp = [0u8; 8];
        if !pipe_read_exact(fd_r, &mut resp) { child_abandoned(); }
        return i64::from_le_bytes(resp);
    }
    let htc = unsafe { &*tc };
    htc.tc.draw(gs::integers::<i64>().min_value(min_val).max_value(max_val))
}

/// Draw a random u64 in [min_val, max_val].
///
/// # Safety
/// `tc` must be a valid pointer obtained from a hegel test callback.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_draw_u64(
    tc: *mut HegelTestCase,
    min_val: u64,
    max_val: u64,
) -> u64 {
    if unsafe { IN_FORK_CHILD } {
        let fd_w = unsafe { FORK_REQ_WR };
        let fd_r = unsafe { FORK_RESP_RD };
        let mut req = [0u8; 17];
        req[0] = MSG_DRAW_U64;
        req[1..9].copy_from_slice(&min_val.to_le_bytes());
        req[9..17].copy_from_slice(&max_val.to_le_bytes());
        pipe_write_all(fd_w, &req);
        let mut resp = [0u8; 8];
        if !pipe_read_exact(fd_r, &mut resp) { child_abandoned(); }
        return u64::from_le_bytes(resp);
    }
    let htc = unsafe { &*tc };
    htc.tc.draw(gs::integers::<u64>().min_value(min_val).max_value(max_val))
}

/// Draw a random usize in [min_val, max_val].
///
/// # Safety
/// `tc` must be a valid pointer obtained from a hegel test callback.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_draw_usize(
    tc: *mut HegelTestCase,
    min_val: usize,
    max_val: usize,
) -> usize {
    if unsafe { IN_FORK_CHILD } {
        let fd_w = unsafe { FORK_REQ_WR };
        let fd_r = unsafe { FORK_RESP_RD };
        let mut req = [0u8; 17];
        req[0] = MSG_DRAW_USIZE;
        req[1..9].copy_from_slice(&min_val.to_le_bytes());
        req[9..17].copy_from_slice(&max_val.to_le_bytes());
        pipe_write_all(fd_w, &req);
        let mut resp = [0u8; 8];
        if !pipe_read_exact(fd_r, &mut resp) { child_abandoned(); }
        return usize::from_le_bytes(resp);
    }
    let htc = unsafe { &*tc };
    htc.tc.draw(gs::integers::<usize>().min_value(min_val).max_value(max_val))
}

/// Draw a random f64 in [min_val, max_val].
///
/// # Safety
/// `tc` must be a valid pointer obtained from a hegel test callback.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_draw_double(
    tc: *mut HegelTestCase,
    min_val: f64,
    max_val: f64,
) -> f64 {
    if unsafe { IN_FORK_CHILD } {
        let fd_w = unsafe { FORK_REQ_WR };
        let fd_r = unsafe { FORK_RESP_RD };
        let mut req = [0u8; 17];
        req[0] = MSG_DRAW_DOUBLE;
        req[1..9].copy_from_slice(&min_val.to_le_bytes());
        req[9..17].copy_from_slice(&max_val.to_le_bytes());
        pipe_write_all(fd_w, &req);
        let mut resp = [0u8; 8];
        if !pipe_read_exact(fd_r, &mut resp) { child_abandoned(); }
        return f64::from_le_bytes(resp);
    }
    let htc = unsafe { &*tc };
    htc.tc.draw(gs::floats::<f64>().min_value(min_val).max_value(max_val))
}

/// Draw a random f32 in [min_val, max_val].
///
/// # Safety
/// `tc` must be a valid pointer obtained from a hegel test callback.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_draw_float(
    tc: *mut HegelTestCase,
    min_val: f32,
    max_val: f32,
) -> f32 {
    if unsafe { IN_FORK_CHILD } {
        let fd_w = unsafe { FORK_REQ_WR };
        let fd_r = unsafe { FORK_RESP_RD };
        let mut req = [0u8; 9];
        req[0] = MSG_DRAW_FLOAT;
        req[1..5].copy_from_slice(&min_val.to_le_bytes());
        req[5..9].copy_from_slice(&max_val.to_le_bytes());
        pipe_write_all(fd_w, &req);
        let mut resp = [0u8; 4];
        if !pipe_read_exact(fd_r, &mut resp) { child_abandoned(); }
        return f32::from_le_bytes(resp);
    }
    let htc = unsafe { &*tc };
    htc.tc.draw(gs::floats::<f32>().min_value(min_val).max_value(max_val))
}

/// Draw a random text string with length in [min_size, max_size].
/// Writes a null-terminated string to `buf`.  Returns the string
/// length (not counting the null terminator), or 0 if capacity < 1.
///
/// # Safety
/// `tc` must be valid.  `buf` must point to at least `capacity` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_draw_text(
    tc: *mut HegelTestCase,
    min_size: c_int,
    max_size: c_int,
    buf: *mut c_char,
    capacity: c_int,
) -> c_int {
    if capacity < 1 { return 0; }
    let bytes = if unsafe { IN_FORK_CHILD } {
        let fd_w = unsafe { FORK_REQ_WR };
        let fd_r = unsafe { FORK_RESP_RD };
        let mut req = [0u8; 9];
        req[0] = MSG_DRAW_TEXT;
        req[1..5].copy_from_slice(&(min_size as u32).to_le_bytes());
        req[5..9].copy_from_slice(&(max_size as u32).to_le_bytes());
        pipe_write_all(fd_w, &req);
        let mut len_buf = [0u8; 4];
        if !pipe_read_exact(fd_r, &mut len_buf) { child_abandoned(); }
        let len = u32::from_le_bytes(len_buf) as usize;
        let mut str_buf = vec![0u8; len];
        if len > 0 && !pipe_read_exact(fd_r, &mut str_buf) { child_abandoned(); }
        str_buf
    } else {
        let htc = unsafe { &*tc };
        let val: String = htc.tc.draw(
            gs::text().min_size(min_size as usize).max_size(max_size as usize));
        val.into_bytes()
    };
    let copy_len = bytes.len().min((capacity - 1) as usize);
    if copy_len > 0 {
        unsafe { std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf as *mut u8, copy_len); }
    }
    unsafe { *buf.add(copy_len) = 0; }
    copy_len as c_int
}

/// Draw a string matching a regex pattern.
/// Writes a null-terminated string to `buf`.  Returns the string
/// length (not counting the null terminator), or 0 if capacity < 1.
///
/// # Safety
/// `tc` must be valid.  `pattern` must be a valid null-terminated C string.
/// `buf` must point to at least `capacity` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_draw_regex(
    tc: *mut HegelTestCase,
    pattern: *const c_char,
    buf: *mut c_char,
    capacity: c_int,
) -> c_int {
    if capacity < 1 { return 0; }
    let pat = unsafe { CStr::from_ptr(pattern) }.to_string_lossy();
    let bytes = if unsafe { IN_FORK_CHILD } {
        let fd_w = unsafe { FORK_REQ_WR };
        let fd_r = unsafe { FORK_RESP_RD };
        let pat_bytes = pat.as_bytes();
        let mut header = [0u8; 5];
        header[0] = MSG_DRAW_REGEX;
        header[1..5].copy_from_slice(&(pat_bytes.len() as u32).to_le_bytes());
        pipe_write_all(fd_w, &header);
        pipe_write_all(fd_w, pat_bytes);
        let mut len_buf = [0u8; 4];
        if !pipe_read_exact(fd_r, &mut len_buf) { child_abandoned(); }
        let len = u32::from_le_bytes(len_buf) as usize;
        let mut str_buf = vec![0u8; len];
        if len > 0 && !pipe_read_exact(fd_r, &mut str_buf) { child_abandoned(); }
        str_buf
    } else {
        let htc = unsafe { &*tc };
        let val: String = htc.tc.draw(gs::from_regex(&pat));
        val.into_bytes()
    };
    let copy_len = bytes.len().min((capacity - 1) as usize);
    if copy_len > 0 {
        unsafe { std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf as *mut u8, copy_len); }
    }
    unsafe { *buf.add(copy_len) = 0; }
    copy_len as c_int
}

/// Print a debug message that only appears during the final replay of a
/// failing test case (not during generation or shrinking).  Useful for
/// annotating the minimal counterexample with computed values.
///
/// # Safety
/// `tc` must be a valid pointer obtained from a hegel test callback.
/// `msg` must be a valid null-terminated C string, or NULL.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_note(tc: *mut HegelTestCase, msg: *const c_char) {
    let message = if msg.is_null() {
        return;
    } else {
        unsafe { CStr::from_ptr(msg) }
            .to_string_lossy()
    };
    let htc = unsafe { &*tc };
    htc.tc.note(&message);
}

/// Begin a span — group subsequent draws into one logical unit for shrinking.
///
/// Spans tell hegel "the draws between start_span and stop_span belong
/// together." The shrinker can then operate on the span as a whole
/// (delete it, minimize it, swap it) instead of treating each draw
/// independently. Wrap the draws produced by a list element, struct,
/// oneof variant, etc. — anywhere a *group* of draws describes one
/// structural unit. Spans nest; always pair start_span/stop_span.
///
/// `label` identifies the *kind* of span (LIST, ONE_OF, etc.). Built-in
/// labels are 1..15; user code should use values >= HEGEL_SPAN_USER (1024).
///
/// # Safety
/// `tc` must be a valid pointer obtained from a hegel test callback.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_start_span(tc: *mut HegelTestCase, label: u64) {
    if unsafe { IN_FORK_CHILD } {
        let mut req = [0u8; 9];
        req[0] = MSG_START_SPAN;
        req[1..9].copy_from_slice(&label.to_le_bytes());
        pipe_write_all(unsafe { FORK_REQ_WR }, &req);
        return;
    }
    let htc = unsafe { &*tc };
    htc.tc.start_span(label);
}

/// End the current span.  If `discard` is non-zero, the span and its
/// bytes are marked as discarded — used by filter/assume style code to
/// tell the shrinker "this group was a dead end, don't bother
/// minimizing it."  For ordinary structural grouping, pass 0.
///
/// # Safety
/// `tc` must be a valid pointer obtained from a hegel test callback.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_stop_span(tc: *mut HegelTestCase, discard: c_int) {
    if unsafe { IN_FORK_CHILD } {
        let mut req = [0u8; 2];
        req[0] = MSG_STOP_SPAN;
        req[1] = if discard != 0 { 1 } else { 0 };
        pipe_write_all(unsafe { FORK_REQ_WR }, &req);
        return;
    }
    let htc = unsafe { &*tc };
    htc.tc.stop_span(discard != 0);
}

/// Assume a condition. If false, this test case is discarded (not a failure).
///
/// # Safety
/// `tc` must be a valid pointer obtained from a hegel test callback.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_assume(tc: *mut HegelTestCase, condition: c_int) {
    if condition != 0 {
        return;
    }

    if unsafe { IN_FORK_CHILD } {
        pipe_write_all(unsafe { FORK_REQ_WR }, &[MSG_ASSUME]);
        unsafe { libc::_exit(77); }
    }

    let htc = unsafe { &*tc };
    htc.tc.assume(false);
}

/**************************************/
/*                                    */
/* Composable generator system.       */
/*                                    */
/**************************************/

/*
** Generators are opaque Rust objects created via hegel_gen_* factory
** functions and drawn from via hegel_gen_draw_* functions.  They form
** a tree: combinators like one_of contain sub-generators, and draw
** functions recursively evaluate the tree, calling the hegel_draw_*
** primitives at each leaf node.
**
** Because leaf draws go through the existing fork IPC (MSG_DRAW_*),
** generators work transparently in both fork and non-fork modes.
** Hegel sees the individual primitive draws and can shrink them
** independently, which effectively shrinks the generator tree.
**
** Ownership: factory functions return heap-allocated generators.
** Combinators (hegel_gen_one_of) consume their sub-generators —
** do NOT free sub-generators after passing them to a combinator.
** Call hegel_gen_free on the root generator to free the entire tree.
*/

/// Generator description tree.  Evaluated recursively by draw functions.
pub enum HegelGen {
    Int { min: c_int, max: c_int },
    I64 { min: i64, max: i64 },
    U64 { min: u64, max: u64 },
    Float { min: f32, max: f32 },
    Double { min: f64, max: f64 },
    Bool,
    /// Pick one sub-generator at random, then draw from it.
    /// All alternatives must produce the same type.
    OneOf { alternatives: Vec<Box<HegelGen>> },
    /// Draw an index in [0, count).  The C caller maps it to a value.
    /// Equivalent to Int { min: 0, max: count - 1 } but expresses
    /// intent: "pick from a set of N items."
    SampledFrom { count: c_int },
    /// Generate a random text string with length in [min_size, max_size].
    Text { min_size: usize, max_size: usize },
    /// Generate a string matching a regex pattern.
    Regex { pattern: String },
    /// Optionally draw from the inner generator.
    /// Draws a bool first: if true, draws from inner; if false, produces nothing.
    Optional { inner: Box<HegelGen> },
    /// Transform drawn values: draw from source, pass through a C callback.
    /// The callback signature and stored pointer are type-specific; the draw
    /// function casts and calls them.
    MapInt {
        source: Box<HegelGen>,
        map_fn: unsafe extern "C-unwind" fn(c_int, *mut libc::c_void) -> c_int,
        ctx: *mut libc::c_void,
    },
    MapI64 {
        source: Box<HegelGen>,
        map_fn: unsafe extern "C-unwind" fn(i64, *mut libc::c_void) -> i64,
        ctx: *mut libc::c_void,
    },
    MapDouble {
        source: Box<HegelGen>,
        map_fn: unsafe extern "C-unwind" fn(f64, *mut libc::c_void) -> f64,
        ctx: *mut libc::c_void,
    },
    /// Only keep values that satisfy a C predicate (returns non-zero).
    /// Retries up to 3 times, then discards the test case via hegel_assume.
    FilterInt {
        source: Box<HegelGen>,
        pred_fn: unsafe extern "C-unwind" fn(c_int, *mut libc::c_void) -> c_int,
        ctx: *mut libc::c_void,
    },
    FilterI64 {
        source: Box<HegelGen>,
        pred_fn: unsafe extern "C-unwind" fn(i64, *mut libc::c_void) -> c_int,
        ctx: *mut libc::c_void,
    },
    FilterDouble {
        source: Box<HegelGen>,
        pred_fn: unsafe extern "C-unwind" fn(f64, *mut libc::c_void) -> c_int,
        ctx: *mut libc::c_void,
    },
    /// Draw a value, use it to create a new generator via a C callback,
    /// then draw from that generator.  The callback must return a fresh
    /// generator each time; it is drawn from once and then freed.
    FlatMapInt {
        source: Box<HegelGen>,
        flat_map_fn: unsafe extern "C-unwind" fn(c_int, *mut libc::c_void) -> *mut HegelGen,
        ctx: *mut libc::c_void,
    },
    FlatMapI64 {
        source: Box<HegelGen>,
        flat_map_fn: unsafe extern "C-unwind" fn(i64, *mut libc::c_void) -> *mut HegelGen,
        ctx: *mut libc::c_void,
    },
    FlatMapDouble {
        source: Box<HegelGen>,
        flat_map_fn: unsafe extern "C-unwind" fn(f64, *mut libc::c_void) -> *mut HegelGen,
        ctx: *mut libc::c_void,
    },
}

/* ---- Factory functions ---- */

/// Create a generator that produces integers in [min_val, max_val].
#[unsafe(no_mangle)]
pub extern "C" fn hegel_gen_int(min_val: c_int, max_val: c_int) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::Int { min: min_val, max: max_val }))
}

/// Create a generator that produces i64 values in [min_val, max_val].
#[unsafe(no_mangle)]
pub extern "C" fn hegel_gen_i64(min_val: i64, max_val: i64) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::I64 { min: min_val, max: max_val }))
}

/// Create a generator that produces u64 values in [min_val, max_val].
#[unsafe(no_mangle)]
pub extern "C" fn hegel_gen_u64(min_val: u64, max_val: u64) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::U64 { min: min_val, max: max_val }))
}

/// Create a generator that produces f64 values in [min_val, max_val].
#[unsafe(no_mangle)]
pub extern "C" fn hegel_gen_double(min_val: f64, max_val: f64) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::Double { min: min_val, max: max_val }))
}

/// Create a generator that produces 0 or 1.
#[unsafe(no_mangle)]
pub extern "C" fn hegel_gen_bool() -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::Bool))
}

/// Create a generator that picks one of `count` sub-generators at random
/// and draws from it.  All sub-generators must produce the same type.
///
/// **Ownership transfer**: the sub-generator pointers in `gens[0..count]`
/// are consumed.  Do NOT free them after this call — they will be freed
/// when the returned one_of generator is freed.
///
/// # Safety
/// `gens` must point to `count` valid HegelGen pointers.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_gen_one_of(
    gens: *const *mut HegelGen,
    count: c_int,
) -> *mut HegelGen {
    let mut alternatives = Vec::with_capacity(count as usize);
    for i in 0..count as usize {
        let gen_ptr = unsafe { *gens.add(i) };
        alternatives.push(unsafe { Box::from_raw(gen_ptr) });
    }
    Box::into_raw(Box::new(HegelGen::OneOf { alternatives }))
}

/// Create a generator that draws an index in [0, count).
/// The C caller maps the index to an actual value (string, enum, etc.).
///
/// Semantically equivalent to `hegel_gen_int(0, count - 1)` but
/// expresses the intent "pick from a set of N items."
#[unsafe(no_mangle)]
pub extern "C" fn hegel_gen_sampled_from(count: c_int) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::SampledFrom { count }))
}

/// Create a generator that produces f32 values in [min_val, max_val].
#[unsafe(no_mangle)]
pub extern "C" fn hegel_gen_float(min_val: f32, max_val: f32) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::Float { min: min_val, max: max_val }))
}

/// Create a generator that produces random text strings
/// with length in [min_size, max_size].
#[unsafe(no_mangle)]
pub extern "C" fn hegel_gen_text(min_size: c_int, max_size: c_int) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::Text {
        min_size: min_size as usize,
        max_size: max_size as usize,
    }))
}

/// Create a generator that produces strings matching a regex pattern.
/// The pattern is copied — the caller may free the original after this call.
///
/// # Safety
/// `pattern` must be a valid null-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_gen_regex(pattern: *const c_char) -> *mut HegelGen {
    let pat = unsafe { CStr::from_ptr(pattern) }.to_string_lossy().into_owned();
    Box::into_raw(Box::new(HegelGen::Regex { pattern: pat }))
}

/// Create a generator that optionally draws from `inner`.
/// Draws a bool first: if true, produces a value from `inner`;
/// if false, produces nothing (draw functions return 0).
///
/// **Ownership transfer**: `inner` is consumed.  Do NOT free it
/// after this call.
///
/// # Safety
/// `inner` must be a valid HegelGen pointer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_gen_optional(inner: *mut HegelGen) -> *mut HegelGen {
    let inner_box = unsafe { Box::from_raw(inner) };
    Box::into_raw(Box::new(HegelGen::Optional { inner: inner_box }))
}

/* ---- map / filter / flat_map factories ---- */

/*
** These combinators transform generators using C callbacks.
** Each takes a source generator (ownership transferred), a function
** pointer, and a void* context that is passed to the callback on
** every draw.  The caller manages the lifetime of whatever ctx
** points to — it must outlive the generator.
*/

/// Transform int values: draw from `source`, pass through `map_fn`.
/// Takes ownership of `source`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_gen_map_int(
    source: *mut HegelGen,
    map_fn: unsafe extern "C-unwind" fn(c_int, *mut libc::c_void) -> c_int,
    ctx: *mut libc::c_void,
) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::MapInt {
        source: unsafe { Box::from_raw(source) }, map_fn, ctx,
    }))
}

/// Transform i64 values.  Takes ownership of `source`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_gen_map_i64(
    source: *mut HegelGen,
    map_fn: unsafe extern "C-unwind" fn(i64, *mut libc::c_void) -> i64,
    ctx: *mut libc::c_void,
) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::MapI64 {
        source: unsafe { Box::from_raw(source) }, map_fn, ctx,
    }))
}

/// Transform f64 values.  Takes ownership of `source`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_gen_map_double(
    source: *mut HegelGen,
    map_fn: unsafe extern "C-unwind" fn(f64, *mut libc::c_void) -> f64,
    ctx: *mut libc::c_void,
) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::MapDouble {
        source: unsafe { Box::from_raw(source) }, map_fn, ctx,
    }))
}

/// Keep only int values where `pred_fn` returns non-zero.
/// Retries up to 3 times, then discards the test case.
/// Takes ownership of `source`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_gen_filter_int(
    source: *mut HegelGen,
    pred_fn: unsafe extern "C-unwind" fn(c_int, *mut libc::c_void) -> c_int,
    ctx: *mut libc::c_void,
) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::FilterInt {
        source: unsafe { Box::from_raw(source) }, pred_fn, ctx,
    }))
}

/// Keep only i64 values where `pred_fn` returns non-zero.
/// Takes ownership of `source`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_gen_filter_i64(
    source: *mut HegelGen,
    pred_fn: unsafe extern "C-unwind" fn(i64, *mut libc::c_void) -> c_int,
    ctx: *mut libc::c_void,
) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::FilterI64 {
        source: unsafe { Box::from_raw(source) }, pred_fn, ctx,
    }))
}

/// Keep only f64 values where `pred_fn` returns non-zero.
/// Takes ownership of `source`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_gen_filter_double(
    source: *mut HegelGen,
    pred_fn: unsafe extern "C-unwind" fn(f64, *mut libc::c_void) -> c_int,
    ctx: *mut libc::c_void,
) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::FilterDouble {
        source: unsafe { Box::from_raw(source) }, pred_fn, ctx,
    }))
}

/// Draw an int, use it to create a new generator via `flat_map_fn`,
/// then draw from that generator.  The callback must return a fresh
/// generator (via hegel_gen_* factories); it is drawn from once and freed.
/// Takes ownership of `source`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_gen_flat_map_int(
    source: *mut HegelGen,
    flat_map_fn: unsafe extern "C-unwind" fn(c_int, *mut libc::c_void) -> *mut HegelGen,
    ctx: *mut libc::c_void,
) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::FlatMapInt {
        source: unsafe { Box::from_raw(source) }, flat_map_fn, ctx,
    }))
}

/// Draw an i64, use it to create and draw from a new generator.
/// Takes ownership of `source`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_gen_flat_map_i64(
    source: *mut HegelGen,
    flat_map_fn: unsafe extern "C-unwind" fn(i64, *mut libc::c_void) -> *mut HegelGen,
    ctx: *mut libc::c_void,
) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::FlatMapI64 {
        source: unsafe { Box::from_raw(source) }, flat_map_fn, ctx,
    }))
}

/// Draw an f64, use it to create and draw from a new generator.
/// Takes ownership of `source`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_gen_flat_map_double(
    source: *mut HegelGen,
    flat_map_fn: unsafe extern "C-unwind" fn(f64, *mut libc::c_void) -> *mut HegelGen,
    ctx: *mut libc::c_void,
) -> *mut HegelGen {
    Box::into_raw(Box::new(HegelGen::FlatMapDouble {
        source: unsafe { Box::from_raw(source) }, flat_map_fn, ctx,
    }))
}

/// Free a generator and all sub-generators it owns.
/// Safe to call with NULL (no-op).
///
/// # Safety
/// `gen` must be a pointer returned by a hegel_gen_* factory function,
/// or NULL.  Must not be freed twice.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hegel_gen_free(gn: *mut HegelGen) {
    if !gn.is_null() {
        drop(unsafe { Box::from_raw(gn) });
    }
}

/* ---- Internal draw implementations ---- */

/*
** These functions recursively walk the generator tree.  At each leaf,
** they call the corresponding hegel_draw_* primitive, which handles
** the fork IPC transparently.  Combinators (OneOf) draw an index
** first, then recurse into the selected sub-generator.
*/

unsafe fn gen_draw_int_impl(tc: *mut HegelTestCase, gn: &HegelGen) -> c_int {
    match gn {
        HegelGen::Int { min, max } =>
            unsafe { hegel_draw_int(tc, *min, *max) },
        HegelGen::Bool =>
            unsafe { hegel_draw_int(tc, 0, 1) },
        HegelGen::SampledFrom { count } =>
            unsafe { hegel_draw_int(tc, 0, *count - 1) },
        HegelGen::OneOf { alternatives } => {
            let idx = unsafe { hegel_draw_int(tc, 0, alternatives.len() as c_int - 1) };
            unsafe { gen_draw_int_impl(tc, &alternatives[idx as usize]) }
        }
        HegelGen::MapInt { source, map_fn, ctx } => {
            let val = unsafe { gen_draw_int_impl(tc, source) };
            unsafe { map_fn(val, *ctx) }
        }
        HegelGen::FilterInt { source, pred_fn, ctx } => {
            for _ in 0..3 {
                let val = unsafe { gen_draw_int_impl(tc, source) };
                if unsafe { pred_fn(val, *ctx) } != 0 { return val; }
            }
            unsafe { hegel_assume(tc, 0) };
            unreachable!()
        }
        HegelGen::FlatMapInt { source, flat_map_fn, ctx } => {
            let val = unsafe { gen_draw_int_impl(tc, source) };
            let derived = unsafe { flat_map_fn(val, *ctx) };
            let result = unsafe { gen_draw_int_impl(tc, &*derived) };
            drop(unsafe { Box::from_raw(derived) });
            result
        }
        _ => panic!("hegel_gen_draw_int: generator does not produce int"),
    }
}

unsafe fn gen_draw_i64_impl(tc: *mut HegelTestCase, gn: &HegelGen) -> i64 {
    match gn {
        HegelGen::I64 { min, max } =>
            unsafe { hegel_draw_i64(tc, *min, *max) },
        HegelGen::OneOf { alternatives } => {
            let idx = unsafe { hegel_draw_int(tc, 0, alternatives.len() as c_int - 1) };
            unsafe { gen_draw_i64_impl(tc, &alternatives[idx as usize]) }
        }
        HegelGen::MapI64 { source, map_fn, ctx } => {
            let val = unsafe { gen_draw_i64_impl(tc, source) };
            unsafe { map_fn(val, *ctx) }
        }
        HegelGen::FilterI64 { source, pred_fn, ctx } => {
            for _ in 0..3 {
                let val = unsafe { gen_draw_i64_impl(tc, source) };
                if unsafe { pred_fn(val, *ctx) } != 0 { return val; }
            }
            unsafe { hegel_assume(tc, 0) };
            unreachable!()
        }
        HegelGen::FlatMapI64 { source, flat_map_fn, ctx } => {
            let val = unsafe { gen_draw_i64_impl(tc, source) };
            let derived = unsafe { flat_map_fn(val, *ctx) };
            let result = unsafe { gen_draw_i64_impl(tc, &*derived) };
            drop(unsafe { Box::from_raw(derived) });
            result
        }
        _ => panic!("hegel_gen_draw_i64: generator does not produce i64"),
    }
}

unsafe fn gen_draw_u64_impl(tc: *mut HegelTestCase, gn: &HegelGen) -> u64 {
    match gn {
        HegelGen::U64 { min, max } =>
            unsafe { hegel_draw_u64(tc, *min, *max) },
        HegelGen::OneOf { alternatives } => {
            let idx = unsafe { hegel_draw_int(tc, 0, alternatives.len() as c_int - 1) };
            unsafe { gen_draw_u64_impl(tc, &alternatives[idx as usize]) }
        }
        _ => panic!("hegel_gen_draw_u64: generator does not produce u64"),
    }
}

unsafe fn gen_draw_double_impl(tc: *mut HegelTestCase, gn: &HegelGen) -> f64 {
    match gn {
        HegelGen::Double { min, max } =>
            unsafe { hegel_draw_double(tc, *min, *max) },
        HegelGen::OneOf { alternatives } => {
            let idx = unsafe { hegel_draw_int(tc, 0, alternatives.len() as c_int - 1) };
            unsafe { gen_draw_double_impl(tc, &alternatives[idx as usize]) }
        }
        HegelGen::MapDouble { source, map_fn, ctx } => {
            let val = unsafe { gen_draw_double_impl(tc, source) };
            unsafe { map_fn(val, *ctx) }
        }
        HegelGen::FilterDouble { source, pred_fn, ctx } => {
            for _ in 0..3 {
                let val = unsafe { gen_draw_double_impl(tc, source) };
                if unsafe { pred_fn(val, *ctx) } != 0 { return val; }
            }
            unsafe { hegel_assume(tc, 0) };
            unreachable!()
        }
        HegelGen::FlatMapDouble { source, flat_map_fn, ctx } => {
            let val = unsafe { gen_draw_double_impl(tc, source) };
            let derived = unsafe { flat_map_fn(val, *ctx) };
            let result = unsafe { gen_draw_double_impl(tc, &*derived) };
            drop(unsafe { Box::from_raw(derived) });
            result
        }
        _ => panic!("hegel_gen_draw_double: generator does not produce double"),
    }
}

unsafe fn gen_draw_float_impl(tc: *mut HegelTestCase, gn: &HegelGen) -> f32 {
    match gn {
        HegelGen::Float { min, max } =>
            unsafe { hegel_draw_float(tc, *min, *max) },
        HegelGen::OneOf { alternatives } => {
            let idx = unsafe { hegel_draw_int(tc, 0, alternatives.len() as c_int - 1) };
            unsafe { gen_draw_float_impl(tc, &alternatives[idx as usize]) }
        }
        _ => panic!("hegel_gen_draw_float: generator does not produce float"),
    }
}

/// Internal: draw a string from a Text or Regex generator into `buf`.
/// Returns the number of bytes written (not counting null terminator).
unsafe fn gen_draw_text_impl(
    tc: *mut HegelTestCase,
    gn: &HegelGen,
    buf: *mut c_char,
    capacity: c_int,
) -> c_int {
    if capacity < 1 { return 0; }
    match gn {
        HegelGen::Text { min_size, max_size } =>
            unsafe { hegel_draw_text(tc, *min_size as c_int, *max_size as c_int, buf, capacity) },
        HegelGen::Regex { pattern } => {
            let c_str = std::ffi::CString::new(pattern.as_str()).unwrap();
            unsafe { hegel_draw_regex(tc, c_str.as_ptr(), buf, capacity) }
        }
        HegelGen::OneOf { alternatives } => {
            let idx = unsafe { hegel_draw_int(tc, 0, alternatives.len() as c_int - 1) };
            unsafe { gen_draw_text_impl(tc, &alternatives[idx as usize], buf, capacity) }
        }
        _ => panic!("hegel_gen_draw_text: generator does not produce text"),
    }
}

/* ---- C-facing draw functions ---- */

/// Draw an int from a generator.
///
/// Compatible generators: hegel_gen_int, hegel_gen_bool, hegel_gen_sampled_from,
/// hegel_gen_one_of (if all alternatives produce int).
///
/// # Safety
/// `tc` and `gen` must be valid pointers.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_int(
    tc: *mut HegelTestCase,
    gn: *const HegelGen,
) -> c_int {
    unsafe { gen_draw_int_impl(tc, &*gn) }
}

/// Draw an i64 from a generator.
///
/// # Safety
/// `tc` and `gen` must be valid pointers.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_i64(
    tc: *mut HegelTestCase,
    gn: *const HegelGen,
) -> i64 {
    unsafe { gen_draw_i64_impl(tc, &*gn) }
}

/// Draw a u64 from a generator.
///
/// # Safety
/// `tc` and `gen` must be valid pointers.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_u64(
    tc: *mut HegelTestCase,
    gn: *const HegelGen,
) -> u64 {
    unsafe { gen_draw_u64_impl(tc, &*gn) }
}

/// Draw an f64 from a generator.
///
/// # Safety
/// `tc` and `gen` must be valid pointers.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_double(
    tc: *mut HegelTestCase,
    gn: *const HegelGen,
) -> f64 {
    unsafe { gen_draw_double_impl(tc, &*gn) }
}

/// Draw a boolean (0 or 1) from a generator.
/// Shorthand for hegel_gen_draw_int with a bool generator.
///
/// # Safety
/// `tc` and `gen` must be valid pointers.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_bool(
    tc: *mut HegelTestCase,
    gn: *const HegelGen,
) -> c_int {
    unsafe { gen_draw_int_impl(tc, &*gn) }
}

/// Draw an f32 from a generator.
///
/// # Safety
/// `tc` and `gn` must be valid pointers.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_float(
    tc: *mut HegelTestCase,
    gn: *const HegelGen,
) -> f32 {
    unsafe { gen_draw_float_impl(tc, &*gn) }
}

/// Draw a text string from a Text or Regex generator.
/// Writes a null-terminated string to `buf`.  Returns the string
/// length (not counting the null terminator), or 0 if capacity < 1.
///
/// # Safety
/// `tc` and `gn` must be valid pointers.  `buf` must point to at
/// least `capacity` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_text(
    tc: *mut HegelTestCase,
    gn: *const HegelGen,
    buf: *mut c_char,
    capacity: c_int,
) -> c_int {
    unsafe { gen_draw_text_impl(tc, &*gn, buf, capacity) }
}

/// Draw an optional int.  Draws a bool first: if true, draws from the
/// inner generator and writes the value to `*out`, returning 1.
/// If false (or if `gn` is not Optional), returns 0 and `*out` is unchanged.
///
/// For non-Optional generators, always draws and returns 1.
///
/// # Safety
/// `tc`, `gn`, and `out` must be valid pointers.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_optional_int(
    tc: *mut HegelTestCase,
    gn: *const HegelGen,
    out: *mut c_int,
) -> c_int {
    let gn = unsafe { &*gn };
    match gn {
        HegelGen::Optional { inner } => {
            let present = unsafe { hegel_draw_int(tc, 0, 1) };
            if present != 0 {
                unsafe { *out = gen_draw_int_impl(tc, inner) };
                1
            } else {
                0
            }
        }
        _ => {
            unsafe { *out = gen_draw_int_impl(tc, gn) };
            1
        }
    }
}

/// Draw an optional i64.  See `hegel_gen_draw_optional_int` for semantics.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_optional_i64(
    tc: *mut HegelTestCase,
    gn: *const HegelGen,
    out: *mut i64,
) -> c_int {
    let gn = unsafe { &*gn };
    match gn {
        HegelGen::Optional { inner } => {
            let present = unsafe { hegel_draw_int(tc, 0, 1) };
            if present != 0 {
                unsafe { *out = gen_draw_i64_impl(tc, inner) };
                1
            } else {
                0
            }
        }
        _ => {
            unsafe { *out = gen_draw_i64_impl(tc, gn) };
            1
        }
    }
}

/// Draw an optional u64.  See `hegel_gen_draw_optional_int` for semantics.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_optional_u64(
    tc: *mut HegelTestCase,
    gn: *const HegelGen,
    out: *mut u64,
) -> c_int {
    let gn = unsafe { &*gn };
    match gn {
        HegelGen::Optional { inner } => {
            let present = unsafe { hegel_draw_int(tc, 0, 1) };
            if present != 0 {
                unsafe { *out = gen_draw_u64_impl(tc, inner) };
                1
            } else {
                0
            }
        }
        _ => {
            unsafe { *out = gen_draw_u64_impl(tc, gn) };
            1
        }
    }
}

/// Draw an optional f32.  See `hegel_gen_draw_optional_int` for semantics.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_optional_float(
    tc: *mut HegelTestCase,
    gn: *const HegelGen,
    out: *mut f32,
) -> c_int {
    let gn = unsafe { &*gn };
    match gn {
        HegelGen::Optional { inner } => {
            let present = unsafe { hegel_draw_int(tc, 0, 1) };
            if present != 0 {
                unsafe { *out = gen_draw_float_impl(tc, inner) };
                1
            } else {
                0
            }
        }
        _ => {
            unsafe { *out = gen_draw_float_impl(tc, gn) };
            1
        }
    }
}

/// Draw an optional f64.  See `hegel_gen_draw_optional_int` for semantics.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_optional_double(
    tc: *mut HegelTestCase,
    gn: *const HegelGen,
    out: *mut f64,
) -> c_int {
    let gn = unsafe { &*gn };
    match gn {
        HegelGen::Optional { inner } => {
            let present = unsafe { hegel_draw_int(tc, 0, 1) };
            if present != 0 {
                unsafe { *out = gen_draw_double_impl(tc, inner) };
                1
            } else {
                0
            }
        }
        _ => {
            unsafe { *out = gen_draw_double_impl(tc, gn) };
            1
        }
    }
}

/* ---- List draw functions ---- */

/*
** Draw a variable-length sequence of values into a caller-provided
** buffer.  The length is drawn from hegel (so it participates in
** shrinking), then each element is drawn using the element generator.
**
** Returns the number of elements drawn.  The actual length is
** min(drawn_length, capacity) to prevent buffer overflows.
*/

/// Draw a list of ints.  Returns the number of elements written to `buf`.
///
/// # Safety
/// `buf` must point to space for at least `capacity` ints.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_list_int(
    tc: *mut HegelTestCase,
    elem_gen: *const HegelGen,
    min_len: c_int,
    max_len: c_int,
    buf: *mut c_int,
    capacity: c_int,
) -> c_int {
    let effective_max = max_len.min(capacity);
    let len = unsafe { hegel_draw_int(tc, min_len.min(effective_max), effective_max) };
    let gn = unsafe { &*elem_gen };
    for i in 0..len as usize {
        unsafe { *buf.add(i) = gen_draw_int_impl(tc, gn) };
    }
    len
}

/// Draw a list of i64s.  Returns the number of elements written to `buf`.
///
/// # Safety
/// `buf` must point to space for at least `capacity` i64s.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_list_i64(
    tc: *mut HegelTestCase,
    elem_gen: *const HegelGen,
    min_len: c_int,
    max_len: c_int,
    buf: *mut i64,
    capacity: c_int,
) -> c_int {
    let effective_max = max_len.min(capacity);
    let len = unsafe { hegel_draw_int(tc, min_len.min(effective_max), effective_max) };
    let gn = unsafe { &*elem_gen };
    for i in 0..len as usize {
        unsafe { *buf.add(i) = gen_draw_i64_impl(tc, gn) };
    }
    len
}

/// Draw a list of u64s.  Returns the number of elements written to `buf`.
///
/// # Safety
/// `buf` must point to space for at least `capacity` u64s.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_list_u64(
    tc: *mut HegelTestCase,
    elem_gen: *const HegelGen,
    min_len: c_int,
    max_len: c_int,
    buf: *mut u64,
    capacity: c_int,
) -> c_int {
    let effective_max = max_len.min(capacity);
    let len = unsafe { hegel_draw_int(tc, min_len.min(effective_max), effective_max) };
    let gn = unsafe { &*elem_gen };
    for i in 0..len as usize {
        unsafe { *buf.add(i) = gen_draw_u64_impl(tc, gn) };
    }
    len
}

/// Draw a list of f64s.  Returns the number of elements written to `buf`.
///
/// # Safety
/// `buf` must point to space for at least `capacity` doubles.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_list_double(
    tc: *mut HegelTestCase,
    elem_gen: *const HegelGen,
    min_len: c_int,
    max_len: c_int,
    buf: *mut f64,
    capacity: c_int,
) -> c_int {
    let effective_max = max_len.min(capacity);
    let len = unsafe { hegel_draw_int(tc, min_len.min(effective_max), effective_max) };
    let gn = unsafe { &*elem_gen };
    for i in 0..len as usize {
        unsafe { *buf.add(i) = gen_draw_double_impl(tc, gn) };
    }
    len
}

/// Draw a list of f32s.  Returns the number of elements written to `buf`.
///
/// # Safety
/// `buf` must point to space for at least `capacity` floats.
#[unsafe(no_mangle)]
pub unsafe extern "C-unwind" fn hegel_gen_draw_list_float(
    tc: *mut HegelTestCase,
    elem_gen: *const HegelGen,
    min_len: c_int,
    max_len: c_int,
    buf: *mut f32,
    capacity: c_int,
) -> c_int {
    let effective_max = max_len.min(capacity);
    let len = unsafe { hegel_draw_int(tc, min_len.min(effective_max), effective_max) };
    let gn = unsafe { &*elem_gen };
    for i in 0..len as usize {
        unsafe { *buf.add(i) = gen_draw_float_impl(tc, gn) };
    }
    len
}
