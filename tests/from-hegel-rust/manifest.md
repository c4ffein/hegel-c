<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# hegel-rust → hegel-c Test Correspondence

Each line maps a Rust test to its C equivalent. Run `make report` to verify.

## Ported

- [x] `test_integers.rs::test_i32` → `test_integers_i32_bounds.c`
- [x] `test_integers.rs::test_i64` → `test_integers_i64_bounds.c`
- [x] `test_integers.rs::test_u64` → `test_integers_u64_bounds.c`
- [x] `test_combinators.rs::test_filter` → `test_combinators_filter.c`
- [x] `test_combinators.rs::test_one_of_returns_value_from_one_generator` → `test_combinators_one_of.c`
- [x] `test_combinators.rs::test_optional_respects_inner_generator_bounds` → `test_combinators_optional.c`
- [x] `test_combinators.rs::test_flat_map` → `test_combinators_flat_map.c` (adapted: int not text)
- [x] `test_combinators.rs::test_draw_silent_non_debug` → `test_combinators_map.c` (adapted: map correctness)
- [x] `test_shrink_quality/integers.rs::test_can_find_an_int_above_13` → `test_shrink_int_above_13.c`
- [x] `test_shrink_quality/integers.rs::test_can_find_an_int` → `test_shrink_int_to_zero.c`
- [x] `test_shrink_quality/integers.rs::test_integers_from_minimizes_leftwards` → `test_shrink_boundary_101.c`
- [x] `test_floats.rs::f64_tests::with_min_and_max` + `can_find_positive/negative` → `test_floats_f64_bounds.c`
- [x] `test_floats.rs::f32_tests::with_min_and_max` + `can_find_positive/negative` → `test_floats_f32_bounds.c`
- [x] `test_combinators.rs::test_one_of_many` → `test_combinators_one_of_many.c`
- [x] `test_shrink_quality/integers.rs::test_minimize_bounded_integers_to_zero` → `test_shrink_bounded_zero.c`
- [x] `test_shrink_quality/integers.rs::test_minimize_bounded_integers_to_positive` → `test_shrink_bounded_positive.c`
- [x] `test_shrink_quality/integers.rs::test_minimize_single_element_in_silly_large_int_range` → `test_shrink_large_range.c`
- [x] `test_collections.rs::test_vec_with_{max,min,min_and_max}_size` → `test_lists_bounds.c`
- [x] `test_collections.rs::test_vec_with_mapped_elements` → `test_lists_mapped.c`

## Not yet ported — portable

- [ ] `test_integers.rs::test_i8` — needs i8 draw (hegel-c has no hegel_draw_i8)
- [ ] `test_integers.rs::test_i16` — needs i16 draw
- [ ] `test_integers.rs::test_u8` — needs u8 draw
- [ ] `test_integers.rs::test_u16` — needs u16 draw
- [ ] `test_integers.rs::test_u32` — needs u32 draw
- [ ] `test_integers.rs::test_isize` — covered by i64
- [ ] `test_integers.rs::test_usize` — covered by hegel_draw_usize
- [ ] `test_combinators.rs::test_sampled_from_returns_element_from_list` — needs list generation + sampled_from
- [ ] `test_combinators.rs::test_sampled_from_strings` — needs text draw + sampled_from
- [ ] `test_combinators.rs::test_boxed_generator_clone` — Rust-specific (boxed), test draw range instead
- [ ] `test_floats.rs::exclude_min/exclude_max` — hegel-c has no exclude_min/exclude_max option
- [ ] `test_floats.rs::can_find_nan/can_find_inf` — hegel-c bounded draws don't produce NaN/inf
- [ ] `test_shrink_quality/collections.rs::*` — needs list draw + shrink verification
- [ ] `test_shrink_quality/strings.rs::*` — needs text draw + shrink verification
- [ ] `test_shrink_quality/floats.rs::*` — needs float draw + shrink verification
- [ ] `test_compose.rs` — dependent generation (multiple draws)
- [ ] `test_validation.rs` — error handling for invalid generator configs
- [ ] `test_arrays.rs` — fixed-size array generation

## Not portable

- `test_lifetimes.rs` — Rust lifetime/borrow semantics
- `test_derive.rs` — `#[derive(Generate)]` macro
- `test_derive_compile.rs` — compile-time checks
- `test_composite.rs` — `#[hegel::composite]` macro
- `test_stateful.rs` — `#[hegel::state_machine]` macro
- `test_hegel_test.rs` — `#[hegel::test]` macro attribute tests
- `test_output.rs` — Rust panic/backtrace formatting
- `test_flaky_global_state.rs` — Rust-specific determinism check
- `test_antithesis.rs` — Antithesis SDK integration
- `test_bad_server_command.rs` — server error handling (infrastructure)
- `test_install_errors.rs` — uv/installation errors
- `test_database_key.rs` — database persistence internals
