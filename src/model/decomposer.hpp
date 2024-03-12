#pragma once

#include <functional>
#include <vector>

#include "cafe/class_file.hpp"
#include "cafe/data_rw.hpp"
#include "cafe/model/annotation.hpp"
#include "cafe/model/class_model.hpp"
#include "cafe/model/instruction.hpp"
#include "cafe/value.hpp"

namespace cafe {

class decomposer_ctx {
public:
  cp::constant_pool pool;
  cp::bsm_buffer bsm_buffer;
  data_writer& writer;
  explicit decomposer_ctx(data_writer& writer);
  uint16_t get_vaule(const value& value);
  uint16_t get_bsm_index(const method_handle& handle, const std::vector<value>& args);
};

class class_decomposer {
public:
  explicit class_decomposer(decomposer_ctx& ctx);
  class_file decompose(const class_model& class_model);
  attribute::type_annotation decompose(const class_model& class_model, const type_annotation& annotation);

private:
  decomposer_ctx& ctx_;
};

class field_decomposer {
public:
  explicit field_decomposer(decomposer_ctx& ctx);
  field_info decompose(const field_model& field_model);

private:
  decomposer_ctx& ctx_;
};

class method_decomposer {
public:
  explicit method_decomposer(decomposer_ctx& ctx);
  method_info decompose(const method_model& method_model);
  attribute::type_annotation decompose(const method_model& method_model, const type_annotation& annotation);

private:
  decomposer_ctx& ctx_;
};
class code_decomposer {
public:
  explicit code_decomposer(decomposer_ctx& ctx);
  attribute::code decompose(const code& code);
  attribute::type_annotation decompose(const code& code, const type_annotation& annotation);

private:
  decomposer_ctx& ctx_;
  std::vector<std::pair<label, size_t>> labels_;
  uint16_t get_label(const label& label) const;
};
class annotation_decomposer {
public:
  explicit annotation_decomposer(decomposer_ctx& ctx);
  attribute::annotation decompose(const annotation& annotation);
  attribute::element_value decompose(const element_value& value);

private:
  decomposer_ctx& ctx_;
};

class record_decomposer {
public:
  explicit record_decomposer(decomposer_ctx& ctx);
  attribute::record::component decompose(const record_component& component);

private:
  decomposer_ctx& ctx_;
};

} // namespace cafe
