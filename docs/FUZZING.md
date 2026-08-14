# Host fuzz testing

This document defines the first-stage host fuzzing workflow for `giflib-embedded`. It is a host checking workflow, not an embedded-target validation or a proof about every possible GIF. The decoder, target builds, and installed library are unaffected unless the explicit fuzzer CMake option is enabled.

## Scope

`gif_decoder_fuzzer` receives arbitrary byte sequences through the same public decoder lifecycle used by an application:

1. a memory-backed implementation of `gif_porting_open/read/close` exposes the bytes as a forward-only source;
2. `gif_decoder_open()` processes the header and logical-screen descriptor;
3. for a bounded logical screen, the harness creates an application-owned RGB888 framebuffer and calls `gif_decoder_bind_output()`;
4. `gif_decoder_next_frame()` continues through image descriptors, extensions, palette handling, LZW data, and trailer/error paths; and
5. every successful open is closed before the invocation returns.

For every generated input, the harness executes four source variants: the complete input, the final-byte truncation, and two deterministic arbitrary-position truncations. It also varies legal port short-read limits from unrestricted down to one byte. This keeps truncation, parser, extension, and LZW coverage in the single public-facade target without multiplying a separate test matrix.

The harness accepts all documented public results for non-normal input. It stops only when the instrumented build reports a condition, libFuzzer stops a unit, or the source-handle close invariant is not met. To keep application memory bounded, it binds an output surface only when the required RGB888 framebuffer is at most 4 MiB. This limit is a harness resource boundary, not a decoder format limit.

## Prerequisites and build boundary

The tracked harness requires a host Clang toolchain with libFuzzer and the project's instrumented checking runtime. The supported Windows workflow uses `clang.exe` from the Visual Studio LLVM installation with the Ninja generator. `GIFLIB_BUILD_FUZZER=ON` rejects cross-compilation, non-Clang compilers, the MSVC-style command line, and configurations without `GIFLIB_ENABLE_HOST_SANITIZERS=ON`.

The fuzzer uses the LIBC allocator backend deliberately: it enables normal host mutation throughput while the existing backend-specific tests continue to cover BUILTIN, PRIVATE, and LVGL allocator semantics. It does not change the selected backend of a product build. The Windows campaign and the separately validated Linux host build use the checking runtimes available from their respective compilers. Neither result substitutes for real-hardware validation.

## Local input and result locations

All mutable fuzz material is intentionally Git-ignored:

| Location | Contents |
| --- | --- |
| `testdata/fuzz/seeds/` | Initial copied seeds and libFuzzer's minimized/resumed corpus. |
| `testdata/fuzz/artifacts/` | Saved inputs associated with an instrumented-run stop. |
| `testdata/fuzz/logs/` | Console logs and per-run JSON metadata. |

The default initial source is the existing local compatibility corpus at `testdata/compatibility_corpus/codec-corpus/gif-conformance/`. Its provenance and license are recorded in [COMPATIBILITY_CORPUS.md](COMPATIBILITY_CORPUS.md). The runner copies the local GIFs only on the first run; later runs resume the generated corpus. No external GIF or fuzz artifact is staged or pushed.

## Run a smoke check

From a PowerShell session in the repository:

```powershell
.\tools\run_fuzz.ps1 -Smoke
```

The script loads the MSVC environment, configures a separate `build/host-fuzz-clang` build with the Visual Studio LLVM `clang.exe` and Ninja, builds only `gif_decoder_fuzzer`, initializes the local seed directory if needed, and runs 2,000 bounded fuzz iterations. It is suitable for validating the toolchain and harness wiring, not for measuring confidence.

## Start a timed campaign

Choose an explicit duration. One worker is the default and is recommended for the first campaign because its log is simple to inspect:

```powershell
.\tools\run_fuzz.ps1 -DurationMinutes 240
```

For an intentionally parallel host run, choose a worker count explicitly:

```powershell
.\tools\run_fuzz.ps1 -DurationMinutes 240 -Jobs 4
```

Use only one runner invocation for a given local corpus at a time. The script rejects a new invocation while its fuzzer executable is already active, so two independent runs cannot compete for the same corpus files. The runner does not start an unbounded campaign. `Ctrl+C` stops the current process; corpus units already written below `testdata/fuzz/seeds/` remain available for the next run. A clean exit has status zero. A non-zero exit preserves the log, JSON metadata, and any saved input for diagnosis.

## Observe and replay

The runner prints live libFuzzer status and a time-based PowerShell progress indicator. In another terminal, the latest log can be followed directly:

```powershell
Get-Content .\testdata\fuzz\logs\fuzz-<timestamp>.log -Wait
```

When a run produces a saved input, do not delete it. Replay it with the same instrumented target:

```powershell
.\tools\run_fuzz.ps1 -ReplayArtifact .\testdata\fuzz\artifacts\<artifact-file>
```

For handoff, provide the newest `fuzz-<timestamp>.log` and `fuzz-<timestamp>.json`, the command used, the resulting exit code, and every new file from `testdata/fuzz/artifacts/`. If a campaign finishes cleanly, provide the same log/metadata plus the seed-corpus file count and total size. Keep the files local unless their provenance and disclosure are separately reviewed.

## Limits and follow-up

libFuzzer explores mutations around the available seed corpus; it does not prove that arbitrary inputs, all platform ports, or a target allocator are defect-free. A production integration still needs ordinary regression, actual-corpus validation, and target testing. Future work may add corpus-specific host sizing and fuzz tooling only when it can preserve these boundaries; see [TODO_LIST.md](TODO_LIST.md).
