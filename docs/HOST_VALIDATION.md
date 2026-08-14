# Host Validation

This document explains what the project checks on a host, why those checks matter, and what the completed results mean. It is the canonical record for corpus-based compatibility, non-normal input handling, instrumentation, and fuzzing of giflib-embedded. It does not change the decoder contract, add a target dependency, or establish real-hardware behavior.

The ordinary repository regression remains independent of this guide's local GIF corpus. External GIF inputs, generated fuzz inputs, and raw run records are intentionally excluded from Git.

## 1. What has been verified

| Area | Why it is checked | Result and meaning |
| --- | --- | --- |
| Curated corpus | Ensure public behavior is tested against independently generated GIFs rather than only small project fixtures. | All 39 pinned files have provenance, a digest, a classification, and a frozen structural outcome. |
| Public lifecycle | Check the same open, bind, decode, and close calls an application uses. | The opt-in harness uses only the public decoder API and a forward-only test port. |
| Composition | Detect errors in interlace, partial rectangles, transparency, overlap, and disposal behavior. | Reviewed RGB888 hashes cover selected high-value composition cases; the ordinary fixture suite also checks optional disposal method 3 in RGB888 and RGB565. |
| Read and cleanup | Confirm short reads, final-byte EOF, source failures, and repeated lifecycles do not violate the port contract. | The selected sparse matrix passes without an unnecessary Cartesian-product expansion. |
| Allocator scope | Confirm decoder behavior is not coupled to one host allocator implementation. | The full corpus uses PRIVATE accounting; selected cases also pass through BUILTIN, LIBC, and the LVGL public-API mock. |
| Host instrumentation | Detect invalid memory or undefined-behavior paths while ordinary regression and the example run. | The complete 16-test matrix passed under both supported host configurations. |
| Fuzzing | Explore variations around the curated corpus and measure whether the current input model reaches new code paths. | Two four-worker campaigns ran for about 4 hours 56 minutes and at least 3,877,840 executions; code coverage reached a stable plateau. |

The results are evidence for the stated host configuration, corpus, and source revision. They are not a proof about every GIF, platform port, allocator configuration, or future revision.

## 2. Local corpus and provenance

### 2.1 Repository boundary

The local corpus is kept below testdata/compatibility_corpus/, a durable local location explicitly excluded by .gitignore. It is not disposable _TEMP material, but neither its GIF files nor raw generated results are staged or pushed.

The local source checkout contains a MANIFEST.tsv with a SHA-256 digest for every GIF. Before an external binary can become a committed fixture, its provenance and redistribution terms must be reviewed again and the applicable attribution added to the repository.

### 2.2 Initial source

The initial source is the gif-conformance/ subset of [imazen/codec-corpus](https://github.com/imazen/codec-corpus), acquired by sparse checkout at commit 28205bbc5cf40364d012c462240ba28143373d67.

It contains 39 generated GIF files:

| Upstream group | Count | Validation value |
| --- | ---: | --- |
| valid/ | 28 | GIF87a/GIF89a parsing, palettes, interlace, geometry, transparency, timing, looping, and disposal 0/1/2. |
| invalid/ | 7 | Header handling, truncation, corrupt LZW, missing trailer, zero dimensions, and forward-version behavior. |
| edge-cases/ | 4 | GIF87a, comment and Plain Text extensions, and a full palette with sparse use. |

The upstream suite is generated from its Python standard-library generator and is declared CC0 1.0/public-domain material. Its labels are inputs to investigation, not this library's public contract.

### 2.3 Frozen classifications

The frozen manifest is implemented in tests/test_compatibility.c.

| Classification | Count | Expected result |
| --- | ---: | --- |
| Supported-valid | 30 by default; 31 with method 3 enabled | Open, bind, decode, and finish with GIF_STATUS_END_OF_STREAM. |
| Deliberately unsupported | 2 by default; 1 with method 3 enabled | GIF_STATUS_UNSUPPORTED_FEATURE. |
| Malformed or truncated | 6 | One documented non-success public status at the documented lifecycle boundary. |
| Forward-version compatibility | 1 | Decode one frame and finish with GIF_STATUS_END_OF_STREAM. |

Plain Text remains deliberately unsupported. `dispose_previous.gif` is deliberately unsupported in the default build and becomes supported-valid when `GIF_ENABLE_DISPOSAL_METHOD_3=1`.

The six malformed/truncated files are bad_lzw_code.gif, empty.gif, no_trailer.gif, truncated_header.gif, truncated_lzw.gif, and zero_dimensions.gif.

### 2.4 Defined policy edges

**Missing trailer.** no_trailer.gif emits one complete image, then gif_decoder_next_frame() returns GIF_STATUS_UNEXPECTED_EOF. GIF_STATUS_END_OF_STREAM is reserved for a trailer actually read.

**Plain Text.** plain_text_ext.gif emits its first image, then returns GIF_STATUS_UNSUPPORTED_FEATURE. This is deliberate current policy, not malformed input.

**Forward-declared version.** bad_magic.gif declares GIF90a. The library requires the fixed GIF signature, then applies capability-oriented best effort: a later declared version is not rejected solely by its label. The fixture uses otherwise supported semantics, so it decodes one frame and reaches GIF_STATUS_END_OF_STREAM.

### 2.5 Malformed-input outcome map

| Input condition | Lifecycle boundary | Frozen public result |
| --- | --- | --- |
| Empty or truncated header | Open | GIF_STATUS_UNEXPECTED_EOF |
| Zero logical canvas | Bind output | GIF_STATUS_INVALID_FORMAT |
| Corrupt LZW payload | Decode | GIF_STATUS_INVALID_FORMAT |
| Truncated image data | Decode | GIF_STATUS_UNEXPECTED_EOF |
| Missing trailer after a complete image | Next frame | GIF_STATUS_UNEXPECTED_EOF |
| Disposal method 3 | Decode before frame 0 | GIF_STATUS_UNSUPPORTED_FEATURE when disabled; decode and GIF_STATUS_END_OF_STREAM when enabled |
| Plain Text extension | Decode after frame 0 | GIF_STATUS_UNSUPPORTED_FEATURE |

Each result is a single expected outcome, rather than a choice among broadly similar statuses. The harness also verifies that a successful source open is paired with exactly one close across normal completion and all frozen non-success paths.

## 3. Opt-in compatibility harness

The compatibility target is deliberately opt-in and receives the local corpus path at test time. It may use host file I/O only to preload each GIF into application-owned memory; the decoder consumes that memory through the normal forward-only test port and public API.

    cmake -S . -B build/local-compat -DGIFLIB_BUILD_TESTS=OFF -DGIFLIB_BUILD_LOCAL_COMPATIBILITY_TESTS=ON -DGIFLIB_LOCAL_COMPATIBILITY_CORPUS_DIR=<local-gif-conformance-directory>
    cmake --build build/local-compat
    ctest --test-dir build/local-compat --output-on-failure

The harness verifies public statuses, frame count, canvas dimensions, relevant GifFrameInfo fields, close ownership, and allocation balance where the selected test backend makes it observable. It does not call giflib or private allocator APIs to parse or classify a GIF.

### 3.1 Composition and metadata coverage

| Area | Representative files | Checked behavior |
| --- | --- | --- |
| Minimal/static | 1x1.gif, 2color.gif, static_4x4_red.gif | Stream metadata, surface requirements, EOS, close. |
| Palettes | global_ct_only.gif, local_ct.gif, mixed_ct.gif | Global/local palette selection and colour output. |
| Interlace and geometry | static_interlaced.gif, small_frame_big_canvas.gif, overlapping_frames.gif | Pass order, background, offsets, updated rectangle, composited output. |
| Transparency and disposal 0/1/2 | transparent_bg.gif, transparent_frame.gif, dispose_background.gif | GCE state, transparent pixels, restore-to-background, continuous composition. |
| Optional disposal 3 | dispose_previous.gif plus ordinary hand-authored fixtures | Structural lifecycle result in the enabled corpus configuration; RGB888/RGB565 restore-to-previous composition, consecutive frames, and cleanup in ordinary regression. |
| Animation/timing | anim_*.gif, delay_*.gif, variable_delay.gif, loop_*.gif | Frame count, delay_ms, no decoder-owned waiting or looping. |
| Version/extensions | gif87a.gif, bad_magic.gif, comment_ext.gif | Version policy and non-rendering extension handling. |

Reviewed FNV-1a RGB888 hashes cover static_interlaced.gif, small_frame_big_canvas.gif, both overlapping_frames.gif frames, both transparent_frame.gif frames, and both dispose_background.gif frames. Pillow 12.3.0 was used only as an offline reference generator; it is not a committed test dependency.

### 3.2 Sparse read, lifecycle, and allocator matrix

The test matrix intentionally avoids an allocator × read-schedule × injection-offset cross product.

1. Every corpus case receives one normal in-memory lifecycle.
2. One representative per supported family and every malformed/unsupported case receive one-byte reads.
3. Selected static, animated, interlaced, and disposal cases also receive a practical chunk-size run.
4. Source I/O injection uses selected representatives and four named parser-phase positions.
5. Supported, malformed, and deliberately unsupported paths are repeated to check handle cleanup and allocation balance.

The primary full-corpus run uses the PRIVATE test allocator. Selected supported and malformed cases also pass through BUILTIN, LIBC, and the LVGL mock backend. BUILTIN uses the production fixed-pool path; LVGL coverage is restricted to the public allocator bridge mock. Configure `-DGIFLIB_ENABLE_DISPOSAL_METHOD_3=ON` to validate the enabled classification; the normal default continues to freeze the deliberately unsupported result.

## 4. Host instrumentation

GIFLIB_ENABLE_HOST_SANITIZERS=ON creates a separate host-only instrumented configuration. It is rejected for cross-compilation and fails configuration unless the selected compiler can link its required runtime.

| Compiler family | Instrumentation |
| --- | --- |
| MSVC or clang-cl | AddressSanitizer |
| GCC or native Clang | AddressSanitizer and UndefinedBehaviorSanitizer |

The completed validation matrix passed under MSVC 19.51 ASan and GCC 13.3 ASan+UBSan in WSL Ubuntu 24.04. These runs cover the normal host regression, embedded-player example, and opt-in compatibility harness. They do not replace target validation.

## 5. Host fuzzer

### 5.1 Harness boundary

gif_decoder_fuzzer is an explicit host-only target, not a CTest test. It uses the LIBC backend for host throughput without changing any product backend selection.

Every generated input uses the public lifecycle:

1. A memory-backed port supplies a forward-only source.
2. gif_decoder_open() processes stream metadata.
3. If the RGB888 surface requirement is at most 4 MiB, the harness binds an application-owned surface.
4. gif_decoder_next_frame() proceeds through images, extensions, palette handling, LZW data, and terminal paths.
5. Every successful open is closed before the invocation returns.

Each generated input is exercised as the complete input, a one-byte-shorter prefix when possible, and two prefix lengths selected from the first and last input bytes. The variants use unrestricted, one-byte, two-byte, and seven-byte read limits. The harness caps one invocation at 2,048 successful frames and never performs an unbounded framebuffer allocation.

### 5.2 Build and smoke run

The supported Windows workflow uses the Visual Studio LLVM clang.exe with Ninja. The runner loads the local MSVC environment, configures build/host-fuzz-clang, and enables both GIFLIB_BUILD_FUZZER=ON and GIFLIB_ENABLE_HOST_SANITIZERS=ON.

    .\tools\run_fuzz.ps1 -Smoke

The smoke mode runs 2,000 bounded executions to validate the local build and harness wiring.

### 5.3 Timed campaign

    .\tools\run_fuzz.ps1 -DurationMinutes 240 -Jobs 4

Only one runner invocation may use a local corpus at a time. The script refuses an unbounded run. A normal Ctrl+C stop preserves units already written to the seed directory, but does not create formal DONE summaries for interrupted workers.

Mutable material remains Git-ignored:

| Location | Contents |
| --- | --- |
| testdata/fuzz/seeds/ | Initial copied seeds and reusable units retained across runs. Each worker derives a temporary minimized working subset. |
| testdata/fuzz/artifacts/ | Inputs retained when an instrumented run stops on one. |
| testdata/fuzz/logs/ | Per-run JSON metadata, controller output, and worker records. |

The initial seed source is the local gif-conformance/ corpus. The runner copies its GIFs only when the seed directory is empty; later runs resume the existing local corpus.

### 5.4 Observe, replay, and handoff

For a parallel run, follow one live worker from the newest record directory:

    $run = Get-ChildItem .\testdata\fuzz\logs\workers-* |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    Get-Content (Join-Path $run.FullName 'fuzz-0.log') -Tail 20 -Wait

Use fuzz-1.log, fuzz-2.log, or fuzz-3.log for the other workers. The fuzz-<timestamp>.log file is a short run summary; fuzz-<timestamp>-controller.log is written when the runner exits.

Replay a retained input with the same instrumented target:

    .\tools\run_fuzz.ps1 -ReplayArtifact .\testdata\fuzz\artifacts\<input-file>

For a handoff, retain the run JSON, controller output, worker directory, command line, exit code, seed count/size, and every retained input. Keep all of these local unless their provenance and disclosure are separately reviewed.

### 5.5 Completed campaign evidence

Two contiguous four-worker campaigns ran on 2026-08-14 against the same host build and persistent seed directory.

| Run | Duration | Completion | Executions |
| --- | ---: | --- | ---: |
| 1 | about 2 h 56 min | Deliberately stopped | at least 2,543,856 |
| 2 | 2 h | Exit status 0 | 1,333,984 |
| **Combined** | **about 4 h 56 min** | — | **at least 3,877,840** |

Every worker replayed the inherited corpus to cov: 1336. Code coverage stayed at that value throughout both campaigns. Feature coverage rose from ft: 8180 at the end of the first run to ft: 8307 in the completed run. The second run had no retained inputs, worker peak RSS of 294–307 MiB, and no unit reaching the configured 10-second limit.

This indicates a plateau for the current harness, compiler configuration, seed corpus, and LIBC backend. A much longer identical campaign can add endurance evidence, but has limited expected value for new code coverage.

## 6. What the results support

The current evidence supports the following statements:

- Each selected corpus file has provenance, digest, classification, and a stable expected outcome.
- Supported cases reach expected structural results and selected composition oracles.
- Deliberately unsupported and malformed cases return their documented public statuses and clean up.
- The selected read, I/O, lifecycle, and allocator matrix passes.
- The host instrumentation and sustained fuzzer evidence are recorded.
- No external corpus binary enters Git, examples, installation content, or target builds.

Do not add a second corpus, a full backend Cartesian matrix, or a longer identical campaign merely to increase activity. Extend this validation only when one of the following occurs:

- A demonstrated untested GIF semantic or product corpus requirement needs a clearly licensed local fixture.
- Decoder/parser behavior changes enough to invalidate the current coverage baseline.
- An allocator or porting contract change requires a targeted lifecycle repeat.
- A separately maintained target integration becomes available for its own validation stage.

Real target behavior, vendor integration, and product-specific corpus acceptance remain outside this host validation guide.
