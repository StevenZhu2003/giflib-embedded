# Project TODO list

This document records planned work, not a feature promise. Priority reflects current value and dependencies: GIF correctness comes before platform-specific integration or performance work. The decoder remains platform-neutral, forward-only, caller-framebuffer-owned, and independent of display and scheduling policy.

Current supported behavior is summarized in [README.md](../README.md). An item is not supported until its implementation, tests, and documentation land.

## HIGH — next work

No work is currently scheduled at this priority.

## MID-HIGH — planned after the immediate work

### BURST_READ — optional buffered input adapter

The detailed design and implementation order are recorded in [BURST_READ_PLAN.md](BURST_READ_PLAN.md).

- Add `GIF_ENABLE_BURST_READ`, disabled by default, plus centralized compile-time configuration for `GIF_BURST_READ_FIFO_SIZE` and `GIF_BURST_READ_LOW_WATERMARK`. Validate a non-zero FIFO, a low-water mark strictly below its capacity, and all relevant size calculations at compile time.
- Implement BURST_READ in the private decoder input adapter: embed one fixed FIFO and its read state in each enabled `GifDecoder`, with capacity selected by `GIF_BURST_READ_FIFO_SIZE`. The FIFO is not application-provided and is not a separate variable-size allocation; it is bounded decoder-domain storage obtained when the decoder object is created. Do not move this state into application code or require each `gif_porting.c` implementation to recreate the same buffering logic.
- Keep `gif_porting_read()` as the single platform read primitive. When buffered bytes reach the configured low-water mark, request a contiguous burst from that primitive, including a second request only when ring wrap requires it; consume bytes from the FIFO before requesting more source data. The feature remains synchronous: it does not introduce a pending result, task, thread, DMA policy, or new public decoder lifecycle.
- Preserve the existing forward-only and short-read contract exactly. In particular, model final-byte `GIF_PORTING_EOF`, final-byte `GIF_PORTING_IO_ERROR`, zero-byte `GIF_PORTING_OK`, and early close explicitly so prefetch cannot discard buffered final bytes or change the public terminal result.
- Cover disabled behavior, threshold boundaries, ring wrap, sparse source reads, EOF/error after valid bytes, repeated open/decode/close, and multiple live decoders. Compare decoded output and status sequences with BURST_READ disabled; record storage-read request counts separately rather than treating fewer calls as a correctness requirement.
- Update CMake, Porting Guide, Memory Configuration, the BUILTIN sizing estimator, and pool regression coverage. Enabled FIFO bytes are fixed decoder-domain storage and must be counted once per live decoder in the pool model; they remain separate from the application framebuffer and disposal-method-3 snapshot.
- Validate the default-off code/RAM boundary on ARM and all allocator backends. Measure a real target source before presenting BURST_READ as a performance recommendation; its value depends on the platform storage latency and the giflib request pattern.

## MID — useful, but not the next priority

### Stage 10 — Auditability and integration guidance

- Add a concise giflib import/delta and re-import record beyond the existing attribution notice, so retained files and local behavioral changes remain easy to review when upstream is updated.
- Add application guidance for deadline-based playback and asynchronous display/DMA ownership without moving scheduling, cache maintenance, or display control into the decoder.

## LOW — evaluate when there is explicit demand

### Stage 11 — Measured integration and performance follow-up

- Consider a maintained real-hardware reference or reproducible benchmark only when a target integration can be built, run, and maintained without placing a vendor SDK or board policy in the core repository.
- **Confirmed baseline:** the synchronous decoder path has no busy-wait or external-event polling. `gif_decoder_open()` and `gif_decoder_next_frame()` synchronously reach `gif_porting_read()` through giflib's input callback; the short-read bridge accepts only positive progress and converts a zero-byte `GIF_PORTING_OK` result into an I/O failure. Parser/LZW loops are bounded decode computation, not waiting. Frame delay remains entirely application policy; the hosted example's `clock()` delay loop is example-only and is not decoder behavior.
- **Platform-specific boundary:** a port may perform ordinary blocking I/O, or start DMA/asynchronous I/O inside `gif_porting_read()` and wait on its platform's completion primitive before returning. It must still return only produced bytes, final EOF, or an I/O error; the stable synchronous callback contract has no pending/would-block result. No scheduler, semaphore, notification, cache, or device API belongs in decoder core.
- **Pending validation:** evaluate a DMA-backed port only after measurements show that storage reads are a bottleneck. Validate destination-DMA accessibility, cache/ownership rules, timeout/cancellation behavior, close-with-operation-in-flight handling, and the cost of the actual platform read requests. Do not add a generic asynchronous API to the decoder without that evidence.
- Consider an LVGL integration example only when it can test a supported LVGL release as a real consumer, rather than duplicating the existing allocator mock coverage.
- Consider an opt-in dynamic allocator fallback for Disposal 3 snapshots only when a concrete product cannot reserve its own `GifOutputSurface` snapshot storage. Preserve the current strict caller-owned path as the default and re-measure all affected pool profiles before making such a fallback available.
- Consider corpus-specific host-side sizing tooling only when it can accept a real GIF corpus, declared concurrency and lifecycle constraints, and an equivalent production allocator path. It should search a minimum passing pool and report measured, Balanced, and Hardened recommendations; optional fragmentation stress must remain optional rather than becoming a target dependency.

### Stage 12 — Optional public Memory API

- Revisit a public `GifMemoryService` only after a concrete application needs it. Do not publish the existing private `gif_mem_*` facade unchanged.
- A future contract must settle ownership, lifecycle/reset rules, alignment, accounting, OOM behavior, and synchronization across BUILTIN, PRIVATE, LIBC, and LVGL backends. See [MEMORY_API_EVALUATION.md](MEMORY_API_EVALUATION.md).
