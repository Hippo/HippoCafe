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

  auto& file = parsed->class_file;
  auto match = cafe::pattern::find_first(file, *method, cafe::pattern::opcode(cafe::Opcode::IReturn));
  if (!match || !*match)
    return 1;
  auto editor = file.edit_method(*method);
  if (!editor || !editor->insert_before((*match)->start(), [](cafe::CodeBuilder& code) { code.bipush(10).pop(); }))
    return 1;

  // Pull in the archive implementation to check exported static dependencies.
  auto archive = cafe::archive::Archive::create(cafe::archive::Format::Jar);
  auto output = cafe::write_class(file, {.code_analysis = cafe::CodeAnalysisMode::ComputeMaxs});
  if (!output || !archive.add("Example.class", output->bytes))
    return 1;
  return archive.write() ? 0 : 1;
}
