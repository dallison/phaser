# Phaser

**Zero-copy Protocol Buffers for C++ — no serialization required.**

Phaser is a [Protocol Buffers](https://protobuf.dev) (`protobuf`) compiler plugin that
generates C++ message classes whose data lives directly in a memory buffer, in
wire-format, instead of in a tree of heap-allocated objects. Once a message is built,
it can be written to disk, placed in shared memory, or sent over an IPC system **without
a serialization step** — the bytes in the buffer *are* the message.

Choose a familiar protobuf-style C++ API or a ROS-style public-field API. Both
frontends use the same native payload and expose the same protobuf and ROS wire
conversion backends.

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
- **Two C++ frontends** — protobuf-style accessors or ROS-style public field proxies.
- **Direct wire-to-wire conversion** between protobuf and ROS1 without constructing
  an intermediate user-facing message.
- **Allocation-free receive paths** when native payloads and wire conversions use
  caller-provided buffers.

## Features

1. proto3 (primary) and proto2 IDL support
2. Protobuf and ROS-style generated C++ frontends
3. Native Phaser, protobuf, and ROS1 wire input/output
4. Direct protobuf-to-ROS and ROS-to-protobuf backend conversion
5. Fixed, caller-owned, and dynamically growing buffers
6. Allocation-free typed receive, traversal, and fixed-buffer transcoding
7. Full zero-copy `google.protobuf.Any` support
8. Repeated vectors, ROS fixed-array facades, and variant-like ROS `oneof` fields
9. ROS1 `Time`, `Duration`, and `Header` intrinsic mappings
10. Hybrid dense/sparse field metadata and protobuf version compatibility
11. Reflection, enum conversion, field presence, and user metadata
12. Bazel integration and modern C++17 with [Abseil](https://abseil.io)

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
  compact per-message **field-metadata** index. Dense field-number ranges use direct lookup;
  outliers use binary search. That indirection is what enables protobuf's version
  compatibility: a reader built with a different schema version can still find the fields
  present in the data.

The hybrid metadata layout is a new native Phaser payload format. New runtimes can
read legacy payload metadata, but older runtimes cannot read newly generated native
payloads. Protobuf and ROS wire formats are unchanged.

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
    enable_active_message = False,  # optionally add std::any active_message
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
struct interface:

```python
phaser_library(
    name = "foo_ros_phaser",
    add_namespace = "ros_api",
    frontend = "ros",
    deps = [":foo_proto"],
)
```

The frontend changes only the C++ access syntax:

```c++
// frontend = "protobuf"
foo::bar::phaser::Foo protobuf_api;
protobuf_api.set_count(3);
protobuf_api.set_name("sensor");
protobuf_api.add_samples(1.5);

// frontend = "ros"
foo::bar::ros_api::Foo ros_api;
ros_api.count = 3;
ros_api.name = "sensor";
ros_api.samples.push_back(1.5);
```

Both frontends preserve the same native payload layout. Code generated from the
same schema can therefore attach to the same received bytes with
`CreateReadonly`, regardless of which frontend produced them. Both also expose
protobuf serialization, ROS1 serialization, and the direct wire conversion
APIs described below.

Set `enable_active_message = True` if each generated source object should also
carry an application-owned `std::any active_message`. This transient member is
not part of the native payload and is not serialized.

Fixed-size ROS fields are repeated protobuf fields annotated with
`[(phaser.array_size) = N]`; import `phaser/options.proto` in the schema. ROS
`oneof` fields expose a variant-like proxy with generated alternative tags.
See the user guide for the complete array and oneof APIs.

The ROS frontend also maps singular `google.protobuf.Timestamp`,
`google.protobuf.Duration`, and `std_msgs.Header` message fields to
`ros::Time`, `ros::Duration`, and `std_msgs::Header`. Mutable Header access
remains compatible with existing ROS1 reference-taking functions. Read-only
Header access returns `phaser::RosHeaderView`, whose `frame_id` is a
`std::string_view`; call `ToOwned()` when an owning `std_msgs::Header` is
required. Add the corresponding ROS C++ targets through the `cc_deps` attribute.

Every generated message, in either frontend style, can also produce and consume
ROS1 wire bytes:

```c++
::phaser::ROSBuffer ros_output;
absl::Status status = msg.SerializeToROS(ros_output);

// Convert serialized protobuf or a native Phaser payload without first
// constructing the user-facing message.
status = Foo::ProtobufToROS(protobuf_bytes, ros_output);
::phaser::ProtoBuffer protobuf_output(output_data, output_capacity);
status = Foo::ROSToProtobuf(ros_bytes, protobuf_output);
status = Foo::PhaserToROS(phaser_bytes, ros_output);
status = Foo::ConvertToROS(input_bytes, ros_output);  // infers input format

// Decode received ROS1 bytes directly into msg's native PayloadBuffer.
status = msg.ParseFromROS(ros_bytes);
```

`ROSBuffer` can own a growing allocation or wrap caller-provided output memory.
`ROSReader` provides bounds-checked input decoding. ROS wire data uses
little-endian ROS1 layout. Phaser's custom `oneof` layout writes a `uint32`
protobuf field-number discriminator before the selected arm, with zero meaning
unset, so it can be decoded without an external arm selection.
`InferMessageWireFormat` validates both the protobuf structure and Phaser
payload header; magic alone is not enough because the same four-byte prefix can
begin a valid protobuf tag. Malformed or genuinely ambiguous input is reported
explicitly.

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

Caller-buffer `CreateMutable`, typed mutation, `CreateReadonly`, typed field
traversal, typed `Any`, caller-buffer protobuf serialization, and fixed-buffer
ROS serialization do not use the system heap. Runtime bookkeeping and generated
type metadata live inside the `PayloadBuffer`; fixed-buffer creation therefore
needs enough room for both message data and this small control data. Repeated
strings iterate as `std::string_view`; repeated-message
indexing and iteration return lightweight message handles by value. These views
remain valid while the caller-owned receive buffer remains alive and unchanged.
Protobuf/ROS deserialization into a fixed mutable message and
`ProtobufToROS` with a sufficiently large fixed `ROSBuffer` are also
system-heap-allocation-free, including registered `Any` payloads.
`ProtobufToROS` scans protobuf wire fields directly and emits ROS bytes without
constructing an intermediate protobuf or Phaser message.
`ROSToProtobuf` performs the reverse direct conversion through `ROSReader` and
`ProtoWriter`; nested and packed protobuf lengths are computed with allocation-
free counting passes. ROS Header decoding writes `frame_id` directly from its
wire view into payload storage without an owning `std::string` intermediate.
Dynamic messages, reflection/debug output, unknown `Any` error handling, and
explicit owning conversions such as `RosHeaderView::ToOwned()` are outside this
guarantee.

### 3. Zero-copy field access

Beyond the standard protobuf accessors, Phaser adds helpers that hand you the final storage
location so you can skip intermediate copies:

```c++
// Strings/bytes: allocate space and write straight into it.
absl::Span<char> dst = msg.allocate_s(len);

// Repeated primitives: get a mutable span over the backing store.
msg.resize_vi32(n);
absl::Span<int32_t> data = msg.vi32_as_mutable_span();

// Repeated messages: allocate many at once (one payload allocation).
std::vector<InnerMessage> items = msg.allocate_vm(n);
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

## Wire-to-wire backend conversions

Wire conversion APIs are generated for every message in both frontend styles.
They scan the source wire format and write the destination format directly;
they do not construct an intermediate protobuf object or Phaser source message.

Convert protobuf bytes directly to ROS1:

```c++
std::string protobuf_wire = GetProtobufBytes();
::phaser::ROSBuffer ros_output;  // owns a growing output buffer
absl::Status status =
    foo::bar::phaser::Foo::ProtobufToROS(protobuf_wire, ros_output);
if (status.ok()) {
  SendROS(ros_output.data(), ros_output.size());
}
```

Convert ROS1 bytes directly to protobuf using caller-owned output memory:

```c++
absl::Span<const char> ros_wire = ReceiveROS();
std::array<char, 64 * 1024> storage;
::phaser::ProtoBuffer protobuf_output(storage.data(), storage.size());

absl::Status status =
    foo::bar::phaser::Foo::ROSToProtobuf(ros_wire, protobuf_output);
if (status.ok()) {
  SendProtobuf(storage.data(), protobuf_output.Size());
}
```

Convert a native Phaser payload to ROS1, or let Phaser distinguish native and
protobuf input:

```c++
const auto* data = static_cast<const char*>(msg.Data());
absl::Span<const char> native(data, msg.Size());

::phaser::ROSBuffer ros_output;
absl::Status status =
    foo::bar::phaser::Foo::PhaserToROS(native, ros_output);

// Accept either protobuf wire bytes or a native Phaser payload.
status = foo::bar::phaser::Foo::ConvertToROS(input, ros_output);
```

`ProtobufToROS` and `ROSToProtobuf` preserve schema order, recursively convert
nested messages, transform protobuf varints when required, and bulk-copy
wire-compatible packed fixed-width arrays. With fixed `ROSBuffer` and
`ProtoBuffer` instances, successful conversion performs no system-heap
allocation. `ParseFromROS` is the corresponding ROS1-to-native operation.

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
    --phaser_out=frontend=ros,add_namespace=NS,package_name=PACKAGE,target_name=TARGET:OUTPUT_DIR \
    -I IPATH \
    FILE...
```

Output is written to `OUTPUT_DIR/PACKAGE/TARGET`. See the user guide for the full argument
reference. Use `frontend=protobuf` for the default accessor API and optionally
add `active_message=true`.

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
