// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

// Fuzz target for the Phaser CreateReadonly receive path.
//
// Any byte string fed through PhaserReceiveFuzzOneInput, followed by typical
// receiver reads, must not crash or cause out-of-bounds access (ASan).
//
// Modes (first input byte):
//   even: remaining bytes are a raw received buffer.
//   odd:  start from a valid TestMessage and overlay/mutate with remaining bytes.

#include "phaser/receive_fuzz.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "phaser/testdata/TestMessage.phaser.h"

namespace {

std::vector<char> MakeValidSeed() {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");
  msg.mutable_m()->set_str("inner");
  msg.mutable_m()->set_f(0x1111);
  msg.mutable_m()->add_ev(foo::bar::phaser::BAR);
  msg.mutable_m()->add_ev(foo::bar::phaser::FOO);
  msg.add_vi32(0x11111111);
  msg.add_vi32(0x22222222);
  msg.add_vi32(0x33333333);
  msg.add_vstr("one");
  msg.add_vstr("two");
  msg.add_vstr("three");
  msg.add_vm().set_str("vm0");
  msg.set_u1b(4321);
  msg.set_u2b("u2b");
  msg.mutable_u3b()->set_str("u3b");
  msg.set_buffer("buffer\ndata");
  msg.set_e(foo::bar::phaser::FOO);
  msg.set_fl(1.5f);
  msg.set_db(2.5);
  {
    auto entry = msg.add_values();
    entry.set_key("map key");
    entry.set_value(99);
  }
  const char* data = static_cast<const char*>(msg.Data());
  return std::vector<char>(data, data + msg.Size());
}

const std::vector<char>& ValidSeed() {
  static const std::vector<char> seed = MakeValidSeed();
  return seed;
}

void TouchInner(const foo::bar::phaser::InnerMessage& m) {
  (void)m.has_str();
  (void)m.str();
  (void)m.has_f();
  (void)m.f();
  (void)m.has_e();
  (void)m.e();
  const int n = m.ev_size();
  for (int i = 0; i < n; ++i) {
    (void)m.ev(i);
  }
  // Aggregate getter and range iteration exercise the whole-vector code path,
  // which is distinct from the per-index accessor above.
  for (auto v : m.ev()) {
    (void)v;
  }
  {
    auto all = m.ev().Get();
    (void)all.size();
  }
  {
    absl::Span<const foo::bar::phaser::EnumTest> span = m.ev().AsSpan();
    for (auto v : span) {
      (void)v;
    }
  }
  (void)m.has_uva();
  (void)m.uva();
  (void)m.has_uvb();
  (void)m.uvb();
}

void TouchMessage(const foo::bar::phaser::TestMessage& msg) {
  (void)msg.has_x();
  (void)msg.x();
  (void)msg.has_y();
  (void)msg.y();
  (void)msg.has_s();
  {
    std::string_view s = msg.s();
    (void)s.size();
    (void)s.data();
  }
  (void)msg.has_m();
  TouchInner(msg.m());

  const int vi = msg.vi32_size();
  for (int i = 0; i < vi; ++i) {
    (void)msg.vi32(i);
  }
  for (int32_t v : msg.vi32()) {
    (void)v;
  }
  (void)msg.vi32().capacity();
  {
    auto all = msg.vi32().Get();
    (void)all.size();
  }

  const int vs = msg.vstr_size();
  for (int i = 0; i < vs; ++i) {
    std::string_view s = msg.vstr(i);
    (void)s.size();
  }
  for (std::string_view s : msg.vstr()) {
    (void)s.size();
  }

  const int vm = msg.vm_size();
  for (int i = 0; i < vm; ++i) {
    TouchInner(msg.vm(i));
  }
  {
    auto all = msg.vm().Get();
    for (const auto& inner : all) {
      TouchInner(inner);
    }
  }

  (void)msg.has_u1a();
  (void)msg.u1a();
  (void)msg.has_u1b();
  (void)msg.u1b();
  (void)msg.has_u2a();
  (void)msg.u2a();
  (void)msg.has_u2b();
  {
    std::string_view s = msg.u2b();
    (void)s.size();
  }
  (void)msg.has_u3a();
  (void)msg.u3a();
  (void)msg.has_u3b();
  TouchInner(msg.u3b());

  (void)msg.has_buffer();
  {
    std::string_view b = msg.buffer();
    (void)b.size();
  }
  (void)msg.has_e();
  (void)msg.e();
  (void)msg.has_fl();
  (void)msg.fl();
  (void)msg.has_db();
  (void)msg.db();

  // map<string,int32> is compiled to a repeated ValuesEntry message.
  const int nvals = msg.values_size();
  for (int i = 0; i < nvals; ++i) {
    auto entry = msg.values(i);
    (void)entry.has_key();
    std::string_view k = entry.key();
    (void)k.size();
    (void)entry.has_value();
    (void)entry.value();
  }
  for (auto entry : msg.values()) {
    (void)entry.key().size();
    (void)entry.value();
  }

  (void)msg.has_any();
  (void)msg.any().has_type_url();
  (void)msg.any().type_url();
  (void)msg.any().has_value();
  (void)msg.any().value().size();

  {
    std::ostringstream os;
    os << msg;
  }
  (void)msg.DebugString();
  std::string serialized;
  (void)msg.SerializeToString(&serialized);
}

void FuzzBuffer(const char* data, size_t size) {
  auto msg = foo::bar::phaser::TestMessage::CreateReadonly(data, size);
  TouchMessage(msg);
}

}  // namespace

int PhaserReceiveFuzzOneInput(const uint8_t* data, size_t size) {
  if (size == 0) {
    FuzzBuffer(nullptr, 0);
    return 0;
  }

  const uint8_t mode = data[0] & 1u;
  const uint8_t* payload = data + 1;
  const size_t payload_size = size - 1;

  try {
    if (mode == 0) {
      FuzzBuffer(reinterpret_cast<const char*>(payload), payload_size);
    } else {
      std::vector<char> buf = ValidSeed();
      if (payload_size == 0) {
        FuzzBuffer(buf.data(), buf.size());
        return 0;
      }
      for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = static_cast<char>(static_cast<unsigned char>(buf[i]) ^
                                   payload[i % payload_size]);
      }
      if (payload_size >= 8) {
        uint32_t off = 0;
        uint32_t val = 0;
        std::memcpy(&off, payload, 4);
        std::memcpy(&val, payload + 4, 4);
        if (!buf.empty()) {
          const size_t idx = off % buf.size();
          std::memcpy(buf.data() + idx, &val,
                      std::min(sizeof(val), buf.size() - idx));
        }
      }
      FuzzBuffer(buf.data(), buf.size());
    }
  } catch (const std::exception&) {
  } catch (...) {
  }
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  return PhaserReceiveFuzzOneInput(data, size);
}
