/**
 * @file
 * @brief Reachability and index assignment for compact class-file emission.
 * @ingroup implementation
 */

#pragma once

#include <cstddef>
#include <expected>
#include <list>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <cafe/attribute.hpp>
#include <cafe/error.hpp>

#include "binary_writer.hpp"
#include "class_format.hpp"

namespace cafe {
class Method;
}

namespace cafe::detail {

/// Write a constant whose references already use output indices.
/// @tparam Buffer Byte container backing the writer.
/// @param writer Output cursor.
/// @param entry Constant tag and wire operands.
/// @param form Wire layout of the constant.
/// @param utf8 Encoded bytes for a Utf8 entry.
template<class Buffer>
void write_constant(BinaryWriter<Buffer>& writer, const CpEntry& entry, CpForm form, std::string_view utf8 = {}) {
  writer.u1(static_cast<std::uint8_t>(entry.tag));
  switch (form) {
    case CpForm::Utf8:
      writer.u2(static_cast<std::uint16_t>(utf8.size()));
      writer.bytes(std::as_bytes(std::span(utf8)));
      break;
    case CpForm::Bits32:
      writer.u4(static_cast<std::uint32_t>(entry.bits));
      break;
    case CpForm::Bits64:
      writer.u8(entry.bits);
      break;
    case CpForm::Handle:
      writer.u1(entry.reference_kind);
      [[fallthrough]];
    case CpForm::Index:
      writer.u2(static_cast<std::uint16_t>(entry.first.value));
      break;
    case CpForm::Pair:
      writer.u2(static_cast<std::uint16_t>(entry.first.value));
      writer.u2(static_cast<std::uint16_t>(entry.second.value));
      break;
    case CpForm::Invalid:
      break;
  }
}

/// Intern constants reachable from emitted references without changing the class model.
class CompactConstantPool {
public:
  /// Start a compact pool for one class write.
  /// @param pool Source constants addressed by model indices.
  /// @param version Version used for tag and reference validation.
  /// @param module Whether the class is a module descriptor.
  /// @param bootstraps Source bootstrap rows.
  /// @param resource Allocator for writer-local storage.
  CompactConstantPool(const ConstantPool& pool, ClassVersion version, bool module,
                      std::span<const BootstrapMethod> bootstraps, std::pmr::memory_resource* resource);

  /// Request a wire index for an emitted reference.
  /// @param source Model pool slot or zero for an absent reference.
  /// @return Output slot or a validation or capacity error.
  [[nodiscard]] std::expected<CpIndex, WriteError> index(CpIndex source);

  /// Assign the first output slots to constants used by narrow ldc operands.
  /// @param methods Methods whose instruction widths must be retained.
  /// @param preserve_encodings Whether to honor wide encoding hints.
  /// @return Success or a pool validation error.
  [[nodiscard]] std::expected<void, WriteError> reserve_narrow_constants(
      std::span<const Method> methods, bool preserve_encodings);

  /// Look up the assigned slot of a source entry after collecting the pool.
  /// @param source Source pool slot.
  /// @return Assigned slot or zero if the source entry is unused or absent.
  [[nodiscard]] CpIndex mapped(CpIndex source) const noexcept;

  /// Build an in-memory pool from the collected constants without encoding class bytes.
  /// @param destination Empty pool to receive the replacement entries.
  /// @return Success or a pool capacity error.
  [[nodiscard]] std::expected<void, WriteError> build_model(ConstantPool& destination);
  /// Request a Utf8 constant for generated metadata.
  /// @param value Modified UTF-8 bytes.
  /// @param path Diagnostic path.
  /// @return Output slot or a UTF-8 or capacity error.
  [[nodiscard]] std::expected<CpIndex, WriteError> utf8(std::string_view value, std::string_view path);
  /// Request a Class constant for a verified frame type.
  /// @param name Class name or array descriptor in modified UTF-8.
  /// @param path Diagnostic path.
  /// @return Output slot or a UTF-8 or capacity error.
  [[nodiscard]] std::expected<CpIndex, WriteError> class_reference(std::string_view name, std::string_view path);

  /// Follow bootstrap argument graphs to completion.
  /// @param path Diagnostic path of the bootstrap attribute.
  /// @return Source row ordinals in output order or a validation or capacity error.
  [[nodiscard]] std::expected<std::span<const std::size_t>, WriteError> require_bootstraps(std::string_view path);

  /// Assign slots to remaining dependencies and encode the pool, including its count.
  /// @param origin Absolute byte offset at which the pool will be inserted.
  /// @param invalid_utf8 Receives the first retained invalid modified UTF-8 diagnostic.
  /// @return Serialized pool bytes or a capacity error.
  [[nodiscard]] std::expected<std::vector<std::byte>, WriteError> finish(
      std::size_t origin, std::optional<Diagnostic>& invalid_utf8);

private:
  /// Internal identity used before assigning wire slots to constant dependencies.
  using ConstantId = std::size_t;

  /// Constant payload after replacing source indices with canonical identities.
  struct Key {
    CpTag tag; ///< Constant tag.
    std::uint8_t reference_kind = 0; ///< MethodHandle kind.
    ConstantId first = 0; ///< First dependency or output bootstrap ordinal.
    ConstantId second = 0; ///< Second dependency for entries with a pair of operands.
    std::uint64_t bits = 0; ///< Exact numeric bits.
    std::string_view text = {}; ///< Borrowed modified UTF-8 bytes.

    /// Compare canonical payloads.
    /// @return Whether every normalized payload field matches.
    bool operator==(const Key&) const = default;
  };

  /// Hash canonical payloads for interning.
  struct KeyHash {
    /// @param key Canonical constant payload.
    /// @return Hash of the normalized payload fields.
    std::size_t operator()(const Key& key) const noexcept;
  };

  /// Interned payload and its optional assigned wire slot.
  struct Constant {
    Key key; ///< Canonical constant payload.
    CpIndex index; ///< Assigned output slot or zero until assignment.
  };

  /// @param key Canonical payload.
  /// @return The existing or newly allocated canonical ID.
  ConstantId intern(const Key& key);
  /// @param value Modified UTF-8 bytes.
  /// @param path Diagnostic path.
  /// @return Canonical ID for the text or an encoding error.
  [[nodiscard]] std::expected<ConstantId, WriteError> intern_utf8(std::string_view value, std::string_view path);
  /// @param source Model pool slot.
  /// @return Canonical ID after recursively interning valid dependencies.
  [[nodiscard]] std::expected<ConstantId, WriteError> resolve(CpIndex source);
  /// @param id Canonical constant identity.
  /// @param path Diagnostic path.
  /// @return Assigned wire slot or a capacity error.
  [[nodiscard]] std::expected<CpIndex, WriteError> assign(ConstantId id, std::string_view path);
  /// @return Success or a capacity error while assigning remaining slots.
  [[nodiscard]] std::expected<void, WriteError> assign_dependencies();
  /// @param key Interned payload whose dependencies have assigned slots.
  /// @return Entry with assigned pool slots and bootstrap ordinal.
  CpEntry remapped_entry(const Key& key) const noexcept;

  const ConstantPool& pool; ///< Borrowed model pool.
  ClassVersion version; ///< Declared class version.
  bool module; ///< Whether module-only constant tags are permitted.
  std::span<const BootstrapMethod> bootstraps; ///< Borrowed model bootstrap table.
  std::pmr::unordered_map<std::size_t, ConstantId> source_ids; ///< Canonical identity of each visited model slot.
  std::pmr::list<std::pmr::string> generated_text; ///< Stable storage for generated frame constants.
  std::pmr::unordered_map<Key, ConstantId, KeyHash> keys; ///< Interned payloads eligible for sharing.
  std::pmr::vector<Constant> constants; ///< Payloads indexed by canonical identity.
  std::pmr::vector<ConstantId> output_order; ///< Canonical IDs in wire slot order.
  std::pmr::unordered_map<std::size_t, std::size_t> bootstrap_indices; ///< Output ordinals of visited bootstrap rows.
  std::pmr::vector<std::size_t> bootstrap_order; ///< Visited source bootstrap rows in output order.
  std::uint16_t next_index = 1; ///< Next unassigned wire slot.
  bool has_invalid_utf8 = false; ///< Whether an interned source entry needs a UTF-8 diagnostic.
};

} // namespace cafe::detail
