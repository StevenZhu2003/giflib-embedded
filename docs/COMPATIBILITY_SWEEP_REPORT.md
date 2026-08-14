# Initial compatibility corpus observational sweep

## Scope and status

This is the completed first observation pass for Stage 8. It records current behaviour only; it is not a compatibility promise, a frozen regression oracle, or evidence that every input is safe under all source and allocator conditions. No decoder or giflib-derived source was changed while making this report.

The run used the locally retained imazen `gif-conformance/` corpus at revision `28205bbc5cf40364d012c462240ba28143373d67`, as recorded in [COMPATIBILITY_CORPUS.md](COMPATIBILITY_CORPUS.md). The source files and raw result manifest remain local-only under `testdata/compatibility_corpus/` and are excluded from Git. The disposable C99 runner is under `_TEMP/compatibility_sweep/`.

Each of the 39 inputs was preloaded into application-owned host memory, then exercised only through `GifDecoderConfig`, `gif_decoder_open()`, `gif_decoder_bind_output()`, `gif_decoder_next_frame()`, and `gif_decoder_close()`. The normal forward-read schedule was used; this pass intentionally does not add a read-schedule, allocator-backend, or injected-I/O-failure matrix. The runner supplied an independent porting handle through the standard `gif_porting_mem_alloc()` / `gif_porting_mem_free()` bridge for every open.

The per-file raw observation record is `testdata/compatibility_corpus/OBSERVATIONAL_SWEEP_NORMAL.tsv`. It contains file size, public status at each lifecycle boundary, decoded-frame count, canvas dimensions, port operation counts, and the read schedule.

## Result summary

| Observed classification | Files | Count | Current outcome |
| --- | --- | ---: | --- |
| Supported pass | 27 `valid/` files excluding disposal 3; `comment_ext.gif`; `gif87a.gif`; `large_palette_small_image.gif` | 30 | Open, bind, decode, and finish with `GIF_STATUS_END_OF_STREAM`. |
| Deliberately unsupported | `dispose_previous.gif`; `plain_text_ext.gif` | 2 | `GIF_STATUS_UNSUPPORTED_FEATURE`; no decoder change is indicated. |
| Malformed handling | `bad_lzw_code.gif`; `empty.gif`; `no_trailer.gif`; `truncated_header.gif`; `truncated_lzw.gif`; `zero_dimensions.gif` | 6 | A stable non-success public status was returned at open, bind, or next-frame. |
| Unexpected acceptance | `bad_magic.gif` | 1 | Decodes one frame and ends with `GIF_STATUS_END_OF_STREAM`, despite the upstream invalid classification. |
| Remaining specification edge | None in this 39-file pass | 0 | Missing trailer and Plain Text have a defined current outcome below. |

All successful opens performed one observed port open and one observed port close. No case hung or crashed in this normal-read pass. This result does not yet establish allocation balance, TLSF integrity, short-read behaviour, injected I/O cleanup, or rendering-pixel correctness.

## Defined current outcomes for formerly ambiguous inputs

### Missing trailer

`invalid/no_trailer.gif` opens and emits its one complete image. Its next `gif_decoder_next_frame()` returns `GIF_STATUS_UNEXPECTED_EOF`, rather than `GIF_STATUS_END_OF_STREAM`.

This is the single later regression expectation. It follows the public status contract: end-of-stream means that the GIF trailer was reached, whereas unexpected EOF means the input ended before completion. In the current parser path, the source reports EOF while giflib is attempting to read the next record, and `gif_decoder_map_error()` maps that read failure to `GIF_STATUS_UNEXPECTED_EOF`. The suite must not accept either status interchangeably.

### Plain Text extension

`edge-cases/plain_text_ext.gif` opens, emits its first image, then returns `GIF_STATUS_UNSUPPORTED_FEATURE` when its Plain Text extension is encountered. `gif_decoder_core.c` explicitly selects that status for `PLAINTEXT_EXT_FUNC_CODE`. This is a deliberate unsupported-feature case, not malformed input and not a remaining specification edge.

## Failure landscape

| Input class | Lifecycle point | Observed status | Interpretation |
| --- | --- | --- | --- |
| Empty or truncated header | Open | `GIF_STATUS_UNEXPECTED_EOF` | The source reaches EOF before giflib can read a complete header. |
| Invalid zero canvas | Bind output | `GIF_STATUS_INVALID_FORMAT` | The stream opens, but output binding rejects non-positive logical-screen dimensions. |
| Corrupt LZW payload | Decode | `GIF_STATUS_INVALID_FORMAT` | The parser rejects the code stream before it emits a frame. |
| Truncated image data | Decode | `GIF_STATUS_UNEXPECTED_EOF` | The source ends during image-data processing. |
| Omitted trailer after an image | Decode after frame 0 | `GIF_STATUS_UNEXPECTED_EOF` | A complete frame does not make a missing trailer a clean end-of-stream. |
| Disposal method 3 | Decode before frame 0 | `GIF_STATUS_UNSUPPORTED_FEATURE` | Valid GIF semantic outside the current supported feature boundary. |
| Plain Text extension | Decode after frame 0 | `GIF_STATUS_UNSUPPORTED_FEATURE` | Explicit current core policy. |
| Bad GIF version string | Decode succeeds | `GIF_STATUS_END_OF_STREAM` | Unexpected acceptance requiring later review. |

The zero-dimension result deserves a runner note: the first exploratory runner had refused to call `gif_decoder_bind_output()` for a zero-size canvas and therefore produced its own buffer-size result. The runner was corrected to supply a non-NULL minimal surface; the manifest above is the corrected full 39-file re-run and records the decoder's `GIF_STATUS_INVALID_FORMAT` instead.

## Header/version policy review before assertions

`invalid/bad_magic.gif` has the six-byte header `GIF90a`. The current giflib-derived opening code verifies only the fixed three-byte `GIF` signature, then parses the stream. It therefore accepts this input, which contains otherwise decodable baseline image data. The source's private `gif89` flag is false for this header, but that flag is used only by the private version-reporting helper and does not change the public decoder's parsing path.

The upstream `invalid/` classification must not decide the project policy by itself. The GIF89a specification defines the version as a declaration of the minimum decoder capabilities, gives an ordering that includes potential later numeric revisions, and recommends that a decoder attempt a stream it cannot fully process to the best of its ability. `GIF90a` is consequently not a malformed signature merely because it differs from the two revisions known in the 1990 specification.

Three coherent policies are available for review:

| Policy | `GIF90a` outcome | Consequence |
| --- | --- | --- |
| Retain capability-oriented best effort (recommended) | Decode it when its actual blocks use supported semantics; otherwise return the existing format/unsupported error at the point encountered. | Preserves giflib behaviour and the specification's decoder recommendation. The later regression treats this file as a version-forward-compatibility success case, not malformed input. |
| Restrict to known revisions | Reject every header except `GIF87a` and `GIF89a` at open with `GIF_STATUS_INVALID_FORMAT`. | Simple and deterministic, but is stricter than the specification's best-effort recommendation and rejects harmless forward-version files. |
| Treat an unknown declared version as unsupported | Reject `GIF90a` at open with `GIF_STATUS_UNSUPPORTED_FEATURE`. | Makes the declared capability boundary explicit, but gives up best-effort decoding even where actual content fits the implemented feature set. It also requires a precise definition of valid future-version syntax. |

No change was made in this phase. Before freezing an assertion, the project must choose one policy and document it in the public supported-format boundary.

## Deliberately deferred checks

The next phase may add only the sparse matrix defined in [COMPATIBILITY_TEST_PLAN.md](COMPATIBILITY_TEST_PLAN.md): representative short reads and I/O failures, selected backend smoke coverage, repeated lifecycle and allocation-balance checks, then independently reviewed composition oracles. It must not expand into a full allocator × read schedule × injection offset cross product.
