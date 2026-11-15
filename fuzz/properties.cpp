#include "properties.hpp"

#include <algorithm>
#include <cstdlib>
#include <span>

#include <hippo/cafe.hpp>

#include "archive/zip_archive.hpp"
#include "classpath/class_path_storage.hpp"
#include "classpath/classpath_detail.hpp"

namespace cafe::fuzz {
namespace {

ReadOptions read_options() {
  ReadOptions options;
  options.limits.max_input_bytes = MAX_INPUT_BYTES;
  options.limits.max_total_allocated_bytes = 16 * 1024 * 1024;
  options.limits.max_attributes = 10'000;
  options.limits.max_diagnostics = 64;
  options.allow_opaque_code = true;
  return options;
}

} // namespace

void exercise_reader(std::string_view input) {
  const auto bytes = std::as_bytes(std::span(input));
  auto options = read_options();
  (void)read_class(bytes, options);
  options.mode = ParseMode::Strict;
  (void)read_class(bytes, options);
}

void exercise_roundtrip(std::string_view input) {
  const auto bytes = std::as_bytes(std::span(input));
  auto options = read_options();
  const auto parsed = read_class(bytes, options);
  if (!parsed) {
    return;
  }

  options.mode = parsed->diagnostics.empty() ? ParseMode::Strict : ParseMode::Compatible;
  options.allow_opaque_code = false;
  for (const auto force_reencode : {false, true}) {
    const auto written = write_class(parsed->class_file, WriteOptions{.force_reencode = force_reencode});
    if (!written) {
      continue;
    }
    if (parsed->class_file.validity() == ClassValidity::OpaqueEssentialContent) {
      std::abort();
    }
    const auto reread = read_class(written->bytes, options);
    if (!reread) {
      std::abort();
    }
    const auto canonical = write_class(reread->class_file);
    if (!canonical || canonical->bytes != written->bytes) {
      std::abort();
    }
  }
}

void exercise_analysis(std::string_view input) {
  const auto bytes = std::as_bytes(std::span(input));
  classpath::ClassPathLimits source_limits;
  source_limits.max_class_bytes = MAX_INPUT_BYTES;
  (void)classpath::detail::read_class_metadata(bytes, source_limits, {});

  auto options = read_options();
  options.mode = ParseMode::Strict;
  options.allow_opaque_code = false;
  options.limits.max_methods = 512;
  const auto parsed = read_class(bytes, options);
  if (!parsed)
    return;
  const auto& owner = parsed->class_file;
  classpath::ClassPathBuilder builder(source_limits);
  if (!builder.add_class(owner))
    return;
  classpath::ClassHierarchy hierarchy{classpath::ClassResolver{builder.build()}};
  const auto* self = owner.constant_pool().get(owner.this_class());
  if (self) {
    const auto name = owner.constant_pool().get_utf8(self->first);
    if (name) {
      const auto metadata = hierarchy.resolve(*name);
      if (metadata && (*metadata)->super_name)
        (void)hierarchy.is_assignable((*metadata)->name, *(*metadata)->super_name);
    }
  }

  analysis::AnalysisContext context{std::move(hierarchy), {.max_instructions = 4096, .max_edges = 8192, .max_work = 65'536, .max_memory_bytes = 4 * 1024 * 1024}};
  for (const auto& method : owner.methods()) {
    const auto* attribute = method.code();
    if (!attribute)
      continue;
    const auto handlers = attribute->exception_handlers();
    const auto graph = analysis::build_cfg(attribute->code(), handlers, std::pmr::get_default_resource(), context.limits());
    if (graph && !graph->is_current(attribute->code(), handlers))
      std::abort();
    const auto max_stack = analysis::compute_max_stack(owner, method, context.limits());
    const auto max_locals = analysis::compute_max_locals(method, owner, context.limits());
    const auto frames = analysis::compute_frames(owner, method, context);
    if (frames && (!frames->is_current(attribute->code(), handlers) ||
                   (max_stack && *max_stack != frames->max_stack()) ||
                   (max_locals && *max_locals != frames->max_locals())))
      std::abort();
  }
  const auto wrote = write_class(owner, {.code_analysis = CodeAnalysisMode::ComputeFrames}, context);
  if (wrote && wrote->bytes.size() <= options.limits.max_input_bytes && !read_class(wrote->bytes, options))
    std::abort();
}

void exercise_archive(std::string_view input) {
  if (input.size() > MAX_INPUT_BYTES)
    return;
  const auto bytes = std::as_bytes(std::span(input));
  classpath::ClassPathLimits limits;
  limits.max_archive_bytes = MAX_INPUT_BYTES + 4;
  limits.max_archive_entries = 2048;
  limits.max_archive_index_bytes = 512 * 1024;
  limits.max_entry_uncompressed_bytes = 128 * 1024;
  (void)classpath::detail::parse_multi_release_manifest(bytes);

  const auto inspect = [&](std::vector<std::byte> data, classpath::SourceKind kind) {
    const auto opened = archive::ZipArchive::open(std::move(data), kind, limits);
    if (!opened)
      return;
    std::size_t budget = limits.max_entry_uncompressed_bytes;
    for (const auto& entry : opened->entries().first(std::min<std::size_t>(opened->entries().size(), 8))) {
      if (entry.directory)
        continue;
      const auto contents = opened->read(entry.name, budget);
      if (!contents)
        continue;
      budget -= contents->size();
      if (entry.name.ends_with(".class"))
        (void)classpath::detail::read_class_metadata(*contents, limits, {});
      if (entry.name == "META-INF/MANIFEST.MF")
        (void)classpath::detail::parse_multi_release_manifest(*contents);
    }
  };
  using enum classpath::SourceKind;
  inspect({bytes.begin(), bytes.end()}, Jar);
  inspect({bytes.begin(), bytes.end()}, Jmod);
  if (input.starts_with("PK")) {
    std::vector<std::byte> jmod{std::byte{0x4a}, std::byte{0x4d}, std::byte{1}, std::byte{0}};
    jmod.insert(jmod.end(), bytes.begin(), bytes.end());
    inspect(std::move(jmod), Jmod);
  }
}

} // namespace cafe::fuzz
