#include "cafe/result.hpp"

cafe::error::error(const std::string& message) : message_(message) {
}
cafe::error::error(std::string&& message) : message_(std::move(message)) {
}
const std::string& cafe::error::message() const {
  return message_;
}
