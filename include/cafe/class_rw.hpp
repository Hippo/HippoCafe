#pragma once

#include <functional>
#include <istream>

#include "class_file.hpp"

namespace cafe {

class class_reader {
public:
  class_reader(class_model& model, const std::function<void(const std::exception&)>& error_handler);
  ~class_reader() = default;
  class_reader(const class_reader&) = delete;
  class_reader(class_reader&&) = delete;
  class_reader& operator=(const class_reader&) = delete;
  class_reader& operator=(class_reader&&) = delete;

  friend std::istream& operator>>(std::istream& stream, class_reader& reader);
  friend std::istream& operator>>(std::istream& stream, class_reader&& reader);

private:
  class_model& model_;
  std::function<void(const std::exception&)> error_handler_;
};

std::istream& operator>>(std::istream& stream, class_reader& reader);
std::istream& operator>>(std::istream& stream, class_reader&& reader);

} // namespace cafe
