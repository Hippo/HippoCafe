#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <cafe/data_rw.hpp>

TEST(data_reader, read_utf) {
  std::vector<int8_t> data = {0,   28,  -19, -96, -66, -19, -74, -101, -30,  -104, -107, -17, -72, -113, 32,
                              105, 115, 32,  -19, -96, -67, -19, -76,  -101, -19,  -96,  -67, -19, -76,  -99};
  std::string str(data.begin(), data.end());
  std::istringstream stream(str, std::ios::binary);
  cafe::data_reader reader(stream);
  const auto result = reader.read_utf();
  const std::string test = "🦛☕️ is 🔛🔝";
  ASSERT_EQ(result, test);
}

TEST(data_writer, write_utf) {
  const std::vector<int8_t> expected = {0,   28,  -19, -96, -66, -19, -74, -101, -30,  -104, -107, -17, -72, -113, 32,
                                        105, 115, 32,  -19, -96, -67, -19, -76,  -101, -19,  -96,  -67, -19, -76,  -99};
  std::ostringstream stream(std::ios::binary);
  cafe::data_writer writer(stream);
  const std::string str = "🦛☕️ is 🔛🔝";
  writer.write_utf(str);
  const auto stream_str = stream.str();
  const std::vector<int8_t> value(stream_str.begin(), stream_str.end());
  const auto len = expected.size();
  ASSERT_EQ(value.size(), len);
  for (auto i = 0; i < len; i++) {
    ASSERT_EQ(value[i], expected[i]);
  }
}
