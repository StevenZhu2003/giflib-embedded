# TLSF lineage and migration record

`vendor/tlsf/gif_tlsf.c` and `vendor/tlsf/gif_tlsf.h` are derived from the Two-Level Segregated Fit allocator written by Matthew Conte. They are licensed under the BSD 3-Clause License, not the repository's MIT license. Their full notice is retained in both derived source files and summarized in `THIRD_PARTY_NOTICES.md`.

## Fixed sources

- Original TLSF upstream: <https://github.com/mattconte/tlsf>, commit `deff9ab509341f264addbd3c8ada533678591905` (`Update tlsf.c`, 2020-03-29).
- Imported algorithm baseline: <https://github.com/lvgl/lvgl>, commit `940c86ae3ade38de8c28b9096a87848d82c6ac36` (2026-08-12), files `src/stdlib/builtin/lv_tlsf.c` and `src/stdlib/builtin/lv_tlsf.h`.

LVGL is an intermediate integration source, not the original TLSF author. This repository does not claim that TLSF is LVGL-original work.

## Upstream-to-LVGL changes retained

The following LVGL improvements are deliberate parts of this import:

- `FL_INDEX_MAX` is derived from the configured maximum pool size, reducing control metadata for a known, bounded embedded pool.
- Compile-time `TLSF_LOG2_CEIL` helpers support that pool-size calculation.
- `adjust_request_size()` rejects arithmetic wraparound before alignment.
- `realloc()` rejects a non-zero request that produces an invalid adjusted size.
- `add_pool()` treats the maximum pool boundary as exclusive, matching the allocator's indexable range.

The LVGL-only namespace, configuration guards, logging, assertions, string wrapper, global state, linked-list pool manager, OS locks, monitoring, junk fill, and dynamic pool expansion are not imported.

## Repository changes after import

- `lv_tlsf_*` and related public types become `gif_tlsf_*` to avoid collisions with applications that use LVGL.
- The allocator's bounded maximum is `GIF_MEM_POOL_SIZE` from `gif_config.h`.
- Diagnostics are silent and construction/pool creation reject invalid input without dereferencing null pointers or underflowing a byte count.
- `memcpy()` uses the C standard library directly; no LVGL wrapper is retained.
- `gif_mem_builtin.c` is an original, MIT-licensed single fixed-pool wrapper; it is intentionally separate from the derived TLSF core.
