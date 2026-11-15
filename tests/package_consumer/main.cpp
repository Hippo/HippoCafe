#include <utility>

#include <hippo/cafe.hpp>

int main() {
  auto created = cafe::ClassFile::create({52, 0}, "Example");
  if (!created)
    return 1;
  auto written = cafe::write_class(*created);
  if (!written)
    return 1;
  auto parsed = cafe::read_class(std::move(written->bytes));
  if (!parsed)
    return 1;

  // Pull in the archive implementation to check exported static dependencies.
  auto archive = cafe::archive::Archive::create(cafe::archive::Format::Jar);
  auto output = cafe::write_class(parsed->class_file);
  if (!output || !archive.add("Example.class", output->bytes))
    return 1;
  return archive.write() ? 0 : 1;
}
