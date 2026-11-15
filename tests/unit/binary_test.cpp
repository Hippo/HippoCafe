#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "io/binary_reader.hpp"
#include "io/binary_writer.hpp"

namespace cafe::detail {
namespace {

TEST(BinaryIo, BigEndianAndSignedBoundaries) {
  std::vector<std::byte> bytes;
  BinaryWriter writer(bytes);
  writer.u1(0xff);
  writer.u2(0x1234);
  writer.u4(0xa1b2c3d4);
  writer.u8(0x0123456789abcdef);
  writer.s1(std::numeric_limits<std::int8_t>::min());
  writer.s2(std::numeric_limits<std::int16_t>::min());
  writer.s4(std::numeric_limits<std::int32_t>::min());
  writer.s8(std::numeric_limits<std::int64_t>::min());
  EXPECT_FALSE(writer.patch_u4(bytes.size() - 3, 0));
  EXPECT_FALSE(writer.patch_u4(std::numeric_limits<std::size_t>::max(), 0));
  EXPECT_TRUE(writer.patch_u4(3, 0xffa1b2c3));
  EXPECT_EQ(bytes[2], std::byte{0x34});
  EXPECT_EQ(bytes[3], std::byte{0xff});
  EXPECT_EQ(bytes[4], std::byte{0xa1});
  BinaryReader reader(bytes);
  EXPECT_EQ(reader.u1(), 0xff);
  EXPECT_EQ(reader.u2(), 0x1234);
  EXPECT_EQ(reader.u4(), 0xffa1b2c3);
  EXPECT_EQ(reader.u8(), 0x0123456789abcdef);
  EXPECT_EQ(reader.s1(), std::numeric_limits<std::int8_t>::min());
  EXPECT_EQ(reader.s2(), std::numeric_limits<std::int16_t>::min());
  EXPECT_EQ(reader.s4(), std::numeric_limits<std::int32_t>::min());
  EXPECT_EQ(reader.s8(), std::numeric_limits<std::int64_t>::min());
  EXPECT_EQ(reader.remaining(), 0u);
  EXPECT_FALSE(reader.u1());
}

TEST(BinaryIo, BoundedReadersDoNotAdvanceOnFailure) {
  constexpr std::array data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
  constexpr auto MAX_OFFSET = std::numeric_limits<std::size_t>::max();
  BinaryReader reader(data, MAX_OFFSET - 3);
  EXPECT_FALSE(reader.u4());
  EXPECT_FALSE(reader.take_slice(4));
  ASSERT_TRUE(reader.sub_reader(0));
  EXPECT_EQ(reader.offset(), MAX_OFFSET - 3);
  auto pair = reader.sub_reader(2);
  ASSERT_TRUE(pair);
  EXPECT_EQ(pair->offset(), MAX_OFFSET - 3);
  EXPECT_EQ(pair->u2(), 0x0102);
  EXPECT_FALSE(pair->u1());
  EXPECT_EQ(reader.u1(), 3);
  EXPECT_EQ(reader.offset(), MAX_OFFSET);
  EXPECT_FALSE(reader.u1());
  EXPECT_TRUE(reader.take_slice(0)->empty());

  BinaryReader empty({});
  EXPECT_FALSE(empty.u8());
  EXPECT_TRUE(empty.take_slice(0)->empty());
}

} // namespace
} // namespace cafe::detail
