# BUILTIN Pool Sizing and Fragmentation Study

## Status

This document records the test protocol and results used to establish practical memory budgets for the `GIF_MEM_USE_BUILTIN` backend. The study is intentionally conservative: it does not treat a successful benchmark as a mathematical proof that every valid GIF and every application schedule can never exhaust a finite pool.

The study produces three different results. Do not treat the last, hardened profile as the minimum for every product:

```text
payload-derived estimate: calculated from the declared ABI and GIF limits
balanced mixed-use profile: W + 32 KiB + 40 KiB × N + H_port(N)
hardened stress profile:  W + 128 KiB + 64 KiB × N + H_port(N)
```

The balanced profile is a simple envelope over the observed mixed random-workload boundary matrix. The hardened profile passed every declared adverse-lifecycle test and is intentionally much larger. The dependency-free [`tools/estimate_builtin_pool.py`](../tools/estimate_builtin_pool.py) calculator combines these profiles with declared decoder, palette, port-handle, TLSF-control, alignment, and margin inputs.

The hardened candidate was tested as:

```text
P_hardened_tested(N, W) = W + 128 KiB + 64 KiB × N
```

`N` is the maximum number of simultaneously live decoders, `W` is the maximum accepted GIF image width in bytes of palette-index row storage (one byte per pixel in the current decoder), and `H_port(N)` is the total payload of all live dynamic handles created by the target's `gif_porting.c`. The caller-owned output framebuffer is separate.

## Scope and memory boundary

The BUILTIN backend owns a single fixed TLSF pool. It includes every dynamic allocation made through `gif_mem_*`, including decoder state, giflib state, palette objects, the current image row buffer, and any dynamic port handle allocated with `gif_porting_mem_alloc()`.

The output framebuffer is deliberately excluded. It is owned by the application and must be budgeted separately. Let `pixel_bytes` be 3 for RGB888/BGR888 or 2 for RGB565. The required accessible storage is:

```text
(canvas_height - 1) × stride_bytes + canvas_width × pixel_bytes
```

For a tightly packed surface, this simplifies to `canvas_width × canvas_height × pixel_bytes` bytes.

The test harness serializes calls into the library, matching the BUILTIN backend's current thread-safety contract. It does not evaluate concurrent calls from multiple threads or interrupt contexts.

## Why this study is needed

The decoder's live memory is not determined by canvas area alone. A live decoder retains giflib's LZW state, descriptor state, palettes, and its port handle. During `gif_decoder_next_frame()`, it additionally allocates an image-width row buffer. Therefore a useful pool budget must consider both the number of simultaneously live decoders and the maximum image width that may be decoded.

The study distinguishes five conditions:

| Condition | Evidence used |
| --- | --- |
| Capacity OOM | The adjusted TLSF request exceeds aggregate free payload. |
| External-fragmentation OOM | Aggregate free payload is sufficient, but the largest free block is smaller than the adjusted request. |
| Allocator-path indeterminate OOM | The pool walk appears to show sufficient space, but TLSF returns `NULL`; this is not force-classified without stronger evidence. |
| Long-term degradation | Fragmentation metrics worsen over time while live decoders remain allocated. |
| Leak or coalescing failure | Pool integrity fails or the initialized free-pool state is not restored after all decoders close. |

## Reproducible corpus

The local study corpus is deterministic and generated from a recorded seed. It contains valid GIF files with the following allocation-relevant variety:

- narrow/tall, medium, wide/short, and larger canvases;
- width-boundary images up to 60,000 pixels wide, with small heights to isolate the transient row-buffer term;
- 1–12 frames per file;
- 2-, 16-, and 256-colour source patterns;
- global and local colour-table layouts as emitted and verified by the generator;
- transparent pixels and GIF disposal methods 0, 1, and 2;
- solid, striped, checkerboard, and noisy pixel data.

The source bytes are preloaded by the host test program only. The decoder itself still receives them through the standard porting contract, using controlled short reads. The host preload and caller-owned test framebuffer do not consume the BUILTIN pool.

## Workload classes

Each workload is driven by a reproducible xorshift seed and checks TLSF integrity after every operation.

| Workload | Purpose |
| --- | --- |
| `sequential` | Repeated open, full decode, and close. Establishes the non-fragmented baseline. |
| `random` | Random open, decode, EOS close, and early close across mixed GIFs. |
| `few_wide_long` | A smaller set of wide decoders remains live while many transient mixed-shape decoders churn. |
| `many_narrow_long` | Most live instances are narrow decoders; wide transient decoders repeatedly request large row buffers. |
| `adversarial` | Alternates wide and narrow retained allocations, creates non-adjacent holes, then requests large transient row buffers. |
| `error_cleanup` | Injects source I/O errors at random offsets while instances are live, validating close/error cleanup under fragmentation. |

The measurement suite tests `N = 1, 2, 4, 8, 16, 32` simultaneously live decoder limits. This range covers the observed scaling region without treating unrealistic concurrency as evidence of product suitability. The test framework can be extended if a product has a justified higher limit.

## Measurements and acceptance criteria

For every run the harness records:

- seed, workload, operations, maximum live decoders, pool capacity, and corpus revision;
- each OOM's requested allocation, total free payload, and largest free block;
- peak live count, minimum total free payload, minimum largest free block, free-block count, and a fragmentation ratio while decoders remain live;
- TLSF integrity after each operation;
- exact initialized, post-close, and final free-pool summaries;
- a complete sequential corpus pass after the workload has closed every decoder.

A no-OOM run must have no open/decode allocation failure, no unexpected decoder status, no TLSF integrity failure, exact post-close and final pool restoration, and a successful post-workload corpus pass. Expected injected I/O failures in the `error_cleanup` workload are recorded separately and are not allocation failures.

### Efficient boundary search without weakening acceptance

The sizing runner does not perform expensive full-corpus baselines at every binary-search capacity. It first performs a deterministic, single-seed candidate search with a bounded operation count. Each search trial decodes the widest corpus image before the workload, so a capacity that cannot satisfy the maximum transient row-buffer request is rejected immediately. It still performs TLSF integrity checks, pool-restoration checks, and the selected workload's fragmentation measurements.

The first passing candidate is then confirmed at the requested full operation count for every declared seed. Every confirmation performs the complete corpus baseline before and after the workload. Only confirmed capacities contribute to the published no-OOM envelope. This removes redundant screening work; it does not relax the acceptance criteria for a reported boundary.

## Results

### Observed typical demand

The random mixed-workload boundary search measured the smallest no-OOM pool in that workload. It is a useful description of typical demand, not a production guarantee.

| N | Smallest observed random no-OOM pool | KiB per decoder |
| ---: | ---: | ---: |
| 1 | 92 KiB | 92.00 |
| 2 | 140 KiB | 70.00 |
| 4 | 232 KiB | 58.00 |
| 8 | 396 KiB | 49.50 |
| 16 | 712 KiB | 44.50 |
| 32 | 1,348 KiB | 42.12 |

The apparent per-decoder cost falls as `N` rises because a maximum-width transient row buffer is normally active in only one decoder at a time. A flat `48 KiB × N` multiplier therefore hides the image-width term and is not adopted as a sizing rule.

### Hardened no-OOM envelope

The formula `P_hardened_tested(N, W) = W + 128 KiB + 64 KiB × N` was evaluated with `W = 60,000` bytes. For each `N = 1, 2, 4, 8, 16, 32`, it ran every lifecycle workload except the baseline-only `sequential` mode, three fixed seeds, and 10,000 operations per run. All 90 full runs passed: no decoder allocation OOM, no unexpected decoder status, no TLSF integrity failure, exact pool restoration after close, and a complete post-workload corpus pass.

| N | Evaluated pool | Full runs | Passed | Peak fragmentation | Minimum largest free block |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 250.59 KiB | 15 | 15 | 0‰ | 225,600 B |
| 2 | 314.59 KiB | 15 | 15 | 177‰ | 239,432 B |
| 4 | 442.59 KiB | 15 | 15 | 378‰ | 243,936 B |
| 8 | 698.59 KiB | 15 | 15 | 491‰ | 308,464 B |
| 16 | 1,210.59 KiB | 15 | 15 | 471‰ | 514,200 B |
| 32 | 2,234.59 KiB | 15 | 15 | 452‰ | 876,056 B |

The test port's dynamic handle was an 8-byte host wrapper and is already represented in these measurements. A production port must not assume that a real filesystem, cache, or driver wrapper has the same size.

### Long-duration fragmentation result

At N=32, the same tested candidate was exercised for 100,000 operations with five seeds for each of the five non-baseline workloads: 25 additional full runs. All passed with no allocation OOM, unexpected decoder result, TLSF integrity error, or pool-restoration mismatch.

| Workload | Runs | Passed | Maximum fragmentation | Minimum largest free block |
| --- | ---: | ---: | ---: | ---: |
| `adversarial` | 5 | 5 | 451‰ | 971,424 B |
| `error_cleanup` | 5 | 5 | 452‰ | 901,592 B |
| `few_wide_long` | 5 | 5 | 409‰ | 890,840 B |
| `many_narrow_long` | 5 | 5 | 150‰ | 1,235,176 B |
| `random` | 5 | 5 | 460‰ | 876,056 B |

The pool did fragment transiently while live decoders had varied shapes and lifetimes; this is expected in a general allocator. Periodic trace samples showed values changing with the live allocation mix rather than a monotonic, irreversible loss of free capacity. Exact restoration after all decoders closed is checked separately and passed in every run. This is evidence against leak or failed coalescing in the exercised paths, not a claim that fragmentation can never occur.

## Interpretation and scope

The fixed decoder state, retained palettes, one active row buffer, and port handle are real, separate components. The row term is one `W`, not `N × W`, because this study uses the documented serialized `gif_decoder_next_frame()` call contract. One global and one local palette may exist for every live decoder, so conservative declared palette counts are both `N`.

The boundary search deliberately included undersized candidates. They produced capacity and external-fragmentation OOM telemetry during screening and confirmation, while passing candidates still had to pass full seeds, pool-integrity checks, exact post-close restoration, and a post-workload corpus pass. A short candidate pass was never published as a no-OOM boundary. This distinction is why the study describes an observed typical boundary, a mixed-use profile, and a hardened profile rather than one universal minimum.

The user-facing profile selection, calculator invocation, orientation matrix, framebuffer boundary, and all product limits are maintained in [MEMORY_CONFIGURATION.md](MEMORY_CONFIGURATION.md). The calculator is a planning utility, not a proof tool. None of this study covers arbitrary application allocations, unbounded port caches, calls from threads or interrupt contexts, a production handle larger than `H_port(N)`, or GIFs outside the declared limits.

## Reproduction material

The corpus generator, fixed seeds, C harness, equivalent fixed-pool TLSF probe, raw CSV records, per-run pool traces, and generated local plots are retained under the ignored local directory `_TEMP/memory_fragmentation_test/`. They are host-only study material, not library runtime code. The local study uses a `uv`-managed Python environment and records enough information to reproduce a failure: workload, seed, pool capacity, operation count, OOM request/free-block data, and trace samples.
