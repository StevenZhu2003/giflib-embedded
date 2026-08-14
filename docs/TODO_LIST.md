# Project TODO list

This document records planned work, not a feature promise. Priority reflects current value and dependencies: GIF correctness comes before platform-specific integration or performance work. The decoder remains platform-neutral, forward-only, caller-framebuffer-owned, and independent of display and scheduling policy.

Current supported behavior is summarized in [README.md](../README.md). An item is not supported until its implementation, tests, and documentation land.

## HIGH — next work

### Stage 8 — Compatibility corpus and host-side malformed-input hardening

- Add a small, curated GIF corpus only where each asset has clear provenance and a stable expected result; cover valid composition cases and malformed/truncated inputs not represented by the current synthetic fixtures.
- Add host-only fuzzing. Keep it outside target builds and do not claim real-hardware sanitizer validation.

## MID-HIGH — planned after the immediate work

No work is currently scheduled at this priority.

## MID — useful, but not the next priority

### Stage 9 — Disposal method 3

- Implement restore-to-previous only after method 2 is stable. First define and document the required backup-memory cost and failure behavior; do not silently add a second full canvas.

### Stage 10 — Auditability and integration guidance

- Add a concise giflib import/delta and re-import record beyond the existing attribution notice, so retained files and local behavioral changes remain easy to review when upstream is updated.
- Add application guidance for deadline-based playback and asynchronous display/DMA ownership without moving scheduling, cache maintenance, or display control into the decoder.

## LOW — evaluate when there is explicit demand

### Stage 11 — Measured integration and performance follow-up

- Consider a maintained real-hardware reference or reproducible benchmark only when a target integration can be built, run, and maintained without placing a vendor SDK or board policy in the core repository.
- Evaluate a port-layer read-ahead example for storage backends only after target measurements show that small sequential reads are a bottleneck; do not add filesystem-specific buffering to the decoder.
- Consider an LVGL integration example only when it can test a supported LVGL release as a real consumer, rather than duplicating the existing allocator mock coverage.
- Consider corpus-specific host-side sizing tooling only when it can accept a real GIF corpus, declared concurrency and lifecycle constraints, and an equivalent production allocator path. It should search a minimum passing pool and report measured, Balanced, and Hardened recommendations; optional fragmentation stress must remain optional rather than becoming a target dependency.

### Stage 12 — Optional public Memory API

- Revisit a public `GifMemoryService` only after a concrete application needs it. Do not publish the existing private `gif_mem_*` facade unchanged.
- A future contract must settle ownership, lifecycle/reset rules, alignment, accounting, OOM behavior, and synchronization across BUILTIN, PRIVATE, LIBC, and LVGL backends. See [MEMORY_API_EVALUATION.md](MEMORY_API_EVALUATION.md).
