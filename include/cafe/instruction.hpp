#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "apidef.hpp"
#include "label.hpp"
#include "value.hpp"

namespace cafe {

class CAFE_API insn {
public:
  uint8_t opcode{};
  insn() = default;
  explicit insn(uint8_t opcode);
  virtual ~insn() = default;
  insn(const insn&) = default;
  insn(insn&&) = default;
  insn& operator=(const insn&) = default;
  insn& operator=(insn&&) = default;


  virtual std::string to_string() const;
};

class CAFE_API var_insn : public insn {
public:
  uint16_t index{};
  var_insn() = default;
  var_insn(uint8_t opcode, uint16_t index);
  ~var_insn() override = default;
  var_insn(const var_insn&) = default;
  var_insn(var_insn&&) = default;
  var_insn& operator=(const var_insn&) = default;
  var_insn& operator=(var_insn&&) = default;

  bool is_load() const;
  bool is_store() const;
  bool is_wide() const;

  std::string to_string() const override;
};

class CAFE_API type_insn : public insn {
public:
  std::string type;
  type_insn() = default;
  type_insn(uint8_t opcode, const std::string_view& type);
  ~type_insn() override = default;
  type_insn(const type_insn&) = default;
  type_insn(type_insn&&) = default;
  type_insn& operator=(const type_insn&) = default;
  type_insn& operator=(type_insn&&) = default;

  std::string to_string() const override;
};

class CAFE_API field_insn : public insn {
public:
  std::string owner;
  std::string name;
  std::string desc;
  field_insn() = default;
  field_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
             const std::string_view& desc);
  ~field_insn() override = default;
  field_insn(const field_insn&) = default;
  field_insn(field_insn&&) = default;
  field_insn& operator=(const field_insn&) = default;
  field_insn& operator=(field_insn&&) = default;

  std::string to_string() const override;
};

class CAFE_API method_insn : public insn {
public:
  std::string owner;
  std::string name;
  std::string desc;
  bool interface{};
  method_insn() = default;
  method_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
             const std::string_view& desc);
  method_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
             const std::string_view& desc, bool interface);
  ~method_insn() override = default;
  method_insn(const method_insn&) = default;
  method_insn(method_insn&&) = default;
  method_insn& operator=(const method_insn&) = default;
  method_insn& operator=(method_insn&&) = default;

  std::string to_string() const override;
};

class CAFE_API iinc_insn {
public:
  uint16_t index{};
  int16_t value{};
  iinc_insn() = default;
  iinc_insn(uint16_t index, int16_t value);
  ~iinc_insn() = default;
  iinc_insn(const iinc_insn&) = default;
  iinc_insn(iinc_insn&&) = default;
  iinc_insn& operator=(const iinc_insn&) = default;
  iinc_insn& operator=(iinc_insn&&) = default;


  std::string to_string() const;
};

class CAFE_API push_insn {
public:
  value value;
  push_insn() = default;
  explicit push_insn(cafe::value value);
  ~push_insn() = default;
  push_insn(const push_insn&) = default;
  push_insn(push_insn&&) = default;
  push_insn& operator=(const push_insn&) = default;
  push_insn& operator=(push_insn&&) = default;

  bool is_wide() const;

  uint8_t opcode() const;
  std::string to_string() const;

  static uint8_t opcode_of(const cafe::value& value);
};

class CAFE_API branch_insn : public insn {
public:
  label target;
  branch_insn() = default;
  branch_insn(uint8_t opcode, label target);
  ~branch_insn() override = default;
  branch_insn(const branch_insn&) = default;
  branch_insn(branch_insn&&) = default;
  branch_insn& operator=(const branch_insn&) = default;
  branch_insn& operator=(branch_insn&&) = default;
  std::string to_string() const override;
};

class CAFE_API lookup_switch_insn {
public:
  label default_target;
  std::vector<std::pair<int32_t, label>> targets;
  lookup_switch_insn() = default;
  lookup_switch_insn(label default_target, const std::vector<std::pair<int32_t, label>>& targets);
  ~lookup_switch_insn() = default;
  lookup_switch_insn(const lookup_switch_insn&) = default;
  lookup_switch_insn(lookup_switch_insn&&) = default;
  lookup_switch_insn& operator=(const lookup_switch_insn&) = default;
  lookup_switch_insn& operator=(lookup_switch_insn&&) = default;

  std::string to_string() const;
};

class CAFE_API table_switch_insn {
public:
  label default_target;
  int32_t low{};
  int32_t high{};
  std::vector<label> targets;
  table_switch_insn() = default;
  table_switch_insn(label default_target, int32_t low, int32_t high, const std::vector<label>& targets);
  ~table_switch_insn() = default;
  table_switch_insn(const table_switch_insn&) = default;
  table_switch_insn(table_switch_insn&&) = default;
  table_switch_insn& operator=(const table_switch_insn&) = default;
  table_switch_insn& operator=(table_switch_insn&&) = default;

  std::string to_string() const;
};

class CAFE_API multi_array_insn {
public:
  std::string desc;
  uint8_t dims{};
  multi_array_insn() = default;
  multi_array_insn(const std::string_view& desc, uint8_t dims);
  ~multi_array_insn() = default;
  multi_array_insn(const multi_array_insn&) = default;
  multi_array_insn(multi_array_insn&&) = default;
  multi_array_insn& operator=(const multi_array_insn&) = default;
  multi_array_insn& operator=(multi_array_insn&&) = default;

  std::string to_string() const;
};

class CAFE_API array_insn {
public:
  std::variant<uint8_t, std::string> type;
  array_insn() = default;
  explicit array_insn(const std::variant<uint8_t, std::string>& type);
  ~array_insn() = default;
  array_insn(const array_insn&) = default;
  array_insn(array_insn&&) = default;
  array_insn& operator=(const array_insn&) = default;
  array_insn& operator=(array_insn&&) = default;

  std::string to_string() const;
};

class CAFE_API invoke_dynamic_insn {
public:
  std::string name;
  std::string desc;
  method_handle handle;
  std::vector<value> args;
  invoke_dynamic_insn() = default;
  invoke_dynamic_insn(const std::string_view& name, const std::string_view& desc, method_handle handle,
                      const std::vector<value>& args);
  ~invoke_dynamic_insn() = default;
  invoke_dynamic_insn(const invoke_dynamic_insn&) = default;
  invoke_dynamic_insn(invoke_dynamic_insn&&) = default;
  invoke_dynamic_insn& operator=(const invoke_dynamic_insn&) = default;
  invoke_dynamic_insn& operator=(invoke_dynamic_insn&&) = default;

  std::string to_string() const;
};

using instruction =
    std::variant<label, insn, var_insn, type_insn, field_insn, method_insn, iinc_insn, push_insn, branch_insn, lookup_switch_insn,
                 table_switch_insn, multi_array_insn, array_insn, invoke_dynamic_insn>;

CAFE_API int16_t opcode(const instruction& insn);
CAFE_API int16_t opcode(instruction&& insn);
CAFE_API std::string to_string(const instruction& insn);
CAFE_API std::string to_string(instruction&& insn);

class CAFE_API tcb {
public:
  label start;
  label end;
  label handler;
  std::optional<std::string> type;
  tcb(label start, label end, label handler, const std::optional<std::string>& type);
  ~tcb() = default;
  tcb(const tcb&) = default;
  tcb(tcb&&) = default;
  tcb& operator=(const tcb&) = default;
  tcb& operator=(tcb&&) = default;
  std::string to_string() const;
};

class CAFE_API local_var {
public:
  std::string name;
  std::string desc;
  std::string signature;
  label start;
  label end;
  uint16_t index;
  local_var(const std::string_view& name, const std::string_view& desc, const std::string_view& signature,
            label start, label end, uint16_t index);
  ~local_var() = default;
  local_var(const local_var&) = default;
  local_var(local_var&&) = default;
  local_var& operator=(const local_var&) = default;
  local_var& operator=(local_var&&) = default;
  std::string to_string() const;
};

class CAFE_API top_var {
public:
  top_var() = default;
  ~top_var() = default;
  top_var(const top_var&) = default;
  top_var(top_var&&) = default;
  top_var& operator=(const top_var&) = default;
  top_var& operator=(top_var&&) = default;
  bool operator==(const top_var&) const;
  bool operator!=(const top_var&) const;
};
class CAFE_API int_var {
public:
  int_var() = default;
  ~int_var() = default;
  int_var(const int_var&) = default;
  int_var(int_var&&) = default;
  int_var& operator=(const int_var&) = default;
  int_var& operator=(int_var&&) = default;
  bool operator==(const int_var&) const;
  bool operator!=(const int_var&) const;
};
class CAFE_API float_var {
public:
  float_var() = default;
  ~float_var() = default;
  float_var(const float_var&) = default;
  float_var(float_var&&) = default;
  float_var& operator=(const float_var&) = default;
  float_var& operator=(float_var&&) = default;
  bool operator==(const float_var&) const;
  bool operator!=(const float_var&) const;
};
class CAFE_API null_var {
public:
  null_var() = default;
  ~null_var() = default;
  null_var(const null_var&) = default;
  null_var(null_var&&) = default;
  null_var& operator=(const null_var&) = default;
  null_var& operator=(null_var&&) = default;
  bool operator==(const null_var&) const;
  bool operator!=(const null_var&) const;
};
class CAFE_API uninitialized_this_var {
public:
  uninitialized_this_var() = default;
  ~uninitialized_this_var() = default;
  uninitialized_this_var(const uninitialized_this_var&) = default;
  uninitialized_this_var(uninitialized_this_var&&) = default;
  uninitialized_this_var& operator=(const uninitialized_this_var&) = default;
  uninitialized_this_var& operator=(uninitialized_this_var&&) = default;
  bool operator==(const uninitialized_this_var&) const;
  bool operator!=(const uninitialized_this_var&) const;
};
class CAFE_API object_var {
public:
  std::string type;
  object_var(const std::string_view& type);
  ~object_var() = default;
  object_var(const object_var&) = default;
  object_var(object_var&&) = default;
  object_var& operator=(const object_var&) = default;
  object_var& operator=(object_var&&) = default;
  bool operator==(const object_var& other) const;
  bool operator!=(const object_var& other) const;
};
class CAFE_API uninitialized_var {
public:
  label offset;
  uninitialized_var(label offset);
  ~uninitialized_var() = default;
  uninitialized_var(const uninitialized_var&) = default;
  uninitialized_var(uninitialized_var&&) = default;
  uninitialized_var& operator=(const uninitialized_var&) = default;
  uninitialized_var& operator=(uninitialized_var&&) = default;
  bool operator==(const uninitialized_var& other) const;
  bool operator!=(const uninitialized_var& other) const;
};
class CAFE_API long_var {
public:
  long_var() = default;
  ~long_var() = default;
  long_var(const long_var&) = default;
  long_var(long_var&&) = default;
  long_var& operator=(const long_var&) = default;
  long_var& operator=(long_var&&) = default;
  bool operator==(const long_var&) const;
  bool operator!=(const long_var&) const;
};
class CAFE_API double_var {
public:
  double_var() = default;
  ~double_var() = default;
  double_var(const double_var&) = default;
  double_var(double_var&&) = default;
  double_var& operator=(const double_var&) = default;
  double_var& operator=(double_var&&) = default;
  bool operator==(const double_var&) const;
  bool operator!=(const double_var&) const;
};

using frame_var = std::variant<top_var, int_var, float_var, long_var, double_var, null_var, uninitialized_this_var,
                               object_var, uninitialized_var>;
CAFE_API std::string to_string(const frame_var& var);

class CAFE_API same_frame {
public:
  std::optional<frame_var> stack;
  same_frame() = default;
  same_frame(const frame_var& stack);
  ~same_frame() = default;
  same_frame(const same_frame&) = default;
  same_frame(same_frame&&) = default;
  same_frame& operator=(const same_frame&) = default;
  same_frame& operator=(same_frame&&) = default;
};
class CAFE_API full_frame {
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
class CAFE_API chop_frame {
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
class CAFE_API append_frame {
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
CAFE_API std::string to_string(const frame& frame);

} // namespace cafe
