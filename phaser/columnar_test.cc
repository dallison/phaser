// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "phaser/runtime/phaser_bank.h"
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
  ASSERT_TRUE(phaser::PhaserBankProtobufToAvro(foo::bar::phaser::TestMessage::FullName(),
                                              protobuf, &bank_protobuf)
                  .ok());
  ASSERT_TRUE(phaser::PhaserBankPhaserToAvro(foo::bar::phaser::TestMessage::FullName(),
                                            payload, &bank_phaser)
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
  ASSERT_TRUE(phaser::PhaserBankProtobufToParquet(
                  foo::bar::phaser::TestMessage::FullName(), protobuf,
                  &bank_parquet)
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
  ASSERT_TRUE(phaser::PhaserBankConvert(full_name, phaser::BankInputType::kPhaser,
                                       payload, phaser::BankOutputType::kAvro,
                                       &converted)
                  .ok());
  EXPECT_EQ(from_phaser, converted);
  ASSERT_TRUE(phaser::PhaserBankConvert(
                  full_name, phaser::BankInputType::kProtobuf,
                  absl::MakeConstSpan(protobuf.data(), protobuf.size()),
                  phaser::BankOutputType::kParquet, &converted)
                  .ok());
  EXPECT_EQ(parquet_protobuf, converted);
  EXPECT_FALSE(phaser::PhaserBankConvert(full_name, phaser::BankInputType::kRos,
                                        payload, phaser::BankOutputType::kAvro,
                                        &converted)
                   .ok());
  EXPECT_TRUE(converted.empty());
  EXPECT_FALSE(phaser::PhaserBankConvert(full_name, phaser::BankInputType::kPhaser,
                                        payload, phaser::BankOutputType::kAvro,
                                        nullptr)
                   .ok());
}
