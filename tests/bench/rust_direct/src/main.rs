// SPDX-License-Identifier: MIT
// Copyright (c) 2026 c4ffein
// Part of hegel-c — see hegel/LICENSE for terms.
//
// Direct-to-server Rust bench.  Bypasses the entire C/FFI/fork stack
// and uses the hegeltest crate exactly as a native Rust test would.
// Shape matches tests/bench/test_bench_single.c: N cases, two i32
// draws per case, trivial assertion that can never fail.
//
// Usage: bench_direct [N]   (default N = 1000)

use hegel::generators as gs;
use hegel::{Hegel, Settings, TestCase};

fn main() {
    let n: u64 = std::env::args()
        .nth(1)
        .and_then(|s| s.parse().ok())
        .unwrap_or(1000);

    Hegel::new(|tc: TestCase| {
        let x: i32 = tc.draw(gs::integers::<i32>().min_value(0).max_value(100));
        let y: i32 = tc.draw(gs::integers::<i32>().min_value(0).max_value(100));
        assert!(x + y <= 200, "unreachable: {} + {}", x, y);
    })
    .settings(Settings::new().test_cases(n))
    .run();
}
