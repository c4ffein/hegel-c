/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 c4ffein
 * Part of hegel-c — see hegel/LICENSE for terms.
 *
 * Compile hegel_gen.c (the pure-C schema/shape system) and link it
 * into libhegel_c.a alongside the Rust code.  Users include
 * "hegel_gen.h" and get schema helpers backed by the same static
 * library as the primitive FFI.
 */

fn main() {
    // Re-run if the C source or header changes.
    println!("cargo:rerun-if-changed=../hegel_gen.c");
    println!("cargo:rerun-if-changed=../hegel_gen.h");
    println!("cargo:rerun-if-changed=../hegel_c.h");

    cc::Build::new()
        .file("../hegel_gen.c")
        .include("..")
        .flag_if_supported("-Wall")
        .flag_if_supported("-Wextra")
        .flag_if_supported("-funwind-tables")
        .flag_if_supported("-fexceptions")
        .compile("hegel_gen");
}
