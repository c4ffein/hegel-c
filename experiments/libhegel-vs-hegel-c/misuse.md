<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Pair 6, misuse: a bug in the test, not in the code

`len` in [0, 10], then `idx` in [0, len - 1]. That is an inverted range when `len` is 0. The property itself always holds.

| file | code lines | of which boilerplate | what it does |
|---|---:|---:|---|
| [lite_misuse.c](lite_misuse.c) | 44 | 31 | "any non-OK draw ends the case", then prints the run's error |
| [raw_misuse.c](raw_misuse.c) | 72 | 57 | the same pattern, with every other call checked |
| [hc_misuse.c](hc_misuse.c) | 15 | 5 | the draw itself reports the bad bounds |

Measured (see [README.md](README.md)), the same on all 20 seeds:

```
lite     run error: FailedHealthCheck: LargeInitialTestCase — the smallest natural input for this
         test is very large, ... suppress the LargeInitialTestCase health check ...
careful  run error: FailedHealthCheck: LargeInitialTestCase — (same)
hegel-c  Health check failure: hegel-c: integer draw with min > max
```

## Why both libhegel versions blame the input size

`hegel_generate_integer (ctx, tc, 0, -1, &idx)` returns `HEGEL_E_INVALID_ARG`. The usual pattern (upstream's examples end a case on `HEGEL_E_STOP_TEST`, and lite and careful end it on any non-OK draw) turns that into an overrun. Every case that draws `len = 0` then ends as an overrun, and the engine reads a run of overruns on small inputs as "the input doesn't fit the budget": its health check names the wrong cause.

Careful's `CHECK` doesn't help here, because the draw isn't wrapped in it: an overrun has to be a normal outcome, not an abort. The real message is one call away. `hegel_context_last_error` holds `generate_integer requires min_value <= max_value, got [0, -1]`, but only a program that tells "out of budget" apart from "you called me wrong" will read it:

```c
if (rc == HEGEL_E_STOP_TEST) return HEGEL_STATUS_OVERRUN;
if (rc != HEGEL_OK) { /* print hegel_context_last_error (ctx), abort the run */ }
```

## Lite's minimum here

Without the 5 lines that print the run's error, lite exits 1 with **no output at all**: a run that errored looks just like a run that failed. Those lines are in every lite file for that reason.

## What hegel-c adds (15 lines)

hegel-c separates the two cases for every draw: a stop or an assume ends the case, anything else aborts the run as a health failure that names the call ("integer draw with min > max"). It drops the engine's detail, though, which says exactly which bounds came in: worth adding to the message.
