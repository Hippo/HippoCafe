#pragma once

#include <functional>

#include "cafe/annotation.hpp"
#include "cafe/class_file.hpp"
#include "cafe/instruction.hpp"
#include "cafe/data_rw.hpp"
#include "cafe/value.hpp"

namespace cafe {

class decomposer_ctx {
public:
  cp::constant_pool pool;
  cp::bsm_buffer bsm_buffer;
  data_writer& writer;
  explicit decomposer_ctx(data_writer& writer);
  uint16_t get_vaule(const value& value);
};

class class_decomposer {
public:
  explicit class_decomposer(decomposer_ctx& ctx);
  class_file decompose(const class_model& class_model);
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
private:
  decomposer_ctx& ctx_;
};

class annotation_decomposer {
public:
  explicit annotation_decomposer(decomposer_ctx& ctx);
  attribute::annotation decompose(const annotation& annotation);
  attribute::element_value decompose(const element_value& value);
private:
  decomposer_ctx& ctx_;
};

}