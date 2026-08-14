# Project TODO list

This document records planned work, not a feature promise. Priority reflects current value and dependencies: GIF correctness comes before platform-specific integration or performance work. The decoder remains platform-neutral, forward-only, caller-framebuffer-owned, and independent of display and scheduling policy.

Current supported behavior is summarized in [README.md](../README.md). An item is not supported until its implementation, tests, and documentation land.

## HIGH — next work

### Disposal method 3 — Restore to Previous

- Implement optional restore-to-previous while preserving the current public decoder lifecycle and caller-owned framebuffer model. Disabled builds must retain the current unsupported-feature result without carrying method-3 snapshot state.
- Follow the reviewed [implementation plan](DISPOSAL_METHOD_3_PLAN.md): define the required backup-memory cost, ownership, and failure behavior before implementation; do not silently add a second full canvas.
- Add multi-frame composition regression coverage for disposal methods 0, 1, 2, and 3, including transparency, partial rectangles, palette selection, consecutive disposal-3 frames, cleanup, and updated-rectangle semantics.

## MID-HIGH — planned after the immediate work

No work is currently scheduled at this priority.

## MID — useful, but not the next priority

### Stage 10 — Auditability and integration guidance

- Add a concise giflib import/delta and re-import record beyond the existing attribution notice, so retained files and local behavioral changes remain easy to review when upstream is updated.
- Add application guidance for deadline-based playback and asynchronous display/DMA ownership without moving scheduling, cache maintenance, or display control into the decoder.

## LOW — evaluate when there is explicit demand

### Stage 11 — Measured integration and performance follow-up

- Consider a maintained real-hardware reference or reproducible benchmark only when a target integration can be built, run, and maintained without placing a vendor SDK or board policy in the core repository.
- **Confirmed baseline:** the synchronous decoder path has no busy-wait or external-event polling. `gif_decoder_open()` and `gif_decoder_next_frame()` synchronously reach `gif_porting_read()` through giflib's input callback; the short-read bridge accepts only positive progress and converts a zero-byte `GIF_PORTING_OK` result into an I/O failure. Parser/LZW loops are bounded decode computation, not waiting. Frame delay remains entirely application policy; the hosted example's `clock()` delay loop is example-only and is not decoder behavior.
- **Platform-specific boundary:** a port may perform ordinary blocking I/O, or start DMA/asynchronous I/O inside `gif_porting_read()` and wait on its platform's completion primitive before returning. It must still return only produced bytes, final EOF, or an I/O error; the stable synchronous callback contract has no pending/would-block result. No scheduler, semaphore, notification, cache, or device API belongs in decoder core.
- **Pending validation:** evaluate a target-owned read-ahead or DMA-backed port only after measurements show that small sequential reads are a bottleneck. Validate destination-DMA accessibility, cache/ownership rules, timeout/cancellation behavior, close-with-operation-in-flight handling, and the cost of small giflib read requests. Do not add filesystem-specific buffering or a generic asynchronous API to the decoder without that evidence.
- Consider an LVGL integration example only when it can test a supported LVGL release as a real consumer, rather than duplicating the existing allocator mock coverage.
- Consider corpus-specific host-side sizing tooling only when it can accept a real GIF corpus, declared concurrency and lifecycle constraints, and an equivalent production allocator path. It should search a minimum passing pool and report measured, Balanced, and Hardened recommendations; optional fragmentation stress must remain optional rather than becoming a target dependency.

### Stage 12 — Optional public Memory API

- Revisit a public `GifMemoryService` only after a concrete application needs it. Do not publish the existing private `gif_mem_*` facade unchanged.
- A future contract must settle ownership, lifecycle/reset rules, alignment, accounting, OOM behavior, and synchronization across BUILTIN, PRIVATE, LIBC, and LVGL backends. See [MEMORY_API_EVALUATION.md](MEMORY_API_EVALUATION.md).
