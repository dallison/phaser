#include "phaser/testdata/RosIntrinsics.phaser.h"

#include <fstream>
#include <iterator>
#include <string>

namespace {

using Message = ::foo::bar::phaser::RosIntrinsicMessage;

bool Write(const char* path) {
  Message message;
  message.stamp = ::ros::Time(12, 345);
  message.timeout = ::ros::Duration(-4, -500);

  ::std_msgs::Header header;
  header.seq = 9;
  header.stamp = ::ros::Time(21, 654);
  header.frame_id = "map";
  message.header = header;

  message.count = 42;
  message.name = "wire";
  message.samples.push_back(3);
  message.samples.push_back(5);
  message.tags.push_back("front");
  message.tags.push_back("rear");

  auto* first_child = message.children.Add();
  first_child->id = 101;
  first_child->label = "left";
  auto* second_child = message.children.Add();
  second_child->id = 202;
  second_child->label = "right";

  message.fixed_names[0] = "fixed-a";
  message.fixed_names[1] = "fixed-b";
  message.fixed_children[0]->id = 301;
  message.fixed_children[0]->label = "fixed-left";
  message.fixed_children[1]->id = 302;
  message.fixed_children[1]->label = "fixed-right";

  using ChoiceChild = Message::ChoiceChildAlternative;
  auto& choice = message.choice.emplace<ChoiceChild>();
  choice.id = 404;
  choice.label = "selected";

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

  if (message.samples.size() != 2 || message.tags.size() != 2 ||
      message.children.size() != 2 ||
      !message.choice
           .holds_alternative<Message::ChoiceChildAlternative>()) {
    return false;
  }
  const auto& choice =
      message.choice.get<Message::ChoiceChildAlternative>();
  return message.stamp->sec == 12 && message.stamp->nsec == 345 &&
         message.timeout->sec == -4 && message.timeout->nsec == -500 &&
         message.header->seq == 9 && message.header->stamp.sec == 21 &&
         message.header->stamp.nsec == 654 &&
         message.header->frame_id == "map" && message.count.Get() == 42 &&
         message.name.Get() == "wire" && message.samples[0] == 3 &&
         message.samples[1] == 5 && message.tags[0].Get() == "front" &&
         message.tags[1].Get() == "rear" &&
         message.children[0]->id.Get() == 101 &&
         message.children[0]->label.Get() == "left" &&
         message.children[1]->id.Get() == 202 &&
         message.children[1]->label.Get() == "right" &&
         message.fixed_names[0].Get() == "fixed-a" &&
         message.fixed_names[1].Get() == "fixed-b" &&
         message.fixed_children[0]->id.Get() == 301 &&
         message.fixed_children[0]->label.Get() == "fixed-left" &&
         message.fixed_children[1]->id.Get() == 302 &&
         message.fixed_children[1]->label.Get() == "fixed-right" &&
         choice.id.Get() == 404 && choice.label.Get() == "selected";
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
