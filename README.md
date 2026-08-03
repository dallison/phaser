# Phaser

**Zero-copy Protocol Buffers for C++ — no serialization required.**

Phaser is a [Protocol Buffers](https://protobuf.dev) (`protobuf`) compiler plugin that
generates C++ message classes whose data lives directly in a memory buffer, in
wire-format, instead of in a tree of heap-allocated objects. Once a message is built,
it can be written to disk, placed in shared memory, or sent over an IPC system **without
a serialization step** — the bytes in the buffer *are* the message.

The generated API is intentionally almost identical to the standard protobuf C++ API, so
if you know protobuf, you already know Phaser.

> 📖 For the full reference, see the **[Phaser User Guide](phaser/docs/phaser_user_guide.md)**.

---

## Why Phaser?

Every program that uses classic protobuf follows the same pattern: build messages as heap
(or arena) objects, **serialize** them into a buffer, send the buffer, then **deserialize**
on the other side. For small messages the conversion cost is negligible — but it isn't
always small:

- **Serialization can dominate the CPU.** In data-heavy domains like robotics and
  autonomous vehicles, messages carry large payloads (LIDAR scans, camera frames), and
  serialization/deserialization can consume well over half of available CPU. Google has
  estimated serialization at ~30% of CPU time across its data centers.
- **Deserializing messages you never read is pure waste.** Many real-time systems only act
  on the most recent message and drop the rest — every message deserialized and discarded
  is wasted work.
- **Shared-memory IPC makes serialization redundant.** When subscribers can read the same
  physical memory directly, serializing into shared memory just to deserialize it again in
  every reader burns cycles for no benefit.

Phaser removes that overhead. By writing values directly into their final location in the
buffer, message construction can be **up to an order of magnitude faster**, and reading
costs nothing until you actually touch a field.

### Benefits at a glance

- **No serialization / deserialization** on the hot path — the buffer is ready to send.
- **Direct buffer access** for bulk data: write straight into a vector's backing store,
  or allocate many sub-messages in a single allocation.
- **Works in shared memory** and other externally-provided buffers (fixed or resizable).
- **Protobuf version compatibility** is preserved — old and new message versions
  interoperate via per-message field metadata.
- **Protobuf wire-format transcoding** is available when you *do* need it (e.g. storing in
  systems like BigQuery that expect protobuf bytes).
- **Familiar API** — same accessor names as protobuf, with extra zero-copy helpers.

## Features

1. proto3 (primary) and proto2 IDL support
2. Message printing to `std::ostream`
3. Fixed- and variable-sized buffers
4. User-supplied per-buffer metadata
5. Full `google.protobuf.Any` support (zero-copy)
6. Enum printing and parsing
7. Message reflection
8. Field presence masks
9. Bazel build integration
10. Modern C++17 with [Abseil](https://abseil.io)

## How it works

Phaser runs as a plugin to `protoc`. `protoc` parses your `.proto` files and hands the
descriptors to Phaser, which emits C++ (`*.phaser.h` / `*.phaser.cc`).

The key idea is the split between two representations:

- The **source message** is the C++ object your code interacts with. It is lightweight —
  it holds only a pointer to the runtime and an offset, *not* the field values. It can live
  on the stack, the heap, anywhere.
- The **binary message** is the actual data, stored in wire-format inside a relocatable
  `PayloadBuffer`.

```
   Your code            Source message            PayloadBuffer (the bytes you send)
  ┌──────────┐         ┌───────────────┐         ┌─────────────────────────────────┐
  │ set_x(7) │ ──────► │  offset + rt  │ ──────► │ [header][metadata][fields...]    │
  │  x()     │ ◄────── │  (no data!)   │ ◄────── │  x = 7  ...                      │
  └──────────┘         └───────────────┘         └─────────────────────────────────┘
```

- When you call `set_x(...)`, the value is written **directly into the binary buffer**.
- When you call `x()`, the value is read back **from the binary buffer**, located via a
  small per-message **field-metadata** array. That indirection is what enables protobuf's
  version compatibility: a reader built with a different schema version can still find the
  fields present in the data.

The `PayloadBuffer` (from the [cpp_toolbelt](https://github.com/dallison/cpp_toolbelt)
library) is a relocatable heap — a malloc/free/realloc allocator that uses only offsets
(never raw pointers), so the whole buffer can be copied or moved anywhere. It offers a fast
**bitmap allocator** for small blocks (performance mode, the default) and a **free-list
allocator** that trades speed for compactness (size mode), selectable via
`::phaser::Tuning`.

## Quick start

### 1. Add a `phaser_library` to your build

Phaser integrates with Bazel through the `phaser_library` rule. Point it at a standard
`proto_library`, much like you would a `cc_proto_library`:

```python
load("@phaser//phaser:phaser_library.bzl", "phaser_library")

proto_library(
    name = "foo_proto",
    srcs = ["Foo.proto"],
)

phaser_library(
    name = "foo_phaser",
    add_namespace = "phaser",  # optional: avoids clashing with protobuf classes
    frontend = "protobuf",     # default; use "ros" for public field proxies
    deps = [":foo_proto"],
)
```

If `Foo.proto` is in package `foo.bar`, the generated classes live in
`::foo::bar::phaser` (when `add_namespace = "phaser"`), and you include them as you would
any protobuf header:

```c++
#include "foo/bar/Foo.phaser.h"
```

Phaser can generate either the default protobuf-style accessors or a ROS-style
struct interface. The ROS frontend preserves the same native payload layout and
protobuf wire transcoding:

```python
phaser_library(
    name = "foo_ros_phaser",
    frontend = "ros",
    deps = [":foo_proto"],
)
```

```c++
Foo msg;
msg.count = 3;
msg.name = "sensor";
msg.samples.push_back(1.5);
```

Fixed-size ROS fields are repeated protobuf fields annotated with
`[(phaser.array_size) = N]`; import `phaser/options.proto` in the schema. ROS
`oneof` fields expose a variant-like proxy with generated alternative tags.
See the user guide for the complete array and oneof APIs.

The ROS frontend also maps singular `google.protobuf.Timestamp`,
`google.protobuf.Duration`, and `std_msgs.Header` message fields to
`ros::Time`, `ros::Duration`, and `std_msgs::Header`. This allows existing ROS1
functions taking values, const references, or mutable references to accept the
generated fields unchanged. Add the corresponding ROS C++ targets through the
`cc_deps` attribute.

### 2. Create and use a message

Creating a message looks just like protobuf — the binary data is backed by a dynamic
buffer allocated from the heap that grows as needed:

```c++
foo::bar::phaser::TestMessage msg;     // optional: TestMessage msg(initial_size, tuning);
msg.set_x(1234);

// The buffer is ready to send — no serialize step.
SendMessage(msg.Data(), msg.ByteSizeLong());
```

Build directly inside an externally-provided buffer (e.g. shared memory from an IPC system):

```c++
auto msg = foo::bar::phaser::TestMessage::CreateMutable(buffer, size);
msg.set_x(1234);
```

Read a message received in a read-only buffer (all field access is bounds-checked against
the buffer you provide):

```c++
auto msg = foo::bar::phaser::TestMessage::CreateReadonly(buffer, size);
int x = msg.x();
```

### 3. Zero-copy field access

Beyond the standard protobuf accessors, Phaser adds helpers that hand you the final storage
location so you can skip intermediate copies:

```c++
// Strings/bytes: allocate space and write straight into it.
absl::Span<char> dst = msg.allocate_s(len);

// Repeated primitives: get a mutable span over the backing store.
msg.resize_vi32(n);
absl::Span<int32_t> data = msg.vi32_as_mutable_span();

// Repeated messages: allocate many at once (one allocation).
std::vector<InnerMessage*> items = msg.allocate_vm(n);
```

## Protobuf interoperability

Phaser's native layout is *not* protobuf wire-format, but full transcoding is provided for
when you need to interoperate with protobuf-based systems:

```c++
size_t SerializedSize() const;
bool   SerializeToArray(char* array, size_t size) const;
bool   ParseFromArray(const char* array, size_t size);
bool   SerializeToString(std::string* str) const;
std::string SerializeAsString() const;
bool   ParseFromString(const std::string& str);
```

`google.protobuf.Any` is supported with zero-copy semantics: the `value` field holds a real
binary message you can access directly (via `Is<T>()` / `As<T>()` / `MutableAny<T>()`), with
`PackFrom` / `UnpackTo` provided for protobuf-compatible copying.

## The Phaser Bank (type-erased operations & reflection)

The **Phaser Bank** lets you operate on messages given only their type *name* — stream,
clear, copy, transcode, allocate, and reflect over fields — without compile-time knowledge
of the type. Message libraries register themselves via static initializers, so they just
need to be linked in:

```c++
absl::StatusOr<bool> present =
    ::phaser::PhaserBankHasField("foo.bar.TestMessage", msg, 100);

auto field = ::phaser::PhaserBankGetFieldByNumber<::phaser::Int32Field<>>(
    "foo.bar.TestMessage", msg, 100);
int value = (*field)->Get();
```

See the user guide's *Phaser Bank* and *Message information* sections for the full surface
(reflection, `MessageInfo`/`FieldInfo`, protobuf transcoding helpers, etc.).

## Building from source

Phaser uses [Bazel](https://bazel.build) (with Bzlmod) and is developed against the version
pinned in [`.bazelversion`](.bazelversion). Dependencies (Abseil, protobuf, cpp_toolbelt,
GoogleTest, …) are declared in [`MODULE.bazel`](MODULE.bazel).

```bash
# Build everything
bazelisk build //phaser/...

# Run the tests
bazelisk test //phaser/...

# AddressSanitizer build/test (see .bazelrc for the asan config)
bazelisk test --config=asan //phaser/...
```

On Apple Silicon, the `asan` config already pulls in the native `apple_silicon` settings;
see [`.bazelrc`](.bazelrc) for the available configurations.

### Using Phaser without Bazel

Phaser is a `protoc` plugin, so any build system can drive it as long as the plugin binary
and dependencies are available:

```bash
protoc --plugin=protoc-gen-phaser=DIR/bin/phaser/compiler/phaser \
    --phaser_out=add_namespace=NS,package_name=PACKAGE,target_name=TARGET:OUTPUT_DIR \
    -I IPATH \
    FILE...
```

Output is written to `OUTPUT_DIR/PACKAGE/TARGET`. See the user guide for the full argument
reference.

## Project layout

| Path | Description |
| --- | --- |
| `phaser/compiler/` | The `protoc` plugin that generates C++ code (`gen`, `enum_gen`, `message_gen`, `main`). |
| `phaser/runtime/` | The runtime library: `Message`, fields, vectors, unions, wire-format, the Phaser Bank, and `PayloadBuffer` glue. |
| `phaser/phaser_library.bzl` | The `phaser_library` Bazel rule and supporting aspect. |
| `phaser/testdata/` | Example `.proto` files used by the tests. |
| `phaser/docs/` | Reference documentation (the user guide). |

## Documentation

The complete reference — message layout, buffer internals, the allocator, reflection, the
Phaser Bank, and more — is in the **[Phaser User Guide](phaser/docs/phaser_user_guide.md)**.

## License

Phaser is licensed under the [Apache License 2.0](LICENSE).
