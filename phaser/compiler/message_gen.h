#pragma once
#include "absl/status/status.h"
#include "google/protobuf/descriptor.h"
#include <iostream>
#include <memory>
#include <vector>

namespace phaser {

class MessageGenerator {
public:
  MessageGenerator(const google::protobuf::Descriptor *message)
      : message_(message) {
    for (int i = 0; i < message_->nested_type_count(); i++) {
      nested_message_gens_.push_back(
          std::make_unique<MessageGenerator>(message_->nested_type(i)));
    }
  }

  absl::Status GenerateHeader(std::ostream &os);
  absl::Status GenerateSource(std::ostream &os);

  absl::Status GenerateFieldDeclarations(std::ostream &os);
  absl::Status GenerateFieldDefinitions(std::ostream &os);
  
private:
  const google::protobuf::Descriptor *message_;
  std::vector<std::unique_ptr<MessageGenerator>> nested_message_gens_;
};

} // namespace phaser
