# Project TODO list

This document is the project roadmap. It intentionally separates confirmed current behavior from work that is planned, under evaluation, or only worth doing when a real user needs it. Priority is about **what to do next**, not a promise of release order.

Current supported behavior is summarized in [README.md](../README.md). An item in this list is not supported until its implementation, tests, and documentation land.

## HIGH — next work

No HIGH-priority feature is currently scheduled. Allocator backend work is complete; the next planned implementation work is the MID-HIGH disposal behavior stage below.

## MID-HIGH — planned after the immediate work

### Stage 6 — Disposal behavior completion

- Audit and extend tests for the already-supported disposal methods 0/1, especially multi-frame composition and transparency interactions.
- Implement GIF disposal method 2, including correct canvas restoration, updated-rectangle reporting, failure cleanup, and regression coverage.

## MID — useful, but not the next priority

### Stage 8 — Decoder feature coverage

- Implement disposal method 3.
- Add interlaced-image decoding.
- Evaluate RGBA output alongside the current RGB888/BGR888 formats, including the public surface contract and memory cost.
- Review support for currently rejected GIF extensions only when their display semantics are defined and testable.

### Stage 9 — Decoder memory and integration refinements

- Measure and, where worthwhile, reduce decoder peak memory and row-buffer lifetimes without mixing the work with feature changes.
- Evaluate reusable, optional byte-source adapter modules while preserving the single-file platform-porting boundary.
- Evaluate an explicit decoder-concurrency model after a real multi-decoder use case establishes the required guarantees.

## LOW — evaluate when there is explicit demand

### Stage 10 — Optional public Memory API

- Revisit a public `GifMemoryService` only after a concrete application needs it. Do not publish the existing private `gif_mem_*` facade unchanged.
- A future contract must settle ownership, lifecycle/reset rules, alignment, allocation accounting, OOM behavior, and synchronization consistently across BUILTIN, PRIVATE, LIBC, and LVGL backends.
- The U8g2-buffer use case remains an evaluation input, not a reason to share the current global decoder pool without an explicit service contract. See [MEMORY_API_EVALUATION.md](MEMORY_API_EVALUATION.md).

### Stage 11 — Extended allocator/runtime options

- Consider per-decoder allocator contexts, application-supplied BUILTIN pool storage, explicit lock hooks, multi-pool management, or controlled pool reset only if production requirements justify their added API and test cost.
- Do not add automatic pool expansion or an implicit fallback from BUILTIN or PRIVATE to libc.
