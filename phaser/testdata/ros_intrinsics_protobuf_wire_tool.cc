#include "phaser/testdata/RosIntrinsics.pb.h"

#include <fstream>
#include <iterator>
#include <string>

namespace {

using Message = ::foo::bar::RosIntrinsicMessage;

bool Write(const char* path) {
  Message message;
  message.mutable_stamp()->set_seconds(12);
  message.mutable_stamp()->set_nanos(345);
  message.mutable_timeout()->set_seconds(-4);
  message.mutable_timeout()->set_nanos(-500);
  message.mutable_header()->set_seq(9);
  message.mutable_header()->mutable_stamp()->set_seconds(21);
  message.mutable_header()->mutable_stamp()->set_nanos(654);
  message.mutable_header()->set_frame_id("map");

  message.set_count(42);
  message.set_name("wire");
  message.add_samples(3);
  message.add_samples(5);
  message.add_tags("front");
  message.add_tags("rear");

  auto* first_child = message.add_children();
  first_child->set_id(101);
  first_child->set_label("left");
  auto* second_child = message.add_children();
  second_child->set_id(202);
  second_child->set_label("right");

  message.add_fixed_names("fixed-a");
  message.add_fixed_names("fixed-b");
  auto* first_fixed_child = message.add_fixed_children();
  first_fixed_child->set_id(301);
  first_fixed_child->set_label("fixed-left");
  auto* second_fixed_child = message.add_fixed_children();
  second_fixed_child->set_id(302);
  second_fixed_child->set_label("fixed-right");

  auto* choice = message.mutable_choice_child();
  choice->set_id(404);
  choice->set_label("selected");

  std::string wire;
  if (!message.SerializeToString(&wire)) {
    return false;
  }
  std::ofstream output(path, std::ios::binary);
  output.write(wire.data(), static_cast<std::streamsize>(wire.size()));
  return output.good();
}

bool Verify(const char* path) {
  std::ifstream input(path, std::ios::binary);
  std::string wire((std::istreambuf_iterator<char>(input)),
                   std::istreambuf_iterator<char>());
  Message message;
  if (!input.good() && !input.eof()) {
    return false;
  }
  if (!message.ParseFromString(wire)) {
    return false;
  }

  if (message.samples_size() != 2 || message.tags_size() != 2 ||
      message.children_size() != 2 || message.fixed_names_size() != 2 ||
      message.fixed_children_size() != 2 || !message.has_choice_child()) {
    return false;
  }
  return message.stamp().seconds() == 12 &&
         message.stamp().nanos() == 345 &&
         message.timeout().seconds() == -4 &&
         message.timeout().nanos() == -500 &&
         message.header().seq() == 9 &&
         message.header().stamp().seconds() == 21 &&
         message.header().stamp().nanos() == 654 &&
         message.header().frame_id() == "map" && message.count() == 42 &&
         message.name() == "wire" && message.samples(0) == 3 &&
         message.samples(1) == 5 && message.tags(0) == "front" &&
         message.tags(1) == "rear" && message.children(0).id() == 101 &&
         message.children(0).label() == "left" &&
         message.children(1).id() == 202 &&
         message.children(1).label() == "right" &&
         message.fixed_names(0) == "fixed-a" &&
         message.fixed_names(1) == "fixed-b" &&
         message.fixed_children(0).id() == 301 &&
         message.fixed_children(0).label() == "fixed-left" &&
         message.fixed_children(1).id() == 302 &&
         message.fixed_children(1).label() == "fixed-right" &&
         message.choice_child().id() == 404 &&
         message.choice_child().label() == "selected";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    return 2;
  }
  const std::string command(argv[1]);
  if (command == "write") {
    return Write(argv[2]) ? 0 : 1;
  }
  if (command == "verify") {
    return Verify(argv[2]) ? 0 : 1;
  }
  return 2;
}
