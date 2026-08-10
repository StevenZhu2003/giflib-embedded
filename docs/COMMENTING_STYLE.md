# Source commenting style

Project-original code uses a Doxygen-compatible documentation style based on
conventions common to established embedded C libraries. Existing comments in
giflib-derived code are deliberately kept in their upstream form.

## Documentation blocks

- Every project-original C and header file starts with `@file` and `@brief`
  documentation.
- Public and private functions have `@brief`, `@param`, and `@return` entries.
- Pointer direction is documented with `@param[in]`, `@param[out]`, or
  `@param[in,out]`.
- Structures, enumerations, callbacks, macros, and file-scope constants have a
  concise `@brief` description.
- Structure fields and enumeration members use `/**< ... */` member comments
  when that keeps the declaration easier to scan.
- Implementation comments explain rationale, invariants, state transitions,
  format details, or safety checks. They do not merely restate the next line.

Example:

```c
/**
 * @brief Bind caller-owned pixel storage to a decoder instance.
 *
 * @param[in,out] decoder Decoder instance returned by `gif_decoder_open()`.
 * @param[in] surface     Surface descriptor copied by the decoder.
 * @return `GIF_STATUS_OK` on success, otherwise a validation status.
 */
GifStatus gif_decoder_bind_output(GifDecoder *decoder,
                                  const GifOutputSurface *surface);
```

## Upstream-derived files

Do not rewrite, move, reflow, or restyle existing giflib comments merely for
consistency. Original SPDX and copyright notices remain intact. A port-specific
comment may be added or normalized only when it documents code introduced or
changed by this project, and it must be clearly limited to that change.

## Maintenance rule

New project functions, public types, constants, and non-obvious state fields
must be documented in the same change that introduces them. Comment-only
maintenance must not alter executable behavior or upstream attribution.
