"""Shared compiler warning flags for Phaser's own (hand-written) targets.

These are intentionally applied per-target so they only affect code in this
repository, and never leak into external dependencies or generated code.
"""

# Our own code is compiled with clang's -Weverything and then opts out of the
# diagnostics that are not meaningful for this project. Everything that is left
# on is expected to stay clean. Third-party headers are treated as system
# headers (see --features=external_include_paths in .bazelrc) so these flags
# only police code in this repository.
#
# -Weverything is a clang-only flag; GCC rejects it outright. The warning set is
# therefore only applied when building with clang (see //phaser:is_clang), and
# GCC builds (e.g. the default Linux toolchain) compile without it.
_PHASER_CLANG_COPTS = [
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
]

# GCC has no single -Weverything switch, so we approximate the clang coverage by
# turning on -Wall/-Wextra/-Wpedantic plus the broad set of additional
# diagnostics GCC exposes individually. The opt-outs below mirror the clang list
# above so both compilers police the same things and ignore the same noise.
# These are warnings only (there is no -Werror), matching the clang build.
_PHASER_GCC_COPTS = [
    "-Wall",
    "-Wextra",
    "-Wcast-align",
    "-Wcast-qual",
    "-Wconversion",
    "-Wctor-dtor-privacy",
    "-Wdisabled-optimization",
    "-Wdouble-promotion",
    "-Wduplicated-branches",
    "-Wduplicated-cond",
    "-Wextra-semi",
    "-Wformat=2",
    "-Winit-self",
    "-Wlogical-op",
    "-Wmissing-declarations",
    "-Wmissing-include-dirs",
    "-Wnoexcept",
    "-Wnon-virtual-dtor",
    "-Wnull-dereference",
    "-Wold-style-cast",
    "-Woverloaded-virtual",
    "-Wredundant-decls",
    "-Wshadow",
    "-Wsign-conversion",
    "-Wsign-promo",
    "-Wstrict-null-sentinel",
    "-Wundef",
    "-Wunused",
    "-Wuseless-cast",
    "-Wzero-as-null-pointer-constant",

    # The zero-copy message layout deliberately takes offsetof() of the
    # generated non-standard-layout message structs, and the runtime/generated
    # code uses C++20 designated initializers and a flexible array member by
    # design. Those are intentional, not bugs, so silence the corresponding
    # diagnostics (g++ enables -Winvalid-offsetof by default; -Wpedantic is
    # intentionally not enabled). We also keep clang-only #pragma diagnostics in
    # shared headers, which GCC reports as unknown pragmas.
    "-Wno-invalid-offsetof",
    "-Wno-unknown-pragmas",
]

PHASER_COPTS = select({
    "//phaser:is_clang": _PHASER_CLANG_COPTS,
    "//conditions:default": _PHASER_GCC_COPTS,
})
