#pragma once

#include "cafe/analysis.hpp"
#include "cafe/annotation.hpp"
#include "cafe/class_file.hpp"
#include "cafe/class_reader.hpp"
#include "cafe/class_tree.hpp"
#include "cafe/class_writer.hpp"
#include "cafe/constants.hpp"
#include "cafe/instruction.hpp"
#include "cafe/label.hpp"
#include "cafe/value.hpp"

namespace cafe {
template <typename T>
std::string to_string(const T& value) {
  return value.to_string();
}
}
