#include "cafe/class_rw.hpp"

#include "model/composer.hpp"

namespace cafe {
class_reader::class_reader(class_model& model, const std::function<void(const std::exception&)>& error_handler) :
    model_(model), error_handler_(error_handler) {
}

class_model& class_reader::model() const {
  return model_;
}
const std::function<void(const std::exception&)>& class_reader::error_handler() const {
  return error_handler_;
}

std::istream& operator>>(std::istream& stream, const class_reader& reader) {
  class_file cf;
  stream >> cf;
  class_composer composer(cf, reader.error_handler());
  composer.compose(reader.model());
  return stream;
}
} // namespace cafe
