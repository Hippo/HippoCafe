#pragma once

#include <functional>

#include "cafe/annotation.hpp"
#include "cafe/class_file.hpp"
#include "cafe/instruction.hpp"

namespace cafe {

class ref {
public:
  std::string owner;
  std::string name;
  std::string descriptor;
  ref() = default;
  ref(const std::string_view& owner, const std::string_view& name, const std::string_view& descriptor);
  ~ref() = default;
  ref(const ref&) = default;
  ref(ref&&) = default;
  ref& operator=(const ref&) = default;
  ref& operator=(ref&&) = default;
  static ref from(const class_file& cf, uint16_t index);
};

value get_value(const class_file& cf, uint16_t index);

class composer {
public:
  composer(const class_file& cf, const std::function<void(const std::exception&)>& error_handler);
  virtual ~composer() = default;
  annotation compose_annotation(const attribute::annotation& anno);
  element_value compose_element_value(const attribute::element_value& value);
  virtual type_annotation compose_type_annotation(const attribute::type_annotation& anno) = 0;

protected:
  const class_file& cf_;
  std::function<void(const std::exception&)> error_handler_;
};

class class_composer : public composer {
public:
  class_composer(const class_file& cf, const std::function<void(const std::exception&)>& error_handler);
  type_annotation compose_type_annotation(const attribute::type_annotation& anno) override;
  void compose(class_model& class_model);
};

class field_composer : public composer {
public:
  field_composer(const class_file& cf, const field_info& field,
                 const std::function<void(const std::exception&)>& error_handler);
  type_annotation compose_type_annotation(const attribute::type_annotation& anno) override;
  field_model compose();

private:
  const field_info& field_;
};

class method_composer : public composer {
public:
  method_composer(const class_file& cf, const method_info& method,
                  const std::function<void(const std::exception&)>& error_handler);
  type_annotation compose_type_annotation(const attribute::type_annotation& anno) override;
  method_model compose();

private:
  const method_info& method_;
  std::vector<std::string> exceptions_;
};

class record_composer : public composer {
public:
  record_composer(const class_file& cf, const attribute::record::component& component,
                  const std::function<void(const std::exception&)>& error_handler);
  type_annotation compose_type_annotation(const attribute::type_annotation& anno) override;
  record_component compose();

private:
  const attribute::record::component& component_;
};

class code_composer : public composer {
public:
  code_composer(const class_file& cf, const method_composer& method, const attribute::code& code_attr,
                const std::function<void(const std::exception&)>& error_handler);
  type_annotation compose_type_annotation(const attribute::type_annotation& anno) override;
  code compose();

private:
  const method_composer& method_composer_;
  const attribute::code& code_attr_;
  std::vector<std::pair<uint32_t, label>> needed_labels_;
  label get_label(uint32_t pc);
  int label_count_ = 0;
};

} // namespace cafe
