#include "cafe/class_writer.hpp"

#include <iostream>

#include "cafe/constants.hpp"

#include "bytebuf.hpp"
#include "visitor.hpp"

namespace cafe {
record_component_writer::record_component_writer(class_writer& writer) : writer_(writer) {
}
void record_component_writer::visit(const std::string_view& name, const std::string_view& descriptor) {
  name_index_ = writer_.get_utf(name);
  descriptor_index_ = writer_.get_utf(descriptor);
}
void record_component_writer::visit_signature(const std::string_view& signature) {
  const auto attr_name = writer_.get_utf("Signature");
  const auto sig = writer_.get_utf(signature);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(2);
  buf.write_u16(sig);
  const auto data = buf.data();
  attributes_count_++;
  attributes_.insert(attributes_.end(), data.begin(), data.end());
}
void record_component_writer::visit_visible_annotation(const annotation& annotation) {
  visible_annotations_count_++;
  const auto data = writer_.get_annotation(annotation);
  visible_annotations_.insert(writer_.visible_annotations_.end(), data.begin(), data.end());
}
void record_component_writer::visit_invisible_annotation(const annotation& annotation) {
  invisible_annotations_count_++;
  const auto data = writer_.get_annotation(annotation);
  invisible_annotations_.insert(writer_.invisible_annotations_.end(), data.begin(), data.end());
}
void record_component_writer::visit_visible_type_annotation(const type_annotation& annotation) {
  std::vector<std::pair<size_t, label>> dummy_labels;
  visible_type_annotations_count_++;
  const auto data = writer_.get_type_annotation(annotation, dummy_labels);
  visible_type_annotations_.insert(writer_.visible_type_annotations_.end(), data.begin(), data.end());
}
void record_component_writer::visit_invisible_type_annotation(const type_annotation& annotation) {
  std::vector<std::pair<size_t, label>> dummy_labels;
  invisible_type_annotations_count_++;
  const auto data = writer_.get_type_annotation(annotation, dummy_labels);
  invisible_type_annotations_.insert(writer_.invisible_type_annotations_.end(), data.begin(), data.end());
}
void record_component_writer::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
  attributes_count_++;
  const auto attr_name = writer_.get_utf(name);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>(data.size()));
  buf.write_all(data);
  const auto info = buf.data();
  attributes_.insert(attributes_.end(), info.begin(), info.end());
}
module_writer::module_writer(class_writer& writer) : writer_(writer) {
}
void module_writer::visit(const std::string_view& name, uint16_t access_flags, const std::string_view& version) {
  const auto name_index = writer_.get_module(name);
  const auto version_index = version.empty() ? 0 : writer_.get_utf(version);
  name_index_ = name_index;
  module_flags_ = access_flags;
  version_index_ = version_index;
}
void module_writer::visit_require(const std::string_view& module, uint16_t access_flags,
                                  const std::string_view& version) {
  requires_count_++;
  const auto index = writer_.get_module(module);
  const auto version_index = version.empty() ? 0 : writer_.get_utf(version);
  bytebuf buf;
  buf.write_u16(index);
  buf.write_u16(access_flags);
  buf.write_u16(version_index);
  requires_.insert(requires_.end(), buf.data().begin(), buf.data().end());
}
void module_writer::visit_export(const std::string_view& package, uint16_t access_flags,
                                 const std::vector<std::string>& modules) {
  exports_count_++;
  const auto package_index = writer_.get_package(package);
  bytebuf buf;
  buf.write_u16(package_index);
  buf.write_u16(access_flags);
  buf.write_u16(static_cast<uint16_t>(modules.size()));
  for (const auto& module : modules) {
    buf.write_u16(writer_.get_module(module));
  }
  exports_.insert(exports_.end(), buf.data().begin(), buf.data().end());
}
void module_writer::visit_open(const std::string_view& package, uint16_t access_flags,
                               const std::vector<std::string>& modules) {
  opens_count_++;
  const auto package_index = writer_.get_package(package);
  bytebuf buf;
  buf.write_u16(package_index);
  buf.write_u16(access_flags);
  buf.write_u16(static_cast<uint16_t>(modules.size()));
  for (const auto& module : modules) {
    buf.write_u16(writer_.get_module(module));
  }
  opens_.insert(opens_.end(), buf.data().begin(), buf.data().end());
}
void module_writer::visit_use(const std::string_view& service) {
  uses_.emplace_back(writer_.get_class(service));
}
void module_writer::visit_provide(const std::string_view& service, const std::vector<std::string>& providers) {
  provides_count_++;
  const auto service_index = writer_.get_class(service);
  bytebuf buf;
  buf.write_u16(service_index);
  buf.write_u16(static_cast<uint16_t>(providers.size()));
  for (const auto& provider : providers) {
    buf.write_u16(writer_.get_class(provider));
  }
  provides_.insert(provides_.end(), buf.data().begin(), buf.data().end());
}
code_writer::code_writer(class_writer& writer, method_writer& parent) : writer_(writer), parent_(parent) {
}
void code_writer::visit(uint16_t max_stack, uint16_t max_locals) {
  max_stack_ = max_stack;
  max_locals_ = max_locals;
}
void code_writer::visit_line_number(uint16_t line, label start) {
  lines_.emplace_back(line, start);
}
void code_writer::visit_tcb(const tcb& try_catch) {
  tcbs_.emplace_back(try_catch);
}
void code_writer::visit_local(const local_var& local) {
  local_variables_.emplace_back(local);
}
void code_writer::visit_frame(const label& target, const frame& frame) {
  frames_stack_.emplace_back(target, frame);
}
void code_writer::visit_visible_type_annotation(const type_annotation& annotation) {
  type_annotations_.emplace_back(annotation, true);
}
void code_writer::visit_invisible_type_annotation(const type_annotation& annotation) {
  type_annotations_.emplace_back(annotation, false);
}
void code_writer::visit_label(const label& label) {
  estimates_.emplace_back(label, estimated_size_);
  labels_.emplace_back(code_.size(), label);
}
void code_writer::visit_insn(uint8_t opcode) {
  code_.emplace_back(opcode);
  estimated_size_++;
}
void code_writer::visit_var_insn(uint8_t opcode, uint16_t index) {
  const auto wide = index > std::numeric_limits<uint8_t>::max();
  if (wide) {
    code_.emplace_back(op::wide);
  }
  code_.emplace_back(opcode);
  if (wide) {
    code_.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
    code_.emplace_back(static_cast<uint8_t>(index & 0xff));
  } else {
    code_.emplace_back(static_cast<uint8_t>(index));
  }
  estimated_size_ += wide ? 4 : 2;
}
void code_writer::visit_type_insn(uint8_t opcode, const std::string_view& type) {
  const auto index = writer_.get_class(type);
  code_.emplace_back(opcode);
  code_.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
  code_.emplace_back(static_cast<uint8_t>(index & 0xff));
  estimated_size_ += 3;
}
void code_writer::visit_ref_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                                 const std::string_view& descriptor) {
  uint16_t index;
  switch (opcode) {
    case op::getfield:
    case op::putfield:
    case op::getstatic:
    case op::putstatic:
      index = writer_.get_field_ref(owner, name, descriptor);
      break;
    case op::invokeinterface:
      index = writer_.get_interface_method_ref(owner, name, descriptor);
      break;
    default:
      index = writer_.get_method_ref(owner, name, descriptor);
  }
  code_.emplace_back(opcode);
  code_.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
  code_.emplace_back(static_cast<uint8_t>(index & 0xff));
  estimated_size_ += 3;
  if (opcode == op::invokeinterface) {
    const auto parsed_arg = parse_method_descriptor(descriptor).first;
    auto size = 1;
    for (const auto& ar : parsed_arg) {
      size += cafe::size(ar);
    }
    code_.emplace_back(size);
    code_.emplace_back(0);
    estimated_size_ += 2;
  }
}
void code_writer::visit_iinc_insn(uint16_t index, int16_t value) {
  const auto wide = index > std::numeric_limits<uint8_t>::max() || value > std::numeric_limits<int8_t>::max() ||
                    value < std::numeric_limits<int8_t>::min();
  if (wide) {
    code_.emplace_back(op::wide);
  }
  code_.emplace_back(op::iinc);
  if (wide) {
    code_.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
    code_.emplace_back(static_cast<uint8_t>(index & 0xff));
    code_.emplace_back(static_cast<uint8_t>((value >> 8) & 0xff));
    code_.emplace_back(static_cast<uint8_t>(value & 0xff));
  } else {
    code_.emplace_back(static_cast<uint8_t>(index));
    code_.emplace_back(static_cast<uint8_t>(value));
  }
  estimated_size_ += wide ? 6 : 3;
}
void code_writer::visit_push_insn(value val) {
  const auto opcode = push_insn::opcode_of(val);
  if (opcode == op::bipush) {
    code_.emplace_back(opcode);
    code_.emplace_back(static_cast<int8_t>(std::get<int32_t>(val)));
    estimated_size_ += 2;
  } else if (opcode == op::sipush) {
    const auto value = static_cast<int16_t>(std::get<int32_t>(val));
    code_.emplace_back(opcode);
    code_.emplace_back(static_cast<uint8_t>((value >> 8) & 0xff));
    code_.emplace_back(static_cast<uint8_t>(value & 0xff));
    estimated_size_ += 3;
  } else if (opcode != op::ldc) {
    code_.emplace_back(opcode);
    estimated_size_++;
  } else {
    const auto index = writer_.get_value(val);
    if (std::holds_alternative<int64_t>(val) || std::holds_alternative<double>(val)) {
      code_.emplace_back(op::ldc2_w);
      code_.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
      code_.emplace_back(static_cast<uint8_t>(index & 0xff));
      estimated_size_ += 3;
    } else if (index > std::numeric_limits<int8_t>::max()) {
      code_.emplace_back(op::ldc_w);
      code_.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
      code_.emplace_back(static_cast<uint8_t>(index & 0xff));
      estimated_size_ += 3;
    } else {
      code_.emplace_back(op::ldc);
      code_.emplace_back(static_cast<int8_t>(index));
      estimated_size_ += 2;
    }
  }
}
void code_writer::visit_branch_insn(uint8_t opcode, label target) {
  const auto jmp = opcode == op::goto_ || opcode == op::jsr || opcode == op::jsr_w || opcode == op::goto_w;
  if (jmp) {
    const auto est = get_estimated(target);
    if (est != 0) {
      const auto offset = static_cast<int32_t>(est) - static_cast<int32_t>(estimated_size_);
      if (offset > std::numeric_limits<int16_t>::max() || offset < std::numeric_limits<int16_t>::min()) {
        if (opcode == op::goto_ || opcode == op::goto_w) {
          opcode = op::goto_w;
        } else {
          opcode = op::jsr_w;
        }
      }
    }
  }
  code_.emplace_back(opcode);
  branches_.emplace_back(target, code_.size(), opcode == op::goto_w || opcode == op::jsr_w, code_.size() - 1);
  code_.emplace_back(0);
  code_.emplace_back(0);
  if (opcode == op::goto_w || opcode == op::jsr_w) {
    code_.emplace_back(0);
    code_.emplace_back(0);
  }
  estimated_size_ += jmp ? 5 : 3;
}
void code_writer::visit_lookup_switch_insn(label default_target,
                                           const std::vector<std::pair<int32_t, label>>& targets) {
  const auto insn_pos = code_.size();
  code_.emplace_back(op::lookupswitch);
  switch_pads_.emplace_back(code_.size());
  branches_.emplace_back(default_target, code_.size(), true, insn_pos);
  code_.insert(code_.end(), 4, 0);
  const auto target_size = targets.size();
  code_.emplace_back(static_cast<uint8_t>((target_size >> 24) & 0xff));
  code_.emplace_back(static_cast<uint8_t>((target_size >> 16) & 0xff));
  code_.emplace_back(static_cast<uint8_t>((target_size >> 8) & 0xff));
  code_.emplace_back(static_cast<uint8_t>(target_size & 0xff));
  estimated_size_ += 12;
  for (const auto& [key, target] : targets) {
    estimated_size_ += 8;
    code_.emplace_back(static_cast<uint8_t>((key >> 24) & 0xff));
    code_.emplace_back(static_cast<uint8_t>((key >> 16) & 0xff));
    code_.emplace_back(static_cast<uint8_t>((key >> 8) & 0xff));
    code_.emplace_back(static_cast<uint8_t>(key & 0xff));
    branches_.emplace_back(target, code_.size(), true, insn_pos);
    code_.insert(code_.end(), 4, 0);
  }
}
void code_writer::visit_table_switch_insn(label default_target, int32_t low, int32_t high,
                                          const std::vector<label>& targets) {
  const auto insn_pos = code_.size();
  code_.emplace_back(op::tableswitch);
  switch_pads_.emplace_back(code_.size());
  branches_.emplace_back(default_target, code_.size(), true, insn_pos);
  code_.insert(code_.end(), 4, 0);
  code_.emplace_back(static_cast<uint8_t>((low >> 24) & 0xff));
  code_.emplace_back(static_cast<uint8_t>((low >> 16) & 0xff));
  code_.emplace_back(static_cast<uint8_t>((low >> 8) & 0xff));
  code_.emplace_back(static_cast<uint8_t>(low & 0xff));
  code_.emplace_back(static_cast<uint8_t>((high >> 24) & 0xff));
  code_.emplace_back(static_cast<uint8_t>((high >> 16) & 0xff));
  code_.emplace_back(static_cast<uint8_t>((high >> 8) & 0xff));
  code_.emplace_back(static_cast<uint8_t>(high & 0xff));
  estimated_size_ += 16;
  for (const auto& target : targets) {
    estimated_size_ += 4;
    branches_.emplace_back(target, code_.size(), true, insn_pos);
    code_.insert(code_.end(), 4, 0);
  }
}
void code_writer::visit_multi_array_insn(const std::string_view& descriptor, uint8_t dims) {
  const auto index = writer_.get_class(descriptor);
  code_.emplace_back(op::multianewarray);
  code_.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
  code_.emplace_back(static_cast<uint8_t>(index & 0xff));
  code_.emplace_back(dims);
  estimated_size_ += 4;
}
void code_writer::visit_array_insn(const std::variant<uint8_t, std::string>& type) {
  if (const auto ty = std::get_if<std::string>(&type)) {
    const auto index = writer_.get_class(*ty);
    code_.emplace_back(op::anewarray);
    code_.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
    code_.emplace_back(static_cast<uint8_t>(index & 0xff));
    estimated_size_ += 3;
  } else {
    const auto array_type = std::get<uint8_t>(type);
    code_.emplace_back(op::newarray);
    code_.emplace_back(array_type);
    estimated_size_++;
  }
}
void code_writer::visit_invoke_dynamic_insn(const std::string_view& name, const std::string_view& descriptor,
                                            method_handle handle, const std::vector<value>& args) {
  const auto index = writer_.get_invoke_dynamic(name, descriptor, handle, args);
  code_.emplace_back(op::invokedynamic);
  code_.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
  code_.emplace_back(static_cast<uint8_t>(index & 0xff));
  code_.emplace_back(0);
  code_.emplace_back(0);
  estimated_size_ += 5;
}
void code_writer::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
  attributes_count_++;
  const auto attr_name = writer_.get_utf(name);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>(data.size()));
  buf.write_all(data);
  const auto info = buf.data();
  attributes_.insert(attributes_.end(), info.begin(), info.end());
}
void code_writer::finish_attributes() {
  for (const auto& [line, start] : lines_) {
    line_numbers_count_++;
    const auto start_pc = get_label(start);
    bytebuf buf;
    buf.write_u16(static_cast<uint16_t>(start_pc));
    buf.write_u16(line);
    const auto data = buf.data();
    line_numbers_.insert(line_numbers_.end(), data.begin(), data.end());
  }
  for (const auto& try_catch : tcbs_) {
    exception_table_count_++;
    const auto start_pc = get_label(try_catch.start);
    const auto end_pc = get_label(try_catch.end);
    const auto handler_pc = get_label(try_catch.handler);
    const auto catch_type = try_catch.type.empty() ? 0 : writer_.get_class(try_catch.type);
    bytebuf buf;
    buf.write_u16(static_cast<uint16_t>(start_pc));
    buf.write_u16(static_cast<uint16_t>(end_pc));
    buf.write_u16(static_cast<uint16_t>(handler_pc));
    buf.write_u16(catch_type);
    const auto data = buf.data();
    exception_table_.insert(exception_table_.end(), data.begin(), data.end());
  }
  for (const auto& local : local_variables_) {
    const auto start_pc = get_label(local.start);
    const auto length = get_label(local.end) - start_pc;
    const auto name_index = writer_.get_utf(local.name);
    const auto descriptor_index = local.descriptor.empty() ? 0 : writer_.get_utf(local.descriptor);
    const auto signature_index = local.signature.empty() ? 0 : writer_.get_utf(local.signature);
    if (descriptor_index != 0) {
      local_vars_count_++;
      bytebuf buf;
      buf.write_u16(static_cast<uint16_t>(start_pc));
      buf.write_u16(static_cast<uint16_t>(length));
      buf.write_u16(name_index);
      buf.write_u16(descriptor_index);
      buf.write_u16(local.index);
      const auto data = buf.data();
      local_vars_.insert(local_vars_.end(), data.begin(), data.end());
    }

    if (signature_index != 0) {
      local_type_vars_count_++;
      bytebuf buf;
      buf.write_u16(static_cast<uint16_t>(start_pc));
      buf.write_u16(static_cast<uint16_t>(length));
      buf.write_u16(name_index);
      buf.write_u16(signature_index);
      buf.write_u16(local.index);
      const auto data = buf.data();
      local_type_vars_.insert(local_type_vars_.end(), data.begin(), data.end());
    }
  }
  for (const auto& [target, frame] : frames_stack_) {
    frames_count_++;
    const auto delta = next_delta(target);
    bytebuf buf;
    std::visit(
        [this, &buf, delta](const auto& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, same_frame>) {
            if (const auto stack_opt = arg.stack) {
              const auto stack = write_frame_var(*stack_opt);
              if (delta <= 63) {
                buf.write_u8(static_cast<uint8_t>(delta + 64));
              } else {
                buf.write_u8(247);
                buf.write_u16(delta);
              }
              buf.write_all(stack);
            } else {
              if (delta <= 63) {
                buf.write_u8(static_cast<uint8_t>(delta));
              } else {
                buf.write_u8(251);
                buf.write_u16(delta);
              }
            }
          } else if constexpr (std::is_same_v<T, full_frame>) {
            buf.write_u8(255);
            buf.write_u16(delta);
            buf.write_u16(static_cast<uint16_t>(arg.locals.size()));
            for (const auto& local : arg.locals) {
              buf.write_all(write_frame_var(local));
            }
            buf.write_u16(static_cast<uint16_t>(arg.stack.size()));
            for (const auto& stack : arg.stack) {
              buf.write_all(write_frame_var(stack));
            }
          } else if constexpr (std::is_same_v<T, chop_frame>) {
            buf.write_u8(static_cast<uint8_t>(251 - arg.size));
            buf.write_u16(delta);
          } else if constexpr (std::is_same_v<T, append_frame>) {
            buf.write_u8(static_cast<uint8_t>(arg.locals.size() + 251));
            buf.write_u16(delta);
            for (const auto& local : arg.locals) {
              buf.write_all(write_frame_var(local));
            }
          }
        },
        frame);
    const auto data = buf.data();
    frames_.insert(frames_.end(), data.begin(), data.end());
  }
  for (const auto& [annotation, visible] : type_annotations_) {
    const auto data = writer_.get_type_annotation(annotation, labels_);
    if (visible) {
      visible_type_annotations_count_++;
      visible_type_annotations_.insert(visible_type_annotations_.end(), data.begin(), data.end());
    } else {
      invisible_type_annotations_count_++;
      invisible_type_annotations_.insert(invisible_type_annotations_.end(), data.begin(), data.end());
    }
  }
}
void code_writer::shift(size_t start, size_t offset) {
  for (auto& pos : switch_pads_) {
    if (pos >= start) {
      pos += offset;
    }
  }
  for (auto& [target, pos, wide, insn_start] : branches_) {
    if (pos >= start) {
      pos += offset;
    }
  }
  for (auto& [pos, label] : labels_) {
    if (pos >= start) {
      pos += offset;
    }
  }
}
size_t code_writer::get_label(const label& lbl) {
  for (const auto& [pos, label] : labels_) {
    if (label == lbl) {
      return pos;
    }
  }
  return 0;
}
size_t code_writer::get_estimated(const label& lbl) {
  for (const auto& [label, pos] : estimates_) {
    if (label == lbl) {
      return pos;
    }
  }
  return 0;
}

std::vector<int8_t> code_writer::write_frame_var(const frame_var& var) {
  bytebuf buf;
  std::visit(
      [this, &buf](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, top_var>) {
          buf.write_u8(0);
        } else if constexpr (std::is_same_v<T, int_var>) {
          buf.write_u8(1);
        } else if constexpr (std::is_same_v<T, float_var>) {
          buf.write_u8(2);
        } else if constexpr (std::is_same_v<T, null_var>) {
          buf.write_u8(5);
        } else if constexpr (std::is_same_v<T, uninitialized_this_var>) {
          buf.write_u8(6);
        } else if constexpr (std::is_same_v<T, object_var>) {
          const auto cpool_index = writer_.get_class(arg.type);
          buf.write_u8(7);
          buf.write_u16(cpool_index);
        } else if constexpr (std::is_same_v<T, uninitialized_var>) {
          const auto offset_pc = get_label(arg.offset);
          buf.write_u8(8);
          buf.write_u16(static_cast<uint16_t>(offset_pc));
        } else if constexpr (std::is_same_v<T, long_var>) {
          buf.write_u8(4);
        } else if constexpr (std::is_same_v<T, double_var>) {
          buf.write_u8(3);
        }
      },
      var);
  return buf.data();
}
uint16_t code_writer::next_delta(const label& target) {
  const auto pos = get_label(target);
  auto delta = static_cast<uint16_t>(pos - last_label_);
  if (last_label_ != 0) {
    delta--;
  }
  last_label_ = pos;
  return delta;
}
field_writer::field_writer(class_writer& writer) : writer_(writer) {
}
void field_writer::visit(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) {
  access_flags_ = access_flags;
  name_index_ = writer_.get_utf(name);
  descriptor_index_ = writer_.get_utf(descriptor);
}
void field_writer::visit_constant_value(const value& constant_value) {
  const auto attr_name = writer_.get_utf("ConstantValue");
  const auto index = writer_.get_value(constant_value);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(2);
  buf.write_u16(index);
  const auto data = buf.data();
  attributes_count_++;
  attributes_.insert(attributes_.end(), data.begin(), data.end());
}
void field_writer::visit_synthetic(bool synthetic) {
  if (!synthetic) {
    return;
  }
  const auto attr_name = writer_.get_utf("Synthetic");
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(0);
  const auto data = buf.data();
  attributes_count_++;
  attributes_.insert(attributes_.end(), data.begin(), data.end());
}
void field_writer::visit_deprecated(bool deprecated) {
  if (!deprecated) {
    return;
  }
  const auto attr_name = writer_.get_utf("Deprecated");
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(0);
  const auto data = buf.data();
  attributes_count_++;
  attributes_.insert(attributes_.end(), data.begin(), data.end());
}
void field_writer::visit_signature(const std::string_view& signature) {
  const auto attr_name = writer_.get_utf("Signature");
  const auto sig = writer_.get_utf(signature);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(2);
  buf.write_u16(sig);
}
void field_writer::visit_visible_annotation(const annotation& annotation) {
  visible_annotations_count_++;
  const auto data = writer_.get_annotation(annotation);
  visible_annotations_.insert(visible_annotations_.end(), data.begin(), data.end());
}
void field_writer::visit_invisible_annotation(const annotation& annotation) {
  invisible_annotations_count_++;
  const auto data = writer_.get_annotation(annotation);
  invisible_annotations_.insert(invisible_annotations_.end(), data.begin(), data.end());
}
void field_writer::visit_visible_type_annotation(const type_annotation& annotation) {
  visible_type_annotations_count_++;
  std::vector<std::pair<size_t, label>> dummy_labels;
  const auto data = writer_.get_type_annotation(annotation, dummy_labels);
  visible_type_annotations_.insert(visible_type_annotations_.end(), data.begin(), data.end());
}
void field_writer::visit_invisible_type_annotation(const type_annotation& annotation) {
  invisible_type_annotations_count_++;
  std::vector<std::pair<size_t, label>> dummy_labels;
  const auto data = writer_.get_type_annotation(annotation, dummy_labels);
  invisible_type_annotations_.insert(invisible_type_annotations_.end(), data.begin(), data.end());
}
void field_writer::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
  attributes_count_++;
  const auto attr_name = writer_.get_utf(name);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>(data.size()));
  buf.write_all(data);
  const auto info = buf.data();
  attributes_.insert(attributes_.end(), info.begin(), info.end());
}
method_writer::method_writer(class_writer& writer) : writer_(writer) {
}
void method_writer::visit(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) {
  access_flags_ = access_flags;
  name_index_ = writer_.get_utf(name);
  descriptor_index_ = writer_.get_utf(descriptor);
}
code_visitor& method_writer::visit_code(uint16_t max_stack, uint16_t max_locals) {
  code_.emplace(writer_, *this);
  code_->visit(max_stack, max_locals);
  return code_.value();
}
void method_writer::visit_exception(const std::string_view& exception) {
  exceptions_.emplace_back(writer_.get_class(exception));
}
void method_writer::visit_visible_parameter_annotation(uint8_t parameter, const annotation& annotation) {
  if (visible_parameter_annotations_.size() <= parameter) {
    visible_parameter_annotations_.resize(parameter + 1);
    visible_parameter_annotations_count_.resize(parameter + 1);
  }
  visible_parameter_annotations_count_[parameter]++;
  const auto data = writer_.get_annotation(annotation);
  visible_parameter_annotations_[parameter].insert(visible_parameter_annotations_[parameter].end(), data.begin(),
                                                   data.end());
}
void method_writer::visit_invisible_parameter_annotation(uint8_t parameter, const annotation& annotation) {
  if (invisible_parameter_annotations_.size() <= parameter) {
    invisible_parameter_annotations_.resize(parameter + 1);
    invisible_parameter_annotations_count_.resize(parameter + 1);
  }
  invisible_parameter_annotations_count_[parameter]++;
  const auto data = writer_.get_annotation(annotation);
  invisible_parameter_annotations_[parameter].insert(invisible_parameter_annotations_[parameter].end(), data.begin(),
                                                     data.end());
}
void method_writer::visit_annotation_default(const element_value& value) {
  const auto attr_name = writer_.get_utf("AnnotationDefault");
  const auto data = writer_.get_element_value(value);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>(data.size()));
  buf.write_all(data);
}
void method_writer::visit_parameter(uint16_t access_flags, const std::string_view& name) {
  const auto name_index = static_cast<uint16_t>(name.empty() ? 0 : writer_.get_utf(name));
  parameters_count_++;
  parameters_.emplace_back(static_cast<int8_t>((name_index >> 8) & 0xff));
  parameters_.emplace_back(static_cast<int8_t>(name_index & 0xff));
  parameters_.emplace_back(static_cast<int8_t>((access_flags >> 8) & 0xff));
  parameters_.emplace_back(static_cast<int8_t>(access_flags & 0xff));
}
void method_writer::visit_synthetic(bool synthetic) {
  if (!synthetic) {
    return;
  }
  const auto attr_name = writer_.get_utf("Synthetic");
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(0);
  const auto data = buf.data();
  attributes_count_++;
  attributes_.insert(attributes_.end(), data.begin(), data.end());
}
void method_writer::visit_deprecated(bool deprecated) {
  if (!deprecated) {
    return;
  }
  const auto attr_name = writer_.get_utf("Deprecated");
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(0);
}
void method_writer::visit_signature(const std::string_view& signature) {
  const auto attr_name = writer_.get_utf("Signature");
  const auto sig = writer_.get_utf(signature);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(2);
  buf.write_u16(sig);
  const auto data = buf.data();
  attributes_count_++;
  attributes_.insert(attributes_.end(), data.begin(), data.end());
}
void method_writer::visit_visible_annotation(const annotation& annotation) {
  visible_annotations_count_++;
  const auto data = writer_.get_annotation(annotation);
  visible_annotations_.insert(visible_annotations_.end(), data.begin(), data.end());
}
void method_writer::visit_invisible_annotation(const annotation& annotation) {
  invisible_annotations_count_++;
  const auto data = writer_.get_annotation(annotation);
  invisible_annotations_.insert(invisible_annotations_.end(), data.begin(), data.end());
}
void method_writer::visit_visible_type_annotation(const type_annotation& annotation) {
  std::vector<std::pair<size_t, label>> dummy_labels;
  visible_type_annotations_count_++;
  const auto data = writer_.get_type_annotation(annotation, dummy_labels);
  visible_type_annotations_.insert(visible_type_annotations_.end(), data.begin(), data.end());
}
void method_writer::visit_invisible_type_annotation(const type_annotation& annotation) {
  std::vector<std::pair<size_t, label>> dummy_labels;
  invisible_type_annotations_count_++;
  const auto data = writer_.get_type_annotation(annotation, dummy_labels);
  invisible_type_annotations_.insert(invisible_type_annotations_.end(), data.begin(), data.end());
}
void method_writer::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
  attributes_count_++;
  const auto attr_name = writer_.get_utf(name);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>(data.size()));
  buf.write_all(data);
  const auto info = buf.data();
  attributes_.insert(attributes_.end(), info.begin(), info.end());
}
class_writer::class_writer() {
  pool_.emplace_back(cp::pad_info{});
}
void class_writer::visit(uint32_t version, uint16_t access_flags, const std::string_view& name,
                         const std::string_view& super_name) {
  version_ = version;
  access_flags_ = access_flags;
  this_class_ = get_class(name);
  if (!super_name.empty()) {
    super_class_ = get_class(super_name);
  }
}
void class_writer::visit_interface(const std::string_view& name) {
  interfaces_.push_back(get_class(name));
}
field_visitor& class_writer::visit_field(uint16_t access_flags, const std::string_view& name,
                                         const std::string_view& descriptor) {
  fields_.emplace_back(*this);
  fields_.back().visit(access_flags, name, descriptor);
  return fields_.back();
}
method_visitor& class_writer::visit_method(uint16_t access_flags, const std::string_view& name,
                                           const std::string_view& descriptor) {
  methods_.emplace_back(*this);
  methods_.back().visit(access_flags, name, descriptor);
  return methods_.back();
}
void class_writer::visit_source_file(const std::string_view& source_file) {
  const auto attr_name = get_utf("SourceFile");
  const auto source_index = get_utf(source_file);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(2);
  buf.write_u16(source_index);
  const auto data = buf.data();
  attributes_count_++;
  attributes_.insert(attributes_.end(), data.begin(), data.end());
}
void class_writer::visit_inner_class(const std::string_view& name, const std::string_view& outer_name,
                                     const std::string_view& inner_name, uint16_t access_flags) {
  const auto name_index = get_class(name);
  const auto outer_index = outer_name.empty() ? 0 : get_class(outer_name);
  const auto inner_index = inner_name.empty() ? 0 : get_utf(inner_name);
  inner_classes_.emplace_back(name_index, outer_index, inner_index, access_flags);
}
void class_writer::visit_enclosing_method(const std::string_view& owner, const std::string_view& name,
                                          const std::string_view& descriptor) {
  const auto attr_name = get_utf("EnclosingMethod");
  const auto class_index = get_class(owner);
  const auto nat_index = get_name_and_type(name, descriptor);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(4);
  buf.write_u16(class_index);
  buf.write_u16(nat_index);
}
void class_writer::visit_source_debug_extension(const std::string_view& debug_extension) {
  const auto attr_name = get_utf("SourceDebugExtension");
  const auto data = std::vector<int8_t>(debug_extension.begin(), debug_extension.end());
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>(data.size()));
  buf.write_all(data);
  const auto info = buf.data();
  attributes_count_++;
  attributes_.insert(attributes_.end(), info.begin(), info.end());
}
module_visitor& class_writer::visit_module(const std::string_view& name, uint16_t access_flags,
                                           const std::string_view& version) {
  module_.emplace(*this);
  module_->visit(name, access_flags, version);
  return module_.value();
}
void class_writer::visit_module_package(const std::string_view& package) {
  module_packages_.emplace_back(get_package(package));
}
void class_writer::visit_module_main_class(const std::string_view& main_class) {
  const auto attr_name = get_utf("ModuleMainClass");
  const auto module_class = get_class(main_class);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(2);
  buf.write_u16(module_class);
}
void class_writer::visit_nest_host(const std::string_view& host) {
  const auto attr_name = get_utf("NestHost");
  const auto nest_host = get_class(host);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(2);
  buf.write_u16(nest_host);
}
void class_writer::visit_nest_member(const std::string_view& members) {
  nest_members_.emplace_back(get_class(members));
}
record_component_visitor& class_writer::visit_record_component(const std::string_view& name,
                                                               const std::string_view& descriptor) {
  record_components_.emplace_back(*this);
  record_components_.back().visit(name, descriptor);
  return record_components_.back();
}
void class_writer::visit_permitted_subclass(const std::string_view& subclass) {
  permitted_subclasses_.emplace_back(get_class(subclass));
}
void class_writer::visit_synthetic(bool synthetic) {
  if (!synthetic) {
    return;
  }
  const auto attr_name = get_utf("Synthetic");
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(0);
  const auto data = buf.data();
  attributes_count_++;
  attributes_.insert(attributes_.end(), data.begin(), data.end());
}
void class_writer::visit_deprecated(bool deprecated) {
  if (!deprecated) {
    return;
  }
  const auto attr_name = get_utf("Deprecated");
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(0);
  const auto data = buf.data();
  attributes_count_++;
  attributes_.insert(attributes_.end(), data.begin(), data.end());
}
void class_writer::visit_signature(const std::string_view& signature) {
  const auto attr_name = get_utf("Signature");
  const auto sig = get_utf(signature);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(2);
  buf.write_u16(sig);
  const auto data = buf.data();
  attributes_count_++;
  attributes_.insert(attributes_.end(), data.begin(), data.end());
}
void class_writer::visit_visible_annotation(const annotation& annotation) {
  visible_annotations_count_++;
  const auto data = get_annotation(annotation);
  visible_annotations_.insert(visible_annotations_.end(), data.begin(), data.end());
}
void class_writer::visit_invisible_annotation(const annotation& annotation) {
  invisible_annotations_count_++;
  const auto data = get_annotation(annotation);
  invisible_annotations_.insert(invisible_annotations_.end(), data.begin(), data.end());
}
void class_writer::visit_visible_type_annotation(const type_annotation& annotation) {
  std::vector<std::pair<size_t, label>> dummy_labels;
  visible_type_annotations_count_++;
  const auto data = get_type_annotation(annotation, dummy_labels);
  visible_type_annotations_.insert(visible_type_annotations_.end(), data.begin(), data.end());
}
void class_writer::visit_invisible_type_annotation(const type_annotation& annotation) {
  std::vector<std::pair<size_t, label>> dummy_labels;
  invisible_type_annotations_count_++;
  const auto data = get_type_annotation(annotation, dummy_labels);
  invisible_type_annotations_.insert(invisible_type_annotations_.end(), data.begin(), data.end());
}
void class_writer::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
  attributes_count_++;
  const auto attr_name = get_utf(name);
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>(data.size()));
  buf.write_all(data);
  const auto info = buf.data();
  attributes_.insert(attributes_.end(), info.begin(), info.end());
}
uint16_t class_writer::get_class(const std::string_view& name) {
  const auto utf_index = get_utf(name);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto cls = std::get_if<cp::class_info>(&pool_.at(i))) {
      if (cls->name_index == utf_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::class_info{utf_index});
  return static_cast<uint32_t>(index);
}
uint16_t class_writer::get_field_ref(const std::string_view& owner, const std::string_view& name,
                                     const std::string_view& descriptor) {
  const auto class_index = get_class(owner);
  const auto nat_index = get_name_and_type(name, descriptor);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto field = std::get_if<cp::field_ref_info>(&pool_.at(i))) {
      if (field->class_index == class_index && field->name_and_type_index == nat_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::field_ref_info{class_index, nat_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_method_ref(const std::string_view& owner, const std::string_view& name,
                                      const std::string_view& descriptor) {
  const auto class_index = get_class(owner);
  const auto nat_index = get_name_and_type(name, descriptor);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto method = std::get_if<cp::method_ref_info>(&pool_.at(i))) {
      if (method->class_index == class_index && method->name_and_type_index == nat_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::method_ref_info{class_index, nat_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_interface_method_ref(const std::string_view& owner, const std::string_view& name,
                                                const std::string_view& descriptor) {
  const auto class_index = get_class(owner);
  const auto nat_index = get_name_and_type(name, descriptor);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto method = std::get_if<cp::interface_method_ref_info>(&pool_.at(i))) {
      if (method->class_index == class_index && method->name_and_type_index == nat_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::interface_method_ref_info{class_index, nat_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_string(const std::string_view& str) {
  const auto utf_index = get_utf(str);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto string = std::get_if<cp::string_info>(&pool_.at(i))) {
      if (string->string_index == utf_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::string_info{utf_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_int(int32_t value) {
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto integer = std::get_if<cp::integer_info>(&pool_.at(i))) {
      if (integer->value == value) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::integer_info{value});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_float(float value) {
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto flt = std::get_if<cp::float_info>(&pool_.at(i))) {
      if (flt->value == value) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::float_info{value});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_long(int64_t value) {
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto lng = std::get_if<cp::long_info>(&pool_.at(i))) {
      if (lng->value == value) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::long_info{value});
  pool_.emplace_back(cp::pad_info{});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_double(double value) {
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto dbl = std::get_if<cp::double_info>(&pool_.at(i))) {
      if (dbl->value == value) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::double_info{value});
  pool_.emplace_back(cp::pad_info{});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_name_and_type(const std::string_view& name, const std::string_view& descriptor) {
  const auto name_index = get_utf(name);
  const auto descriptor_index = get_utf(descriptor);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto nat = std::get_if<cp::name_and_type_info>(&pool_.at(i))) {
      if (nat->name_index == name_index && nat->descriptor_index == descriptor_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::name_and_type_info{name_index, descriptor_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_utf(const std::string_view& utf) {
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto str = std::get_if<cp::utf8_info>(&pool_.at(i))) {
      if (str->value == utf) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::utf8_info{std::string(utf)});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_method_handle(uint8_t kind, const std::string_view& owner, const std::string_view& name,
                                         const std::string_view& descriptor) {
  uint16_t reference_index;
  switch (kind) {
    case reference_kind::get_field:
    case reference_kind::get_static:
    case reference_kind::put_field:
    case reference_kind::put_static:
      reference_index = get_field_ref(owner, name, descriptor);
      break;
    case reference_kind::invoke_virtual:
    case reference_kind::new_invoke_special:
    case reference_kind::invoke_static:
    case reference_kind::invoke_special:
      reference_index = get_method_ref(owner, name, descriptor);
      break;
    case reference_kind::invoke_interface:
      reference_index = get_interface_method_ref(owner, name, descriptor);
      break;
    default:
      throw std::invalid_argument("Invalid reference kind");
  }
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto handle = std::get_if<cp::method_handle_info>(&pool_.at(i))) {
      if (handle->reference_kind == kind && handle->reference_index == reference_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::method_handle_info{kind, reference_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_method_type(const std::string_view& descriptor) {
  const auto utf_index = get_utf(descriptor);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto type = std::get_if<cp::method_type_info>(&pool_.at(i))) {
      if (type->descriptor_index == utf_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::method_type_info{utf_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_dynamic(const std::string_view& name, const std::string_view& descriptor,
                                   const method_handle& handle, const std::vector<value>& args) {
  const auto bsm_index = get_bsm(handle, args);
  const auto nat_index = get_name_and_type(name, descriptor);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto dynamic = std::get_if<cp::dynamic_info>(&pool_.at(i))) {
      if (dynamic->bootstrap_method_attr_index == bsm_index && dynamic->name_and_type_index == nat_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::dynamic_info{bsm_index, nat_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_invoke_dynamic(const std::string_view& name, const std::string_view& descriptor,
                                          const method_handle& handle, const std::vector<value>& args) {
  const auto bsm_index = get_bsm(handle, args);
  const auto nat_index = get_name_and_type(name, descriptor);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto invoke = std::get_if<cp::invoke_dynamic_info>(&pool_.at(i))) {
      if (invoke->bootstrap_method_attr_index == bsm_index && invoke->name_and_type_index == nat_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::invoke_dynamic_info{bsm_index, nat_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_module(const std::string_view& name) {
  const auto utf_index = get_utf(name);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto module = std::get_if<cp::module_info>(&pool_.at(i))) {
      if (module->name_index == utf_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::module_info{utf_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_package(const std::string_view& name) {
  const auto utf_index = get_utf(name);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto package = std::get_if<cp::package_info>(&pool_.at(i))) {
      if (package->name_index == utf_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::package_info{utf_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_value(const value& val) {
  return std::visit(
      [this](const auto& arg) -> uint16_t {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int32_t>) {
          return get_int(arg);
        } else if constexpr (std::is_same_v<T, float>) {
          return get_float(arg);
        } else if constexpr (std::is_same_v<T, int64_t>) {
          return get_long(arg);
        } else if constexpr (std::is_same_v<T, double>) {
          return get_double(arg);
        } else if constexpr (std::is_same_v<T, class_value>) {
          return get_class(arg.get());
        } else if constexpr (std::is_same_v<T, std::string>) {
          return get_string(arg);
        } else if constexpr (std::is_same_v<T, method_handle>) {
          return get_method_handle(arg.kind, arg.owner, arg.name, arg.descriptor);
        } else if constexpr (std::is_same_v<T, method_type>) {
          return get_method_type(arg.descriptor);
        } else if constexpr (std::is_same_v<T, dynamic>) {
          return get_dynamic(arg.name, arg.descriptor, arg.handle, arg.args);
        }
      },
      val);
}
uint16_t class_writer::get_bsm(const method_handle& handle, const std::vector<value>& args) {
  const auto handle_index = get_method_handle(handle.kind, handle.owner, handle.name, handle.descriptor);
  std::vector<uint16_t> arg_indices;
  arg_indices.reserve(args.size());
  for (const auto& arg : args) {
    arg_indices.emplace_back(get_value(arg));
  }
  for (uint16_t i = 0; i < bsm_buffer_.size(); i++) {
    if (bsm_buffer_[i].first == handle_index && bsm_buffer_[i].second == arg_indices) {
      return i;
    }
  }
  const auto index = bsm_buffer_.size();
  bsm_buffer_.emplace_back(handle_index, arg_indices);
  return static_cast<uint16_t>(index);
}
std::vector<int8_t> class_writer::get_annotation(const annotation& anno) {
  const auto type_index = get_utf(anno.descriptor);
  bytebuf buf;
  buf.write_u16(type_index);
  buf.write_u16(static_cast<uint16_t>(anno.values.size()));
  for (const auto& [name, value] : anno.values) {
    const auto element_name_index = get_utf(name);
    const auto element_value = get_element_value(value);
    buf.write_u16(element_name_index);
    buf.write_all(element_value);
  }
  return buf.data();
}
std::vector<int8_t> class_writer::get_element_value(const element_value& value) {
  bytebuf buf;
  std::visit(
      [this, &buf](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int8_t>) {
          buf.write_u8('B');
          buf.write_u16(get_int(arg));
        } else if constexpr (std::is_same_v<T, uint16_t>) {
          buf.write_u8('C');
          buf.write_u16(get_int(arg));
        } else if constexpr (std::is_same_v<T, double>) {
          buf.write_u8('D');
          buf.write_u16(get_double(arg));
        } else if constexpr (std::is_same_v<T, float>) {
          buf.write_u8('F');
          buf.write_u16(get_float(arg));
        } else if constexpr (std::is_same_v<T, int32_t>) {
          buf.write_u8('I');
          buf.write_u16(get_int(arg));
        } else if constexpr (std::is_same_v<T, int64_t>) {
          buf.write_u8('J');
          buf.write_u16(get_long(arg));
        } else if constexpr (std::is_same_v<T, int16_t>) {
          buf.write_u8('S');
          buf.write_u16(get_int(arg));
        } else if constexpr (std::is_same_v<T, bool>) {
          buf.write_u8('Z');
          buf.write_u16(get_int(arg));
        } else if constexpr (std::is_same_v<T, std::string>) {
          buf.write_u8('s');
          buf.write_u16(get_string(arg));
        } else if constexpr (std::is_same_v<T, std::pair<std::string, std::string>>) {
          buf.write_u8('e');
          buf.write_u16(get_utf(arg.first));
          buf.write_u16(get_utf(arg.second));
        } else if constexpr (std::is_same_v<T, class_value>) {
          buf.write_u8('c');
          buf.write_u16(get_class(arg.get()));
        } else if constexpr (std::is_same_v<T, annotation>) {
          buf.write_u8('@');
          buf.write_all(get_annotation(arg));
        } else if constexpr (std::is_same_v<T, std::vector<element_value>>) {
          buf.write_u8('[');
          buf.write_u16(static_cast<uint16_t>(arg.size()));
          for (const auto& elem : arg) {
            buf.write_all(get_element_value(elem));
          }
        }
      },
      value.value);
  return buf.data();
}
std::vector<int8_t> class_writer::get_type_annotation(const type_annotation& anno,
                                                      const std::vector<std::pair<size_t, label>>& labels) {
  const auto get_label = [&labels](const label& lbl) -> size_t {
    for (const auto& [idx, label] : labels) {
      if (lbl == label) {
        return idx;
      }
    }
    return 0;
  };
  bytebuf buf;
  buf.write_u8(anno.target_type);
  std::visit(
      [this, &buf, &get_label](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, target::type_parameter>) {
          buf.write_u8(arg.index);
        } else if constexpr (std::is_same_v<T, target::supertype>) {
          buf.write_u16(arg.index);
        } else if constexpr (std::is_same_v<T, target::type_parameter_bound>) {
          buf.write_u8(arg.type_parameter_index);
          buf.write_u8(arg.bound_index);
        } else if constexpr (std::is_same_v<T, target::empty>) {
          // Do nothing
        } else if constexpr (std::is_same_v<T, target::formal_parameter>) {
          buf.write_u8(arg.index);
        } else if constexpr (std::is_same_v<T, target::throws>) {
          buf.write_u16(arg.index);
        } else if constexpr (std::is_same_v<T, target::localvar>) {
          buf.write_u16(static_cast<uint16_t>(arg.table.size()));
          for (const auto& local : arg.table) {
            const auto start_pc = get_label(local.start);
            const auto end_pc = get_label(local.end);
            const auto length = end_pc - start_pc;
            buf.write_u16(static_cast<uint16_t>(start_pc));
            buf.write_u16(static_cast<uint16_t>(length));
            buf.write_u16(local.index);
          }
        } else if constexpr (std::is_same_v<T, target::catch_target>) {
          buf.write_u16(arg.index);
        } else if constexpr (std::is_same_v<T, target::offset_target>) {
          const auto offset_pc = get_label(arg.offset);
          buf.write_u16(static_cast<uint16_t>(offset_pc));
        } else if constexpr (std::is_same_v<T, target::type_argument>) {
          const auto offset_pc = get_label(arg.offset);
          buf.write_u16(static_cast<uint16_t>(offset_pc));
          buf.write_u8(arg.index);
        }
      },
      anno.target_info);
  buf.write_u8(static_cast<uint8_t>(anno.target_path.path.size()));
  for (const auto& [kind, index] : anno.target_path.path) {
    buf.write_u8(kind);
    buf.write_u8(index);
  }
  const auto type_index = get_utf(anno.descriptor);
  buf.write_u16(type_index);
  buf.write_u16(static_cast<uint16_t>(anno.values.size()));
  for (const auto& [name, value] : anno.values) {
    const auto element_name_index = get_utf(name);
    const auto element_value = get_element_value(value);
    buf.write_u16(element_name_index);
    buf.write_all(element_value);
  }
  return buf.data();
}
void record_component_writer::visit_end() {
  if (visible_annotations_count_ > 0) {
    const auto attr_name = writer_.get_utf("RuntimeVisibleAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(visible_annotations_count_ + 2);
    buf.write_u16(visible_annotations_count_);
    buf.write_all(visible_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (invisible_annotations_count_ > 0) {
    const auto attr_name = writer_.get_utf("RuntimeInvisibleAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(invisible_annotations_count_ + 2);
    buf.write_u16(invisible_annotations_count_);
    buf.write_all(invisible_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (visible_type_annotations_count_ > 0) {
    const auto attr_name = writer_.get_utf("RuntimeVisibleTypeAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(visible_type_annotations_count_ + 2);
    buf.write_u16(visible_type_annotations_count_);
    buf.write_all(visible_type_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (invisible_type_annotations_count_ > 0) {
    const auto attr_name = writer_.get_utf("RuntimeInvisibleTypeAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(invisible_type_annotations_count_ + 2);
    buf.write_u16(invisible_type_annotations_count_);
    buf.write_all(invisible_type_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  bytebuf buf;
  buf.write_u16(name_index_);
  buf.write_u16(descriptor_index_);
  buf.write_u16(attributes_count_);
  buf.write_all(attributes_);
  const auto data = buf.data();
  writer_.record_components_count_++;
  writer_.record_components_bin_.insert(writer_.record_components_bin_.end(), data.begin(), data.end());
}
void module_writer::visit_end() {
  const auto attr_name = writer_.get_utf("Module");
  bytebuf buf;
  buf.write_u16(name_index_);
  buf.write_u16(module_flags_);
  buf.write_u16(version_index_);
  buf.write_u16(requires_count_);
  buf.write_all(requires_);
  buf.write_u16(exports_count_);
  buf.write_all(exports_);
  buf.write_u16(opens_count_);
  buf.write_all(opens_);
  buf.write_u16(static_cast<uint16_t>(uses_.size()));
  for (const auto& use : uses_) {
    buf.write_u16(use);
  }
  buf.write_u16(provides_count_);
  buf.write_all(provides_);
  const auto data = buf.data();
  bytebuf attrbuf;
  attrbuf.write_u16(attr_name);
  attrbuf.write_u32(static_cast<uint32_t>(data.size()));
  attrbuf.write_all(data);
  const auto mod = attrbuf.data();
  writer_.attributes_count_++;
  writer_.attributes_.insert(writer_.attributes_.end(), mod.begin(), mod.end());
}
void code_writer::visit_end() {
  for (auto& [label, pos, wide, insn_start] : branches_) {
    if (code_[insn_start] == op::goto_ || code_[insn_start] == op::jsr) {
      const auto est = get_estimated(label);
      const auto offset = static_cast<int32_t>(est - pos);
      if (offset > std::numeric_limits<int16_t>::max() || offset < std::numeric_limits<int16_t>::min()) {
        if (code_[insn_start] == op::goto_) {
          code_[insn_start] = op::goto_w;
        } else if (code_[insn_start] == op::jsr) {
          code_[insn_start] = op::jsr_w;
        }
        wide = true;
        code_.insert(code_.begin() + pos, 2, 0);
        shift(pos, 2);
      }
    }
  }
  for (const auto& pos : switch_pads_) {
    const auto padding = (4 - (pos % 4)) % 4;
    if (padding > 0) {
      code_.insert(code_.begin() + pos, padding, 0);
      shift(pos, padding);
    }
  }
  for (const auto& [label, pos, wide, insn_start] : branches_) {
    const auto target = get_label(label);
    if (wide) {
      const auto offset = static_cast<int32_t>(target - insn_start);
      code_[pos] = static_cast<int8_t>((offset >> 24) & 0xff);
      code_[pos + 1] = static_cast<int8_t>((offset >> 16) & 0xff);
      code_[pos + 2] = static_cast<int8_t>((offset >> 8) & 0xff);
      code_[pos + 3] = static_cast<int8_t>(offset & 0xff);
    } else {
      const auto offset = static_cast<int16_t>(target - insn_start);
      code_[pos] = static_cast<int8_t>((offset >> 8) & 0xff);
      code_[pos + 1] = static_cast<int8_t>(offset & 0xff);
    }
  }

  finish_attributes();

  if (line_numbers_count_ > 0) {
    bytebuf attrbuf;
    const auto attr_name = writer_.get_utf("LineNumberTable");
    attrbuf.write_u16(attr_name);
    attrbuf.write_u32(static_cast<uint32_t>(line_numbers_.size() + 2));
    attrbuf.write_u16(line_numbers_count_);
    attrbuf.write_all(line_numbers_);
    const auto data = attrbuf.data();
    attributes_count_++;
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (local_vars_count_ > 0) {
    bytebuf attrbuf;
    const auto attr_name = writer_.get_utf("LocalVariableTable");
    attrbuf.write_u16(attr_name);
    attrbuf.write_u32(static_cast<uint32_t>(local_vars_.size() + 2));
    attrbuf.write_u16(local_vars_count_);
    attrbuf.write_all(local_vars_);
    const auto data = attrbuf.data();
    attributes_count_++;
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (local_type_vars_count_ > 0) {
    bytebuf attrbuf;
    const auto attr_name = writer_.get_utf("LocalVariableTypeTable");
    attrbuf.write_u16(attr_name);
    attrbuf.write_u32(static_cast<uint32_t>(local_type_vars_.size() + 2));
    attrbuf.write_u16(local_type_vars_count_);
    attrbuf.write_all(local_type_vars_);
    const auto data = attrbuf.data();
    attributes_count_++;
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (frames_count_ > 0) {
    bytebuf attrbuf;
    const auto attr_name = writer_.get_utf("StackMapTable");
    attrbuf.write_u16(attr_name);
    attrbuf.write_u32(static_cast<uint32_t>(frames_.size() + 2));
    attrbuf.write_u16(frames_count_);
    attrbuf.write_all(frames_);
    const auto data = attrbuf.data();
    attributes_count_++;
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (visible_type_annotations_count_ > 0) {
    bytebuf attrbuf;
    const auto attr_name = writer_.get_utf("RuntimeVisibleTypeAnnotations");
    attrbuf.write_u16(attr_name);
    attrbuf.write_u32(static_cast<uint32_t>(visible_type_annotations_.size() + 2));
    attrbuf.write_u16(visible_type_annotations_count_);
    attrbuf.write_all(visible_type_annotations_);
    const auto data = attrbuf.data();
    attributes_count_++;
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (invisible_type_annotations_count_ > 0) {
    bytebuf attrbuf;
    const auto attr_name = writer_.get_utf("RuntimeInvisibleTypeAnnotations");
    attrbuf.write_u16(attr_name);
    attrbuf.write_u32(static_cast<uint32_t>(invisible_type_annotations_.size() + 2));
    attrbuf.write_u16(invisible_type_annotations_count_);
    attrbuf.write_all(invisible_type_annotations_);
    const auto data = attrbuf.data();
    attributes_count_++;
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  const auto oak = writer_.version_ < class_version::v1_1;
  bytebuf codebuf;
  if (oak) {
    codebuf.write_u8(static_cast<uint8_t>(max_stack_));
    codebuf.write_u8(static_cast<uint8_t>(max_locals_));
    codebuf.write_u16(static_cast<uint16_t>(code_.size()));
  } else {
    codebuf.write_u16(max_stack_);
    codebuf.write_u16(max_locals_);
    codebuf.write_u32(static_cast<uint32_t>(code_.size()));
  }
  codebuf.write_all(code_);
  codebuf.write_u16(exception_table_count_);
  codebuf.write_all(exception_table_);
  codebuf.write_u16(attributes_count_);
  codebuf.write_all(attributes_);

  const auto result = codebuf.data();
  const auto attr_name = writer_.get_utf("Code");
  bytebuf buf;
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>(result.size()));
  buf.write_all(result);
  const auto code_attr = buf.data();
  parent_.attributes_count_++;
  parent_.attributes_.insert(parent_.attributes_.end(), code_attr.begin(), code_attr.end());
}
void field_writer::visit_end() {
  if (visible_annotations_count_ > 0) {
    const auto attr_name = writer_.get_utf("RuntimeVisibleAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(visible_annotations_.size() + 2));
    buf.write_u16(visible_annotations_count_);
    buf.write_all(visible_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (invisible_annotations_count_ > 0) {
    const auto attr_name = writer_.get_utf("RuntimeInvisibleAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(invisible_annotations_.size() + 2));
    buf.write_u16(invisible_annotations_count_);
    buf.write_all(invisible_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (visible_type_annotations_count_ > 0) {
    const auto attr_name = writer_.get_utf("RuntimeVisibleTypeAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(visible_type_annotations_.size() + 2));
    buf.write_u16(visible_type_annotations_count_);
    buf.write_all(visible_type_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (invisible_type_annotations_count_ > 0) {
    const auto attr_name = writer_.get_utf("RuntimeInvisibleTypeAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(invisible_type_annotations_.size() + 2));
    buf.write_u16(invisible_type_annotations_count_);
    buf.write_all(invisible_type_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  bytebuf buf;
  buf.write_u16(access_flags_);
  buf.write_u16(name_index_);
  buf.write_u16(descriptor_index_);
  buf.write_u16(attributes_count_);
  buf.write_all(attributes_);
  const auto data = buf.data();
  writer_.fields_count_++;
  writer_.fields_bin_.insert(writer_.fields_bin_.end(), data.begin(), data.end());
}
void method_writer::visit_end() {
  if (!exceptions_.empty()) {
    const auto attr_name = writer_.get_utf("Exceptions");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>((exceptions_.size() * 2) + 2));
    buf.write_u16(static_cast<uint16_t>(exceptions_.size()));
    for (const auto& i : exceptions_) {
      buf.write_u16(i);
    }
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (!visible_parameter_annotations_count_.empty()) {
    const auto attr_name = writer_.get_utf("RuntimeVisibleParameterAnnotations");
    attributes_count_++;
    bytebuf buf;
    const auto num_params = static_cast<uint8_t>(visible_parameter_annotations_count_.size());
    buf.write_u8(num_params);
    for (auto i = 0; i < num_params; i++) {
      buf.write_u16(visible_parameter_annotations_count_[i]);
      buf.write_all(visible_parameter_annotations_[i]);
    }
    const auto data = buf.data();
    bytebuf attrbuf;
    attrbuf.write_u16(attr_name);
    attrbuf.write_u32(static_cast<uint32_t>(data.size()));
    attrbuf.write_all(data);
    const auto attr = attrbuf.data();
    attributes_.insert(attributes_.end(), attr.begin(), attr.end());
  }
  if (!invisible_parameter_annotations_count_.empty()) {
    const auto attr_name = writer_.get_utf("RuntimeInvisibleParameterAnnotations");
    attributes_count_++;
    bytebuf buf;
    const auto num_params = static_cast<uint8_t>(invisible_parameter_annotations_.size());
    buf.write_u8(num_params);
    for (auto i = 0; i < num_params; i++) {
      buf.write_u16(invisible_parameter_annotations_count_[i]);
      buf.write_all(invisible_parameter_annotations_[i]);
    }
    const auto data = buf.data();
    bytebuf attrbuf;
    attrbuf.write_u16(attr_name);
    attrbuf.write_u32(static_cast<uint32_t>(data.size()));
    attrbuf.write_all(data);
    const auto attr = attrbuf.data();
    attributes_.insert(attributes_.end(), attr.begin(), attr.end());
  }
  if (visible_annotations_count_ > 0) {
    const auto attr_name = writer_.get_utf("RuntimeVisibleAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(visible_annotations_.size() + 2));
    buf.write_u16(visible_annotations_count_);
    buf.write_all(visible_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (invisible_annotations_count_ > 0) {
    const auto attr_name = writer_.get_utf("RuntimeInvisibleAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(invisible_annotations_.size() + 2));
    buf.write_u16(invisible_annotations_count_);
    buf.write_all(invisible_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (visible_type_annotations_count_ > 0) {
    const auto attr_name = writer_.get_utf("RuntimeVisibleTypeAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(visible_type_annotations_.size() + 2));
    buf.write_u16(visible_type_annotations_count_);
    buf.write_all(visible_type_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (invisible_type_annotations_count_ > 0) {
    const auto attr_name = writer_.get_utf("RuntimeInvisibleTypeAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(invisible_type_annotations_.size() + 2));
    buf.write_u16(invisible_type_annotations_count_);
    buf.write_all(invisible_type_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (parameters_count_ > 0) {
    const auto attr_name = writer_.get_utf("MethodParameters");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(parameters_.size() + 1));
    buf.write_u8(parameters_count_);
    buf.write_all(parameters_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  bytebuf buf;
  buf.write_u16(access_flags_);
  buf.write_u16(name_index_);
  buf.write_u16(descriptor_index_);
  buf.write_u16(attributes_count_);
  buf.write_all(attributes_);
  const auto data = buf.data();
  writer_.method_count_++;
  writer_.methods_bin_.insert(writer_.methods_bin_.end(), data.begin(), data.end());
}
void class_writer::visit_end() {
  if (!module_packages_.empty()) {
    const auto attr_name = get_utf("ModulePackages");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>((module_packages_.size() * 2) + 2));
    buf.write_u16(static_cast<uint16_t>(module_packages_.size()));
    for (const auto& i : module_packages_) {
      buf.write_u16(i);
    }
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (!nest_members_.empty()) {
    const auto attr_name = get_utf("NestMembers");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>((nest_members_.size() * 2) + 2));
    buf.write_u16(static_cast<uint16_t>(nest_members_.size()));
    for (const auto& i : nest_members_) {
      buf.write_u16(i);
    }
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (!permitted_subclasses_.empty()) {
    const auto attr_name = get_utf("PermittedSubclasses");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>((permitted_subclasses_.size() * 2) + 2));
    buf.write_u16(static_cast<uint16_t>(permitted_subclasses_.size()));
    for (const auto& i : permitted_subclasses_) {
      buf.write_u16(i);
    }
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (visible_annotations_count_ > 0) {
    const auto attr_name = get_utf("RuntimeVisibleAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(visible_annotations_.size() + 2));
    buf.write_u16(visible_annotations_count_);
    buf.write_all(visible_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (invisible_annotations_count_ > 0) {
    const auto attr_name = get_utf("RuntimeInvisibleAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(invisible_annotations_.size() + 2));
    buf.write_u16(invisible_annotations_count_);
    buf.write_all(invisible_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (visible_type_annotations_count_ > 0) {
    const auto attr_name = get_utf("RuntimeVisibleTypeAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(visible_type_annotations_.size() + 2));
    buf.write_u16(visible_type_annotations_count_);
    buf.write_all(visible_type_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (invisible_type_annotations_count_ > 0) {
    const auto attr_name = get_utf("RuntimeInvisibleTypeAnnotations");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(invisible_type_annotations_.size() + 2));
    buf.write_u16(invisible_type_annotations_count_);
    buf.write_all(invisible_type_annotations_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (record_components_count_ > 0) {
    const auto attr_name = get_utf("Record");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(record_components_bin_.size() + 2));
    buf.write_u16(record_components_count_);
    buf.write_all(record_components_bin_);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (!bsm_buffer_.empty()) {
    const auto attr_name = get_utf("BootstrapMethods");
    attributes_count_++;
    bytebuf attrbuf;
    attrbuf.write_u16(static_cast<uint16_t>(bsm_buffer_.size()));
    for (const auto& [ref, args] : bsm_buffer_) {
      attrbuf.write_u16(ref);
      attrbuf.write_u16(static_cast<uint16_t>(args.size()));
      for (const auto& i : args) {
        attrbuf.write_u16(i);
      }
    }
    const auto attr = attrbuf.data();
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(attr.size()));
    buf.write_all(attr);
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
  if (!inner_classes_.empty()) {
    const auto attr_name = get_utf("InnerClasses");
    attributes_count_++;
    bytebuf buf;
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>((inner_classes_.size() * 8) + 2));
    buf.write_u16(static_cast<uint16_t>(inner_classes_.size()));
    for (const auto& [name, outer, inner, access] : inner_classes_) {
      buf.write_u16(name);
      buf.write_u16(outer);
      buf.write_u16(inner);
      buf.write_u16(access);
    }
    const auto data = buf.data();
    attributes_.insert(attributes_.end(), data.begin(), data.end());
  }
}
std::vector<int8_t> class_writer::write() const {
  bytebuf buf;
  buf.write_u32(0xcafebabe);
  buf.write_u16(static_cast<uint16_t>(version_ & 0xffff));
  buf.write_u16(static_cast<uint16_t>(version_ >> 16));
  uint16_t pool_count = 1;
  for (const auto& info : pool_) {
    if (std::holds_alternative<cp::pad_info>(info)) {
      continue;
    }
    pool_count++;
    if (std::holds_alternative<cp::long_info>(info) || std::holds_alternative<cp::double_info>(info)) {
      pool_count++;
    }
  }
  buf.write_u16(pool_count);
  for (const auto& info : pool_) {
    std::visit(constant_pool_visitor(buf), info);
  }
  buf.write_u16(access_flags_);
  buf.write_u16(this_class_);
  buf.write_u16(super_class_);
  buf.write_u16(static_cast<uint16_t>(interfaces_.size()));
  for (const auto& i : interfaces_) {
    buf.write_u16(i);
  }
  buf.write_u16(fields_count_);
  buf.write_all(fields_bin_);
  buf.write_u16(method_count_);
  buf.write_all(methods_bin_);
  buf.write_u16(attributes_count_);
  buf.write_all(attributes_);
  return buf.data();
}
} // namespace cafe
