#pragma once

#include <unordered_map>

#include "analysis_detail.hpp"
#include "cafe/classpath/class_hierarchy.hpp"

namespace cafe::analysis {

struct JoinSlot {
  InstructionId block_entry;
  std::size_t index;
  bool local;

  friend bool operator==(JoinSlot, JoinSlot) = default;
};

class JoinSearch {
public:
  explicit JoinSearch(std::pmr::memory_resource* resource) :
      names(resource), indices(resource), choices(resource) {
  }

  [[nodiscard]] std::string_view select(JoinSlot slot, std::string_view left, std::string_view right,
                                        const classpath::JoinResult& common);
  [[nodiscard]] bool encountered_ambiguity() const noexcept {
    return !choices.empty();
  }
  [[nodiscard]] bool advance();

private:
  struct Key {
    JoinSlot slot;
    std::string_view left;
    std::string_view right;

    friend bool operator==(const Key&, const Key&) = default;
  };

  struct KeyHash {
    [[nodiscard]] std::size_t operator()(const Key& key) const noexcept;
  };

  struct Choice {
    Key key;
    // Zero is the common superclass. Alternatives are one-based.
    std::size_t selected;
    std::size_t alternatives;
  };

  NameSet names;
  std::pmr::unordered_map<Key, std::size_t, KeyHash> indices;
  std::pmr::vector<Choice> choices;
};

class FrameMerger {
public:
  FrameMerger(const classpath::ClassHierarchy* hierarchy, JoinSearch& search,
              std::pmr::memory_resource* scratch) :
      hierarchy(hierarchy), search(search), names(scratch) {
  }

  [[nodiscard]] std::expected<bool, AnalysisError> merge(Frame& target, const Frame& incoming,
                                                         InstructionId block_entry);
  [[nodiscard]] std::expected<void, AnalysisError> check(const Frame& target, const Frame& incoming) const;

private:
  [[nodiscard]] std::expected<VerificationType, AnalysisError> join(
      VerificationType left, VerificationType right, JoinSlot slot);
  [[nodiscard]] std::expected<bool, AnalysisError> assignable(
      VerificationType from, VerificationType to) const;

  const classpath::ClassHierarchy* hierarchy;
  JoinSearch& search;
  NameSet names;
};

} // namespace cafe::analysis
