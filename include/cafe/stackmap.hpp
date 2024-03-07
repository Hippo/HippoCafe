#pragma once

#include <cstdint>
#include <variant>
#include <vector>

namespace cafe::attribute {
template<int tag_value>
class variable_info {
public:
  static constexpr uint8_t tag = tag_value;
};

using top_variable_info = variable_info<0>;
using integer_variable_info = variable_info<1>;
using float_variable_info = variable_info<2>;
using double_variable_info = variable_info<3>;
using long_variable_info = variable_info<4>;
using null_variable_info = variable_info<5>;
using uninitialized_this_variable_info = variable_info<6>;

class object_variable_info {
public:
  static constexpr uint8_t tag = 7;
  uint16_t index;
};

class uninitialized_variable_info {
public:
  static constexpr uint8_t tag = 8;
  uint16_t offset;
};

using verification_type_info =
    std::variant<top_variable_info, integer_variable_info, float_variable_info, double_variable_info,
                 long_variable_info, null_variable_info, uninitialized_this_variable_info, object_variable_info,
                 uninitialized_variable_info>;

class same_frame {
public:
  uint8_t frame_type;
};

class same_locals_1_stack_item_frame {
public:
  uint8_t frame_type;
  verification_type_info stack;
};

class same_locals_1_stack_item_frame_extended {
public:
  static constexpr uint8_t frame_type = 247;
  uint16_t offset_delta;
  verification_type_info stack;
};

class chop_frame {
public:
  uint8_t frame_type;
  uint16_t offset_delta;
};

class same_frame_extended {
public:
  static constexpr uint8_t frame_type = 251;
  uint16_t offset_delta;
};

class append_frame {
public:
  uint8_t frame_type;
  uint16_t offset_delta;
  std::vector<verification_type_info> locals;
};

class full_frame {
public:
  static constexpr uint8_t frame_type = 255;
  uint16_t offset_delta;
  std::vector<verification_type_info> locals;
  std::vector<verification_type_info> stack;
};

using stack_map_frame =
    std::variant<same_frame, same_locals_1_stack_item_frame, same_locals_1_stack_item_frame_extended, chop_frame,
                 same_frame_extended, append_frame, full_frame>;
} // namespace cafe::attribute
