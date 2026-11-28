/**
 * @file
 * @brief Rebuild a class model's constant pool and update its references transactionally.
 * @ingroup implementation
 */

#include <cafe/class_file.hpp>
#include <cafe/read_options.hpp>

#include <cassert>
#include <limits>
#include <utility>

#include "bytecode/code_access.hpp"
#include "class_pool_compaction.hpp"
#include "class_storage.hpp"
#include "io/binary_reader.hpp"
#include "io/class_writer_impl.hpp"

namespace cafe::detail {

/// Stage a compact replacement and then publish it without invalidating code handles.
class ClassPoolCompaction {
public:
  /// @param storage Mutable declaration storage.
  /// @param roots Extra constant references needed by a pending edit.
  /// @param attempt Optional retry to run before making compaction permanent.
  /// @return Number of reclaimed slots or a failure with the original class still intact.
  static std::expected<std::size_t, WriteError> run(
      ClassStorage& storage, std::span<const CpIndex> roots = {}, const CompactedPoolAttempt* attempt = nullptr) {
    BootstrapMethods* bootstraps = nullptr;
    for (auto& attribute : storage.attribute_entries) {
      if (auto* table = std::get_if<BootstrapMethods>(&attribute.payload)) {
        if (bootstraps)
          return write_error(WriteErrorKind::InvalidClassModel, "BootstrapMethods", "duplicate bootstrap table");
        bootstraps = table;
      }
    }
    return ClassPoolCompaction(storage, bootstraps).compact_model(roots, attempt);
  }

private:
  /// @param storage Mutable declaration storage.
  /// @param bootstraps Typed BootstrapMethods payload, if present.
  ClassPoolCompaction(ClassStorage& storage, BootstrapMethods* bootstraps)
      : storage(storage), bootstraps(bootstraps),
        compact(storage.pool, storage.version, is_module(storage.version, storage.flags),
                bootstraps ? std::span<const BootstrapMethod>(bootstraps->methods) : std::span<const BootstrapMethod>{},
                std::pmr::get_default_resource()) {
  }

  /// @param roots Extra constant references needed by a pending edit.
  /// @param attempt Optional retry to run before making compaction permanent.
  /// @return Number of reclaimed slots, or a compaction error.
  std::expected<std::size_t, WriteError> compact_model(
      std::span<const CpIndex> roots, const CompactedPoolAttempt* attempt) {
    // Reserve narrow operands even when a method has a wide encoding hint.
    // A caller-supplied Code layout may have been computed with hints disabled.
    if (auto result = compact.reserve_narrow_constants(storage.method_entries, false); !result)
      return std::unexpected(result.error());
    if (auto result = collect_references(); !result)
      return std::unexpected(result.error());
    for (auto root : roots) {
      if (const auto index = compact.index(root); !index)
        return std::unexpected(index.error());
    }
    const auto live_bootstraps = compact.require_bootstraps("BootstrapMethods");
    if (!live_bootstraps)
      return std::unexpected(live_bootstraps.error());

    ConstantPool replacement(&storage.budget);
    if (auto built = compact.build_model(replacement); !built)
      return std::unexpected(built.error());
    const auto reclaimed = storage.pool.size() - replacement.size();
    if (reclaimed == 0 && (attempt || !bootstraps || live_bootstraps->size() == bootstraps->methods.size()))
      return reclaimed;

    std::optional<std::pmr::vector<BootstrapMethod>> new_bootstraps;
    if (bootstraps) {
      new_bootstraps.emplace(bootstraps->methods.get_allocator());
      new_bootstraps->reserve(live_bootstraps->size());
      for (const auto row : *live_bootstraps) {
        const auto& source = bootstraps->methods[row];
        auto& dest = new_bootstraps->emplace_back(BootstrapMethod{
            compact.mapped(source.method_handle), std::pmr::vector<CpIndex>(source.arguments.get_allocator())});
        dest.arguments.reserve(source.arguments.size());
        for (const auto argument : source.arguments)
          dest.arguments.push_back(compact.mapped(argument));
      }
    }

    std::vector<CpIndex> mapping(storage.pool.size());
    for (std::size_t i = 1; i < mapping.size(); ++i)
      mapping[i] = compact.mapped(CpIndex{static_cast<std::uint16_t>(i)});
    stage_historical_references(mapping);
    decltype(storage.constant_lookup) old_lookup(&storage.budget);
    std::size_t indexed_constants = 1;

    // Swapping holds the original pool (including borrowed strings) alive
    // throughout the retry. The same swaps restore it if the edit fails.
    const auto exchange = [&]() noexcept {
      for (auto& [slot, index] : references)
        std::swap(*slot, index);
      for (auto& update : attribute_updates) {
        auto& attribute = update.class_index ? storage.attribute_entries[*update.class_index] : *update.attribute;
        std::swap(attribute.name, update.name);
        if (update.constant) {
          auto* reference = scalar_reference(attribute);
          assert(reference);
          std::swap(*reference, *update.constant);
        }
        if (update.raw) {
          auto* raw = std::get_if<RawAttribute>(&attribute.payload);
          assert(raw);
          std::swap(*raw, *update.raw);
        }
      }
      for (auto& update : codes) {
        BytecodeAccess::swap_constant_instructions(update.code->instructions, update.instructions);
        if (update.code->original)
          std::swap(update.code->original->pool_remapped, update.pool_remapped);
      }
      for (auto& [slot, instruction] : historical_instructions)
        std::swap(*slot, instruction);
      if (new_bootstraps)
        bootstraps->methods.swap(*new_bootstraps);
      storage.pool.swap_contents(replacement);
      storage.constant_lookup.swap(old_lookup);
      std::swap(storage.indexed_constants, indexed_constants);
    };

    exchange();
    if (attempt) {
      try {
        if (!(*attempt)(mapping)) {
          exchange();
          return 0;
        }
      } catch (...) {
        exchange();
        throw;
      }
    }
    storage.declaration_stamp = next_declaration_revision();
    return reclaimed;
  }

  using Status = std::expected<void, WriteError>; ///< Successful staging or a structured failure.

  /// Declaration or handler reference to exchange during publication and rollback.
  struct Reference {
    CpIndex* slot; ///< Address of the existing model reference.
    CpIndex index; ///< Assigned replacement index or saved original on rollback.
  };

  /// Stage an attribute name and any changed parts of its payload.
  struct AttributeUpdate {
    Attribute* attribute; ///< Address of a member or Code attribute.
    std::optional<std::size_t> class_index; ///< Ordinal of a class attribute, since a bootstrap edit may move the table.
    CpIndex name; ///< Replacement name index.
    std::optional<CpIndex> constant; ///< Replacement typed reference for scalar payloads.
    std::optional<RawAttribute> raw; ///< Remapped bytes with original provenance, if changed.
  };

  /// Stage a method's edited operands and preserve its source-encoding check.
  struct CodeUpdate {
    CodeAttribute* code; ///< Live Code attribute.
    std::vector<InstructionEntry> instructions; ///< Constant instructions to exchange by handle.
    bool pool_remapped = true; ///< State to swap into retained Code provenance.
  };

  /// @param attribute Attribute whose payload may hold exactly one constant reference.
  /// @return The reference in a scalar typed payload or nullptr for other alternatives.
  static CpIndex* scalar_reference(Attribute& attribute) noexcept {
    if (auto* source = std::get_if<SourceFile>(&attribute.payload))
      return &source->file_name;
    if (auto* signature = std::get_if<Signature>(&attribute.payload))
      return &signature->signature;
    if (auto* value = std::get_if<ConstantValue>(&attribute.payload))
      return &value->constant;
    return nullptr;
  }

  /// @return Success after collecting public declarations, instructions and attributes.
  Status collect_references() {
    if (auto status = stage(storage.this_class); !status)
      return status;
    if (auto status = stage(storage.super_class); !status)
      return status;
    for (auto& index : storage.interface_entries) {
      if (auto status = stage(index); !status)
        return status;
    }
    if (auto status = members(std::span(storage.field_entries), AttributeContext::Field, "fields"); !status)
      return status;
    if (auto status = members(std::span(storage.method_entries), AttributeContext::Method, "methods"); !status)
      return status;
    return attributes(storage.attribute_entries, AttributeContext::Class, "class");
  }

  /// @tparam Member Field or Method declaration.
  /// @param entries Declarations to visit.
  /// @param context Attribute context of the declarations.
  /// @param path Diagnostic list name.
  /// @return Success or a reference or attribute error.
  template<class Member>
  Status members(std::span<Member> entries, AttributeContext context, std::string_view path) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
      auto& member = entries[i];
      if (auto status = stage(member.name); !status)
        return status;
      if (auto status = stage(member.descriptor); !status)
        return status;
      if (auto status = attributes(member.attribute_entries, context, index_path(path, i)); !status)
        return status;
    }
    return {};
  }

  /// @param slot Model reference to record for replacement.
  /// @return Success or a constant-reference error.
  Status stage(CpIndex& slot) {
    const auto index = compact.index(slot);
    if (!index)
      return std::unexpected(index.error());
    if (*index != slot)
      references.push_back({&slot, *index});
    return {};
  }

  /// @param entries Model attributes to collect.
  /// @param context Location in the class model.
  /// @param path Diagnostic path of the surrounding declaration or Code attribute.
  /// @return Success or an unsupported attribute or invalid reference error.
  Status attributes(std::span<Attribute> entries, AttributeContext context, std::string_view path) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
      auto& attribute = entries[i];
      const auto name = storage.pool.get_utf8(attribute.name);
      auto location = std::string(path) + "." + index_path("attributes", i);
      if (!name)
        return write_error(WriteErrorKind::InvalidConstantPoolReference, location, "attribute name must refer to valid Utf8");
      location += ".";
      location += *name;
      const auto mapped = compact.index(attribute.name);
      if (!mapped)
        return std::unexpected(mapped.error());
      AttributeUpdate update{&attribute, context == AttributeContext::Class ? std::optional{i} : std::nullopt,
                             *mapped, std::nullopt, std::nullopt};
      if (auto* raw = std::get_if<RawAttribute>(&attribute.payload)) {
        if (auto status = raw_attribute(*raw, find_attribute(*name), context, location, update.raw); !status)
          return status;
      } else if (auto* code = attribute.mutable_code()) {
        if (auto status = code_attribute(*code, location); !status)
          return status;
      } else if (const auto* scalar = scalar_reference(attribute)) {
        const auto index = compact.index(*scalar);
        if (!index)
          return std::unexpected(index.error());
        update.constant = *index;
      } else if (auto* exceptions = std::get_if<Exceptions>(&attribute.payload)) {
        for (auto& type : exceptions->types) {
          if (auto status = stage(type); !status)
            return status;
        }
      } else if (const auto* table = std::get_if<BootstrapMethods>(&attribute.payload)) {
        if (table != bootstraps)
          return write_error(WriteErrorKind::InvalidClassModel, location, "BootstrapMethods must belong to the class");
      } else if (attribute.get<OpaqueCode>()) {
        return write_error(WriteErrorKind::UnsupportedOpaqueCode, location, "opaque Code prevents pool compaction");
      } else if (!attribute.get<Synthetic>() && !attribute.get<Deprecated>()) {
        return write_error(WriteErrorKind::UnsafeOpaqueAttribute, location, "unsupported attribute prevents pool compaction");
      }
      attribute_updates.push_back(std::move(update));
    }
    return {};
  }

  /// @param code Method body to collect.
  /// @param path Diagnostic Code path.
  /// @return Success or an invalid operand or attribute error.
  Status code_attribute(CodeAttribute& code, std::string_view path) {
    auto& update = codes.emplace_back(CodeUpdate{&code, {}});
    for (const auto& entry : code.instructions.instructions()) {
      if (const auto index = entry.instruction.constant_index()) {
        const auto mapped = compact.index(*index);
        if (!mapped)
          return std::unexpected(mapped.error());
        if (*mapped != *index)
          update.instructions.push_back({entry.id, BytecodeAccess::with_constant_index(entry.instruction, *mapped)});
      }
    }
    for (auto& handler : code.handlers) {
      if (auto status = stage(handler.catch_type); !status)
        return status;
    }
    return attributes(code.nested_attributes, AttributeContext::Code, path);
  }

  /// @param raw Attribute bytes to scan for pool references.
  /// @param spec Format definition, or null for an unknown attribute.
  /// @param context Location in the class model.
  /// @param path Diagnostic attribute path.
  /// @param replacement Receives a replacement body if an index changed.
  /// @return Success or an error that prevents safe compaction.
  Status raw_attribute(const RawAttribute& raw, const AttributeSpec* spec, AttributeContext context, std::string_view path,
                       std::optional<RawAttribute>& replacement) {
    if (!spec)
      return write_error(WriteErrorKind::UnsafeOpaqueAttribute, path, "unknown attribute prevents pool compaction");
    const auto bytes = raw.bytes(storage.source);
    if (!bytes)
      return write_error(WriteErrorKind::InvalidClassModel, path, "raw attribute slice is out of bounds");

    struct Rewrite {
      CompactConstantPool& compact;
      std::span<const std::byte> original;
      std::vector<std::byte> bytes;
      std::optional<WriteError> error;
    } rewrite{compact, *bytes, {}, {}};
    const ConstantIndexVisitor references{&rewrite, [](void* state, std::size_t offset, CpIndex source) {
      auto& rewrite = *static_cast<Rewrite*>(state);
      const auto index = rewrite.compact.index(source);
      if (!index) {
        rewrite.error = index.error();
        return false;
      }
      if (source == *index)
        return true;
      if (rewrite.bytes.empty())
        rewrite.bytes.assign(rewrite.original.begin(), rewrite.original.end());
      return BinaryWriter(rewrite.bytes).patch_u2(offset, index->value);
    }};
    BinaryReader reader(*bytes);
    ReadLimits limits;
    limits.max_annotation_depth = bytes->size();
    const auto checked = validate_opaque_attribute(reader, *spec, context, storage.pool, limits, path,
                                                   nullptr, std::pmr::get_default_resource(), {}, references, true);
    if (rewrite.error)
      return std::unexpected(std::move(*rewrite.error));
    if (!checked) {
      return write_error(checked.error().kind == ReadErrorKind::UnsupportedDialect ? WriteErrorKind::UnsafeOpaqueAttribute
                                                                                    : WriteErrorKind::InvalidClassModel,
                         path, checked.error().detail);
    }
    if (!rewrite.bytes.empty()) {
      const auto* source = raw.source_slice();
      replacement = source ? RawAttribute(*source, std::move(rewrite.bytes)) : RawAttribute(std::move(rewrite.bytes));
    }
    return {};
  }

  /// Prepare the remapping of historical metadata used in equality comparisons.
  /// @param mapping Assigned indices of live constants.
  void stage_historical_references(std::span<const CpIndex> mapping) {
    // Use an invalid index for collected references so a new operand or
    // catch-all handler cannot accidentally match the old value.
    constexpr CpIndex COLLECTED{std::numeric_limits<std::uint16_t>::max()};
    const auto former_reference = [&](CpIndex index) {
      if (!index)
        return CpIndex{};
      if (index.value < mapping.size() && mapping[index.value])
        return mapping[index.value];
      return COLLECTED;
    };
    for (const auto& update : codes) {
      auto& code = *update.code;
      for (auto& boundary : code.original_boundaries) {
        auto& instruction = boundary.original_instruction;
        if (const auto index = instruction.constant_index()) {
          const auto mapped = former_reference(*index);
          if (mapped != *index)
            historical_instructions.emplace_back(&instruction, BytecodeAccess::with_constant_index(instruction, mapped));
        }
      }
      if (code.original_handlers) {
        for (auto& handler : *code.original_handlers) {
          const auto mapped = former_reference(handler.catch_type);
          if (mapped != handler.catch_type)
            references.push_back({&handler.catch_type, mapped});
        }
      }
    }
  }

  ClassStorage& storage; ///< Model whose pool and references will be replaced.
  BootstrapMethods* bootstraps; ///< Typed class bootstrap table, if present.
  CompactConstantPool compact; ///< Interned constants and assigned model indices.
  std::vector<Reference> references; ///< Staged direct reference updates.
  std::vector<AttributeUpdate> attribute_updates; ///< Staged attribute updates.
  std::vector<CodeUpdate> codes; ///< Staged constant-bearing instructions.
  std::vector<std::pair<Instruction*, Instruction>> historical_instructions; ///< Staged metadata comparisons.
};

void retry_with_compact_pool(ClassStorage& storage, std::span<const CpIndex> roots,
                             const CompactedPoolAttempt& attempt) {
  // The caller keeps the first edit error if compaction cannot run safely.
  (void)ClassPoolCompaction::run(storage, roots, &attempt);
}

} // namespace cafe::detail

namespace cafe {

std::expected<std::size_t, WriteError> ClassFile::compact_constant_pool() {
  return detail::ClassPoolCompaction::run(*storage);
}

} // namespace cafe
