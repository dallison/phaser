// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "phaser/runtime/phaser_bank.h"
#include "phaser/plugins/avro/avro.h"
#include "phaser/plugins/parquet/parquet.h"
#include "phaser/testdata/TestMessage.phaser.h"

namespace {

foo::bar::phaser::TestMessage Sample() {
  foo::bar::phaser::TestMessage message;
  message.set_x(0);
  message.set_y(7);
  message.set_s("hello");
  message.set_buffer(std::string("a\0b", 3));
  message.add_vi32(1);
  message.add_vi32(2);
  message.mutable_m()->set_str("inner");
  auto entry = message.add_values();
  entry->set_key("k");
  entry->set_value(3);
  return message;
}

}  // namespace

TEST(ColumnarMessageTest, WireFormatsMatchTheMessage) {
  const foo::bar::phaser::TestMessage message = Sample();
  const std::string protobuf = message.SerializeAsString();
  const auto* native = reinterpret_cast<const char*>(message.Data());
  const absl::Span<const char> payload(native, message.Size());

  std::string from_message;
  std::string from_protobuf;
  std::string from_phaser;
  ASSERT_TRUE(message.ToAvro(&from_message).ok());
  ASSERT_TRUE(
      foo::bar::phaser::TestMessage::ProtobufToAvro(protobuf, &from_protobuf)
          .ok());
  ASSERT_TRUE(foo::bar::phaser::TestMessage::PhaserToAvro(payload, &from_phaser)
                  .ok());
  EXPECT_EQ(from_message, from_protobuf);
  EXPECT_EQ(from_message, from_phaser);
  EXPECT_NE(from_message.find("hello"), std::string::npos);
  EXPECT_NE(from_message.find("inner"), std::string::npos);

  std::string bank_protobuf;
  std::string bank_phaser;
  ASSERT_TRUE(phaser::PhaserBankConvert(
                  foo::bar::phaser::TestMessage::FullName(),
                  phaser::kProtobufFormat,
                  absl::MakeConstSpan(protobuf.data(), protobuf.size()),
                  phaser::avro::kFormat, &bank_protobuf)
                  .ok());
  ASSERT_TRUE(phaser::PhaserBankConvert(foo::bar::phaser::TestMessage::FullName(),
                                       phaser::kPhaserFormat, payload,
                                       phaser::avro::kFormat,
                                       &bank_phaser)
                  .ok());
  EXPECT_EQ(from_message, bank_protobuf);
  EXPECT_EQ(from_message, bank_phaser);

  std::string parquet_message;
  std::string parquet_protobuf;
  std::string parquet_phaser;
  ASSERT_TRUE(message.ToParquet(&parquet_message).ok());
  ASSERT_TRUE(foo::bar::phaser::TestMessage::ProtobufToParquet(protobuf,
                                                              &parquet_protobuf)
                  .ok());
  ASSERT_TRUE(
      foo::bar::phaser::TestMessage::PhaserToParquet(payload, &parquet_phaser)
          .ok());
  EXPECT_EQ(parquet_message, parquet_protobuf);
  EXPECT_EQ(parquet_message, parquet_phaser);
  EXPECT_EQ("PAR1", parquet_message.substr(0, 4));
  EXPECT_EQ("PAR1", parquet_message.substr(parquet_message.size() - 4));
  EXPECT_NE(parquet_message.find("hello"), std::string::npos);

  std::string bank_parquet;
  ASSERT_TRUE(phaser::PhaserBankConvert(
                  foo::bar::phaser::TestMessage::FullName(),
                  phaser::kProtobufFormat,
                  absl::MakeConstSpan(protobuf.data(), protobuf.size()),
                  phaser::parquet::kFormat, &bank_parquet)
                  .ok());
  EXPECT_EQ(parquet_message, bank_parquet);

  foo::bar::phaser::TestMessage other;
  other.set_y(8);
  const std::string other_wire = other.SerializeAsString();
  const std::vector<std::string_view> wires = {protobuf, other_wire};
  std::vector<foo::bar::phaser::TestMessage> batch = {message, other};
  std::string batch_message;
  std::string batch_wire;
  ASSERT_TRUE(
      foo::bar::phaser::TestMessage::ToAvro(batch, &batch_message).ok());
  ASSERT_TRUE(
      foo::bar::phaser::TestMessage::ProtobufToAvro(wires, &batch_wire).ok());
  EXPECT_EQ(batch_message, batch_wire);

  foo::bar::phaser::TestMessage empty;
  std::string empty_message;
  std::string empty_wire;
  ASSERT_TRUE(empty.ToAvro(&empty_message).ok());
  ASSERT_TRUE(foo::bar::phaser::TestMessage::ProtobufToAvro("", &empty_wire).ok());
  EXPECT_EQ(empty_message, empty_wire);
  EXPECT_FALSE(foo::bar::phaser::TestMessage::PhaserToAvro(
                   absl::Span<const char>(), &empty_message)
                   .ok());
  EXPECT_FALSE(
      foo::bar::phaser::TestMessage::ProtobufToAvro("\x01", &empty_wire).ok());

  const std::string_view full_name = foo::bar::phaser::TestMessage::FullName();
  std::string converted;
  ASSERT_TRUE(phaser::PhaserBankConvert(full_name, phaser::kPhaserFormat,
                                       payload, phaser::avro::kFormat,
                                       &converted)
                  .ok());
  EXPECT_EQ(from_phaser, converted);
  ASSERT_TRUE(phaser::PhaserBankConvert(
                  full_name, phaser::kProtobufFormat,
                  absl::MakeConstSpan(protobuf.data(), protobuf.size()),
                  phaser::parquet::kFormat, &converted)
                  .ok());
  EXPECT_EQ(parquet_protobuf, converted);
  EXPECT_FALSE(phaser::PhaserBankConvert(full_name, phaser::kRosFormat,
                                        payload, phaser::avro::kFormat,
                                        &converted)
                   .ok());
  EXPECT_TRUE(converted.empty());
  EXPECT_FALSE(phaser::PhaserBankConvert(full_name, phaser::kPhaserFormat,
                                        payload, phaser::avro::kFormat,
                                        nullptr)
                   .ok());
}

absl::Status WriteDirection(std::string_view full_name, absl::Span<const char> bytes,
                            std::string* out, const void* options,
                            std::string_view label) {
  (void)full_name;
  (void)bytes;
  (void)options;
  *out = std::string(label);
  return absl::OkStatus();
}

absl::Status WriteForward(std::string_view full_name, absl::Span<const char> bytes,
                          std::string* out, const void* options) {
  return WriteDirection(full_name, bytes, out, options, "forward");
}

absl::Status WriteReverse(std::string_view full_name, absl::Span<const char> bytes,
                          std::string* out, const void* options) {
  return WriteDirection(full_name, bytes, out, options, "reverse");
}

TEST(ColumnarMessageTest, ConversionDirectionSelectsTheInstalledEdge) {
  const char* const forward_formats[] = {"plugin-src", "plugin-dst"};
  const phaser::BankConversion forward[] = {
      {"plugin-src", "plugin-dst", phaser::BankConversionDirection::kForward,
       WriteForward, nullptr},
  };
  ASSERT_TRUE(phaser::PhaserBankRegisterConversionPlugin(
                  {"direction-forward", forward_formats, forward})
                  .ok());
  std::string out;
  ASSERT_TRUE(phaser::PhaserBankConvert("unused", "plugin-src", {}, "plugin-dst",
                                       &out)
                  .ok());
  EXPECT_EQ(out, "forward");
  EXPECT_FALSE(
      phaser::PhaserBankConvert("unused", "plugin-dst", {}, "plugin-src", &out)
          .ok());

  const char* const reverse_formats[] = {"plugin-reverse-src",
                                        "plugin-reverse-dst"};
  const phaser::BankConversion reverse[] = {
      {"plugin-reverse-src", "plugin-reverse-dst",
       phaser::BankConversionDirection::kReverse, nullptr, WriteReverse},
  };
  ASSERT_TRUE(phaser::PhaserBankRegisterConversionPlugin(
                  {"direction-reverse", reverse_formats, reverse})
                  .ok());
  ASSERT_TRUE(phaser::PhaserBankConvert("unused", "plugin-reverse-dst", {},
                                       "plugin-reverse-src", &out)
                  .ok());
  EXPECT_EQ(out, "reverse");
  EXPECT_FALSE(phaser::PhaserBankConvert("unused", "plugin-reverse-src", {},
                                        "plugin-reverse-dst", &out)
                   .ok());

  const char* const both_formats[] = {"plugin-both-src", "plugin-both-dst"};
  const phaser::BankConversion both[] = {
      {"plugin-both-src", "plugin-both-dst", phaser::BankConversionDirection::kBoth,
       WriteForward, WriteReverse},
  };
  ASSERT_TRUE(phaser::PhaserBankRegisterConversionPlugin(
                  {"direction-both", both_formats, both})
                  .ok());
  ASSERT_TRUE(phaser::PhaserBankConvert("unused", "plugin-both-src", {},
                                       "plugin-both-dst", &out)
                  .ok());
  EXPECT_EQ(out, "forward");
  ASSERT_TRUE(phaser::PhaserBankConvert("unused", "plugin-both-dst", {},
                                       "plugin-both-src", &out)
                  .ok());
  EXPECT_EQ(out, "reverse");

  const char* const missing_formats[] = {"plugin-missing-src",
                                         "plugin-missing-dst"};
  const phaser::BankConversion missing[] = {
      {"plugin-missing-src", "plugin-missing-dst",
       phaser::BankConversionDirection::kBoth, WriteForward, nullptr},
  };
  EXPECT_FALSE(phaser::PhaserBankRegisterConversionPlugin(
                   {"direction-missing", missing_formats, missing})
                   .ok());

  const char* const undeclared_formats[] = {"plugin-src"};
  const phaser::BankConversion undeclared[] = {
      {"plugin-src", "plugin-undeclared", phaser::BankConversionDirection::kForward,
       WriteForward, nullptr},
  };
  EXPECT_FALSE(phaser::PhaserBankRegisterConversionPlugin(
                   {"direction-undeclared", undeclared_formats, undeclared})
                   .ok());
}
