#pragma once

#include "bytebuf.hpp"
#include "cafe/class_reader.hpp"
#include "cafe/constant_pool.hpp"

namespace cafe {
class constant_pool_visitor {
public:
  explicit constant_pool_visitor(bytebuf& writer);
  void operator()(const cp::pad_info&) const;

  void operator()(const cp::class_info& info);

  void operator()(const cp::field_ref_info& info);

  void operator()(const cp::method_ref_info& info);

  void operator()(const cp::interface_method_ref_info& info);

  void operator()(const cp::string_info& info);

  void operator()(const cp::integer_info& info);

  void operator()(const cp::float_info& info);

  void operator()(const cp::long_info& info);

  void operator()(const cp::double_info& info);

  void operator()(const cp::name_and_type_info& info);

  void operator()(const cp::utf8_info& info);

  void operator()(const cp::method_handle_info& info);

  void operator()(const cp::method_type_info& info);

  void operator()(const cp::dynamic_info& info);

  void operator()(const cp::invoke_dynamic_info& info);

  void operator()(const cp::module_info& info);

  void operator()(const cp::package_info& info);

private:
  bytebuf& writer_;
};

class instruction_visitor {
public:
  explicit instruction_visitor(code_visitor& visitor);
  void operator()(const label& label);
  void operator()(const insn& insn);
  void operator()(const var_insn& insn);
  void operator()(const type_insn& insn);
  void operator()(const ref_insn& insn);
  void operator()(const iinc_insn& insn);
  void operator()(const push_insn& insn);
  void operator()(const branch_insn& insn);
  void operator()(const lookup_switch_insn& insn);
  void operator()(const table_switch_insn& insn);
  void operator()(const multi_array_insn& insn);
  void operator()(const array_insn& insn);
  void operator()(const invoke_dynamic_insn& insn);

private:
  code_visitor& visitor_;
};
} // namespace cafe
