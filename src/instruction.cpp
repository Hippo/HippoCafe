#include <utility>
#include <sstream>

#include "cafe/constants.hpp"
#include "cafe/instruction.hpp"

namespace cafe {
insn::insn() : id_(reinterpret_cast<uintptr_t>(this)) {
}
insn::insn(uint8_t opcode) : opcode(opcode), id_(reinterpret_cast<uintptr_t>(this)) {
}
uint64_t insn::id() const {
  return id_;
}
std::string insn::to_string() const {
  std::ostringstream oss;
  oss << "insn(" << opcode_name(opcode) << ")";
  return oss.str();
}
var_insn::var_insn(uint8_t opcode, uint16_t index) : insn(opcode), index(index) {
}
std::string var_insn::to_string() const {
  std::ostringstream oss;
  oss << "var_insn(" << opcode_name(opcode)  << ", "<< index << ")";
  return oss.str();
}
type_insn::type_insn(uint8_t opcode, const std::string_view& type) : insn(opcode), type(type) {
}
std::string type_insn::to_string() const {
  std::ostringstream oss;
  oss << "type_insn(" << opcode_name(opcode) << ", " << type << ")";
  return oss.str();
}
ref_insn::ref_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                   const std::string_view& descriptor) :
    insn(opcode), owner(owner), name(name), descriptor(descriptor) {
}
std::string ref_insn::to_string() const {
  std::ostringstream oss;
  oss << "ref_insn(" << opcode_name(opcode) << ", " << '"' << owner << '"' << ", " << '"' << name << '"' << ", " << '"' << descriptor
     << '"' << ")";
  return oss.str();
}
iinc_insn::iinc_insn() : id_(reinterpret_cast<uintptr_t>(this)) {
}
iinc_insn::iinc_insn(uint16_t index, int16_t value) :
    index(index), value(value), id_(reinterpret_cast<uintptr_t>(this)) {
}
uint64_t iinc_insn::id() const {
  return id_;
}
std::string iinc_insn::to_string() const {
  std::ostringstream oss;
  oss << "iinc_insn(" << index << ", " << value << ")";
  return oss.str();
}
push_insn::push_insn() : id_(reinterpret_cast<uintptr_t>(this)) {
}
push_insn::push_insn(value val) : val(std::move(val)), id_(reinterpret_cast<uintptr_t>(this)) {
}
uint64_t push_insn::id() const {
  return id_;
}
uint8_t push_insn::opcode() const {
  int64_t int_val;
  if (const auto l = std::get_if<int64_t>(&val)) {
    int_val = *l;
  } else if (const auto i = std::get_if<int32_t>(&val)) {
    int_val = *i;
  } else {
    return op::ldc;
  }
  if (int_val >= -1 && int_val <= 5) {
    return op::iconst_0 + static_cast<int8_t>(int_val);
  }
  if (int_val >= std::numeric_limits<int8_t>::min() && int_val <= std::numeric_limits<int8_t>::max()) {
    return op::bipush;
  }
  if (int_val >= std::numeric_limits<int16_t>::min() && int_val <= std::numeric_limits<int16_t>::max()) {
    return op::sipush;
  }
  return op::ldc;
}
std::string push_insn::to_string() const {
  std::ostringstream oss;
  oss << "push_insn(" << cafe::to_string(val) << ")";
  return oss.str();
}
branch_insn::branch_insn(uint8_t opcode, label target) : insn(opcode), target(std::move(target)) {
}
std::string branch_insn::to_string() const {
  std::ostringstream oss;
  oss << "branch_insn(" << opcode_name(opcode) << ", " << target.to_string() << ")";
  return oss.str();
}
lookup_switch_insn::lookup_switch_insn() : id_(reinterpret_cast<uintptr_t>(this)) {
}
lookup_switch_insn::lookup_switch_insn(label default_target, const std::vector<std::pair<int32_t, label>>& targets) :
    default_target(std::move(default_target)), targets(targets), id_(reinterpret_cast<uintptr_t>(this)) {
}
uint64_t lookup_switch_insn::id() const {
  return id_;
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
table_switch_insn::table_switch_insn() : id_(reinterpret_cast<uintptr_t>(this)) {
}
table_switch_insn::table_switch_insn(label default_target, int32_t low, int32_t high,
                                     const std::vector<label>& targets) :
    default_target(std::move(default_target)), low(low), high(high), targets(targets),
    id_(reinterpret_cast<uintptr_t>(this)) {
}
uint64_t table_switch_insn::id() const {
  return id_;
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
multi_array_insn::multi_array_insn() : id_(reinterpret_cast<uintptr_t>(this)) {
}
multi_array_insn::multi_array_insn(const std::string_view& descriptor, uint8_t dims) :
    descriptor(descriptor), dims(dims), id_(reinterpret_cast<uintptr_t>(this)) {
}
uint64_t multi_array_insn::id() const {
  return id_;
}
std::string multi_array_insn::to_string() const {
  std::ostringstream oss;
  oss << "multi_array_insn(" << descriptor << ", " << static_cast<int>(dims) << ")";
  return oss.str();
}
array_insn::array_insn() : id_(reinterpret_cast<uintptr_t>(this)) {
}
array_insn::array_insn(const std::variant<uint8_t, std::string>& type) :
    type(type), id_(reinterpret_cast<uintptr_t>(this)) {
}
uint64_t array_insn::id() const {
  return id_;
}
std::string array_insn::to_string() const {
  std::ostringstream oss;
  oss << "array_insn(";
  std::visit([&oss](const auto& arg) {
    using T = std::decay_t<decltype(arg)>;
    if constexpr (std::is_same_v<T, uint8_t>) {
      oss << array_name(arg);
    } else {
      oss << '"' << arg << '"';
    }
  }, type);
  oss << ")";
  return oss.str();
}
invoke_dynamic_insn::invoke_dynamic_insn() : id_(reinterpret_cast<uintptr_t>(this)) {
}
invoke_dynamic_insn::invoke_dynamic_insn(const std::string_view& name, const std::string_view& descriptor,
                                         method_handle handle, const std::vector<value>& args) :
    name(name), descriptor(descriptor), handle(std::move(handle)), args(args), id_(reinterpret_cast<uintptr_t>(this)) {
}
uint64_t invoke_dynamic_insn::id() const {
  return id_;
}
std::string invoke_dynamic_insn::to_string() const {
  std::ostringstream oss;
  oss << "invoke_dynamic_insn(" << '"' << name << '"' << ", " << '"' << descriptor << '"' << ", " << cafe::to_string(handle) << ", [";
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
uintptr_t id(const instruction& insn) {
  return std::visit([](const auto& i) { return i.id(); }, insn);
}
uintptr_t id(instruction&& insn) {
  return std::visit([](auto&& i) { return i.id(); }, insn);
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
tcb::tcb(label start, label end, label handler, const std::string_view& type) :
    start(std::move(start)), end(std::move(end)), handler(std::move(handler)), type(type) {
}
std::string tcb::to_string() const {
  std::ostringstream oss;
  oss << "tcb(" << start.to_string() << ", " << end.to_string() << ", " << handler.to_string() << ", " << '"' << type << '"' << ")";
  return oss.str();
}
object_var::object_var(const std::string_view& type) : type(type) {
}
uninitalized_var::uninitalized_var(label offset) : offset(std::move(offset)) {
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
local_var::local_var(const std::string_view& name, const std::string_view& descriptor,
                     const std::string_view& signature, label start, label end, uint16_t index) :
    name(name), descriptor(descriptor), signature(signature), start(std::move(start)), end(std::move(end)),
    index(index) {
}
std::string local_var::to_string() const {
  std::ostringstream oss;
  oss << "local_var(" << '"' << name << '"' << ", " << '"' << descriptor << '"' << ", " << '"' << signature << '"'
      << ", " << start.to_string() << ", " << end.to_string() << ", " << index << ")";
  return oss.str();
}
std::string to_string(const frame_var& var) {
  return std::visit([](const auto& arg) -> std::string {
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
    } else if constexpr (std::is_same_v<T, uninitalized_var>) {
      return "uninitialized(" + arg.offset.to_string() + ")";
    }
  }, var);
}
std::string to_string(const frame& frame) {
  return std::visit([](const auto& arg) -> std::string {
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
  }, frame);
}
void code::clear() noexcept {
  std::vector<instruction>::clear();
  tcb_table.clear();
  line_numbers.clear();
  local_vars.clear();
  frames.clear();
  visible_type_annotations.clear();
  invisible_type_annotations.clear();
  max_stack = 0;
  max_locals = 0;
}
} // namespace cafe
