#include <utility>

#include <hippo/cafe.hpp>

int main() {
  auto created = cafe::ClassFile::create({52, 0}, "Example");
  if (!created)
    return 1;
  auto& pool = created->constant_pool();
  const auto name = pool.append_utf8("identity");
  const auto descriptor = pool.append_utf8("(I)I");
  if (!name || !descriptor)
    return 1;
  const auto method = created->add_method(0x0009, *name, *descriptor);
  if (!method)
    return 1;
  cafe::analysis::AnalysisContext context;
  auto graph = cafe::analysis::create_ssa(*created, *method, context);
  if (!graph || !graph->append_terminator(graph->entry_block(), cafe::Opcode::IReturn, graph->parameters()))
    return 1;
  if (!cafe::analysis::lower_ssa(*created, *graph, context))
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
