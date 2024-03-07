#include "cafe/constant_pool.hpp"

namespace cafe::cp {

std::string constant_pool::get_string(uint16_t index) const {
  if (index == 0 || index >= size()) {
    return "";
  }
  if (const auto utf8 = std::get_if<utf8_info>(&at(index))) {
    return utf8->value;
  }
  if (const auto string = std::get_if<string_info>(&at(index))) {
    if (const auto utf8 = std::get_if<utf8_info>(&at(string->string_index))) {
      return utf8->value;
    }
  }
  if (const auto class_info = std::get_if<cp::class_info>(&at(index))) {
    if (const auto utf8 = std::get_if<utf8_info>(&at(class_info->name_index))) {
      return utf8->value;
    }
  }
  if (const auto module_info = std::get_if<cp::module_info>(&at(index))) {
    if (const auto utf8 = std::get_if<utf8_info>(&at(module_info->name_index))) {
      return utf8->value;
    }
  }
  if (const auto package_info = std::get_if<cp::package_info>(&at(index))) {
    if (const auto utf8 = std::get_if<utf8_info>(&at(package_info->name_index))) {
      return utf8->value;
    }
  }
  return "";
}

} // namespace cafe::cp
