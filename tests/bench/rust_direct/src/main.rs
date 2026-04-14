// SPDX-License-Identifier: MIT
// Copyright (c) 2026 c4ffein
// Part of hegel-c — see hegel/LICENSE for terms.
//
// Direct-to-server Rust bench.  Bypasses the entire C/FFI/fork stack
// and uses the hegeltest crate exactly as a native Rust test would.
// Shape matches tests/bench/test_bench_single.c: N cases, two i32
// draws per case, trivial assertion that can never fail.
//
// Usage: bench_direct N
//
// N is required — no default.  A missing or unparsable N is a hard
// error, because a silent fallback would make it too easy to think
// you benched 10000 cases when you actually benched the default.

use hegel::generators as gs;
use hegel::{Hegel, Settings, TestCase};

fn main() {
    let mut args = std::env::args();
    let prog = args.next().unwrap_or_else(|| "bench_direct".to_string());
    let Some(n_arg) = args.next() else {
        eprintln!("{}: usage: {} N", prog, prog);
        std::process::exit(2);
    };
    let n: u64 = n_arg.parse().unwrap_or_else(|e| {
        eprintln!("{}: invalid N {:?}: {}", prog, n_arg, e);
        std::process::exit(2);
    });
    if args.next().is_some() {
        eprintln!("{}: usage: {} N", prog, prog);
        std::process::exit(2);
    }

    Hegel::new(|tc: TestCase| {
        let x: i32 = tc.draw(gs::integers::<i32>().min_value(0).max_value(100));
        let y: i32 = tc.draw(gs::integers::<i32>().min_value(0).max_value(100));
        assert!(x + y <= 200, "unreachable: {} + {}", x, y);
    })
    .settings(Settings::new().test_cases(n))
    .run();
}
