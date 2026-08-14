# Stage 8 compatibility and malformed-input test plan

## 1. Purpose and boundaries

Stage 8 increases confidence in the current public decoder contract by exercising real, externally generated GIF files in addition to the repository's small project-original byte fixtures. It is a host-side validation stage. It does not change the decoder architecture, add a filesystem dependency to the library, implement disposal method 3, or make a hardware-validation claim.

The input source and license record are defined in [COMPATIBILITY_CORPUS.md](COMPATIBILITY_CORPUS.md). The external corpus remains a durable but Git-ignored local dependency under `testdata/compatibility_corpus/`; no external GIF, derivative GIF, fuzz input, or generated result is part of a commit. Existing small C byte fixtures remain the normal repository regression coverage.

## 2. Test architecture

The new suite will be a separately enabled host target rather than an unconditional CTest dependency. Its test process may use ordinary host file I/O only to preload a local GIF into application-owned memory. The decoder itself continues to consume that memory through the existing forward-only test port and public API.

Implemented build controls:

| Control | Default | Meaning |
| --- | --- | --- |
| `GIFLIB_BUILD_LOCAL_COMPATIBILITY_TESTS` | `OFF` | Builds the external-corpus host suite only when explicitly requested. |
| `GIFLIB_LOCAL_COMPATIBILITY_CORPUS_DIR` | empty | Absolute or relative path to the locally acquired `gif-conformance/` directory. Configuration fails clearly when the suite is enabled but the directory is absent. |

The harness must use `GifDecoderConfig`, `gif_decoder_open()`, `gif_decoder_bind_output()`, `gif_decoder_next_frame()`, and `gif_decoder_close()` exactly as an application would. It must not call `giflib` or allocator private APIs to parse or classify a GIF. Test-only allocator accounting and TLSF integrity checks remain acceptable where the existing test configuration already exposes them.

## 3. Result model

Every corpus case gets one explicit local manifest entry before it becomes a regression assertion:

| Field | Required value |
| --- | --- |
| Identity | Upstream revision, relative filename, SHA-256 digest, and declared license. |
| Classification | Supported-valid, malformed, deliberately unsupported, or forward-version compatibility. |
| Expected public result | Exact open/decode/EOS status sequence, including the point at which an unsupported feature is detected. |
| Stream observations | Canvas dimensions, decoded frame count, and relevant `GifFrameInfo` fields. |
| Rendering oracle | Where composition is important, a separately reviewed expected RGB888 canvas hash or exact pixel description. |
| Lifecycle checks | Close count, allocation balance where available, and repeatability under the selected read schedule. |

The initial exploratory sweep did not turn an upstream label such as “valid” into this library's expected result automatically. The resulting classifications and structural outcomes are now frozen in `tests/test_compatibility.c`. For example, `dispose_previous.gif` is valid GIF but deliberately unsupported by the current public contract; Plain Text returns `GIF_STATUS_UNSUPPORTED_FEATURE` after its first image; and `bad_magic.gif` is a forward-version compatibility case under the adopted best-effort header policy.

## 4. Corpus coverage matrix

### 4.1 Supported-valid baseline

Run every candidate that uses only currently supported semantics through open, bind, frame decode, normal `GIF_STATUS_END_OF_STREAM`, and close.

| Corpus area | Representative files | Required checks |
| --- | --- | --- |
| Minimal/static | `1x1.gif`, `2color.gif`, `static_4x4_red.gif`, `static_8x8_palette.gif`, `static_256colors.gif` | Stream metadata, framebuffer capacity/stride, one frame, EOS, close. |
| Palettes | `global_ct_only.gif`, `local_ct.gif`, `mixed_ct.gif`, `large_palette_small_image.gif` | Global/local palette changes and colour output. |
| Interlace and geometry | `static_interlaced.gif`, `small_frame_big_canvas.gif`, `overlapping_frames.gif` | Interlace pass order, background initialization, image offsets, updated rectangle, composited result. |
| Transparency and disposal 0/1/2 | `transparent_bg.gif`, `transparent_frame.gif`, `dispose_unspecified.gif`, `dispose_none.gif`, `dispose_background.gif` | Transparency, frame-scope GCE state, background restore, continuous composition, updated rectangle. |
| Animation/timing/loop metadata | `anim_2frame.gif`, `anim_3frame_rgb.gif`, `anim_10frame.gif`, `delay_*.gif`, `variable_delay.gif`, `loop_*.gif`, `no_loop_ext.gif` | Frame count, `delay_ms`, decoder does not wait or loop itself, normal EOS. |
| Version/benign extension | `gif87a.gif`, `bad_magic.gif`, `comment_ext.gif` | Accept GIF87a and forward-declared streams whose actual semantics are supported; skip/handle documented non-rendering extension without corrupting stream state. |

### 4.2 Deliberately unsupported valid input

`dispose_previous.gif` must be tested as a valid stream that reaches `GIF_STATUS_UNSUPPORTED_FEATURE` at the documented decode boundary. The test must still verify one close, no leaked test allocations, and no continued successful decode after the sticky failure.

Any corpus file that exposes another deliberately unsupported GIF semantic receives the same treatment. The test must never relabel a deliberate rejection as `GIF_STATUS_INVALID_FORMAT` merely to make the valid set pass.

### 4.3 Malformed and truncated input

The upstream `invalid/` directory supplies six malformed/truncated robustness cases: `truncated_header.gif`, `truncated_lzw.gif`, `empty.gif`, `no_trailer.gif`, `bad_lzw_code.gif`, and `zero_dimensions.gif`. `bad_magic.gif` is instead a forward-version compatibility case: its `GIF90a` declaration is processed using the adopted capability-oriented best-effort policy. The upstream directory name alone is not a public decoder contract.

Before freezing exact assertions, run each through the public facade and record its exact status at open or frame decode. The final assertion permits only the single documented status chosen from the observed public contract. In particular, `no_trailer.gif` must return `GIF_STATUS_UNEXPECTED_EOF` after its decodable image: `GIF_STATUS_END_OF_STREAM` is reserved for a trailer actually read, while the port reports end of input before that trailer. Every malformed case must complete without a hang, crash, out-of-bounds write, leak, double close, or allocator integrity failure.

`plain_text_ext.gif` is deliberately unsupported rather than supported-valid or a remaining specification edge. The core explicitly returns `GIF_STATUS_UNSUPPORTED_FEATURE` for the Plain Text extension.

## 5. Read, lifecycle, and memory schedules

The suite must use a sparse, purpose-driven matrix rather than an allocator × read-schedule × injection-offset Cartesian product:

1. every corpus file receives one normal in-memory baseline run;
2. one representative per supported feature family plus every malformed/unsupported case receives a one-byte short-read run;
3. a small selected set of static, animated, interlaced, and disposal-2 files receives one additional practical chunk-size run; and
4. source-I/O injection applies only to those selected representatives and to a small number of named offsets chosen for distinct parser phases.

For supported-valid inputs, repeat the full open/bind/decode/EOS/close lifecycle enough times to observe allocation balance and source-handle cleanup. For malformed and unsupported inputs, repeat open/decode/close after each failure. All successful opens must close exactly once.

The primary comprehensive run uses the existing PRIVATE test allocator so outstanding allocations can be checked deterministically. It has passed the frozen 39-case structural baseline and sparse matrix. A selected supported-valid and malformed smoke set has also passed through BUILTIN, LIBC, and the LVGL mock backend. BUILTIN uses the production fixed-pool path; LVGL coverage stays restricted to the public allocator bridge mock. No Stage 8 test adds allocator behavior to decoder source files.

The implemented opt-in target is `giflib_local_compatibility_tests`. It receives the corpus root as a CTest runtime argument rather than embedding a machine-specific path into the binary. For example:

```text
cmake -S . -B build/local-compat -DGIFLIB_BUILD_TESTS=OFF -DGIFLIB_BUILD_LOCAL_COMPATIBILITY_TESTS=ON -DGIFLIB_LOCAL_COMPATIBILITY_CORPUS_DIR=<local-gif-conformance-directory>
cmake --build build/local-compat
ctest --test-dir build/local-compat --output-on-failure
```

## 6. Rendering and metadata oracles

Status-only testing is insufficient for composition cases. The plan uses three oracle levels:

| Level | Use | Source of truth |
| --- | --- | --- |
| Structural | All cases | Public statuses, frame count, dimensions, `GifFrameInfo`, close count, and allocation balance. |
| Exact pixels | Small composition cases | Reviewed RGB888 byte sequence or canvas hash generated independently of the decoder under test. |
| Semantic pixels | Larger/animation cases | Reviewed expected palette, rectangle, transparency, disposal, and frame relationship; add a full hash only when it remains easy to audit. |

Reference pixels must not be produced solely by the decoder under test. An independent host decoder or the upstream generator's documented geometry may help create a proposed oracle, but every expected value must be reviewed and recorded before it is used as a pass condition.

The first reviewed exact-pixel subset uses independent Pillow 12.3.0 RGB conversion only as an offline, disposable reference generator. The committed test has no Pillow dependency: it retains reviewed FNV-1a RGB888 hashes for `static_interlaced.gif`, `small_frame_big_canvas.gif`, both `overlapping_frames.gif` frames, both `transparent_frame.gif` frames, and both `dispose_background.gif` frames. These cases cover interlace, caller-owned full-canvas composition, partial/overlapping rectangles, transparency, and restore-to-background disposal.

## 7. Sanitizer and fuzzing follow-up

The project provides the opt-in `GIFLIB_ENABLE_HOST_SANITIZERS` CMake option for a separate host-only instrumentation build. It applies the sanitizer set supported by the selected compiler to every library, test, and example target: MSVC or clang-cl uses AddressSanitizer, while GCC or native Clang uses AddressSanitizer and UndefinedBehaviorSanitizer. It rejects cross-compilation and fails during configuration unless the selected toolchain can link the required runtime. For an MSVC build, the AddressSanitizer runtime is copied next to each local test and example executable.

Use that configuration to run:

- a bounded timeout for every malformed-input case;
- the ordinary host regression and example; and
- the opt-in compatibility corpus harness when its local corpus path is available.

Sanitizers are not prerequisites for the initial corpus classification and are never enabled for target builds. Fuzzing remains deferred: a future fuzz entry point must accept arbitrary bytes through the same memory-backed port and always execute close after a successful open, with a small local seed directory derived from the licensed corpus plus project-original minimal fixtures.

## 8. Acceptance criteria

Stage 8 is complete only when:

- every selected local file has provenance, digest, license, classification, and stable expected outcome recorded;
- supported-valid cases reach normal EOS with expected structural results and reviewed composition oracles where applicable;
- deliberately unsupported valid cases return `GIF_STATUS_UNSUPPORTED_FEATURE` and clean up correctly;
- malformed cases have stable, documented public errors, including `GIF_STATUS_UNEXPECTED_EOF` for a missing trailer;
- the selected short-read, final-byte EOF, I/O-failure, repeated-lifecycle, and backend matrix runs pass;
- no test introduces external corpus binaries into Git, CMake install content, examples, or target builds; and
- the ordinary repository host regression remains independent of the local corpus path.

## 9. Implementation order

1. **Completed:** Run the local observational sweep and complete the per-file manifest.
2. **Completed:** Freeze classifications and structural outcomes after reviewing discrepancies against public documentation and supported-feature policy.
3. **Completed:** Implement the opt-in host harness and bounded short-read/lifecycle matrix.
4. **Completed:** Add reviewed composition oracles for high-value geometry, transparency, interlace, and disposal cases.
5. **Completed:** Run the selected BUILTIN, LIBC, and LVGL smoke matrix; validate the host-only sanitizer configuration using MSVC 19.51 ASan and GCC 13.3 ASan+UBSan. Fuzzing remains deferred.
6. Update user-facing support documentation only after a behavior is implemented, tested, and stable.
