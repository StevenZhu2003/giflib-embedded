# Memory animation example

This complete example decodes a project-original, three-frame GIF stored in
read-only memory. It demonstrates the full application flow:

```text
application-owned memory resource
    -> example gif_porting.c
    -> gif_decoder_open()
    -> caller-owned RGB888 framebuffer
    -> gif_decoder_next_frame()
    -> application display and delay policy
    -> gif_decoder_close()
```

The example is hosted C99 and uses only the C standard library. It does not
bundle or require FatFs, a platform SDK, a display library, or another
third-party component. Console output stands in for a physical display. The
small `clock()` busy wait is application code used only to demonstrate that
frame timing happens above the decoder; embedded applications should replace
it with their own timer, scheduler, event loop, or no-delay policy.

Configure, build, and run it from the repository root:

```sh
cmake -S . -B build/example \
  -DGIFLIB_BUILD_TESTS=OFF \
  -DGIFLIB_BUILD_EXAMPLES=ON
cmake --build build/example
./build/example/gif_memory_animation_example
```

Expected output:

```text
frame 0, delay 20 ms: #445566 #112233
frame 1, delay 30 ms: #000000 #112233
frame 2, delay 0 ms: #000000 #000000
animation complete
```

The first pixel of frame zero remains `#445566` because palette index zero is
transparent for that frame. The third frame has no Graphic Control Extension,
which demonstrates that delay and transparency state do not leak between
frames.
