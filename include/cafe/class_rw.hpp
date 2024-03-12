#pragma once

#include <functional>
#include <istream>

#include "apidef.hpp"
#include "class_file.hpp"
#include "model.hpp"

namespace cafe {

class CAFE_API class_reader {
public:
  class_reader(class_model& model, const std::function<void(const std::exception&)>& error_handler);
  ~class_reader() = default;
  class_reader(const class_reader&) = delete;
  class_reader(class_reader&&) = delete;
  class_reader& operator=(const class_reader&) = delete;
  class_reader& operator=(class_reader&&) = delete;

  class_model& model() const;
  const std::function<void(const std::exception&)>& error_handler() const;

private:
  class_model& model_;
  std::function<void(const std::exception&)> error_handler_;
};

CAFE_API std::istream& operator>>(std::istream& stream, const class_reader& reader);

} // namespace cafe
