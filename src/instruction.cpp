#include "cafe/instruction.hpp"

#include <sstream>

#include "cafe/constants.hpp"

namespace cafe {
insn::insn(uint8_t opcode) : opcode(opcode) {
}
bool insn::operator==(const insn& other) const {
  return opcode == other.opcode;
}
bool insn::operator!=(const insn& other) const {
  return opcode != other.opcode;
}
std::string insn::to_string() const {
  std::ostringstream oss;
  oss << "insn(" << opcode_name(opcode) << ")";
  return oss.str();
}
var_insn::var_insn(uint8_t opcode, uint16_t index) : insn(opcode), index(index) {
}
bool var_insn::is_load() const {
  switch (opcode) {
    case op::aload:
    case op::iload:
    case op::fload:
    case op::dload:
    case op::lload:
      return true;
    default:
      return false;
  }
}
bool var_insn::is_store() const {
  return !is_load() && opcode != op::ret;
}
bool var_insn::is_wide() const {
  return opcode == op::dload || opcode == op::lload || opcode == op::dstore || opcode == op::lstore;
}
bool var_insn::operator==(const var_insn& other) const {
  return opcode == other.opcode && index == other.index;
}
bool var_insn::operator!=(const var_insn& other) const {
  return !(*this == other);
}
bool type_insn::operator==(const type_insn& other) const {
  return opcode == other.opcode && type == other.type;
}
bool type_insn::operator!=(const type_insn& other) const {
  return !(*this == other);
}
std::string var_insn::to_string() const {
  std::ostringstream oss;
  oss << "var_insn(" << opcode_name(opcode) << ", " << index << ")";
  return oss.str();
}
type_insn::type_insn(uint8_t opcode, const std::string_view& type) : insn(opcode), type(type) {
}
std::string type_insn::to_string() const {
  std::ostringstream oss;
  oss << "type_insn(" << opcode_name(opcode) << ", " << type << ")";
  return oss.str();
}
field_insn::field_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                       const std::string_view& desc) : insn(opcode), owner(owner), name(name), desc(desc) {
}
bool field_insn::operator==(const field_insn& other) const {
  return opcode == other.opcode && owner == other.owner && name == other.name && desc == other.desc;
}
bool field_insn::operator!=(const field_insn& other) const {
  return !(*this == other);
}
std::string field_insn::to_string() const {
  std::ostringstream oss;
  oss << "field_insn(" << opcode_name(opcode) << ", " << owner << ", " << name << ", " << desc << ")";
  return oss.str();
}
method_insn::method_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                         const std::string_view& desc) : insn(opcode), owner(owner), name(name), desc(desc), interface(opcode == op::invokeinterface) {
}
method_insn::method_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                         const std::string_view& desc, bool interface) :
    insn(opcode), owner(owner), name(name), desc(desc), interface(interface) {
}
bool method_insn::operator==(const method_insn& other) const {
  return opcode == other.opcode && owner == other.owner && name == other.name && desc == other.desc && interface == other.interface;
}
bool method_insn::operator!=(const method_insn& other) const {
  return !(*this == other);
}
std::string method_insn::to_string() const {
  std::ostringstream oss;
  oss << "method_insn(" << opcode_name(opcode) << ", " << owner << ", " << name << ", " << desc;
  if (interface && opcode != op::invokeinterface) {
    oss << ", true";
  }
  oss << ")";
  return oss.str();
}
iinc_insn::iinc_insn(uint16_t index, int16_t value) : index(index), value(value) {
}
bool iinc_insn::operator==(const iinc_insn& other) const {
  return index == other.index && value == other.value;
}
bool iinc_insn::operator!=(const iinc_insn& other) const {
  return !(*this == other);
}
std::string iinc_insn::to_string() const {
  std::ostringstream oss;
  oss << "iinc_insn(" << index << ", " << value << ")";
  return oss.str();
}
push_insn::push_insn(cafe::value value) : value(std::move(value)) {
}
bool push_insn::is_wide() const {
  return std::holds_alternative<int64_t>(value) || std::holds_alternative<double>(value);
}
bool push_insn::operator==(const push_insn& other) const {
  return value == other.value;
}
bool push_insn::operator!=(const push_insn& other) const {
  return value != other.value;
}
uint8_t push_insn::opcode() const {
  return opcode_of(value);
}
uint8_t push_insn::opcode_of(const cafe::value& value) {
  return std::visit(
      [](auto&& arg) -> uint8_t {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int32_t>) {
          if (arg >= -1 && arg <= 5) {
            return op::iconst_0 + static_cast<uint8_t>(arg);
          }
          if (arg >= std::numeric_limits<int8_t>::min() && arg <= std::numeric_limits<int8_t>::max()) {
            return op::bipush;
          }
          if (arg >= std::numeric_limits<int16_t>::min() && arg <= std::numeric_limits<int16_t>::max()) {
            return op::sipush;
          }
          return op::ldc;
        } else if constexpr (std::is_same_v<T, float>) {
          if (arg == 0.0f) {
            return op::fconst_0;
          }
          if (arg == 1.0f) {
            return op::fconst_1;
          }
          if (arg == 2.0f) {
            return op::fconst_2;
          }
          return op::ldc;
        } else if constexpr (std::is_same_v<T, int64_t>) {
          if (arg == 0) {
            return op::lconst_0;
          }
          if (arg == 1) {
            return op::lconst_1;
          }
          return op::ldc;
        } else if constexpr (std::is_same_v<T, double>) {
          if (arg == 0.0) {
            return op::dconst_0;
          }
          if (arg == 1.0) {
            return op::dconst_1;
          }
          return op::ldc;
        } else {
          return op::ldc;
        }
      },
      value);
}
std::string push_insn::to_string() const {
  std::ostringstream oss;
  oss << "push_insn(" << cafe::to_string(value) << ")";
  return oss.str();
}
branch_insn::branch_insn(uint8_t opcode, label target) : insn(opcode), target(std::move(target)) {
}
bool branch_insn::operator==(const branch_insn& other) const {
  return opcode == other.opcode && target == other.target;
}
bool branch_insn::operator!=(const branch_insn& other) const {
  return !(*this == other);
}
std::string branch_insn::to_string() const {
  std::ostringstream oss;
  oss << "branch_insn(" << opcode_name(opcode) << ", " << target.to_string() << ")";
  return oss.str();
}
lookup_switch_insn::lookup_switch_insn(label default_target, const std::vector<std::pair<int32_t, label>>& targets) :
    default_target(std::move(default_target)), targets(targets) {
}
bool lookup_switch_insn::operator==(const lookup_switch_insn& other) const {
  return default_target == other.default_target && targets == other.targets;
}
bool lookup_switch_insn::operator!=(const lookup_switch_insn& other) const {
  return !(*this == other);
}
std::string lookup_switch_insn::to_string() const {
  std::ostringstream oss;
  oss << "lookup_switch_insn(" << default_target.to_string() << ", [";
  bool first = true;
  for (const auto& [key, target] : targets) {
    if (!first) {
      oss << ", ";
    }
    oss << key << " -> " << target.to_string();
    first = false;
  }
  oss << "])";
  return oss.str();
}
table_switch_insn::table_switch_insn(label default_target, int32_t low, int32_t high,
                                     const std::vector<label>& targets) :
    default_target(std::move(default_target)), low(low), high(high), targets(targets) {
}
bool table_switch_insn::operator==(const table_switch_insn& other) const {
  return default_target == other.default_target && low == other.low && high == other.high && targets == other.targets;
}
bool table_switch_insn::operator!=(const table_switch_insn& other) const {
  return !(*this == other);
}
std::string table_switch_insn::to_string() const {
  std::ostringstream oss;
  oss << "table_switch_insn(" << default_target.to_string() << ", " << low << ", " << high << ", [";
  bool first = true;
  for (const auto& target : targets) {
    if (!first) {
      oss << ", ";
    }
    oss << target.to_string();
    first = false;
  }
  oss << "])";
  return oss.str();
}
multi_array_insn::multi_array_insn(const std::string_view& desc, uint8_t dims) : desc(desc), dims(dims) {
}
bool multi_array_insn::operator==(const multi_array_insn& other) const {
  return desc == other.desc && dims == other.dims;
}
bool multi_array_insn::operator!=(const multi_array_insn& other) const {
  return !(*this == other);
}
std::string multi_array_insn::to_string() const {
  std::ostringstream oss;
  oss << "multi_array_insn(" << desc << ", " << static_cast<int>(dims) << ")";
  return oss.str();
}
array_insn::array_insn(const std::variant<uint8_t, std::string>& type) : type(type) {
}
bool array_insn::operator==(const array_insn& other) const {
  return type == other.type;
}
bool array_insn::operator!=(const array_insn& other) const {
  return type != other.type;
}
std::string array_insn::to_string() const {
  std::ostringstream oss;
  oss << "array_insn(";
  std::visit(
      [&oss](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, uint8_t>) {
          oss << array_name(arg);
        } else {
          oss << '"' << arg << '"';
        }
      },
      type);
  oss << ")";
  return oss.str();
}
invoke_dynamic_insn::invoke_dynamic_insn(const std::string_view& name, const std::string_view& desc,
                                         method_handle handle, const std::vector<value>& args) :
    name(name), desc(desc), handle(std::move(handle)), args(args) {
}
bool invoke_dynamic_insn::operator==(const invoke_dynamic_insn& other) const {
  return name == other.name && desc == other.desc && handle == other.handle && args == other.args;
}
bool invoke_dynamic_insn::operator!=(const invoke_dynamic_insn& other) const {
  return !(*this == other);
}
std::string invoke_dynamic_insn::to_string() const {
  std::ostringstream oss;
  oss << "invoke_dynamic_insn(" << '"' << name << '"' << ", " << '"' << desc << '"' << ", "
      << cafe::to_string(handle) << ", [";
  bool first = true;
  for (const auto& arg : args) {
    if (!first) {
      oss << ", ";
    }
    oss << cafe::to_string(arg);
    first = false;
  }
  oss << "])";
  return oss.str();
}
static int16_t opcode_impl(const instruction& in) {
  return std::visit(
      [](auto&& i) -> int16_t {
        using T = std::decay_t<decltype(i)>;
        if constexpr (std::is_same_v<T, iinc_insn>) {
          return op::iinc;
        } else if constexpr (std::is_same_v<T, lookup_switch_insn>) {
          return op::lookupswitch;
        } else if constexpr (std::is_same_v<T, table_switch_insn>) {
          return op::tableswitch;
        } else if constexpr (std::is_same_v<T, multi_array_insn>) {
          return op::multianewarray;
        } else if constexpr (std::is_same_v<T, invoke_dynamic_insn>) {
          return op::invokedynamic;
        } else if constexpr (std::is_same_v<T, array_insn>) {
          if (std::holds_alternative<uint8_t>(i.type)) {
            return op::newarray;
          }
          return op::anewarray;
        } else if constexpr (std::is_same_v<T, label>) {
          return -1;
        } else if constexpr (std::is_same_v<T, push_insn>) {
          return i.opcode();
        } else {
          return i.opcode;
        }
      },
      in);
}

int16_t opcode(const instruction& insn) {
  return opcode_impl(insn);
}
int16_t opcode(instruction&& insn) {
  return opcode_impl(insn);
}
std::string to_string(const instruction& insn) {
  return std::visit([](const auto& i) -> std::string { return i.to_string(); }, insn);
}
std::string to_string(instruction&& insn) {
  return std::visit([](auto&& i) -> std::string { return i.to_string(); }, insn);
}
tcb::tcb(label start, label end, label handler, const std::optional<std::string>& type) :
    start(std::move(start)), end(std::move(end)), handler(std::move(handler)), type(type) {
}
std::string tcb::to_string() const {
  std::ostringstream oss;
  oss << "tcb(" << start.to_string() << ", " << end.to_string() << ", " << handler.to_string();
  if (type) {
    oss << ", " << '"' << *type << '"';
  }
  oss << ")";
  return oss.str();
}
object_var::object_var(const std::string_view& type) : type(type) {
}
uninitialized_var::uninitialized_var(label offset) : offset(std::move(offset)) {
}
bool top_var::operator==(const top_var&) const {
  return true;
}
bool top_var::operator!=(const top_var&) const {
  return false;
}
bool int_var::operator==(const int_var&) const {
  return true;
}
bool int_var::operator!=(const int_var&) const {
  return false;
}
bool float_var::operator==(const float_var&) const {
  return true;
}
bool float_var::operator!=(const float_var&) const {
  return false;
}
bool null_var::operator==(const null_var&) const {
  return true;
}
bool null_var::operator!=(const null_var&) const {
  return false;
}
bool uninitialized_this_var::operator==(const uninitialized_this_var&) const {
  return true;
}
bool uninitialized_this_var::operator!=(const uninitialized_this_var&) const {
  return false;
}
bool object_var::operator==(const object_var& other) const {
  return type == other.type;
}
bool object_var::operator!=(const object_var& other) const {
  return type != other.type;
}
bool uninitialized_var::operator==(const uninitialized_var& other) const {
  return offset == other.offset;
}
bool uninitialized_var::operator!=(const uninitialized_var& other) const {
  return offset != other.offset;
}
bool long_var::operator==(const long_var&) const {
  return true;
}
bool long_var::operator!=(const long_var&) const {
  return false;
}
bool double_var::operator==(const double_var&) const {
  return true;
}
bool double_var::operator!=(const double_var&) const {
  return false;
}
same_frame::same_frame(const frame_var& stack) : stack(stack) {
}
full_frame::full_frame(const std::vector<frame_var>& locals, const std::vector<frame_var>& stack) :
    locals(locals), stack(stack) {
}
chop_frame::chop_frame(uint8_t size) : size(size) {
}
append_frame::append_frame(const std::vector<frame_var>& locals) : locals(locals) {
}
local_var::local_var(const std::string_view& name, const std::string_view& desc,
                     const std::string_view& signature, label start, label end, uint16_t index) :
    name(name), desc(desc), signature(signature), start(std::move(start)), end(std::move(end)),
    index(index) {
}
std::string local_var::to_string() const {
  std::ostringstream oss;
  oss << "local_var(" << '"' << name << '"' << ", " << '"' << desc << '"' << ", " << '"' << signature << '"'
      << ", " << start.to_string() << ", " << end.to_string() << ", " << index << ")";
  return oss.str();
}
std::string to_string(const frame_var& var) {
  return std::visit(
      [](const auto& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, top_var>) {
          return "top";
        } else if constexpr (std::is_same_v<T, int_var>) {
          return "int";
        } else if constexpr (std::is_same_v<T, float_var>) {
          return "float";
        } else if constexpr (std::is_same_v<T, double_var>) {
          return "double";
        } else if constexpr (std::is_same_v<T, long_var>) {
          return "long";
        } else if constexpr (std::is_same_v<T, null_var>) {
          return "null";
        } else if constexpr (std::is_same_v<T, uninitialized_this_var>) {
          return "uninitialized_this";
        } else if constexpr (std::is_same_v<T, object_var>) {
          return "object(" + arg.type + ")";
        } else if constexpr (std::is_same_v<T, uninitialized_var>) {
          return "uninitialized(" + arg.offset.to_string() + ")";
        }
      },
      var);
}
std::string to_string(const frame& frame) {
  return std::visit(
      [](const auto& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, same_frame>) {
          std::ostringstream oss;
          oss << "same_frame";
          if (arg.stack) {
            oss << "(" << cafe::to_string(*arg.stack) << ")";
          }
          return oss.str();
        } else if constexpr (std::is_same_v<T, full_frame>) {
          std::ostringstream oss;
          oss << "full_frame([";
          bool first = true;
          for (const auto& var : arg.locals) {
            if (!first) {
              oss << ", ";
            }
            oss << cafe::to_string(var);
            first = false;
          }
          oss << "], [";
          first = true;
          for (const auto& var : arg.stack) {
            if (!first) {
              oss << ", ";
            }
            oss << cafe::to_string(var);
            first = false;
          }
          oss << "])";
          return oss.str();
        } else if constexpr (std::is_same_v<T, chop_frame>) {
          std::ostringstream oss;
          oss << "chop_frame(" << static_cast<int>(arg.size) << ")";
          return oss.str();
        } else if constexpr (std::is_same_v<T, append_frame>) {
          std::ostringstream oss;
          oss << "append_frame([";
          bool first = true;
          for (const auto& var : arg.locals) {
            if (!first) {
              oss << ", ";
            }
            oss << cafe::to_string(var);
            first = false;
          }
          oss << "])";
          return oss.str();
        }
      },
      frame);
}
} // namespace cafe
