#!/usr/bin/env python3
"""Estimate a GIF_MEM_USE_BUILTIN pool for a declared product envelope.

This is the canonical command-line entry point.  It forwards to the estimator
implementation retained under its former file name for backwards compatibility.
The result is a planning estimate, not a proof that an arbitrary GIF corpus
cannot exhaust memory.
"""

from gif_builtin_pool_estimate import main


if __name__ == "__main__":
    main()
