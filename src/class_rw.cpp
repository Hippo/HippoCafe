#include "cafe/class_rw.hpp"

#include "composer.hpp"

namespace cafe {
class_reader::class_reader(class_model& model, const std::function<void(const std::exception&)>& error_handler) :
    model_(model), error_handler_(error_handler) {
}
std::istream& operator>>(std::istream& stream, class_reader& reader) {
  class_file cf;
  stream >> cf;
  class_composer composer(cf, reader.error_handler_);
  composer.compose(reader.model_);
  return stream;
}
std::istream& operator>>(std::istream& stream, class_reader&& reader) {
  return stream >> reader;
}
} // namespace cafe
