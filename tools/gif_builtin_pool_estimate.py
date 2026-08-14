#!/usr/bin/env python3
"""Estimate a GIF_MEM_USE_BUILTIN pool from a declared product envelope.

This utility is dependency-free.  It does not inspect GIF files or prove that
a chosen capacity is sufficient; it turns explicit application limits into the
profiles documented in docs/MEMORY_CONFIGURATION.md. It is a planning utility,
not a proof that an arbitrary corpus cannot exhaust a finite pool.

Copyright (c) 2026 Steven Zhu
SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass


KIB = 1024


@dataclass(frozen=True)
class Estimate:
    """Calculated pool sizes and the input components that produced them."""

    fixed_decoder_bytes: int
    global_palette_bytes: int
    local_palette_bytes: int
    row_buffer_bytes: int
    port_handle_bytes: int
    disposal3_snapshot_bytes: int
    tlsf_control_bytes: int
    payload_model_bytes: int
    payload_estimate_bytes: int
    balanced_bytes: int
    hardened_bytes: int


def non_negative(value: str) -> int:
    """Parse a non-negative decimal or ``0x`` prefixed integer."""
    parsed = int(value, 0)
    if parsed < 0:
        raise argparse.ArgumentTypeError("value must not be negative")
    return parsed


def positive(value: str) -> int:
    """Parse a strictly positive decimal or ``0x`` prefixed integer."""
    parsed = non_negative(value)
    if parsed == 0:
        raise argparse.ArgumentTypeError("value must be greater than zero")
    return parsed


def round_up(value: int, alignment: int) -> int:
    """Round one byte count upward to the selected allocator alignment."""
    return ((value + alignment - 1) // alignment) * alignment


def estimate(arguments: argparse.Namespace) -> Estimate:
    """Calculate payload, balanced, and hardened BUILTIN pool profiles."""
    palette_bytes = round_up(
        arguments.colour_map_object_bytes +
        arguments.palette_entries * arguments.colour_entry_bytes,
        arguments.alignment)
    fixed_decoder_bytes = arguments.live_decoders * arguments.decoder_fixed_bytes
    global_palette_bytes = arguments.global_palettes * palette_bytes
    local_palette_bytes = arguments.local_palettes * palette_bytes
    row_buffer_bytes = round_up(arguments.max_row_width, arguments.alignment)
    port_handle_bytes = arguments.live_decoders * arguments.port_handle_bytes
    disposal3_snapshot_bytes = (
        arguments.live_decoders * arguments.disposal3_snapshot_bytes_per_decoder
    )
    payload_model_bytes = (
        fixed_decoder_bytes + global_palette_bytes + local_palette_bytes +
        row_buffer_bytes + port_handle_bytes + disposal3_snapshot_bytes +
        arguments.tlsf_control_bytes
    )
    payload_estimate_bytes = payload_model_bytes + arguments.model_margin_bytes

    # Derived from the complete mixed random-workload boundary matrix.  Calls
    # into gif_decoder_next_frame() are serialized, so the row term is W, not
    # W multiplied by N.
    balanced_floor = (
        row_buffer_bytes + 32 * KIB + 40 * KIB * arguments.live_decoders +
        port_handle_bytes
    )
    balanced_bytes = max(
        payload_estimate_bytes + arguments.balanced_margin_bytes,
        balanced_floor,
    )

    # This is the N=1..32, W=60,000 tested upper envelope.  It is preserved as
    # a profile, not forced upon small, controlled products.
    hardened_floor = (
        row_buffer_bytes + 128 * KIB + 64 * KIB * arguments.live_decoders +
        port_handle_bytes
    )
    hardened_bytes = max(
        payload_estimate_bytes + arguments.hardened_margin_bytes,
        hardened_floor,
    )
    return Estimate(
        fixed_decoder_bytes=fixed_decoder_bytes,
        global_palette_bytes=global_palette_bytes,
        local_palette_bytes=local_palette_bytes,
        row_buffer_bytes=row_buffer_bytes,
        port_handle_bytes=port_handle_bytes,
        disposal3_snapshot_bytes=disposal3_snapshot_bytes,
        tlsf_control_bytes=arguments.tlsf_control_bytes,
        payload_model_bytes=payload_model_bytes,
        payload_estimate_bytes=payload_estimate_bytes,
        balanced_bytes=balanced_bytes,
        hardened_bytes=hardened_bytes,
    )


def format_bytes(value: int) -> str:
    """Return a compact byte and KiB representation for terminal output."""
    return f"{value:,} B ({value / KIB:.2f} KiB)"


def main() -> None:
    """Parse product limits and print a human-readable or JSON estimate."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--live-decoders", type=positive, required=True,
                        help="maximum simultaneously open GifDecoder instances (N)")
    parser.add_argument("--max-row-width", type=positive, required=True,
                        help="maximum accepted GIF image descriptor width in pixels (W)")
    parser.add_argument("--global-palettes", type=non_negative, default=None,
                        help="maximum retained global palettes; defaults to N")
    parser.add_argument("--local-palettes", type=non_negative, default=None,
                        help="maximum retained local palettes; defaults to N")
    parser.add_argument("--palette-entries", type=positive, default=256,
                        help="maximum entries in each retained palette (default: 256)")
    parser.add_argument("--port-handle-bytes", type=non_negative, default=0,
                        help="payload bytes in one gif_porting.c handle (default: 0)")
    parser.add_argument("--disposal3-snapshot-bytes-per-decoder",
                        type=non_negative, default=0,
                        help=("largest packed Restore-to-Previous rectangle per live "
                              "decoder; use zero when method 3 is disabled (default: 0)"))
    parser.add_argument("--decoder-fixed-bytes", type=positive, default=25024,
                        help="per-decoder fixed payload, excluding palettes and row; verified ARM32 default")
    parser.add_argument("--colour-map-object-bytes", type=positive, default=12,
                        help="sizeof(ColorMapObject) for the target ABI; verified ARM32 default")
    parser.add_argument("--colour-entry-bytes", type=positive, default=3,
                        help="sizeof(GifColorType) for the target ABI; default: 3")
    parser.add_argument("--tlsf-control-bytes", type=non_negative, default=1340,
                        help="TLSF control metadata in the selected pool; verified ARM32 default")
    parser.add_argument("--alignment", type=positive, default=8,
                        help="allocator allocation alignment; default: 8")
    parser.add_argument("--model-margin-bytes", type=non_negative, default=8 * KIB,
                        help="metadata/rounding reserve above the payload model; default: 8 KiB")
    parser.add_argument("--balanced-margin-bytes", type=non_negative, default=16 * KIB,
                        help="extra reserve above the model for the balanced profile; default: 16 KiB")
    parser.add_argument("--hardened-margin-bytes", type=non_negative, default=128 * KIB,
                        help="extra reserve above the model for the hardened profile; default: 128 KiB")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    arguments = parser.parse_args()
    if arguments.palette_entries > 256:
        parser.error("GIF colour tables contain at most 256 entries")
    if arguments.global_palettes is None:
        arguments.global_palettes = arguments.live_decoders
    if arguments.local_palettes is None:
        arguments.local_palettes = arguments.live_decoders
    if (arguments.global_palettes > arguments.live_decoders or
            arguments.local_palettes > arguments.live_decoders):
        parser.error("retained palette counts cannot exceed --live-decoders")

    result = estimate(arguments)
    if arguments.json:
        print(json.dumps({"inputs": vars(arguments), "estimate": asdict(result)}, indent=2))
        return

    print("GIF_MEM_USE_BUILTIN pool estimate")
    print("  The fixed-decoder and colour-map defaults are verified ARM32 values.")
    print("  Override them for another ABI; validate the final capacity on the target.")
    print()
    print("Payload model")
    print(f"  fixed decoder state : {format_bytes(result.fixed_decoder_bytes)}")
    print(f"  retained global maps: {format_bytes(result.global_palette_bytes)}")
    print(f"  retained local maps : {format_bytes(result.local_palette_bytes)}")
    print(f"  one active row      : {format_bytes(result.row_buffer_bytes)}")
    print(f"  port handles        : {format_bytes(result.port_handle_bytes)}")
    print(f"  method-3 snapshots  : {format_bytes(result.disposal3_snapshot_bytes)}")
    print(f"  TLSF control        : {format_bytes(result.tlsf_control_bytes)}")
    print()
    print(f"Payload-derived estimate : {format_bytes(result.payload_estimate_bytes)}")
    print(f"Balanced mixed-use pool  : {format_bytes(result.balanced_bytes)}")
    print(f"Hardened stress pool     : {format_bytes(result.hardened_bytes)}")
    print()
    print("Select a profile in the documentation, round upward to the relevant")
    print("linker/allocation granularity, then validate the actual GIF corpus.")


if __name__ == "__main__":
    main()
