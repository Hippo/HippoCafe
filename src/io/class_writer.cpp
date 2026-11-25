/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#include <cafe/io/class_writer.hpp>

#include <limits>
#include <type_traits>
#include <utility>

#include <cafe/analysis/context.hpp>

#include "../detail/member_signature.hpp"
#include "class_format.hpp"
#include "class_writer_impl.hpp"
#include "member_keys.hpp"

namespace cafe::detail {

ClassWriter::ClassWriter(const ClassFile& model, const WriteOptions& options, analysis::AnalysisContext* analysis_context) :
    model(model), options(options), analysis_context(analysis_context), pool(model.constant_pool()), version(model.version()),
    analysis_budget(analysis_context ? analysis_context->limits().upstream_resource : std::pmr::get_default_resource(),
                    analysis_context ? analysis_context->limits().max_memory_bytes : analysis::AnalysisLimits{}.max_memory_bytes),
    stack_map_constants(pool, &analysis_budget), output(result.bytes) {
}

std::expected<WriteResult, WriteError> ClassWriter::write() try {
  if (version.major < 45 || !version.has_legal_minor())
    return write_error(WriteErrorKind::UnsupportedDialect, "class.version", "unsupported declared class version");
  if (const auto* issue = class_identity_issue(pool, version, model.access_flags(), model.this_class(), model.super_class()))
    return write_error(WriteErrorKind::InvalidClassModel, "class.header", issue);

  const auto interfaces = model.interfaces();
  if (interfaces.size() > std::numeric_limits<std::uint16_t>::max())
    return write_error(WriteErrorKind::InvalidClassModel, "interfaces", "interface count does not fit u2");
  if (is_module(version, model.access_flags())) {
    bool has_module = false;
    for (const auto& attribute : model.attributes()) {
      const auto name = pool.get_utf8(attribute.name_index());
      has_module |= name && *name == "Module";
    }
    if (!has_module || !interfaces.empty() || !model.fields().empty() || !model.methods().empty())
      return write_error(WriteErrorKind::InvalidClassModel, "class.header",
                         "ACC_MODULE requires Module and empty interface, field and method tables");
  }

  result.bytes.reserve(model.source_bytes().size());
  output.u4(CLASS_MAGIC);
  output.u2(version.minor);
  output.u2(version.major);
  if (auto written = write_constant_pool(); !written)
    return std::unexpected(std::move(written.error()));
  const auto constant_pool_end = output.size();
  output.u2(model.access_flags());
  output.u2(model.this_class().value);
  output.u2(model.super_class().value);
  output.u2(static_cast<std::uint16_t>(interfaces.size()));
  for (std::size_t i = 0; i < interfaces.size(); ++i) {
    if (!class_name(pool, interfaces[i]))
      return write_error(WriteErrorKind::InvalidConstantPoolReference, index_path("interfaces", i),
                         "interface index must name a non-array class");
    output.u2(interfaces[i].value);
  }
  if (auto written = write_members(model.fields()); !written)
    return std::unexpected(std::move(written.error()));
  if (auto written = write_members(model.methods()); !written)
    return std::unexpected(std::move(written.error()));
  if (auto written = write_attributes(model.attributes(), {AttributeContext::Class, ""}); !written)
    return std::unexpected(std::move(written.error()));
  if (const auto extra = stack_map_constants.bytes(); !extra.empty()) {
    result.bytes.insert(result.bytes.begin() + static_cast<std::ptrdiff_t>(constant_pool_end),
                        extra.begin(), extra.end());
    constexpr std::size_t CONSTANT_POOL_COUNT_OFFSET = 8;
    if (!output.patch_u2(CONSTANT_POOL_COUNT_OFFSET, stack_map_constants.count()))
      return write_error(WriteErrorKind::InvalidClassModel, "constant_pool", "could not finalize constant pool count");
    for (auto& diagnostic : result.diagnostics) {
      if (diagnostic.offset >= constant_pool_end)
        diagnostic.offset += extra.size();
    }
  }
  return std::move(result);
} catch (const BudgetExceeded&) {
  return write_error(WriteErrorKind::ResourceLimitExceeded, "class", "write analysis exceeded its memory limit");
}

template<typename Member>
std::expected<void, WriteError> ClassWriter::write_members(std::span<const Member> members) {
  constexpr bool METHOD = std::is_same_v<Member, Method>;
  constexpr std::string_view LIST = METHOD ? "methods" : "fields";
  if (members.size() > std::numeric_limits<std::uint16_t>::max())
    return write_error(WriteErrorKind::InvalidClassModel, LIST, "member count does not fit u2");
  output.u2(static_cast<std::uint16_t>(members.size()));
  std::pmr::monotonic_buffer_resource scratch;
  std::pmr::vector<MemberKey> keys(&scratch);
  if (members.size() > 1)
    keys.reserve(members.size());
  for (std::size_t i = 0; i < members.size(); ++i) {
    const auto& member = members[i];
    const auto path = index_path(LIST, i);
    const auto name = pool.get_utf8(member.name_index());
    const auto desc = pool.get_utf8(member.descriptor_index());
    if (!name || !desc)
      return write_error(WriteErrorKind::InvalidConstantPoolReference, path, "member requires valid Utf8 name and descriptor indices");
    const auto* issue = METHOD ? method_signature_issue(version, model.access_flags(), member.access_flags(), *name, *desc)
                               : field_signature_issue(*name, *desc);
    if (issue)
      return write_error(WriteErrorKind::InvalidConstantPoolReference, path, issue);
    if (members.size() > 1)
      keys.push_back({*name, *desc, i, 0});
    const bool code_required = METHOD && needs_code(member.access_flags(), *name);
    const Method* method = nullptr;
    if constexpr (METHOD) {
      method = &member;
      if (code_required && !member.code() && !member.opaque_code())
        return write_error(WriteErrorKind::InvalidClassModel, path, "concrete method lacks Code");
    }
    output.u2(member.access_flags());
    output.u2(member.name_index().value);
    output.u2(member.descriptor_index().value);
    if (auto written = write_attributes(member.attributes(),
                                        {.context = METHOD ? AttributeContext::Method : AttributeContext::Field,
                                         .path = path,
                                         .descriptor = *desc,
                                         .code_required = code_required,
                                         .is_static = is_static_method(member.access_flags(), *name),
                                         .annotation_element = METHOD && is_annotation_element(model.access_flags(), member.access_flags(), *desc),
                                         .method = method});
        !written)
      return written;
  }
  if (const auto* duplicate = find_duplicate_member(keys))
    return write_error(WriteErrorKind::InvalidClassModel, index_path(LIST, duplicate->index),
                       "duplicate member name and descriptor");
  return {};
}

std::expected<void, WriteError> ClassWriter::write_constant_pool() {
  constexpr auto MAX_COUNT = std::numeric_limits<std::uint16_t>::max();
  if (pool.size() == 0 || pool.size() > MAX_COUNT)
    return write_error(WriteErrorKind::ConstantPoolOverflow, "constant_pool", "constant pool count does not fit u2");
  std::size_t bootstrap_count = 0;
  for (const auto& attribute : model.attributes())
    if (const auto* bootstraps = attribute.get<BootstrapMethods>())
      bootstrap_count = bootstraps->methods.size();

  output.u2(static_cast<std::uint16_t>(pool.size()));
  bool invalid_utf8_reported = false;
  for (std::size_t i = 1; i < pool.size(); ++i) {
    const CpIndex index{static_cast<std::uint16_t>(i)};
    const auto fail = [i](WriteErrorKind kind, std::string_view detail) {
      return write_error(kind, index_path("constant_pool", i), detail);
    };
    const auto* entry = pool.get(index);
    if (!entry)
      return fail(WriteErrorKind::InvalidClassModel, "unexpected reserved constant-pool slot");
    const auto [form, introduced] = cp_format(entry->tag);
    if (form == CpForm::Invalid || version.major < introduced)
      return fail(WriteErrorKind::UnsupportedDialect, "constant-pool tag is not supported by the declared version");
    if (const auto* issue = cp_reference_issue(pool, *entry, version, is_module(version, model.access_flags())))
      return fail(WriteErrorKind::InvalidConstantPoolReference, issue);
    if (dynamic_constant(entry->tag) && entry->first.value >= bootstrap_count)
      return fail(WriteErrorKind::InvalidConstantPoolReference, "invalid bootstrap method index");

    output.u1(static_cast<std::uint8_t>(entry->tag));
    switch (form) {
      case CpForm::Utf8: {
        const auto text = pool.utf8_bytes(index);
        if (!text || text->size() > MAX_COUNT)
          return fail(WriteErrorKind::InvalidClassModel, "invalid Utf8 constant size");
        if (!invalid_utf8_reported && !pool.get_utf8(index)) {
          invalid_utf8_reported = true;
          result.diagnostics.push_back({Severity::Warning, DiagnosticCode::InvalidModifiedUtf8, output.size() - 1,
                                        index_path("constant_pool", i), "retained invalid modified UTF-8 bytes",
                                        RecoveryAction::RetainedRaw});
        }
        output.u2(static_cast<std::uint16_t>(text->size()));
        output.bytes(std::as_bytes(std::span(*text)));
        break;
      }
      case CpForm::Bits32:
        output.u4(static_cast<std::uint32_t>(entry->bits));
        break;
      case CpForm::Bits64:
        if (i + 1 >= pool.size() || pool.get(CpIndex{static_cast<std::uint16_t>(i + 1)}))
          return fail(WriteErrorKind::InvalidClassModel, "Long or Double is missing its reserved slot");
        output.u8(entry->bits);
        ++i;
        break;
      case CpForm::Index:
        output.u2(entry->first.value);
        break;
      case CpForm::Pair:
        output.u2(entry->first.value);
        output.u2(entry->second.value);
        break;
      case CpForm::Handle:
        output.u1(entry->reference_kind);
        output.u2(entry->first.value);
        break;
      case CpForm::Invalid:
        return fail(WriteErrorKind::InvalidClassModel, "unknown constant-pool tag");
    }
  }
  return {};
}

} // namespace cafe::detail

namespace cafe {

std::expected<WriteResult, WriteError> write_class(const ClassFile& class_file, const WriteOptions& options) {
  return detail::ClassWriter(class_file, options).write();
}

std::expected<WriteResult, WriteError> write_class(const ClassFile& class_file, const WriteOptions& options,
                                                   analysis::AnalysisContext& context) {
  return detail::ClassWriter(class_file, options, &context).write();
}

std::expected<WriteResult, WriteError> write_class_with_frames(
    const ClassFile& class_file, const std::filesystem::path& java_home, std::uint16_t target_release,
    std::span<const std::filesystem::path> dependencies, WriteOptions options) {
  const auto registration_error = [](const classpath::ResolutionError& error, const std::filesystem::path& path) {
    auto kind = WriteErrorKind::ClassResolutionFailure;
    if (error.kind == classpath::ResolutionErrorKind::ResourceLimitExceeded)
      kind = WriteErrorKind::ResourceLimitExceeded;
    else if (error.kind == classpath::ResolutionErrorKind::Cancelled)
      kind = WriteErrorKind::Cancelled;
    return WriteError{kind, (error.path.empty() ? path : error.path).string(), error.detail};
  };

  classpath::ClassPathBuilder builder;
  builder.set_target_release(target_release);
  if (auto added = builder.add_java_home(java_home); !added)
    return std::unexpected(registration_error(added.error(), java_home));
  for (const auto& path : dependencies) {
    const auto added = [&] {
      std::error_code error;
      if (std::filesystem::is_directory(path, error))
        return builder.add_directory(path);
      if (path.extension() == ".jar")
        return builder.add_jar(path);
      if (path.extension() == ".jmod")
        return builder.add_jmod(path);
      return builder.add_directory(path);
    }();
    if (!added)
      return std::unexpected(registration_error(added.error(), path));
  }
  analysis::AnalysisContext context{classpath::ClassHierarchy{classpath::ClassResolver{builder.build()}}};
  options.code_analysis = CodeAnalysisMode::ComputeFrames;
  return write_class(class_file, options, context);
}

} // namespace cafe
