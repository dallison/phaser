// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/phaser_bank_protobuf.h"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "phaser/runtime/phaser_bank.h"

namespace phaser {
namespace {

class DescriptorBuildErrorCollector
    : public google::protobuf::DescriptorPool::ErrorCollector {
 public:
  void RecordError(absl::string_view filename, absl::string_view element_name,
                   const google::protobuf::Message* /*descriptor*/,
                   ErrorLocation /*location*/,
                   absl::string_view message) override {
    if (!errors.empty()) {
      errors.push_back('\n');
    }
    errors.append(
        absl::StrFormat("%s: %s: %s", filename, element_name, message));
  }

  std::string errors;
};

struct DescriptorState {
  std::mutex mu;
  std::vector<google::protobuf::FileDescriptorProto> files;
  std::unique_ptr<google::protobuf::DescriptorPool> pool;
  std::string build_error;
  bool built = false;
};

DescriptorState& State() {
  static DescriptorState state;
  return state;
}

bool DependenciesReady(const google::protobuf::DescriptorPool& pool,
                       const google::protobuf::FileDescriptorProto& proto) {
  std::vector<bool> weak(proto.dependency_size(), false);
  for (int index : proto.weak_dependency()) {
    if (index >= 0 && index < proto.dependency_size()) {
      weak[index] = true;
    }
  }
  for (int i = 0; i < proto.dependency_size(); ++i) {
    if (weak[i]) {
      continue;
    }
    if (pool.FindFileByName(proto.dependency(i)) == nullptr) {
      return false;
    }
  }
  return true;
}

void BuildPool(DescriptorState* state) {
  auto pool = std::make_unique<google::protobuf::DescriptorPool>();
  pool->internal_set_underlay(
      google::protobuf::DescriptorPool::generated_pool());

  std::vector<google::protobuf::FileDescriptorProto> pending =
      std::move(state->files);
  state->files.clear();
  DescriptorBuildErrorCollector errors;
  bool progress = true;
  while (progress && !pending.empty()) {
    progress = false;
    for (auto it = pending.begin(); it != pending.end();) {
      if (pool->FindFileByName(it->name()) != nullptr) {
        it = pending.erase(it);
        progress = true;
        continue;
      }
      if (!DependenciesReady(*pool, *it)) {
        ++it;
        continue;
      }
      if (pool->BuildFileCollectingErrors(*it, &errors) == nullptr) {
        if (!state->build_error.empty()) {
          state->build_error.push_back('\n');
        }
        state->build_error.append(absl::StrFormat(
            "failed to build protobuf descriptor for %s", it->name()));
        if (!errors.errors.empty()) {
          state->build_error.append(": ");
          state->build_error.append(errors.errors);
          errors.errors.clear();
        }
      }
      it = pending.erase(it);
      progress = true;
    }
  }
  if (!pending.empty()) {
    std::vector<std::string> missing;
    for (const auto& proto : pending) {
      for (int i = 0; i < proto.dependency_size(); ++i) {
        if (pool->FindFileByName(proto.dependency(i)) == nullptr) {
          missing.push_back(proto.name() + " imports " + proto.dependency(i));
        }
      }
    }
    if (!state->build_error.empty()) {
      state->build_error.push_back('\n');
    }
    state->build_error.append(
        "protobuf descriptor dependencies are missing: " +
        absl::StrJoin(missing, "; "));
  }
  state->pool = std::move(pool);
  state->built = true;
}

}  // namespace

void RegisterProtobufFileDescriptor(std::string_view serialized_file) {
  google::protobuf::FileDescriptorProto proto;
  if (!proto.ParseFromArray(serialized_file.data(),
                            static_cast<int>(serialized_file.size()))) {
    DescriptorState& state = State();
    std::lock_guard<std::mutex> lock(state.mu);
    if (!state.build_error.empty()) {
      state.build_error.push_back('\n');
    }
    state.build_error.append("embedded protobuf file descriptor is invalid");
    return;
  }
  if (proto.name().empty()) {
    return;
  }

  DescriptorState& state = State();
  std::lock_guard<std::mutex> lock(state.mu);
  if (state.built) {
    return;
  }
  for (const auto& existing : state.files) {
    if (existing.name() == proto.name()) {
      return;
    }
  }
  state.files.push_back(std::move(proto));
}

absl::StatusOr<const google::protobuf::Descriptor*>
PhaserBankProtobufDescriptor(std::string_view full_name) {
  absl::StatusOr<std::string_view> protobuf_full_name =
      PhaserBankProtobufFullName(full_name);
  if (!protobuf_full_name.ok()) {
    return protobuf_full_name.status();
  }

  DescriptorState& state = State();
  const google::protobuf::DescriptorPool* pool = nullptr;
  std::string build_error;
  {
    std::lock_guard<std::mutex> lock(state.mu);
    if (!state.built) {
      BuildPool(&state);
    }
    pool = state.pool.get();
    build_error = state.build_error;
  }
  const google::protobuf::Descriptor* descriptor =
      pool->FindMessageTypeByName(*protobuf_full_name);
  if (descriptor == nullptr) {
    if (!build_error.empty()) {
      return absl::NotFoundError(
          absl::StrFormat("Protobuf descriptor for '%s' is unavailable: %s",
                          *protobuf_full_name, build_error));
    }
    return absl::NotFoundError(absl::StrFormat(
        "Protobuf descriptor for '%s' was not embedded in the linked Phaser "
        "library",
        *protobuf_full_name));
  }
  return descriptor;
}

}  // namespace phaser
