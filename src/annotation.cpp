#include "cafe/annotation.hpp"

#include <sstream>
#include <utility>

#include "cafe/constants.hpp"

namespace cafe {
annotation::annotation(const std::string_view& descriptor) : descriptor(descriptor) {
}
std::string annotation::to_string() const {
  std::ostringstream oss;
  oss << "annotation(" << '"' << descriptor << '"' << ", [";
  bool first = false;
  for (const auto& [name, value] : values) {
    if (!first) {
      oss << ", ";
    }
    oss << '"' << name << '"' << " -> " << value.to_string();
    first = false;
  }
  oss << "])";
  return oss.str();
}
std::string element_value::to_string() const {
  return std::visit([](auto&& arg) {
    using T = std::decay_t<decltype(arg)>;
    if constexpr (std::is_same_v<T, std::pair<std::string, std::string>>) {
      return "enum(" + arg.first + ", " + arg.second + ")";
    } else if constexpr (std::is_same_v<T, class_value>) {
      return cafe::to_string(arg);
    } else if constexpr (std::is_same_v<T, annotation>) {
      return arg.to_string();
    } else if constexpr (std::is_same_v<T, std::vector<element_value>>) {
      std::ostringstream oss;
      oss << "[";
      bool first = true;
      for (const auto& e : arg) {
        if (!first) {
          oss << ", ";
        }
        oss << e.to_string();
        first = false;
      }
      oss << "]";
      return oss.str();
    } else if constexpr (std::is_same_v<T, std::string>) {
      std::ostringstream oss;
      oss << '"' << arg << '"';
      return oss.str();
    } else {
      return std::to_string(arg);
    }
  }, value);
}
target::type_parameter::type_parameter(uint8_t index) : index(index) {
}
target::supertype::supertype(const std::string_view& name) : name(name) {
}
target::type_parameter_bound::type_parameter_bound(uint8_t type_parameter_index, uint8_t bound_index) :
    type_parameter_index(type_parameter_index), bound_index(bound_index) {
}
target::formal_parameter::formal_parameter(uint8_t index) : index(index) {
}
target::throws::throws(const std::string_view& name) : name(name) {
}
target::local::local(label start, label end, uint16_t index) :
    start(std::move(start)), end(std::move(end)), index(index) {
}
target::localvar::localvar(const std::vector<local>& table) : table(table) {
}
target::catch_target::catch_target(label handler_label) : handler_label(std::move(handler_label)) {
}
target::offset_target::offset_target(label offset) : offset(std::move(offset)) {
}
target::type_argument::type_argument(label offset, uint8_t index) : offset(std::move(offset)), index(index) {
}
type_path::type_path(const std::vector<std::pair<uint8_t, uint8_t>>& path) : path(path) {
}
type_annotation::type_annotation() : target_info(target::empty()) {
}
type_annotation::type_annotation(uint8_t target_type, type_annotation_target target_info, type_path target_path,
                                 const std::string_view& descriptor) :
    target_type(target_type), target_info(std::move(target_info)), target_path(std::move(target_path)),
    descriptor(descriptor) {
}
std::string to_string(const type_annotation_target& target) {
  return std::visit(
      [](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, target::type_parameter>) {
          return "type_parameter(" + std::to_string(arg.index) + ")";
        } else if constexpr (std::is_same_v<T, target::supertype>) {
          return "supertype(\"" + arg.name + "\")";
        } else if constexpr (std::is_same_v<T, target::type_parameter_bound>) {
          return "type_parameter_bound(" + std::to_string(arg.type_parameter_index) + ", " +
                 std::to_string(arg.bound_index) + ")";
        } else if constexpr (std::is_same_v<T, target::formal_parameter>) {
          return "formal_parameter(" + std::to_string(arg.index) + ")";
        } else if constexpr (std::is_same_v<T, target::throws>) {
          return "throws(\"" + arg.name + "\")";
        } else if constexpr (std::is_same_v<T, target::localvar>) {
          std::ostringstream oss;
          oss << "localvar([";
          bool first = true;
          for (const auto& local : arg.table) {
            if (!first) {
              oss << ", ";
            }
            oss << "local(" << local.start.to_string() << ", " << local.end.to_string() << ", " << local.index << ")";
            first = false;
          }
          oss << "])";
          return oss.str();
        } else if constexpr (std::is_same_v<T, target::catch_target>) {
          return "catch_target(" + arg.handler_label.to_string() + ")";
        } else if constexpr (std::is_same_v<T, target::offset_target>) {
          return "offset_target(" + arg.offset.to_string() + ")";
        } else if constexpr (std::is_same_v<T, target::type_argument>) {
          return "type_argument(" + arg.offset.to_string() + ", " + std::to_string(arg.index) + ")";
        }
        return "empty";
      },
      target);
}
std::string type_path::to_string() const {
  std::ostringstream oss;
  oss << "type_path([";
  bool first = true;
  for (const auto& [kind, index] : path) {
    if (!first) {
      oss << ", ";
    }
    oss << "(" << typepath_name(kind) << ", " << std::to_string(index) << ")";
    first = false;
  }
  oss << "])";
  return oss.str();
}
std::string type_annotation::to_string() const {
  std::ostringstream oss;
  oss << "type_annotation(0x" << std::hex << static_cast<int>(target_type) << std::dec  << ", " << cafe::to_string(target_info) << ", " << target_path.to_string()
      << ", " << '"' << descriptor << '"' << ", [";
  bool first = true;
  for (const auto& [name, value] : values) {
    if (!first) {
      oss << ", ";
    }
    oss << name << " -> " << value.to_string();
    first = false;
  }
  oss << "])";
  return oss.str();
}
} // namespace cafe
