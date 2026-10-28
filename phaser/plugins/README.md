# Phaser format plugins

Each format is one directory with two subtrees:

- **`gen/`** — `codegen.cc` registers a `GeneratorPlugin` (link `:codegen` into the
  `phaser` compiler with `alwayslink = True`). Depends on
  `//phaser/compiler:message_generator`.
- **`runtime/`** — columnar or API code (`*.cc` / `*.h`), optional bank
  `:conversion` (static `PhaserBankRegisterConversionPlugin`), and optional
  `:pybind` (registers a `phaser_bank_py` submodule via
  `RegisterPluginPybind`; link into `//phaser/runtime:phaser_bank_py` with
  `alwayslink = True`).

Shared bank conversion helpers: `common/conversion_plugin.h`.

Public headers are exposed as `phaser/plugins/<name>/<header>.h` via Bazel
`include_prefix` / `strip_include_prefix` on the `:runtime` target.

The parent `BUILD.bazel` defines aliases so existing deps like
`//phaser/plugins:parquet` keep working.
