# Disposal Method 3 Implementation Plan

This document records the reviewed implementation plan for GIF disposal method 3, “Restore to Previous”. It is a design and acceptance record; the feature is not supported until the implementation, regression coverage, and public documentation changes are complete.

## 1. Scope and preserved contracts

Disposal method 3 is an optional compile-time feature. The feature will preserve the existing public decoder lifecycle:

```text
gif_decoder_open()
    -> gif_decoder_bind_output()
    -> gif_decoder_next_frame() repeated
    -> gif_decoder_close()
```

`GifOutputSurface` remains caller-owned and stores the fully composited canvas. The public types, porting contract, allocator backend selectors, and application display/timing responsibilities will not change. The decoder will continue to accept RGB888, BGR888, and RGB565 output surfaces.

Plain Text extensions and Graphic Control Extension user-input requests remain unsupported. The work does not introduce a public backup-buffer API, a second caller-owned canvas, a multi-pool allocator, or per-decoder allocator selection.

### 1.1 Compile-time feature boundary

`gif_config.h` will define `GIF_ENABLE_DISPOSAL_METHOD_3` as the centralized selector. Its default will be `0`, which preserves today’s parser policy: disposal method 3 returns `GIF_STATUS_UNSUPPORTED_FEATURE`. A value of `1` enables restore-to-previous support. Any other value will produce a compile-time configuration error.

The enabled implementation will be conditionally compiled in the hidden decoder core. When the selector is `0`, the decoder object will carry no method-3 snapshot pointer, the rectangle-copy helpers and snapshot allocation path will not be emitted, and the BUILTIN pool has no method-3 storage requirement. This is a real code and RAM reduction boundary, not merely a runtime switch. The public API is unchanged in either configuration.

Every enabled and disabled test configuration must verify its intended policy. Enabled builds must compose method-3 streams; disabled builds must retain the single `GIF_STATUS_UNSUPPORTED_FEATURE` result before the target image. Documentation will state that availability depends on `GIF_ENABLE_DISPOSAL_METHOD_3`.

## 2. Current implementation audit

The hidden core currently remembers one deferred method-2 operation: after a frame requesting restore-to-background is displayed, the next image call clears that image rectangle immediately before it composes the following image. The returned `GifFrameInfo` reports the conservative bounding rectangle of that restoration and the new image rectangle.

Graphic Control Extensions are parsed before their target image. The current implementation rejects a disposal value greater than `DISPOSE_BACKGROUND`, so an otherwise valid method-3 stream returns `GIF_STATUS_UNSUPPORTED_FEATURE` before its target image is decoded. The local compatibility corpus therefore classifies `dispose_previous.gif` as deliberately unsupported. This remains the required behavior of builds where `GIF_ENABLE_DISPOSAL_METHOD_3` is `0`.

All decoder-owned allocations already go through the private `gif_mem_*` facade. The active backends are BUILTIN, PRIVATE, LIBC, and LVGL. The caller framebuffer is deliberately outside this allocator domain. Existing facade tests provide allocation tracking and controlled allocation failure; the BUILTIN test target provides fixed-pool integrity coverage. No public allocator API is available or required for this work.

## 3. Selected design

### 3.1 Save only the affected image rectangle

Before compositing a method-3 image, the decoder will save the packed pixel bytes currently present in that image rectangle. When the following image is about to be composed, it will restore those saved bytes, then release the saved storage. This is the required GIF behavior: restore the canvas state that existed immediately before the prior image was drawn.

The saved allocation is tightly packed by visible pixel bytes. It does not copy output-row padding. Its payload is:

```text
image_width × image_height × pixel_bytes
```

where `pixel_bytes` is 3 for RGB888/BGR888 and 2 for RGB565. The allocation is decoder-owned and comes from the selected `gif_mem_*` backend. It can be as large as a full canvas when an image covers the full canvas, but it is not automatically a second full canvas.

This rectangle snapshot is selected over two alternatives:

- A whole-canvas copy is rejected because it always doubles the composition storage even for small image rectangles.
- An application-provided backup buffer is rejected because it would expand the stable public API and complicate ownership, validation, and portability without a demonstrated need.

### 3.2 Deferred-disposal state

The core will replace the method-2-specific pending flag with one private pending-disposal state: none, restore-to-background, or restore-to-previous. It will retain one valid rectangle for either restoration mode and, only for method 3, one owned packed snapshot pointer.

At the start of each new image operation, after the incoming image descriptor has been validated:

1. Apply the prior frame’s deferred disposal, if any.
2. For method 3, copy the saved bytes back to the prior rectangle and release the saved allocation immediately.
3. Build the current image rectangle.
4. If the current image requests method 3, allocate and capture its pre-composition rectangle before reading any image row.
5. Decode and composite the current image.
6. Transfer the current snapshot into pending-disposal state only after the image completes successfully.

Capturing after the prior disposal is essential for consecutive method-3 frames: each frame must preserve the canvas state visible immediately before that frame, not stale content from an earlier frame.

At GIF end of stream, a pending snapshot is discarded without restoring it because no later displayable image exists. The last successfully decoded canvas remains visible, matching existing method-2 behavior. A terminal decode failure similarly discards any pending snapshot because the decoder is sticky and cannot advance to another image. `gif_decoder_close()` will release a pending snapshot on every close path.

### 3.3 Failure and overflow behavior

The capture path will check all width, height, pixel-byte, and total-byte calculations before allocation. A calculation that cannot be represented or a failed snapshot allocation returns `GIF_STATUS_OUT_OF_MEMORY`; no new public status is introduced.

The current frame is not reported as successful until its rows have been decoded and its snapshot ownership has been committed. On a capture or later decode failure, every temporary row buffer and temporary snapshot is released, the failure becomes sticky, and close still releases the port handle and all remaining decoder state. As with the existing deferred method-2 path, a prior disposal may already have changed the caller framebuffer before a later failure prevents the next frame from being returned; application code must treat a non-OK decode result as not presentable.

## 4. Updated-rectangle policy

For every successful frame, `GifFrameInfo.updated_*` will remain a conservative canvas rectangle. If no deferred restoration occurred, it is the current image rectangle. If the preceding frame used disposal method 2 or 3, it is the bounding union of the restored prior rectangle and the current image rectangle.

This policy is independent of pixel format and remains conservative when transparent pixels, overlap, or identical restored pixels make part of the region visually unchanged. The public header and User Guide will be updated to describe method 3 alongside method 2.

## 5. Implementation steps

1. Add and validate the `GIF_ENABLE_DISPOSAL_METHOD_3` selector in `gif_config.h`, then make all method-3-only core state and logic conditional on it.
2. In enabled builds, extend GCE acceptance to allow `DISPOSE_PREVIOUS` while retaining rejection of values above it and of user-input requests. Disabled builds keep the current rejection boundary.
3. Add private rectangle-copy, restore, disposal-release, and rectangle-union helpers in `src/gif_decoder_core.c`. The copy helpers will operate in packed visible rows using the selected output pixel size and will not touch stride padding.
4. Add private pending-disposal fields to `GifDecoder`; no public struct changes are needed.
5. Integrate the save/restore state transition into the image path, with explicit ownership transfer only after successful decode.
6. Release snapshot ownership on end of stream, sticky failure, and `gif_decoder_close()`.
7. Update the local compatibility harness classification and its expected structural/composition outcomes for `dispose_previous.gif` in an enabled build, while retaining the disabled-build status assertion.
8. Update README, User Guide, Host Validation, and memory configuration material with the configuration-dependent support status, updated-rectangle rule, and enabled-build snapshot memory budget. Keep detailed implementation rationale in this document rather than duplicating it everywhere.

## 6. Regression and acceptance plan

The ordinary fixture suite will add hand-authored composition streams with exact RGB888 and RGB565 checks. They will cover:

- method 3 on the first frame, restoring the initial logical background before the next image;
- partial rectangles with global and local palettes;
- transparent pixels in or adjacent to a restored rectangle;
- overlapping images and conservative updated-rectangle unions;
- consecutive method-3 frames, proving each snapshot is captured after the prior restore;
- method-2-to-method-3 and method-3-to-method-2 transitions;
- non-default stride with preserved row padding;
- end of stream after a method-3 frame, which retains the displayed frame and releases decoder-owned backup storage on terminal handling or close;
- truncated input, source I/O failure, and controlled snapshot-allocation failure after a method-3 frame is pending;
- repeated open/decode/close cycles with allocation balance checks.

The existing allocation-tracking facade tests will verify temporary and pending snapshot cleanup. The fixed-pool BUILTIN test will gain a method-3 pressure case that proves a constrained pool reports `GIF_STATUS_OUT_OF_MEMORY` cleanly and remains internally consistent after close. The existing LIBC and LVGL facade targets exercise the same core path.

After normal regression passes, the opt-in compatibility harness will promote `valid/dispose_previous.gif` from deliberately unsupported to supported-valid, freeze one structural lifecycle outcome, and add RGB888 composition hashes if its content provides useful independent coverage. Its sparse one-byte-read, lifecycle, and backend smoke selections will include that case. Host sanitizer builds and the existing ARM BUILTIN/PRIVATE builds will be rerun; BUILTIN’s unresolved-symbol audit must remain free of C-library heap allocation symbols.

## 7. Memory and documentation acceptance criteria

Enabled BUILTIN sizing guidance will state that the decoder pool must include the maximum simultaneously pending method-3 snapshot in addition to the existing decoder, giflib, row-buffer, and port-handle terms. Disabled builds have no method-3 snapshot term. The application framebuffer remains separate and must not be counted toward the decoder pool.

The feature is accepted only when all of the following are true:

- enabled builds compose disposal methods 0, 1, 2, and 3 correctly in both 24-bit and RGB565 output fixtures, while disabled builds retain their method-3 unsupported result;
- method-3 storage never crosses the public/private allocator boundary and no direct libc allocator dependency is introduced for BUILTIN or PRIVATE;
- every close, end-of-stream, non-success, and allocation-pressure path has balanced decoder allocations and a single port close where an open succeeded;
- updated rectangles satisfy the documented conservative union rule;
- normal host regression, opt-in compatibility checks, host instrumentation, and ARM validation pass; and
- all public support statements and compatibility classifications agree with the implementation.
