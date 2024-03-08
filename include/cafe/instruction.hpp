#pragma once

#include <cstdint>

#include "annotation.hpp"
#include "label.hpp"
#include "value.hpp"

namespace cafe {

class insn {
public:
  uint8_t opcode{};
  insn();
  explicit insn(uint8_t opcode);
  virtual ~insn() = default;
  insn(const insn&) = default;
  insn(insn&&) = default;
  insn& operator=(const insn&) = default;
  insn& operator=(insn&&) = default;
  [[nodiscard]] uintptr_t id() const;

  virtual [[nodiscard]] std::string to_string() const;

private:
  uintptr_t id_;
};

class var_insn : public insn {
public:
  uint16_t index{};
  var_insn() = default;
  var_insn(uint8_t opcode, uint16_t index);
  ~var_insn() override= default;
  var_insn(const var_insn&) = default;
  var_insn(var_insn&&) = default;
  var_insn& operator=(const var_insn&) = default;
  var_insn& operator=(var_insn&&) = default;

  [[nodiscard]] std::string to_string() const override;
};

class type_insn : public insn {
public:
  std::string type;
  type_insn() = default;
  type_insn(uint8_t opcode, const std::string_view& type);
  ~type_insn() override= default;
  type_insn(const type_insn&) = default;
  type_insn(type_insn&&) = default;
  type_insn& operator=(const type_insn&) = default;
  type_insn& operator=(type_insn&&) = default;

  [[nodiscard]] std::string to_string() const override;
};

class ref_insn : public insn {
public:
  std::string owner;
  std::string name;
  std::string descriptor;
  ref_insn() = default;
  ref_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
           const std::string_view& descriptor);
  ~ref_insn() override = default;
  ref_insn(const ref_insn&) = default;
  ref_insn(ref_insn&&) = default;
  ref_insn& operator=(const ref_insn&) = default;
  ref_insn& operator=(ref_insn&&) = default;

  [[nodiscard]] std::string to_string() const override;
};

class iinc_insn {
public:
  uint16_t index{};
  int16_t value{};
  iinc_insn();
  iinc_insn(uint16_t index, int16_t value);
  ~iinc_insn() = default;
  iinc_insn(const iinc_insn&) = default;
  iinc_insn(iinc_insn&&) = default;
  iinc_insn& operator=(const iinc_insn&) = default;
  iinc_insn& operator=(iinc_insn&&) = default;
  [[nodiscard]] uintptr_t id() const;

  [[nodiscard]] std::string to_string() const;

private:
  uintptr_t id_;
};

class push_insn {
public:
  value val;
  push_insn();
  explicit push_insn(value val);
  ~push_insn() = default;
  push_insn(const push_insn&) = default;
  push_insn(push_insn&&) = default;
  push_insn& operator=(const push_insn&) = default;
  push_insn& operator=(push_insn&&) = default;
  [[nodiscard]] uintptr_t id() const;
  [[nodiscard]] uint8_t opcode() const;
  [[nodiscard]] std::string to_string() const;

private:
  uintptr_t id_;
};

class branch_insn : public insn {
public:
  label target;
  branch_insn() = default;
  branch_insn(uint8_t opcode, label target);
  ~branch_insn() override = default;
  branch_insn(const branch_insn&) = default;
  branch_insn(branch_insn&&) = default;
  branch_insn& operator=(const branch_insn&) = default;
  branch_insn& operator=(branch_insn&&) = default;
  [[nodiscard]] std::string to_string() const override;
};

class lookup_switch_insn {
public:
  label default_target;
  std::vector<std::pair<int32_t, label>> targets;
  lookup_switch_insn();
  lookup_switch_insn(label default_target, const std::vector<std::pair<int32_t, label>>& targets);
  ~lookup_switch_insn() = default;
  lookup_switch_insn(const lookup_switch_insn&) = default;
  lookup_switch_insn(lookup_switch_insn&&) = default;
  lookup_switch_insn& operator=(const lookup_switch_insn&) = default;
  lookup_switch_insn& operator=(lookup_switch_insn&&) = default;
  [[nodiscard]] uintptr_t id() const;
  [[nodiscard]] std::string to_string() const;

private:
  uintptr_t id_;
};

class table_switch_insn {
public:
  label default_target;
  int32_t low{};
  int32_t high{};
  std::vector<label> targets;
  table_switch_insn();
  table_switch_insn(label default_target, int32_t low, int32_t high, const std::vector<label>& targets);
  ~table_switch_insn() = default;
  table_switch_insn(const table_switch_insn&) = default;
  table_switch_insn(table_switch_insn&&) = default;
  table_switch_insn& operator=(const table_switch_insn&) = default;
  table_switch_insn& operator=(table_switch_insn&&) = default;
  [[nodiscard]] uintptr_t id() const;
  [[nodiscard]] std::string to_string() const;

private:
  uintptr_t id_;
};

class multi_array_insn {
public:
  std::string descriptor;
  uint8_t dims{};
  multi_array_insn();
  multi_array_insn(const std::string_view& descriptor, uint8_t dims);
  ~multi_array_insn() = default;
  multi_array_insn(const multi_array_insn&) = default;
  multi_array_insn(multi_array_insn&&) = default;
  multi_array_insn& operator=(const multi_array_insn&) = default;
  multi_array_insn& operator=(multi_array_insn&&) = default;
  [[nodiscard]] uintptr_t id() const;
  [[nodiscard]] std::string to_string() const;

private:
  uintptr_t id_;
};

class array_insn {
public:
  std::variant<uint8_t, std::string> type;
  array_insn();
  explicit array_insn(const std::variant<uint8_t, std::string>& type);
  ~array_insn() = default;
  array_insn(const array_insn&) = default;
  array_insn(array_insn&&) = default;
  array_insn& operator=(const array_insn&) = default;
  array_insn& operator=(array_insn&&) = default;
  [[nodiscard]] uintptr_t id() const;
  [[nodiscard]] std::string to_string() const;

private:
  uintptr_t id_;
};

class invoke_dynamic_insn {
public:
  std::string name;
  std::string descriptor;
  method_handle handle;
  std::vector<value> args;
  invoke_dynamic_insn();
  invoke_dynamic_insn(const std::string_view& name, const std::string_view& descriptor, method_handle handle,
                      const std::vector<value>& args);
  ~invoke_dynamic_insn() = default;
  invoke_dynamic_insn(const invoke_dynamic_insn&) = default;
  invoke_dynamic_insn(invoke_dynamic_insn&&) = default;
  invoke_dynamic_insn& operator=(const invoke_dynamic_insn&) = default;
  invoke_dynamic_insn& operator=(invoke_dynamic_insn&&) = default;
  [[nodiscard]] uintptr_t id() const;
  [[nodiscard]] std::string to_string() const;

private:
  uintptr_t id_;
};

using instruction =
    std::variant<label, insn, var_insn, type_insn, ref_insn, iinc_insn, push_insn, branch_insn, lookup_switch_insn,
                 table_switch_insn, multi_array_insn, array_insn, invoke_dynamic_insn>;
uintptr_t id(const instruction& insn);
uintptr_t id(instruction&& insn);
int16_t opcode(const instruction& insn);
int16_t opcode(instruction&& insn);
std::string to_string(const instruction& insn);
std::string to_string(instruction&& insn);


class tcb {
public:
  label start;
  label end;
  label handler;
  std::string type;
  tcb(label start, label end, label handler, const std::string_view& type);
  ~tcb() = default;
  tcb(const tcb&) = default;
  tcb(tcb&&) = default;
  tcb& operator=(const tcb&) = default;
  tcb& operator=(tcb&&) = default;
  [[nodiscard]] std::string to_string() const;
};

class local_var {
public:
  std::string name;
  std::string descriptor;
  std::string signature;
  label start;
  label end;
  uint16_t index;
  local_var(const std::string_view& name, const std::string_view& descriptor, const std::string_view& signature,
            label start, label end, uint16_t index);
  ~local_var() = default;
  local_var(const local_var&) = default;
  local_var(local_var&&) = default;
  local_var& operator=(const local_var&) = default;
  local_var& operator=(local_var&&) = default;
};

class top_var {
public:
  top_var() = default;
  ~top_var() = default;
  top_var(const top_var&) = default;
  top_var(top_var&&) = default;
  top_var& operator=(const top_var&) = default;
  top_var& operator=(top_var&&) = default;
};
class int_var {
public:
  int_var() = default;
  ~int_var() = default;
  int_var(const int_var&) = default;
  int_var(int_var&&) = default;
  int_var& operator=(const int_var&) = default;
  int_var& operator=(int_var&&) = default;
};
class float_var {
public:
  float_var() = default;
  ~float_var() = default;
  float_var(const float_var&) = default;
  float_var(float_var&&) = default;
  float_var& operator=(const float_var&) = default;
  float_var& operator=(float_var&&) = default;
};
class null_var {
public:
  null_var() = default;
  ~null_var() = default;
  null_var(const null_var&) = default;
  null_var(null_var&&) = default;
  null_var& operator=(const null_var&) = default;
  null_var& operator=(null_var&&) = default;
};
class uninitialized_this_var {
public:
  uninitialized_this_var() = default;
  ~uninitialized_this_var() = default;
  uninitialized_this_var(const uninitialized_this_var&) = default;
  uninitialized_this_var(uninitialized_this_var&&) = default;
  uninitialized_this_var& operator=(const uninitialized_this_var&) = default;
  uninitialized_this_var& operator=(uninitialized_this_var&&) = default;
};
class object_var {
public:
  std::string type;
  object_var(const std::string_view& type);
  ~object_var() = default;
  object_var(const object_var&) = default;
  object_var(object_var&&) = default;
  object_var& operator=(const object_var&) = default;
  object_var& operator=(object_var&&) = default;
};
class uninitalized_var {
public:
  label offset;
  uninitalized_var(label offset);
  ~uninitalized_var() = default;
  uninitalized_var(const uninitalized_var&) = default;
  uninitalized_var(uninitalized_var&&) = default;
  uninitalized_var& operator=(const uninitalized_var&) = default;
  uninitalized_var& operator=(uninitalized_var&&) = default;
};
class long_var {};
class double_var {};

using frame_var = std::variant<top_var, int_var, float_var, long_var, double_var, null_var, uninitialized_this_var,
                               object_var, uninitalized_var>;

class same_frame {
public:
  std::vector<frame_var> locals;
  std::vector<frame_var> stack;
  same_frame() = default;
  same_frame(const std::vector<frame_var>& locals, const std::vector<frame_var>& stack);
  ~same_frame() = default;
  same_frame(const same_frame&) = default;
  same_frame(same_frame&&) = default;
  same_frame& operator=(const same_frame&) = default;
  same_frame& operator=(same_frame&&) = default;
};
class full_frame {
public:
  std::vector<frame_var> locals;
  std::vector<frame_var> stack;
  full_frame() = default;
  full_frame(const std::vector<frame_var>& locals, const std::vector<frame_var>& stack);
  ~full_frame() = default;
  full_frame(const full_frame&) = default;
  full_frame(full_frame&&) = default;
  full_frame& operator=(const full_frame&) = default;
  full_frame& operator=(full_frame&&) = default;
};
class chop_frame {
public:
  uint8_t size{};
  chop_frame() = default;
  explicit chop_frame(uint8_t size);
  ~chop_frame() = default;
  chop_frame(const chop_frame&) = default;
  chop_frame(chop_frame&&) = default;
  chop_frame& operator=(const chop_frame&) = default;
  chop_frame& operator=(chop_frame&&) = default;
};
class append_frame {
public:
  std::vector<frame_var> locals;
  append_frame() = default;
  explicit append_frame(const std::vector<frame_var>& locals);
  ~append_frame() = default;
  append_frame(const append_frame&) = default;
  append_frame(append_frame&&) = default;
  append_frame& operator=(const append_frame&) = default;
  append_frame& operator=(append_frame&&) = default;
};

using frame = std::variant<same_frame, full_frame, chop_frame, append_frame>;


class code : public std::vector<instruction> {
public:
  uint16_t max_locals{};
  uint16_t max_stack{};
  std::vector<tcb> tcb_table;
  std::vector<std::pair<label, uint16_t>> line_numbers;
  std::vector<local_var> local_vars;
  std::vector<std::pair<label, frame>> frames;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  code() = default;
  ~code() = default;
  code(const code&) = default;
  code(code&&) = default;
  code& operator=(const code&) = default;
  code& operator=(code&&) = default;
  void clear() noexcept;
};

} // namespace cafe
