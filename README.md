# Phaser

**Zero-copy Protocol Buffers for C++ — no serialization required.**

Phaser is a [Protocol Buffers](https://protobuf.dev) (`protobuf`) compiler plugin that
generates C++ message classes whose data lives directly in a memory buffer, in
wire-format, instead of in a tree of heap-allocated objects. Once a message is built,
it can be written to disk, placed in shared memory, or sent over an IPC system **without
a serialization step** — the bytes in the buffer *are* the message.

Choose a protobuf-style C++ API, a ROS-style public-field API, or a C API.
Python and Rust clients use the protobuf-style accessors. All of them share
the same native payload and the same protobuf and ROS wire conversion backends.

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
- **Three frontends** — protobuf-style C++ accessors, ROS-style public field proxies, or a C API. Python wraps the protobuf accessors. Rust wraps the C API.
- **Direct wire-to-wire conversion** between protobuf and ROS1 without constructing
  an intermediate user-facing message.
- **Allocation-free receive paths** when native payloads and wire conversions use
  caller-provided buffers.

## Features

1. proto3 (primary) and proto2 IDL support
2. Protobuf, ROS, and C frontends, plus Python and Rust clients
3. Native Phaser, protobuf, and ROS1 wire input/output
4. Direct protobuf-to-ROS and ROS-to-protobuf backend conversion
5. Fixed, caller-owned, and dynamically growing buffers
6. Allocation-free typed receive, traversal, and fixed-buffer transcoding
7. Full zero-copy `google.protobuf.Any` support
8. Repeated vectors, ROS fixed-array facades, and variant-like ROS `oneof` fields
9. ROS1 `Time`, `Duration`, and `Header` intrinsic mappings
10. Hybrid dense/sparse field metadata and protobuf version compatibility
11. Reflection, enum conversion, field presence, and user metadata
12. Generated conversion to and from Arrow, and from Phaser or protobuf wire format to Avro and Parquet
13. Bazel integration and modern C++17 with [Abseil](https://abseil.io)

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
    frontend = "protobuf",     # default; "ros" or "c" select the other frontends
    enable_active_message = False,  # optionally add an active_message
    deps = [":foo_proto"],
)
```

If `Foo.proto` is in package `foo.bar`, the generated classes live in
`::foo::bar::phaser` (when `add_namespace = "phaser"`), and you include them as you would
any protobuf header:

```c++
#include "foo/bar/Foo.phaser.h"
```

The C++ library can use protobuf-style accessors or a ROS-style struct:

```python
phaser_library(
    name = "foo_ros_phaser",
    add_namespace = "ros_api",
    frontend = "ros",
    deps = [":foo_proto"],
)
```

### Python, C, and Rust

`python = True` builds a pybind11 module named `<name>_py` over the protobuf accessors. `frontend = "c"` emits `Foo.phaser.c.h`. `rust = True` builds a crate named `<name>_rs` and selects the C frontend when `frontend` is omitted. The ROS frontend is C++ only. Field mapping, ownership, and examples are in the user guide's [Python, C, and Rust](phaser/docs/phaser_user_guide.md#python-c-and-rust) section.

```python
phaser_library(
    name = "foo_py",
    python = True,
    deps = [":foo_proto"],
)

phaser_library(
    name = "foo_c",
    rust = True,  # C API, plus a Rust crate foo_c_rs
    deps = [":foo_proto"],
)
```

```c
phaser_foo_bar_Foo* msg = phaser_foo_bar_Foo_new(8192);
phaser_foo_bar_Foo_set_name(msg, (const uint8_t*)"sensor", 6);
PhaserBytes name = phaser_foo_bar_Foo_name(msg);
phaser_free(name.data);          /* PhaserBytes.data */
phaser_foo_bar_Foo_free(msg);    /* free nested handles first */
```

```rust
let mut msg = foo_c::Foo::new(8192);
msg.set_name("sensor");
{
    let mut child = msg.mutable_child();
    child.set_label("cam");
}
```

The frontend changes source-level access, not the payload layout:

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
carry an application-owned `active_message`, a `phaser::ActiveMessage` that
holds a `std::any`. This transient member is not part of the native payload and
is not serialized.

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
`InferMessageWireFormat` reads only the first byte and, for a Phaser payload,
its header: a Phaser payload starts with a zero byte, which a non-empty protobuf
message cannot.

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
The same conversions are available from the Phaser bank when the caller has
only the message's protobuf full name.
Dynamic messages, reflection/debug output, unknown `Any` error handling, and
explicit owning conversions such as `RosHeaderView::ToOwned()` are outside this
guarantee.

#### When a fixed buffer runs out of space

A caller-provided buffer cannot grow, so every write checks for space and stays
inside the buffer. `TryCreateMutable` reports a buffer that is too small for the
message:

```c++
absl::StatusOr<foo::bar::phaser::TestMessage> msg =
    foo::bar::phaser::TestMessage::TryCreateMutable(buffer, size);
if (!msg.ok()) {
  // InvalidArgument for a buffer smaller than its header, ResourceExhausted
  // when the message itself does not fit.
}
```

`CreateMutable` aborts in the same cases. Once the message exists, a write that
needs more space than is left does nothing and marks the buffer:

- Setters keep the previous value.
- Repeated fields keep their previous elements.
- `mutable_` accessors, `add_` accessors and array elements return a detached
  handle whose writes go to per-thread scratch storage.
- `msg.AllocationFailed()` is true from then on, until `msg.Clear()` empties
  the buffer for a new message.
- `Serialize` and `SerializeToArray` fail, and `Deserialize` returns
  `ResourceExhausted`.
- `CreateReadonly` treats the marked buffer as empty, so a receiver never sees a
  partly written message.

Phaser and its generated code build with `-fno-exceptions -fno-rtti`. Without
exceptions, misuse that would otherwise throw, such as `at()` out of range or
mutating a readonly message, aborts.

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

## Trusting serialized sizes

A program that sizes its output with `SerializedSize()` or `ROSSerializedSize()` before
calling `SerializeToArray` or `SerializeToROSArray` can build with
`--@phaser//phaser/runtime:trust_serialized_size` (CMake:
`-DPHASER_TRUST_SERIALIZED_SIZE=ON`). Serializing a message then writes without checking
for room, which takes about half the instructions for protobuf and up to 15% fewer for
ROS. The array passed must be at least the serialized size; debug builds assert this.
`Serialize` and `SerializeToROS` still make room for the whole message in their buffer
first, and wire-to-wire conversions check as they write. The setting reaches everything
built against the runtime, so all of a program's messages serialize the same way.

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

## Arrow

Generated messages copy to and from an
[Arrow C Data Interface](https://arrow.apache.org/docs/format/CDataInterface.html)
batch. Phaser does not link libarrow. An unset proto3 field becomes an Arrow
null, and an explicit zero stays present. `google.protobuf.Any` is a struct of
`type_url` and a dense union of the concrete types present in that batch.

```c++
ArrowSchema schema{};
ArrowArray array{};
absl::Status status =
    foo::bar::phaser::TestMessage::ToArrow(batch, &schema, &array);
std::vector<foo::bar::phaser::TestMessage> out;
status = foo::bar::phaser::TestMessage::FromArrow(&schema, &array, &out);
if (schema.release != nullptr) schema.release(&schema);
if (array.release != nullptr) array.release(&array);
```

`AppendArrowSchema` is the generated function that adds one message's struct to
an Arrow schema. `ToArrow` calls it. See the user guide's
[Arrow](phaser/docs/phaser_user_guide.md#arrow) section for the column layout,
`Any`, and ownership of the schema and array.

## Avro and Parquet

The Arrow column layout can be written one way as an
[Avro](https://avro.apache.org/docs/1.11.1/specification/) object container or
an uncompressed [Parquet](https://parquet.apache.org/docs/file-format/) file.
Phaser does not link libavro or libparquet, and there is no reader. The source
is a Phaser message, protobuf wire bytes, or a native Phaser payload.

```c++
std::string avro;
absl::Status status =
    foo::bar::phaser::TestMessage::ToAvro(batch, &avro);
status = message.ToAvro(&avro);
status = foo::bar::phaser::TestMessage::ProtobufToAvro(protobuf_bytes, &avro);
status = foo::bar::phaser::TestMessage::PhaserToAvro(phaser_bytes, &avro);

std::string parquet;
status = foo::bar::phaser::TestMessage::ToParquet(batch, &parquet);
status = foo::bar::phaser::TestMessage::ProtobufToParquet(protobuf_bytes, &parquet);
status = foo::bar::phaser::TestMessage::PhaserToParquet(phaser_bytes, &parquet);
```

Each of `ProtobufToAvro`, `PhaserToAvro`, `ProtobufToParquet`, and
`PhaserToParquet` also accepts a span of messages and writes one multi-record
file. An empty protobuf message is a valid empty record. An empty native
Phaser payload is an error.

When the caller has the protobuf full name and not the C++ type, the bank
writes one payload:

```c++
status = ::phaser::PhaserBankProtobufToAvro(
    "foo.bar.TestMessage", protobuf_bytes, &avro);
status = ::phaser::PhaserBankPhaserToParquet(
    "foo.bar.TestMessage", phaser_bytes, &parquet);
status = ::phaser::PhaserBankConvert(
    "foo.bar.TestMessage", ::phaser::BankInputType::kProtobuf, protobuf_bytes,
    ::phaser::BankOutputType::kAvro, &avro);
```

`PhaserBankConvert` also accepts `BankOutputType::kParquet`, and
`BankInputType::kPhaser` for a native payload. See the user guide's
[Avro and Parquet](phaser/docs/phaser_user_guide.md#avro-and-parquet) section
for the type mapping and file layout.

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

The same lookup converts wire formats when the caller has the protobuf full
name and not the C++ type:

```c++
absl::Status status = ::phaser::PhaserBankProtobufToROS(
    "foo.bar.TestMessage", protobuf_bytes, ros_output);
status = ::phaser::PhaserBankRosToProtobuf(
    "foo.bar.TestMessage", ros_bytes, protobuf_output);
status = ::phaser::PhaserBankPhaserToROS(
    "foo.bar.TestMessage", phaser_bytes, ros_output);
status = ::phaser::PhaserBankPhaserToProtobuf(
    "foo.bar.TestMessage", phaser_bytes, protobuf_output);

std::string converted;
status = ::phaser::PhaserBankConvert(
    "foo.bar.TestMessage", ::phaser::BankInputType::kProtobuf, protobuf_bytes,
    ::phaser::BankOutputType::kRos, &converted);
```

`PhaserBankRosDataType`, `PhaserBankRosMd5`, and `PhaserBankRosDefinition`
return the ROS datatype, MD5, and definition when the message was compiled with
ROS metadata. `PhaserBankProtobufFullName` and `PhaserBankProtobufName` return
the protobuf names. The bank also dispatches the generated Arrow functions for
an `Any` payload. The user guide's
[Phaser Bank](phaser/docs/phaser_user_guide.md#the-phaser-bank) and
[Message information](phaser/docs/phaser_user_guide.md#message-information)
sections cover reflection, `MessageInfo`/`FieldInfo`, and the rest of the
bank.

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

### CMake

[`CMakeLists.txt`](CMakeLists.txt) builds the `phaser_runtime` library and the
`protoc-gen-phaser` plugin, and defines `phaser_add_library()`, which generates and
builds a library the way `phaser_library` does in Bazel. A parent project's Abseil,
cpp_toolbelt and protobuf targets are used when they exist; anything missing is fetched
with `FetchContent`.

```cmake
FetchContent_Declare(phaser GIT_REPOSITORY https://github.com/dallison/phaser.git GIT_TAG ...)
FetchContent_MakeAvailable(phaser)

phaser_add_library(foo_phaser
    PROTOS foo/foo.proto
    IMPORTS google/protobuf/any.proto
    IMPORT_DIRS ${CMAKE_SOURCE_DIR} ${protobuf_SOURCE_DIR}/src
    ADD_NAMESPACE phaser)
```

`PROTOS` and `IMPORTS` are named by import path. `IMPORTS` lists the imported files that
need phaser code too. The library's short header is included as `"foo/foo.phaser.h"`.
The plugin runs on the build host, so a cross-compile does not build it; build it for the
host and pass its path in `PHASER_PLUGIN_EXECUTABLE`.

### Accessor names

A field named `has_<name>` next to a field `<name>` would collide with `<name>`'s presence
accessor, so that presence accessor is named `_phaser_has_<name>()` instead.

## Project layout

| Path | Description |
| --- | --- |
| `phaser/compiler/` | The `protoc` plugin (`gen`, `enum_gen`, `message_gen`, and the C frontend emitter). |
| `phaser/runtime/` | The runtime library: `Message`, fields, vectors, unions, wire-format, the Phaser Bank, the C API, and `PayloadBuffer` glue. |
| `phaser/phaser_library.bzl` | The `phaser_library` Bazel rule and supporting aspect. |
| `phaser/testdata/` | Example `.proto` files used by the tests. |
| `phaser/docs/` | Reference documentation (the user guide). |

## Documentation

The complete reference — message layout, buffer internals, the allocator, reflection, the
Phaser Bank, Arrow conversion, and more — is in the **[Phaser User Guide](phaser/docs/phaser_user_guide.md)**.
Python, C, and Rust are in [Python, C, and Rust](phaser/docs/phaser_user_guide.md#python-c-and-rust).
Arrow usage is in the guide's [Arrow](phaser/docs/phaser_user_guide.md#arrow) section.
Protobuf and ROS conversion through the bank is in
[Protobuf and ROS conversion](phaser/docs/phaser_user_guide.md#protobuf-and-ros-conversion).

## License

Phaser is licensed under the [Apache License 2.0](LICENSE).
