/**
 * @file
 * @brief Class source snapshots, metadata resolution, and reference-type relationships.
 * @ingroup implementation
 */

#include "classpath_detail.hpp"

#include <utility>

#include <algorithm>
#include <string>
#include <unordered_set>

#include <cafe/class_file.hpp>

#include "detail/descriptor.hpp"
#include "io/binary_reader.hpp"
#include "io/class_format.hpp"
#include "model/modified_utf8.hpp"

namespace cafe::classpath::detail {
namespace {

/**
 * @brief Append a validated Unicode scalar in ordinary UTF-8 form.
 * @param output Caller-owned output byte container.
 * @param unit Validated Unicode scalar/code unit to encode.
 * @ingroup implementation
 */
void append_utf8(std::string& output, std::uint32_t unit) {
  if (unit < 0x80) {
    output += static_cast<char>(unit);
  } else if (unit < 0x800) {
    output += static_cast<char>(0xc0 | (unit >> 6));
    output += static_cast<char>(0x80 | (unit & 0x3f));
  } else if (unit < 0x10000) {
    output += static_cast<char>(0xe0 | (unit >> 12));
    output += static_cast<char>(0x80 | ((unit >> 6) & 0x3f));
    output += static_cast<char>(0x80 | (unit & 0x3f));
  } else {
    output += static_cast<char>(0xf0 | (unit >> 18));
    output += static_cast<char>(0x80 | ((unit >> 12) & 0x3f));
    output += static_cast<char>(0x80 | ((unit >> 6) & 0x3f));
    output += static_cast<char>(0x80 | (unit & 0x3f));
  }
}

/**
 * @brief Minimal constant-pool information required for class/member metadata scanning.
 * @ingroup implementation
 */
struct HeaderConstant {
  CpTag tag = CpTag::Reserved; ///< Class-file constant tag selecting the entry payload form.
  std::uint16_t index = 0; ///< Referenced UTF-8 constant slot for a Class entry; zero for other stored tags.
  std::string_view text; ///< Borrowed modified UTF-8 constant text.
};

/**
 * @brief Validate class identity and copy a bounded set of hierarchy metadata.
 * @param name Borrowed JVM name in modified UTF-8.
 * @param super_name Direct superclass internal name; nullopt denotes no superclass.
 * @param flags JVM access bit mask for the declaration.
 * @param version Exact JVM class-file major and minor version.
 * @param interface_count Number of direct interface references to visit.
 * @param next_interface Callback producing the next direct-interface name/reference.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @param source Provenance copied into returned metadata or errors.
 * @return Owned ClassInfo, or a malformed/oversized metadata failure.
 * @ingroup implementation
 */
template<typename Interface>
std::expected<ClassInfo, ResolutionError> package_metadata(
    std::string_view name, std::optional<std::string_view> super_name, std::uint16_t flags,
    ClassVersion version, std::size_t interface_count, Interface next_interface,
    const ClassPathLimits& limits, const SourceProvenance& source) {
  const auto too_large = [&] {
    return fail(ResolutionErrorKind::ResourceLimitExceeded, name.substr(0, limits.max_name_bytes), source,
                "class metadata exceeds its configured byte or interface limit");
  };
  const auto malformed = [&](std::string_view reason) {
    return fail(ResolutionErrorKind::MalformedClassMetadata, name.substr(0, limits.max_name_bytes), source, reason);
  };
  const auto check = [&](std::string_view type) -> std::expected<void, ResolutionError> {
    if (const auto checked = resource_name(type, limits); !checked)
      return checked.error().kind == ResolutionErrorKind::ResourceLimitExceeded
                 ? too_large()
                 : malformed(checked.error().detail);
    return {};
  };
  if (auto valid = check(name); !valid)
    return std::unexpected(std::move(valid.error()));
  if (super_name)
    if (auto valid = check(*super_name); !valid)
      return std::unexpected(std::move(valid.error()));
  ClassInfo result{std::string(name), {}, {}, flags, version, source, {}};
  if (super_name)
    result.super_name.emplace(*super_name);
  auto size = metadata_size(result);
  constexpr std::size_t INTERFACE_COST = sizeof(std::string) + sizeof(std::string_view) + 32;
  if (interface_count > limits.max_hierarchy_edges || size > limits.max_class_metadata_bytes ||
      interface_count > (limits.max_class_metadata_bytes - size) / INTERFACE_COST)
    return too_large();
  size += interface_count * INTERFACE_COST;

  std::unordered_set<std::string_view> seen;
  seen.reserve(interface_count);
  result.interfaces.reserve(interface_count);
  for (std::size_t i = 0; i < interface_count; ++i) {
    if (limits.cancellation.stop_requested())
      return fail(ResolutionErrorKind::Cancelled, name, source, "class metadata scan cancelled");
    const auto type = next_interface(i);
    if (!type)
      return malformed("interface has an invalid constant-pool reference");
    if (type->size() > limits.max_class_metadata_bytes - size)
      return too_large();
    if (auto valid = check(*type); !valid)
      return std::unexpected(std::move(valid.error()));
    if (!seen.insert(*type).second)
      return malformed("duplicate direct interface");
    result.interfaces.emplace_back(*type);
    size += type->size();
  }
  return result;
}

/**
 * @brief Bounded class header and minimal constant table borrowing names from the input buffer.
 * @ingroup implementation
 */
struct Header {
  ClassVersion version; ///< Exact class-file major and minor version.
  /**
   * @brief Minimal parsed constant records; UTF-8 payloads borrow the input buffer.
   */
  std::vector<HeaderConstant> constants;

  /**
   * @brief Resolve a minimal-header constant as UTF-8 text.
   * @param index One-based slot in the minimally decoded constant pool.
   * @return A borrowed view, or nullopt for an invalid index/tag/encoding.
   * @ingroup implementation
   */
  [[nodiscard]] std::optional<std::string_view> utf8(std::uint16_t index) const {
    if (index >= constants.size() || constants[index].tag != CpTag::Utf8)
      return std::nullopt;
    return constants[index].text;
  }

  /**
   * @brief Resolve a Class constant through its validated UTF-8 name.
   * @param index One-based slot in the minimally decoded constant pool.
   * @return A borrowed class-name view, or nullopt for an invalid reference.
   * @ingroup implementation
   */
  [[nodiscard]] std::optional<std::string_view> class_name(std::uint16_t index) const {
    if (index >= constants.size() || constants[index].tag != CpTag::Class)
      return std::nullopt;
    return utf8(constants[index].index);
  }
};

/**
 * @brief Scan only the class header and constant information needed for metadata lookup.
 * @param reader Bounded input cursor advanced by this operation.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @param source Provenance copied into returned metadata or errors.
 * @return A bounded header view, or a framing/version/constant error.
 * @ingroup implementation
 */
std::expected<Header, ResolutionError> scan_constant_pool(
    cafe::detail::BinaryReader& reader, const ClassPathLimits& limits, const SourceProvenance& source) {
  constexpr auto MALFORMED = ResolutionErrorKind::MalformedClassMetadata;
  const auto malformed = [&](std::string_view cause) {
    return fail(MALFORMED, {}, source, cause);
  };
  if (reader.remaining() > limits.max_class_bytes)
    return fail(ResolutionErrorKind::EntryTooLarge, {}, source, "class entry exceeds its configured byte limit");

  const auto magic = reader.u4();
  const auto minor = reader.u2();
  const auto major = reader.u2();
  const auto count = reader.u2();
  if (magic != CLASS_MAGIC || !minor || !major || !count || *count == 0)
    return malformed("missing class-file magic, version or constant pool");
  const ClassVersion version{*major, *minor};
  if (*major < 45 || !version.has_legal_minor())
    return fail(ResolutionErrorKind::UnsupportedClassVersion, {}, source, "unsupported class-file version");
  if (*count - 1u > reader.remaining() / 3)
    return malformed("constant-pool count exceeds remaining class bytes");

  std::vector<HeaderConstant> constants(*count);
  for (std::size_t i = 1; i < constants.size(); ++i) {
    if (limits.cancellation.stop_requested())
      return fail(ResolutionErrorKind::Cancelled, {}, source, "metadata scan cancelled");
    const auto tag_byte = reader.u1();
    if (!tag_byte)
      return malformed("truncated constant-pool tag");
    auto& constant = constants[i];
    const auto tag = static_cast<CpTag>(*tag_byte);
    constant.tag = tag;
    const auto [form, introduced] = cafe::detail::cp_format(tag);
    if (form == cafe::detail::CpForm::Invalid || version.major < introduced)
      return malformed("illegal constant-pool tag for declared class version");
    switch (form) {
      case cafe::detail::CpForm::Utf8: {
        const auto length = reader.u2();
        const auto data = length ? reader.take_slice(*length) : std::nullopt;
        if (!data)
          return malformed("truncated UTF-8 constant");
        constant.text = {reinterpret_cast<const char*>(data->data()), data->size()};
        break;
      }
      case cafe::detail::CpForm::Index: {
        const auto index = reader.u2();
        if (!index)
          return malformed("truncated constant-pool reference");
        constant.index = *index;
        break;
      }
      case cafe::detail::CpForm::Bits64:
        if (i + 1 >= constants.size() || !reader.take_slice(8))
          return malformed("truncated long or double constant");
        ++i;
        break;
      case cafe::detail::CpForm::Bits32:
      case cafe::detail::CpForm::Pair:
        if (!reader.take_slice(4))
          return malformed("truncated constant-pool entry");
        break;
      case cafe::detail::CpForm::Handle:
        if (!reader.take_slice(3))
          return malformed("truncated method handle constant");
        break;
      case cafe::detail::CpForm::Invalid:
        return malformed("invalid constant-pool tag");
    }
  }

  return Header{version, std::move(constants)};
}

} // namespace

std::expected<std::string, ResolutionError> resource_name(std::string_view name, const ClassPathLimits& limits) {
  const auto invalid = [&] {
    return fail(ResolutionErrorKind::InvalidName, name, {}, "expected a path-safe JVM internal class name");
  };
  if (name.size() > limits.max_name_bytes || limits.max_path_depth == 0 ||
      static_cast<std::size_t>(std::ranges::count(name, '/')) >= limits.max_path_depth)
    return fail(ResolutionErrorKind::ResourceLimitExceeded, name.substr(0, limits.max_name_bytes), {},
                "class name exceeds the configured length or depth");
  if (!cafe::detail::valid_class_name(name) || name.find_first_of("\\:") != std::string_view::npos)
    return invalid();

  // The class model uses modified UTF-8. Directory and JAR entry names use
  // ordinary UTF-8, including one four-byte sequence per supplementary character.
  std::string result;
  result.reserve(name.size());
  std::size_t cursor = 0;
  while (cursor < name.size()) {
    char16_t unit = 0;
    if (!cafe::detail::next_modified_utf8(name, cursor, unit) || unit == 0)
      return invalid();
    if (unit >= 0xd800 && unit <= 0xdbff) {
      char16_t low = 0;
      if (cursor == name.size() || !cafe::detail::next_modified_utf8(name, cursor, low) ||
          low < 0xdc00 || low > 0xdfff)
        return invalid();
      append_utf8(result, 0x10000u + ((unit - 0xd800u) << 10) + (low - 0xdc00u));
    } else if (unit >= 0xdc00 && unit <= 0xdfff) {
      return invalid();
    } else {
      append_utf8(result, unit);
    }
  }
  return result;
}

std::expected<ClassInfo, ResolutionError> read_class_metadata(
    std::span<const std::byte> bytes, const ClassPathLimits& limits, const SourceProvenance& source) {
  cafe::detail::BinaryReader reader(bytes);
  auto header = scan_constant_pool(reader, limits, source);
  if (!header)
    return std::unexpected(std::move(header.error()));
  const auto malformed = [&](std::string_view cause) {
    return fail(ResolutionErrorKind::MalformedClassMetadata, {}, source, cause);
  };
  const auto flags = reader.u2();
  const auto self = reader.u2();
  const auto parent = reader.u2();
  const auto interface_count = reader.u2();
  if (!flags || !self || !parent || !interface_count || *interface_count > reader.remaining() / 2)
    return malformed("truncated class header or interfaces table");
  const auto name = header->class_name(*self);
  const auto super_name = header->class_name(*parent);
  if (!name || (*parent != 0 && !super_name))
    return malformed("this_class or super_class has an invalid constant-pool reference");
  return package_metadata(*name, super_name, *flags, header->version, *interface_count, [&](std::size_t) { return header->class_name(*reader.u2()); }, limits, source);
}

std::expected<ClassInfo, ResolutionError> copy_class_metadata(const ClassFile& file, const ClassPathLimits& limits) {
  const auto& pool = file.constant_pool();
  const auto name = cafe::detail::class_name(pool, file.this_class());
  const auto super = cafe::detail::class_name(pool, file.super_class());
  const SourceProvenance source;
  if (!name || (file.super_class() && !super))
    return fail(ResolutionErrorKind::MalformedClassMetadata, {}, source, "this_class or super_class has an invalid constant-pool reference");
  const auto version = file.version();
  if (version.major < 45 || !version.has_legal_minor())
    return fail(ResolutionErrorKind::UnsupportedClassVersion, name->substr(0, limits.max_name_bytes),
                source, "unsupported class-file version");
  const auto interfaces = file.interfaces();
  return package_metadata(*name, super, file.access_flags(), version, interfaces.size(), [&](std::size_t position) { return cafe::detail::class_name(pool, interfaces[position]); }, limits, source);
}

std::expected<void, ResolutionError> check_class_release(const ClassInfo& info, std::uint16_t target_release) {
  const auto fits = [&](std::uint16_t release) {
    const unsigned major = release + 44u;
    return info.version.major <= major &&
           (info.version.major < 56 || info.version.minor != 65535 || info.version.major == major);
  };
  if ((target_release != 0 && !fits(target_release)) ||
      (info.source.release && !fits(*info.source.release)))
    return fail(ResolutionErrorKind::UnsupportedClassVersion, info.name, info.source,
                "class-file version exceeds its target release or versioned JAR directory");
  return {};
}

std::size_t metadata_size(const ClassInfo& info) {
  const auto provenance_size = [](const SourceProvenance& source) {
    return source.entry.size() + source.path.native().size() * sizeof(std::filesystem::path::value_type);
  };
  std::size_t size = sizeof(info) + info.name.size() + (info.super_name ? info.super_name->size() : 0) +
                     provenance_size(info.source);
  for (const auto& type : info.interfaces)
    size += sizeof(type) + type.size();
  for (const auto& shadow : info.shadowed_definitions)
    size += sizeof(shadow) + provenance_size(shadow);
  return size;
}

namespace {

/**
 * @brief Compare a member by its exact name and descriptor.
 * @param member Member declaration whose name and descriptor form the lookup key.
 * @return A borrowed pair used for sorting and binary search.
 * @ingroup implementation
 */
auto member_key(const MemberDecl& member) {
  return std::pair{std::string_view(member.name), std::string_view(member.descriptor)};
}

/**
 * @brief Borrowed declaration identity and access bits obtained during a metadata-only scan.
 * @ingroup implementation
 */
struct MemberHeader {
  std::optional<std::string_view> name; ///< JVM internal or declaration name in modified UTF-8.
  std::optional<std::string_view> descriptor; ///< JVM field or method descriptor.
  std::uint16_t flags; ///< JVM access bit mask.
};

/**
 * @brief Validate, copy, and sort a bounded declaration table while rejecting duplicates.
 * @param output Owned declaration tables and byte accounting populated by this scan.
 * @param method True selects method declarations; false selects fields.
 * @param count Number of declarations to read from this table.
 * @param read_member Callback reading one bounded field or method header.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @param selected Previously selected class identity against which metadata stability is checked.
 * @return Success or a malformed declaration/resource failure.
 * @ingroup implementation
 */
template<typename ReadMember>
std::expected<void, ResolutionError> read_members(
    DeclaredMembers& output, bool method, std::size_t count, ReadMember read_member,
    const ClassPathLimits& limits, const ClassInfo& selected) {
  auto& table = method ? output.methods : output.fields;
  auto& bytes = output.byte_size;
  const auto malformed = [&](std::string_view reason) {
    return fail(ResolutionErrorKind::MalformedClassMetadata, selected.name, selected.source, reason);
  };
  const auto too_large = [&] {
    return fail(ResolutionErrorKind::ResourceLimitExceeded, selected.name, selected.source,
                "member declarations exceed their configured byte limit");
  };
  if (bytes > limits.max_class_member_bytes || count > (limits.max_class_member_bytes - bytes) / sizeof(MemberDecl))
    return too_large();
  bytes += count * sizeof(MemberDecl);
  table.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    if (limits.cancellation.stop_requested())
      return fail(ResolutionErrorKind::Cancelled, selected.name, selected.source, "member scan cancelled");
    auto entry = read_member(i);
    if (!entry)
      return malformed("invalid member header or attribute envelope");
    const auto [name, descriptor, flags] = *entry;
    if (!name || !descriptor || !cafe::detail::valid_modified_utf8(*name) ||
        !cafe::detail::valid_modified_utf8(*descriptor) ||
        !(method ? cafe::detail::valid_method_name(*name) && cafe::detail::method_parameter_slots(*descriptor).has_value()
                 : cafe::detail::valid_unqualified_name(*name) && cafe::detail::valid_field_descriptor(*descriptor)))
      return malformed("invalid member name or descriptor");
    if (name->size() > limits.max_class_member_bytes - bytes ||
        descriptor->size() > limits.max_class_member_bytes - bytes - name->size())
      return too_large();
    bytes += name->size() + descriptor->size();
    table.push_back({std::string(*name), std::string(*descriptor), flags});
  }
  std::ranges::sort(table, {}, member_key);
  if (std::ranges::adjacent_find(table, {}, member_key) != table.end())
    return malformed("duplicate member name and descriptor");
  return {};
}

} // namespace

// The interface table byte length is checked before the fixed-width reads below.
std::expected<DeclaredMembers, ResolutionError> read_class_members(
    std::span<const std::byte> bytes, const ClassPathLimits& limits, const ClassInfo& selected) {
  const auto malformed = [&](std::string_view reason) {
    return fail(ResolutionErrorKind::MalformedClassMetadata, selected.name, selected.source, reason);
  };
  cafe::detail::BinaryReader reader(bytes);
  auto header = scan_constant_pool(reader, limits, selected.source);
  if (!header)
    return std::unexpected(std::move(header.error()));
  const auto flags = reader.u2();
  const auto self = reader.u2();
  const auto parent = reader.u2();
  const auto interfaces = reader.u2();
  if (!flags || !self || !parent || !interfaces || *interfaces > reader.remaining() / 2)
    return malformed("truncated member owner's class header");
  if (header->version != selected.version || *flags != selected.access_flags ||
      header->class_name(*self) != selected.name || header->class_name(*parent) != selected.super_name ||
      *interfaces != selected.interfaces.size())
    return malformed("member owner's class metadata changed since resolution");
  for (const auto& expected : selected.interfaces)
    if (header->class_name(*reader.u2()) != expected) // NOLINT(bugprone-unchecked-optional-access)
      return malformed("member owner's interface metadata changed since resolution");

  DeclaredMembers result{{}, {}, sizeof(DeclaredMembers)};
  for (bool method : {false, true}) {
    const auto count = reader.u2();
    if (!count || *count > reader.remaining() / 8)
      return malformed("member table exceeds available class bytes");
    const auto member = [&](std::size_t) -> std::optional<MemberHeader> {
      const auto access = reader.u2();
      const auto name = reader.u2();
      const auto descriptor = reader.u2();
      const auto attributes = reader.u2();
      if (!access || !name || !descriptor || !attributes || *attributes > reader.remaining() / 6)
        return std::nullopt;
      for (std::size_t i = 0; i < *attributes; ++i) {
        if (!reader.u2())
          return std::nullopt;
        const auto length = reader.u4();
        if (!length || !reader.take_slice(*length))
          return std::nullopt;
      }
      return MemberHeader{header->utf8(*name), header->utf8(*descriptor), *access};
    };
    if (auto read = read_members(result, method, *count, member, limits, selected); !read)
      return std::unexpected(std::move(read.error()));
  }
  return result;
}

std::expected<DeclaredMembers, ResolutionError> copy_class_members(
    const ClassFile& file, const ClassPathLimits& limits, const ClassInfo& selected) {
  const auto& pool = file.constant_pool();
  DeclaredMembers result{{}, {}, sizeof(DeclaredMembers)};
  const auto copy = [&](auto table, bool method) {
    const auto member = [&](std::size_t position) -> std::optional<MemberHeader> {
      const auto& value = table[position];
      const auto name = pool.get_utf8(value.name_index());
      const auto descriptor = pool.get_utf8(value.descriptor_index());
      if (!name || !descriptor)
        return std::nullopt;
      return MemberHeader{*name, *descriptor, value.access_flags()};
    };
    return read_members(result, method, table.size(), member, limits, selected);
  };
  if (auto fields = copy(file.fields(), false); !fields)
    return std::unexpected(std::move(fields.error()));
  if (auto methods = copy(file.methods(), true); !methods)
    return std::unexpected(std::move(methods.error()));
  return result;
}

std::optional<std::uint16_t> DeclaredMembers::flags(std::string_view name, std::string_view descriptor, bool method) const {
  const auto& table = method ? methods : fields;
  const auto key = std::pair{name, descriptor};
  const auto entry = std::ranges::lower_bound(table, key, {}, member_key);
  return entry == table.end() || member_key(*entry) != key ? std::nullopt : std::optional{entry->flags};
}

} // namespace cafe::classpath::detail
