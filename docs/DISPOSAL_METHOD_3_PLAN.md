# Disposal Method 3 Design and Verification Record

This document records the reviewed design, implementation, and verification of GIF disposal method 3, “Restore to Previous”. The feature is compile-time optional: the default build keeps it disabled, while an enabled build includes the feature and its dedicated regression coverage.

## Verification status

| Plan item | Result | Evidence |
| --- | --- | --- |
| Public lifecycle and caller-owned framebuffer remain unchanged. | Complete | `gif_decoder_open()`, `gif_decoder_bind_output()`, `gif_decoder_next_frame()`, and `gif_decoder_close()` are unchanged. |
| Default build is genuinely trimmed. | Complete | `GIF_ENABLE_DISPOSAL_METHOD_3=0` omits the snapshot pointer and method-3 copy path from `GifDecoder` and the hidden core. |
| Enabled build accepts method 3 only. | Complete | GCE acceptance permits `DISPOSE_PREVIOUS` only when the selector is `1`; Plain Text, user input, and larger disposal values keep their existing policy. |
| Restore behavior and updated rectangle are correct. | Complete | RGB888/RGB565 composition fixtures cover initial, consecutive, 3-to-2, and 2-to-3 transitions; each restoration reports the conservative union. |
| Snapshot lifetime is bounded and released. | Complete | End-of-stream, close, truncated input, source interruption, controlled allocation pressure, and repeated method-3 lifecycles are covered by the tracking fixture. |
| Backend and build coverage remain aligned. | Complete | Default and enabled host matrices run the decoder tests; enabled local compatibility runs `dispose_previous.gif` through PRIVATE, BUILTIN, LIBC, and the LVGL mock. ARM BUILTIN and PRIVATE builds retain their expected external allocator boundary. |
| Public documentation and sizing guidance are current. | Complete | README, User Guide, Memory Configuration, pool study, Host Validation, and the embedded-player example describe the selector and its boundary. |

## 1. Scope and preserved contracts

Disposal method 3 is an optional compile-time feature. The implemented feature preserves the existing public decoder lifecycle:

```text
gif_decoder_open()
    -> gif_decoder_bind_output()
    -> gif_decoder_next_frame() repeated
    -> gif_decoder_close()
```

`GifOutputSurface` remains caller-owned and stores the fully composited canvas. The public types, porting contract, allocator backend selectors, and application display/timing responsibilities do not change. The decoder continues to accept RGB888, BGR888, and RGB565 output surfaces.

Plain Text extensions and Graphic Control Extension user-input requests remain unsupported. The work does not introduce a public backup-buffer API, a second caller-owned canvas, a multi-pool allocator, or per-decoder allocator selection.

### 1.1 Compile-time feature boundary

`gif_config.h` defines `GIF_ENABLE_DISPOSAL_METHOD_3` as the centralized selector. Its default is `0`, which preserves the parser policy: disposal method 3 returns `GIF_STATUS_UNSUPPORTED_FEATURE`. A value of `1` enables restore-to-previous support. Any other value produces a compile-time configuration error.

The enabled implementation is conditionally compiled in the hidden decoder core. When the selector is `0`, the decoder object carries no method-3 snapshot pointer, the rectangle-copy helpers and snapshot allocation path are not emitted, and the BUILTIN pool has no method-3 storage requirement. This is a real code and RAM reduction boundary, not merely a runtime switch. The public API is unchanged in either configuration.

The enabled and disabled test configurations verify their intended policies. Enabled builds compose method-3 streams; disabled builds retain the single `GIF_STATUS_UNSUPPORTED_FEATURE` result before the target image. Documentation states that availability depends on `GIF_ENABLE_DISPOSAL_METHOD_3`.

## 2. Current implementation audit

The hidden core currently remembers one deferred method-2 operation: after a frame requesting restore-to-background is displayed, the next image call clears that image rectangle immediately before it composes the following image. The returned `GifFrameInfo` reports the conservative bounding rectangle of that restoration and the new image rectangle.

Graphic Control Extensions are parsed before their target image. Disabled builds reject a disposal value greater than `DISPOSE_BACKGROUND`, so an otherwise valid method-3 stream returns `GIF_STATUS_UNSUPPORTED_FEATURE` before its target image is decoded. Enabled builds accept `DISPOSE_PREVIOUS` while retaining rejection of higher disposal values. The local compatibility corpus therefore classifies `dispose_previous.gif` according to the selected configuration.

All decoder-owned allocations already go through the private `gif_mem_*` facade. The active backends are BUILTIN, PRIVATE, LIBC, and LVGL. The caller framebuffer is deliberately outside this allocator domain. Existing facade tests provide allocation tracking and controlled allocation failure; the BUILTIN test target provides fixed-pool integrity coverage. No public allocator API is available or required for this work.

## 3. Selected design

### 3.1 Save only the affected image rectangle

Before compositing a method-3 image, the decoder saves the packed pixel bytes currently present in that image rectangle. When the following image is about to be composed, it restores those saved bytes, then releases the saved storage. This is the required GIF behavior: restore the canvas state that existed immediately before the prior image was drawn.

The saved allocation is tightly packed by visible pixel bytes. It does not copy output-row padding. Its payload is:

```text
image_width × image_height × pixel_bytes
```

where `pixel_bytes` is 3 for RGB888/BGR888 and 2 for RGB565. The allocation is decoder-owned and comes from the selected `gif_mem_*` backend. It can be as large as a full canvas when an image covers the full canvas, but it is not automatically a second full canvas.

This rectangle snapshot is selected over two alternatives:

- A whole-canvas copy is rejected because it always doubles the composition storage even for small image rectangles.
- An application-provided backup buffer is rejected because it would expand the stable public API and complicate ownership, validation, and portability without a demonstrated need.

### 3.2 Deferred-disposal state

The core replaces the method-2-specific pending flag with one private pending-disposal state: none, restore-to-background, or restore-to-previous. It retains one valid rectangle for either restoration mode and, only for method 3, one owned packed snapshot pointer. The state transition is deliberately kept in the single frame-decoding path rather than split into one-use helper functions.

At the start of each new image operation, after the incoming image descriptor has been validated:

1. Apply the prior frame’s deferred disposal, if any.
2. For method 3, copy the saved bytes back to the prior rectangle and release the saved allocation immediately.
3. Build the current image rectangle.
4. If the current image requests method 3, allocate and capture its pre-composition rectangle before reading any image row.
5. Decode and composite the current image.
6. Transfer the current snapshot into pending-disposal state only after the image completes successfully.

Capturing after the prior disposal is essential for consecutive method-3 frames: each frame must preserve the canvas state visible immediately before that frame, not stale content from an earlier frame.

At GIF end of stream, a pending snapshot is discarded without restoring it because no later displayable image exists. The last successfully decoded canvas remains visible, matching existing method-2 behavior. A terminal decode failure similarly discards any pending snapshot because the decoder is sticky and cannot advance to another image. `gif_decoder_close()` releases a pending snapshot on every close path.

### 3.3 Failure and overflow behavior

The capture path checks all width, height, pixel-byte, and total-byte calculations before allocation. A calculation that cannot be represented or a failed snapshot allocation returns `GIF_STATUS_OUT_OF_MEMORY`; no new public status is introduced.

The current frame is not reported as successful until its rows have been decoded and its snapshot ownership has been committed. On a capture or later decode failure, every temporary row buffer and temporary snapshot is released, the failure becomes sticky, and close still releases the port handle and all remaining decoder state. As with the existing deferred method-2 path, a prior disposal may already have changed the caller framebuffer before a later failure prevents the next frame from being returned; application code must treat a non-OK decode result as not presentable.

## 4. Updated-rectangle policy

For every successful frame, `GifFrameInfo.updated_*` remains a conservative canvas rectangle. If no deferred restoration occurred, it is the current image rectangle. If the preceding frame used disposal method 2 or 3, it is the bounding union of the restored prior rectangle and the current image rectangle.

This policy is independent of pixel format and remains conservative when transparent pixels, overlap, or identical restored pixels make part of the region visually unchanged. The public header and User Guide describe method 3 alongside method 2.

## 5. Implementation record

1. `gif_config.h` validates the `GIF_ENABLE_DISPOSAL_METHOD_3` selector; CMake maps `GIFLIB_ENABLE_DISPOSAL_METHOD_3` to the same public definition.
2. Enabled builds accept `DISPOSE_PREVIOUS`; disabled builds retain the prior rejection boundary.
3. `GifDecoder` carries method-3 state only in enabled builds. Packed visible rows are copied directly in the single frame path, without touching output stride padding.
4. The pending state transfers snapshot ownership only after a successful image decode and releases it on end of stream, terminal decode status, and close.
5. The compatibility harness changes `dispose_previous.gif` between its enabled supported-valid result and its disabled deliberately-unsupported result at compile time.
6. The public documentation records the configuration-dependent feature status, conservative updated-rectangle rule, and enabled-build pool term without exposing core implementation interfaces.

## 6. Regression and acceptance plan

The ordinary fixture suite contains hand-authored composition streams with exact RGB888 and RGB565 checks. It covers:

- method 3 on the first frame, restoring the initial logical background before the next image;
- partial rectangles with global and local palettes;
- transparent pixels in or adjacent to a restored rectangle;
- overlapping images and conservative updated-rectangle unions;
- consecutive method-3 frames, proving each snapshot is captured after the prior restore;
- method-2-to-method-3 and method-3-to-method-2 transitions;
- non-default stride with preserved row padding;
- end of stream after a method-3 frame, which retains the displayed frame and releases decoder-owned backup storage on terminal handling or close;
- truncated input, source I/O interruption, and controlled snapshot-allocation pressure after a method-3 frame is pending;
- repeated open/decode/close cycles with allocation balance checks.

The allocation-tracking facade tests verify temporary and pending snapshot cleanup, including a controlled snapshot-allocation pressure path and repeated method-3 open/decode/close cycles. The enabled configuration runs the same fixture suite through PRIVATE, LIBC, and LVGL; the opt-in compatibility matrix runs `dispose_previous.gif` through BUILTIN as well. The existing constrained BUILTIN construction check remains a separate allocation-boundary regression.

The opt-in compatibility harness promotes `valid/dispose_previous.gif` from deliberately unsupported to supported-valid in an enabled build while retaining the disabled-build status assertion. Its backend smoke selection includes that case. The host instrumentation build and ARM BUILTIN/PRIVATE builds are rerun; BUILTIN’s unresolved-symbol audit remains free of C-library heap allocation symbols.

## 7. Memory and documentation acceptance criteria

Enabled BUILTIN sizing guidance states that the decoder pool must include the maximum simultaneously pending method-3 snapshot in addition to the existing decoder, giflib, row-buffer, and port-handle terms. Disabled builds have no method-3 snapshot term. The application framebuffer remains separate and must not be counted toward the decoder pool.

The completed feature satisfies all of the following:

- enabled builds compose disposal methods 0, 1, 2, and 3 correctly in both 24-bit and RGB565 output fixtures, while disabled builds retain their method-3 unsupported result;
- method-3 storage never crosses the public/private allocator boundary and no direct libc allocator dependency is introduced for BUILTIN or PRIVATE;
- every close, end-of-stream, non-success, and allocation-pressure path has balanced decoder allocations and a single port close where an open succeeded;
- updated rectangles satisfy the documented conservative union rule;
- normal host regression, opt-in compatibility checks, host instrumentation, and ARM validation pass; and
- all public support statements and compatibility classifications agree with the implementation.
