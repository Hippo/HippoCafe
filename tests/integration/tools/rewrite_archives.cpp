#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <type_traits>

#include <hippo/cafe.hpp>

namespace {

using namespace cafe::archive;
using Path = std::filesystem::path;

constexpr std::string_view CLASS = "cafe/fixtures/Features.class";
constexpr std::string_view INTERNAL_NAME = "cafe/fixtures/Features";
constexpr std::string_view MANIFEST = "META-INF/MANIFEST.MF";
constexpr std::string_view VERSION_ENTRY = "META-INF/versions/17/cafe/fixtures/Features.class";

template<typename T, typename E>
T checked(std::expected<T, E> result) {
  if (!result)
    throw std::runtime_error(result.error().detail);
  if constexpr (!std::is_void_v<T>)
    return std::move(*result);
}

std::span<const std::byte> bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}

void verify_resolution(const Path& path, Format format, std::uint16_t target, std::string_view entry) {
  cafe::classpath::ClassPathBuilder builder;
  builder.set_target_release(target);
  if (format == Format::Jar)
    checked(builder.add_jar(path));
  else
    checked(builder.add_jmod(path));
  cafe::classpath::ClassResolver resolver(builder.build());
  auto info = checked(resolver.resolve(INTERNAL_NAME));
  if (info->source.entry != entry)
    throw std::runtime_error("resolver selected an unexpected archive entry");
}

void create_archives(const Path& fixtures, const Path& output) {
  auto jar = Archive::create(Format::Jar);
  auto jmod = Archive::create(Format::Jmod);
  for (const auto& file : std::filesystem::recursive_directory_iterator(fixtures)) {
    if (file.is_regular_file()) {
      const auto relative = file.path().lexically_relative(fixtures).generic_string();
      checked(jar.add_file(relative, file.path()));
      checked(jmod.add_file("classes/" + relative, file.path()));
    }
  }
  checked(jar.add(MANIFEST, bytes("Manifest-Version: 1.0\r\n\r\n")));
  checked(jar.add("added.txt", bytes("jar resource")));
  checked(jmod.add("conf/added.txt", bytes("jmod configuration")));
  checked(jar.save(output / "created.jar"));
  checked(jmod.save(output / "created.jmod"));
  verify_resolution(output / "created.jar", Format::Jar, 17, CLASS);
  verify_resolution(output / "created.jmod", Format::Jmod, 17, "classes/cafe/fixtures/Features.class");
}

void edit_class(Archive& archive, std::string_view name) {
  auto parsed = checked(cafe::read_class(checked(archive.read(name))));
  auto rewritten = checked(cafe::write_class(parsed.class_file, {.force_reencode = true}));
  checked(archive.replace(name, rewritten.bytes));
}

void rewrite_archives(const Path& output) {
  auto jar = checked(Archive::open(output / "input.jar", Format::Jar));
  edit_class(jar, VERSION_ENTRY);
  checked(jar.erase("obsolete.txt"));
  checked(jar.add("added.txt", bytes("jar resource")));
  checked(jar.save(output / "rewritten.jar"));
  verify_resolution(output / "rewritten.jar", Format::Jar, 0, CLASS);
  verify_resolution(output / "rewritten.jar", Format::Jar, 17, VERSION_ENTRY);

  auto jmod = checked(Archive::open(output / "input.jmod", Format::Jmod));
  edit_class(jmod, "classes/cafe/fixtures/Features.class");
  checked(jmod.erase("conf/obsolete.txt"));
  checked(jmod.add("conf/added.txt", bytes("jmod configuration")));
  checked(jmod.save(output / "rewritten.jmod"));
  verify_resolution(output / "rewritten.jmod", Format::Jmod, 17, "classes/cafe/fixtures/Features.class");
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 3)
    return 2;
  try {
    create_archives(argv[1], argv[2]);
    rewrite_archives(argv[2]);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
