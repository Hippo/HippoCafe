#pragma once

#include <optional>

#include "cafe/ids.hpp"

namespace cafe {

// A label binds to the boundary immediately before an instruction or to the
// exclusive end (nullopt). An unbound label is represented separately.
struct CodePosition {
  std::optional<InstructionId> before;

  friend bool operator==(const CodePosition&, const CodePosition&) = default;
};

} // namespace cafe
