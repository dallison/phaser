"""Shared compiler warning flags for Phaser's own (hand-written) targets.

These are intentionally applied per-target so they only affect code in this
repository, and never leak into external dependencies or generated code.
"""

# Our own code is compiled with clang's -Weverything and then opts out of the
# diagnostics that are not meaningful for this project. Everything that is left
# on is expected to stay clean. Third-party headers are treated as system
# headers (see --features=external_include_paths in .bazelrc) so these flags
# only police code in this repository.
PHASER_COPTS = [
    "-Weverything",

    # We target C++17; warnings that flag use of post-C++98/14 features (or
    # incompatibilities with older standards we don't support) are pure noise.
    "-Wno-c++98-compat",
    "-Wno-c++98-compat-pedantic",
    "-Wno-c++98-compat-local-type-template-args",
    "-Wno-c++98-compat-unnamed-type-template-args",
    "-Wno-pre-c++14-compat",
    "-Wno-pre-c++17-compat",

    # Cosmetic / not actionable: struct padding and vtable emission decisions.
    "-Wno-padded",
    "-Wno-weak-vtables",

    # Static registration objects (reflection banks, type registries) rely on
    # global constructors/destructors by design.
    "-Wno-global-constructors",
    "-Wno-exit-time-destructors",

    # Switches over enums cover the cases we care about and use a default for
    # the rest; neither a mandatory default nor exhaustive case lists add value.
    "-Wno-switch-default",
    "-Wno-switch-enum",

    # Other innocuous diagnostics.
    "-Wno-disabled-macro-expansion",
    "-Wno-float-equal",
    "-Wno-shadow-field-in-constructor",
    "-Wno-implicit-int-float-conversion",

    # protoc-generated *.pb.h headers (pulled into our test TUs) use
    # implementation-reserved identifiers like '_Foo_default_instance_'. That is
    # protobuf's codegen, not our code, so the diagnostic is not actionable.
    "-Wno-reserved-identifier",

    # clang 16/18 only (older clang silently ignores unknown -Wno-* flags).
    "-Wno-documentation-unknown-command",
    "-Wno-unsafe-buffer-usage",

    # The zero-copy runtime deliberately interconverts signed indices with
    # unsigned 32-bit buffer offsets/sizes, and uses a handful of C-style casts
    # when interfacing with the payload buffer. Auditing every such conversion
    # is a separate effort; keep these off so the meaningful warnings stand out.
    "-Wno-sign-conversion",
    "-Wno-shorten-64-to-32",
    "-Wno-old-style-cast",
]
