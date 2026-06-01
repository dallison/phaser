"""Shared compiler warning flags for Phaser's own (hand-written) targets.

These are intentionally applied per-target so they only affect code in this
repository, and never leak into external dependencies or generated code.
"""

PHASER_COPTS = [
    "-Wall",
    "-Wextra",
    "-Wpedantic",
]
