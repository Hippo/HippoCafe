#include "cafe/class_reader.hpp"

#include <iostream>

#include "cafe/constants.hpp"


namespace cafe {

class_reader::class_reader(std::istream& stream) :
    buffer_(std::istreambuf_iterator(stream), std::istreambuf_iterator<char>()) {
  read_header();
}
class_reader::class_reader(const std::vector<int8_t>& buffer) : buffer_(buffer) {
  read_header();
}
void class_reader::accept(class_visitor& visitor) {
  const auto access_flags = read_u16();
  const auto name = get_string(read_u16());
  const auto super_name = get_string(read_u16());
  visitor.visit(class_version_, access_flags, name, super_name);
  const auto interfaces_count = read_u16();
  for (auto i = 0; i < interfaces_count; i++) {
    visitor.visit_interface(get_string(read_u16()));
  }

  const auto fields_count = read_u16();
  const auto field_offset = cursor_;
  for (auto i = 0; i < fields_count; i++) {
    cursor_ += 6;
    const auto attributes_count = read_u16();
    for (auto j = 0; j < attributes_count; j++) {
      cursor_ += 2;
      cursor_ += read_u32();
    }
  }
  const auto methods_count = read_u16();
  const auto method_offset = cursor_;
  for (auto i = 0; i < methods_count; i++) {
    cursor_ += 6;
    const auto attributes_count = read_u16();
    for (auto j = 0; j < attributes_count; j++) {
      cursor_ += 2;
      cursor_ += read_u32();
    }
  }
  std::vector<std::pair<size_t, label>> dummy_labels;
  const auto attributes_count = read_u16();
  for (auto i = 0; i < attributes_count; i++) {
    const auto attribute_name = get_string(read_u16());
    const auto attribute_length = read_u32();
    const auto attribute_start = cursor_;
    const auto attribute_end = cursor_ + attribute_length;
    try {
      if (attribute_name == "SourceFile") {
        visitor.visit_source_file(get_string(read_u16()));
      } else if (attribute_name == "InnerClasses") {
        const auto number_of_classes = read_u16();
        for (auto j = 0; j < number_of_classes; j++) {
          const auto inner_name = get_string(read_u16());
          const auto inner_outer_name = get_string(read_u16());
          const auto inner_inner_name = get_string(read_u16());
          const auto inner_flags = read_u16();
          visitor.visit_inner_class(inner_name, inner_outer_name, inner_inner_name, inner_flags);
        }
      } else if (attribute_name == "EnclosingMethod") {
        const auto owner = get_string(read_u16());
        const auto [name, descriptor] = get_name_and_type(read_u16());
        visitor.visit_enclosing_method(owner, name, descriptor);
      } else if (attribute_name == "SourceDebugExtension") {
        visitor.visit_source_debug_extension(read_utf());
      } else if (attribute_name == "BootstrapMethods") {
        const auto num_bootstrap_methods = read_u16();
        bootstrap_methods_.reserve(num_bootstrap_methods);
        for (auto j = 0; j < num_bootstrap_methods; j++) {
          bootstrap_methods_.emplace_back(cursor_);
          cursor_ += 2;
          const auto num_bootstrap_arguments = read_u16();
          for (auto k = 0; k < num_bootstrap_arguments; k++) {
            cursor_ += 2;
          }
        }
      } else if (attribute_name == "Module") {
        const auto module_name = get_string(read_u16());
        const auto module_flags = read_u16();
        const auto module_version = get_string(read_u16());

        auto& module_visitor = visitor.visit_module(module_name, module_flags, module_version);

        const auto requires_count = read_u16();
        for (auto j = 0; j < requires_count; j++) {
          const auto requires_name = get_string(read_u16());
          const auto requires_flags = read_u16();
          const auto requires_version = get_string(read_u16());
          module_visitor.visit_require(requires_name, requires_flags, requires_version);
        }
        const auto exports_count = read_u16();
        for (auto j = 0; j < exports_count; j++) {
          const auto exports_name = get_string(read_u16());
          const auto exports_flags = read_u16();
          const auto exports_to_count = read_u16();
          std::vector<std::string> exports_to;
          exports_to.reserve(exports_to_count);
          for (auto k = 0; k < exports_to_count; k++) {
            exports_to.emplace_back(get_string(read_u16()));
          }
          module_visitor.visit_export(exports_name, exports_flags, exports_to);
        }
        const auto opens_count = read_u16();
        for (auto j = 0; j < opens_count; j++) {
          const auto opens_name = get_string(read_u16());
          const auto opens_flags = read_u16();
          const auto opens_to_count = read_u16();
          std::vector<std::string> opens_to;
          opens_to.reserve(opens_to_count);
          for (auto k = 0; k < opens_to_count; k++) {
            opens_to.emplace_back(get_string(read_u16()));
          }
          module_visitor.visit_open(opens_name, opens_flags, opens_to);
        }
        const auto uses_count = read_u16();
        for (auto j = 0; j < uses_count; j++) {
          module_visitor.visit_use(get_string(read_u16()));
        }
        const auto provides_count = read_u16();
        for (auto j = 0; j < provides_count; j++) {
          const auto provides_service = get_string(read_u16());
          const auto provides_with_count = read_u16();
          std::vector<std::string> provides_with;
          provides_with.reserve(provides_with_count);
          for (auto k = 0; k < provides_with_count; k++) {
            provides_with.emplace_back(get_string(read_u16()));
          }
          module_visitor.visit_provide(provides_service, provides_with);
        }
        module_visitor.visit_end();
      } else if (attribute_name == "ModulePackages") {
        const auto package_count = read_u16();
        for (auto j = 0; j < package_count; j++) {
          visitor.visit_module_package(get_string(read_u16()));
        }
      } else if (attribute_name == "ModuleMainClass") {
        const auto main_class = get_string(read_u16());
        visitor.visit_module_main_class(main_class);
      } else if (attribute_name == "NestHost") {
        const auto host = get_string(read_u16());
        visitor.visit_nest_host(host);
      } else if (attribute_name == "NestMembers") {
        const auto member_count = read_u16();
        for (auto j = 0; j < member_count; j++) {
          visitor.visit_nest_member(get_string(read_u16()));
        }
      } else if (attribute_name == "Record") {
        read_record(visitor);
      } else if (attribute_name == "PermittedSubclasses") {
        const auto number_of_classes = read_u16();
        for (auto j = 0; j < number_of_classes; j++) {
          visitor.visit_permitted_subclass(get_string(read_u16()));
        }
      } else if (attribute_name == "Synthetic") {
        visitor.visit_synthetic(true);
      } else if (attribute_name == "Deprecated") {
        visitor.visit_deprecated(true);
      } else if (attribute_name == "Signature") {
        visitor.visit_signature(get_string(read_u16()));
      } else if (attribute_name == "RuntimeVisibleAnnotations") {
        const auto num_annotations = read_u16();
        for (auto j = 0; j < num_annotations; j++) {
          visitor.visit_visible_annotation(read_annotation());
        }
      } else if (attribute_name == "RuntimeInvisibleAnnotations") {
        const auto num_annotations = read_u16();
        for (auto j = 0; j < num_annotations; j++) {
          visitor.visit_invisible_annotation(read_annotation());
        }
      } else if (attribute_name == "RuntimeVisibleTypeAnnotations") {
        const auto num_annotations = read_u16();
        for (auto j = 0; j < num_annotations; j++) {
          visitor.visit_visible_type_annotation(read_type_annotation(dummy_labels));
        }
      } else if (attribute_name == "RuntimeInvisibleTypeAnnotations") {
        const auto num_annotations = read_u16();
        for (auto j = 0; j < num_annotations; j++) {
          visitor.visit_invisible_type_annotation(read_type_annotation(dummy_labels));
        }
      }
    } catch (const std::exception&) {
      std::vector data(buffer_.begin() + attribute_start, buffer_.begin() + attribute_end);
      visitor.visit_attribute(attribute_name, data);
    }
    cursor_ = attribute_end;
  }

  cursor_ = field_offset;
  for (auto i = 0; i < fields_count; i++) {
    read_field(visitor);
  }

  cursor_ = method_offset;
  for (auto i = 0; i < methods_count; i++) {
    read_method(visitor);
  }
  visitor.visit_end();
}
void class_reader::read_code(code_visitor& visitor, uint32_t code_length) {
  std::vector<std::pair<size_t, label>> labels;
  const auto bytecode_start = cursor_;
  const auto bytecode_end = bytecode_start + code_length;
  uint8_t wide = 0;
  while (cursor_ < bytecode_end) {
    const auto insn_start = cursor_ - bytecode_start;
    switch (read_u8()) {
      case op::aload:
      case op::astore:
      case op::dload:
      case op::dstore:
      case op::fload:
      case op::fstore:
      case op::iload:
      case op::istore:
      case op::lload:
      case op::lstore:
      case op::ret:
        if (wide > 0) {
          cursor_ += 2;
        } else {
          cursor_++;
        }
        break;
      case op::anewarray:
      case op::checkcast:
      case op::getfield:
      case op::getstatic:
      case op::iinc:
      case op:: instanceof:
      case op::invokespecial:
      case op::invokestatic:
      case op::invokevirtual:
      case op::ldc_w:
      case op::ldc2_w:
      case op::new_:
      case op::putfield:
      case op::putstatic:
      case op::sipush:
        cursor_ += 2;
        break;
      case op::bipush:
      case op::ldc:
      case op::newarray:
        cursor_++;
        break;
      case op::goto_:
      case op::if_acmpeq:
      case op::if_acmpne:
      case op::if_icmpeq:
      case op::if_icmpne:
      case op::if_icmplt:
      case op::if_icmpge:
      case op::if_icmpgt:
      case op::if_icmple:
      case op::ifeq:
      case op::ifne:
      case op::iflt:
      case op::ifge:
      case op::ifgt:
      case op::ifle:
      case op::ifnonnull:
      case op::ifnull:
      case op::jsr:
        get_label(labels, insn_start + read_i16());
        break;
      case op::goto_w:
      case op::jsr_w:
        get_label(labels, insn_start + read_i32());
        break;
      case op::invokedynamic:
      case op::invokeinterface:
        cursor_ += 4;
        break;
      case op::lookupswitch: {
        const auto pc = cursor_ - bytecode_start;
        cursor_ += (4 - (pc % 4)) % 4;
        get_label(labels, insn_start + read_i32());
        const auto npairs = read_i32();
        for (auto i = 0; i < npairs; i++) {
          cursor_ += 4;
          get_label(labels, insn_start + read_i32());
        }
        break;
      }
      case op::multianewarray: {
        cursor_ += 3;
        break;
      }
      case op::tableswitch: {
        //  cursor_--;
        const auto pc = cursor_ - bytecode_start;
        cursor_ += (4 - (pc % 4)) % 4;
        get_label(labels, insn_start + read_i32());
        const auto low = read_i32();
        const auto high = read_i32();
        for (auto i = low; i <= high; i++) {
          get_label(labels, insn_start + read_i32());
        }
        break;
      }
      case op::wide:
        wide = 2;
        break;
      default: {
        // do nothing, single byte instruction
      }
    }
    if (wide > 0) {
      wide--;
    }
  }
  std::vector<tcb> tcb_table;
  const auto exception_table_length = read_u16();
  tcb_table.reserve(exception_table_length);
  for (auto i = 0; i < exception_table_length; i++) {
    const auto start = get_label(labels, read_u16());
    const auto end = get_label(labels, read_u16());
    const auto handler = get_label(labels, read_u16());
    const auto catch_type = get_string(read_u16());
    tcb_table.emplace_back(start, end, handler, catch_type);
  }
  const auto attributes_count = read_u16();
  std::vector<std::pair<uint16_t, label>> line_numbers;
  std::vector<local_var> local_vars;
  std::vector<std::pair<label, frame>> stack_map;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  bool should_search_local = false;
  for (auto i = 0; i < attributes_count; i++) {
    const auto attribute_name = get_string(read_u16());
    const auto attribute_length = read_u32();
    const auto attribute_start = cursor_;
    const auto attribute_end = cursor_ + attribute_length;
    try {
      if (attribute_name == "LineNumberTable") {
        const auto line_number_table_length = read_u16();
        line_numbers.reserve(line_number_table_length);
        for (auto j = 0; j < line_number_table_length; j++) {
          const auto start_pc = read_u16();
          const auto start_label = get_label(labels, read_u16());
          line_numbers.emplace_back(start_pc, start_label);
        }
      } else if (attribute_name == "LocalVariableTable") {
        const auto local_variable_table_length = read_u16();
        if (!should_search_local) {
          local_vars.reserve(local_variable_table_length);
        }
        for (auto j = 0; j < local_variable_table_length; j++) {
          const auto start_pc = read_u16();
          const auto length = read_u16();
          const auto start = get_label(labels, start_pc);
          const auto end = get_label(labels, start_pc + length);
          const auto name = get_string(read_u16());
          const auto descriptor = get_string(read_u16());
          const auto index = read_u16();
          if (should_search_local) {
            bool found = false;
            for (auto& local : local_vars) {
              if (local.start == start && local.index == index) {
                local.descriptor = descriptor;
                found = true;
                break;
              }
            }
            if (found) {
              continue;
            }
          }
          local_vars.emplace_back(name, descriptor, "", start, end, index);
        }
        should_search_local = true;
      } else if (attribute_name == "LocalVariableTypeTable") {
        const auto local_variable_type_table_length = read_u16();
        if (!should_search_local) {
          local_vars.reserve(local_variable_type_table_length);
        }
        for (auto j = 0; j < local_variable_type_table_length; j++) {
          const auto start_pc = read_u16();
          const auto length = read_u16();
          const auto start = get_label(labels, start_pc);
          const auto end = get_label(labels, start_pc + length);
          const auto name = get_string(read_u16());
          const auto signature = get_string(read_u16());
          const auto index = read_u16();
          if (should_search_local) {
            bool found = false;
            for (auto& local : local_vars) {
              if (local.start == start && local.index == index) {
                local.signature = signature;
                found = true;
                break;
              }
            }
            if (found) {
              continue;
            }
          }
          local_vars.emplace_back(name, "", signature, start, end, index);
        }
      } else if (attribute_name == "StackMapTable") {
        const auto number_of_entries = read_u16();
        uint32_t offset_delta = 0;
        for (auto j = 0; j < number_of_entries; j++) {
          const auto off = offset_delta == 0 ? 0 : 1;
          const auto frame_type = read_u8();
          if (frame_type <= 63) {
            offset_delta += frame_type + off;
            const auto target = get_label(labels, offset_delta);
            stack_map.emplace_back(target, same_frame());
          } else if (frame_type <= 127) {
            offset_delta += (frame_type - 64) + off;
            const auto target = get_label(labels, offset_delta);
            stack_map.emplace_back(target, same_frame(read_frame_var(labels)));
          } else if (frame_type == 247) {
            offset_delta += read_u16() + off;
            const auto target = get_label(labels, offset_delta);
            stack_map.emplace_back(target, same_frame(read_frame_var(labels)));
          } else if (frame_type >= 248 && frame_type <= 250) {
            offset_delta += read_u16() + off;
            const auto target = get_label(labels, offset_delta);
            stack_map.emplace_back(target, chop_frame(251 - frame_type));
          } else if (frame_type == 251) {
            offset_delta += read_u16() + off;
            const auto target = get_label(labels, offset_delta);
            stack_map.emplace_back(target, same_frame());
          } else if (frame_type >= 252 && frame_type <= 254) {
            offset_delta += read_u16() + off;
            std::vector<frame_var> locals;
            const auto locals_count = frame_type - 251;
            locals.reserve(locals_count);
            for (auto k = 0; k < locals_count; k++) {
              locals.emplace_back(read_frame_var(labels));
            }
            const auto target = get_label(labels, offset_delta);
            stack_map.emplace_back(target, append_frame(locals));
          } else if (frame_type == 255) {
            offset_delta += read_u16() + off;
            const auto number_of_locals = read_u16();
            std::vector<frame_var> locals;
            locals.reserve(number_of_locals);
            for (auto k = 0; k < number_of_locals; k++) {
              locals.emplace_back(read_frame_var(labels));
            }
            const auto number_of_stack_items = read_u16();
            std::vector<frame_var> stack;
            stack.reserve(number_of_stack_items);
            for (auto k = 0; k < number_of_stack_items; k++) {
              stack.emplace_back(read_frame_var(labels));
            }
            const auto target = get_label(labels, offset_delta);
            stack_map.emplace_back(target, full_frame(locals, stack));
          }
        }
      } else if (attribute_name == "RuntimeVisibleTypeAnnotations") {
        const auto num_annotations = read_u16();
        visible_type_annotations.reserve(num_annotations);
        for (auto j = 0; j < num_annotations; j++) {
          visible_type_annotations.emplace_back(read_type_annotation(labels));
        }
      } else if (attribute_name == "RuntimeInvisibleTypeAnnotations") {
        const auto num_annotations = read_u16();
        invisible_type_annotations.reserve(num_annotations);
        for (auto j = 0; j < num_annotations; j++) {
          visible_type_annotations.emplace_back(read_type_annotation(labels));
        }
      }
    } catch (const std::exception&) {
      std::vector data(buffer_.begin() + attribute_start, buffer_.begin() + attribute_end);
      visitor.visit_attribute(attribute_name, data);
    }

    cursor_ = attribute_end;
  }
  const auto code_end = cursor_;
  cursor_ = bytecode_start;
  wide = 0;

  while (cursor_ < bytecode_end) {
    const auto insn_start = cursor_ - bytecode_start;
    if (auto lbl = get_label_opt(labels, insn_start)) {
      visitor.visit_label(*lbl);
    }
    switch (const auto opcode = read_u8(); opcode) {
      case op::aload_0:
      case op::aload_1:
      case op::aload_2:
      case op::aload_3:
        visitor.visit_var_insn(op::aload, opcode - op::aload_0);
        break;
      case op::astore_0:
      case op::astore_1:
      case op::astore_2:
      case op::astore_3:
        visitor.visit_var_insn(op::astore, opcode - op::astore_0);
        break;
      case op::dload_0:
      case op::dload_1:
      case op::dload_2:
      case op::dload_3:
        visitor.visit_var_insn(op::dload, opcode - op::dload_0);
        break;
      case op::dstore_0:
      case op::dstore_1:
      case op::dstore_2:
      case op::dstore_3:
        visitor.visit_var_insn(op::dstore, opcode - op::dstore_0);
        break;
      case op::fstore_0:
      case op::fstore_1:
      case op::fstore_2:
      case op::fstore_3:
        visitor.visit_var_insn(op::fstore, opcode - op::fstore_0);
        break;
      case op::fload_0:
      case op::fload_1:
      case op::fload_2:
      case op::fload_3:
        visitor.visit_var_insn(op::fload, opcode - op::fload_0);
        break;
      case op::iload_0:
      case op::iload_1:
      case op::iload_2:
      case op::iload_3:
        visitor.visit_var_insn(op::iload, opcode - op::iload_0);
        break;
      case op::istore_0:
      case op::istore_1:
      case op::istore_2:
      case op::istore_3:
        visitor.visit_var_insn(op::istore, opcode - op::istore_0);
        break;
      case op::lload_0:
      case op::lload_1:
      case op::lload_2:
      case op::lload_3:
        visitor.visit_var_insn(op::lload, opcode - op::lload_0);
        break;
      case op::lstore_0:
      case op::lstore_1:
      case op::lstore_2:
      case op::lstore_3:
        visitor.visit_var_insn(op::lstore, opcode - op::lstore_0);
        break;
      case op::iconst_m1:
      case op::iconst_0:
      case op::iconst_1:
      case op::iconst_2:
      case op::iconst_3:
      case op::iconst_4:
      case op::iconst_5:
        visitor.visit_push_insn(static_cast<int32_t>(opcode - op::iconst_0));
        break;
      case op::fconst_0:
      case op::fconst_1:
      case op::fconst_2:
        visitor.visit_push_insn(static_cast<float>(opcode - op::fconst_0));
        break;
      case op::dconst_0:
      case op::dconst_1:
        visitor.visit_push_insn(static_cast<double>(opcode - op::dconst_0));
        break;
      case op::lconst_0:
      case op::lconst_1:
        visitor.visit_push_insn(static_cast<int64_t>(opcode - op::lconst_0));
        break;
      case op::aaload:
      case op::aastore:
      case op::aconst_null:
      case op::areturn:
      case op::arraylength:
      case op::athrow:
      case op::baload:
      case op::bastore:
      case op::caload:
      case op::castore:
      case op::d2f:
      case op::d2i:
      case op::d2l:
      case op::dadd:
      case op::daload:
      case op::dastore:
      case op::dcmpg:
      case op::dcmpl:
      case op::ddiv:
      case op::dmul:
      case op::dneg:
      case op::drem:
      case op::dreturn:
      case op::dsub:
      case op::dup:
      case op::dup_x1:
      case op::dup_x2:
      case op::dup2:
      case op::dup2_x1:
      case op::dup2_x2:
      case op::f2d:
      case op::f2i:
      case op::f2l:
      case op::fadd:
      case op::faload:
      case op::fastore:
      case op::fcmpg:
      case op::fcmpl:
      case op::fdiv:
      case op::fmul:
      case op::fneg:
      case op::frem:
      case op::freturn:
      case op::fsub:
      case op::i2b:
      case op::i2c:
      case op::i2d:
      case op::i2f:
      case op::i2l:
      case op::i2s:
      case op::iadd:
      case op::iaload:
      case op::iand:
      case op::iastore:
      case op::idiv:
      case op::imul:
      case op::ineg:
      case op::ior:
      case op::irem:
      case op::ireturn:
      case op::ishl:
      case op::ishr:
      case op::isub:
      case op::iushr:
      case op::ixor:
      case op::l2d:
      case op::l2f:
      case op::l2i:
      case op::ladd:
      case op::laload:
      case op::land:
      case op::lastore:
      case op::lcmp:
      case op::ldiv:
      case op::lmul:
      case op::lneg:
      case op::lor:
      case op::lrem:
      case op::lreturn:
      case op::lshl:
      case op::lshr:
      case op::lsub:
      case op::lushr:
      case op::lxor:
      case op::monitorenter:
      case op::monitorexit:
      case op::nop:
      case op::pop:
      case op::pop2:
      case op::return_:
      case op::saload:
      case op::sastore:
      case op::swap:
        visitor.visit_insn(opcode);
        break;
      case op::aload:
      case op::astore:
      case op::dload:
      case op::dstore:
      case op::fload:
      case op::fstore:
      case op::iload:
      case op::istore:
      case op::lload:
      case op::lstore:
      case op::ret: {
        const auto index = wide > 0 ? read_u16() : read_u8();
        visitor.visit_var_insn(opcode, index);
        break;
      }
      case op::anewarray: {
        const auto descriptor = get_string(read_u16());
        visitor.visit_array_insn(descriptor);
        break;
      }
      case op::bipush: {
        const auto value = read_i8();
        visitor.visit_push_insn(value);
        break;
      }
      case op::checkcast:
      case op:: instanceof:
      case op::new_: {
        const auto descriptor = get_string(read_u16());
        visitor.visit_type_insn(opcode, descriptor);
        break;
      }
      case op::getfield:
      case op::getstatic:
      case op::invokespecial:
      case op::invokestatic:
      case op::invokevirtual:
      case op::putfield:
      case op::putstatic: {
        const auto [owner, name, descriptor] = get_ref(read_u16());
        visitor.visit_ref_insn(opcode, owner, name, descriptor);
        break;
      }
      case op::goto_:
      case op::if_acmpeq:
      case op::if_acmpne:
      case op::if_icmpeq:
      case op::if_icmpne:
      case op::if_icmplt:
      case op::if_icmpge:
      case op::if_icmpgt:
      case op::if_icmple:
      case op::ifeq:
      case op::ifne:
      case op::iflt:
      case op::ifge:
      case op::ifgt:
      case op::ifle:
      case op::ifnonnull:
      case op::ifnull:
      case op::jsr: {
        const auto target = get_label(labels, insn_start + read_i16());
        visitor.visit_branch_insn(opcode, target);
        break;
      }
      case op::goto_w:
      case op::jsr_w: {
        const auto target = get_label(labels, insn_start + read_i32());
        visitor.visit_branch_insn(opcode, target);
        break;
      }
      case op::iinc: {
        const auto index = wide > 0 ? read_u16() : read_u8();
        const auto value = static_cast<int16_t>(wide > 0 ? read_i16() : read_i8());
        visitor.visit_iinc_insn(index, value);
        break;
      }
      case op::invokedynamic: {
        const auto dyn = std::get<cp::invoke_dynamic_info>(pool_[read_u16()]);
        cursor_ += 2;
        const auto curr = cursor_;
        cursor_ = bootstrap_methods_[dyn.bootstrap_method_attr_index];
        const auto handle = get_method_handle(read_u16());
        const auto arg_count = read_u16();
        std::vector<uint16_t> arg_indices;
        arg_indices.reserve(arg_count);
        for (auto i = 0; i < arg_count; i++) {
          arg_indices.emplace_back(read_u16());
        }
        cursor_ = curr;
        std::vector<value> args;
        args.reserve(arg_count);
        for (const auto arg_index : arg_indices) {
          args.emplace_back(get_constant(arg_index));
        }
        const auto [name, descriptor] = get_name_and_type(dyn.name_and_type_index);
        visitor.visit_invoke_dynamic_insn(name, descriptor, handle, args);
        break;
      }
      case op::invokeinterface: {
        const auto [owner, name, descriptor] = get_ref(read_u16());
        cursor_ += 2;
        visitor.visit_ref_insn(opcode, owner, name, descriptor);
        break;
      }
      case op::ldc:
        visitor.visit_push_insn(get_constant(read_u8()));
        break;
      case op::ldc_w:
      case op::ldc2_w:
        visitor.visit_push_insn(get_constant(read_u16()));
        break;
      case op::lookupswitch: {
        const auto pc = cursor_ - bytecode_start;
        cursor_ += (4 - (pc % 4)) % 4;
        const auto default_target = get_label(labels, insn_start + read_i32());
        std::vector<std::pair<int32_t, label>> pairs;
        const auto npairs = read_i32();
        pairs.reserve(npairs);
        for (auto i = 0; i < npairs; i++) {
          const auto match = read_i32();
          const auto target = get_label(labels, insn_start + read_i32());
          pairs.emplace_back(match, target);
        }
        visitor.visit_lookup_switch_insn(default_target, pairs);
        break;
      }
      case op::multianewarray: {
        const auto descriptor = get_string(read_u16());
        const auto dimensions = read_u8();
        visitor.visit_multi_array_insn(descriptor, dimensions);
        break;
      }
      case op::newarray:
        visitor.visit_array_insn(read_u8());
        break;
      case op::tableswitch: {
        const auto pc = cursor_ - bytecode_start;
        cursor_ += (4 - (pc % 4)) % 4;
        const auto default_target = get_label(labels, insn_start + read_i32());
        const auto low = read_i32();
        const auto high = read_i32();
        std::vector<label> targets;
        targets.reserve(high - low + 1);
        for (auto i = low; i <= high; i++) {
          targets.emplace_back(get_label(labels, insn_start + read_i32()));
        }
        visitor.visit_table_switch_insn(default_target, low, high, targets);
        break;
      }
      case op::wide:
        wide = 2;
        break;
      default:
        throw std::runtime_error("Unknown opcode: " + std::to_string(opcode));
    }
    if (wide > 0) {
      wide--;
    }
  }
  if (const auto lbl = get_label_opt(labels, code_length)) {
    visitor.visit_label(*lbl);
  }
  cursor_ = code_end;

  for (const auto& tcb : tcb_table) {
    visitor.visit_tcb(tcb);
  }
  for (const auto& [line, start] : line_numbers) {
    visitor.visit_line_number(line, start);
  }
  for (const auto& local : local_vars) {
    visitor.visit_local(local);
  }
  for (const auto& [target, frame] : stack_map) {
    visitor.visit_frame(target, frame);
  }
  for (const auto& annotation : visible_type_annotations) {
    visitor.visit_visible_type_annotation(annotation);
  }
  for (const auto& annotation : invisible_type_annotations) {
    visitor.visit_invisible_type_annotation(annotation);
  }
  visitor.visit_end();
}
void class_reader::read_method(class_visitor& visitor) {
  label_count_ = 0;
  const auto access_flags = read_u16();
  const auto name = get_string(read_u16());
  const auto descriptor = get_string(read_u16());
  auto& method_visitor = visitor.visit_method(access_flags, name, descriptor);
  const auto attributes_count = read_u16();
  std::vector<std::pair<size_t, label>> dummy_labels;
  for (auto i = 0; i < attributes_count; i++) {
    const auto attribute_name = get_string(read_u16());
    const auto attribute_length = read_u32();
    const auto attribute_start = cursor_;
    const auto attribute_end = cursor_ + attribute_length;
    try {
      if (attribute_name == "Code") {
        const auto oak = class_version_ < class_version::v1_1;
        const auto max_stack = static_cast<uint16_t>(oak ? read_u8() : read_u16());
        const auto max_locals = static_cast<uint16_t>(oak ? read_u8() : read_u16());
        const auto code_length = static_cast<uint32_t>(oak ? read_u16() : read_u32());
        auto& code_visitor = method_visitor.visit_code(max_stack, max_locals);
        read_code(code_visitor, code_length);
      } else if (attribute_name == "Exceptions") {
        const auto number_of_exceptions = read_u16();
        for (auto j = 0; j < number_of_exceptions; j++) {
          method_visitor.visit_exception(get_string(read_u16()));
        }
      } else if (attribute_name == "RuntimeVisibleParameterAnnotations") {
        const auto num_parameters = read_u8();
        for (auto j = 0; j < num_parameters; j++) {
          const auto num_annotations = read_u16();
          for (auto k = 0; k < num_annotations; k++) {
            method_visitor.visit_visible_parameter_annotation(j, read_annotation());
          }
        }
      } else if (attribute_name == "RuntimeInvisibleParameterAnnotations") {
        const auto num_parameters = read_u8();
        for (auto j = 0; j < num_parameters; j++) {
          const auto num_annotations = read_u16();
          for (auto k = 0; k < num_annotations; k++) {
            method_visitor.visit_invisible_parameter_annotation(j, read_annotation());
          }
        }
      } else if (attribute_name == "AnnotationDefault") {
        method_visitor.visit_annotation_default(read_element_value());
      }
      if (attribute_name == "MethodParameters") {
        const auto parameters_count = read_u8();
        for (auto j = 0; j < parameters_count; j++) {
          const auto param_name = get_string(read_u16());
          const auto param_access_flags = read_u16();
          method_visitor.visit_parameter(param_access_flags, param_name);
        }
      } else if (attribute_name == "Synthetic") {
        method_visitor.visit_synthetic(true);
      } else if (attribute_name == "Deprecated") {
        method_visitor.visit_deprecated(true);
      } else if (attribute_name == "Signature") {
        method_visitor.visit_signature(get_string(read_u16()));
      } else if (attribute_name == "RuntimeVisibleAnnotations") {
        const auto num_annotations = read_u16();
        for (auto j = 0; j < num_annotations; j++) {
          method_visitor.visit_visible_annotation(read_annotation());
        }
      } else if (attribute_name == "RuntimeInvisibleAnnotations") {
        const auto num_annotations = read_u16();
        for (auto j = 0; j < num_annotations; j++) {
          method_visitor.visit_invisible_annotation(read_annotation());
        }
      } else if (attribute_name == "RuntimeVisibleTypeAnnotations") {
        const auto num_annotations = read_u16();
        for (auto j = 0; j < num_annotations; j++) {
          method_visitor.visit_visible_type_annotation(read_type_annotation(dummy_labels));
        }
      } else if (attribute_name == "RuntimeInvisibleTypeAnnotations") {
        const auto num_annotations = read_u16();
        for (auto j = 0; j < num_annotations; j++) {
          method_visitor.visit_invisible_type_annotation(read_type_annotation(dummy_labels));
        }
      }
    } catch (const std::exception& e) {
      std::cout << e.what() << std::endl;
      std::vector data(buffer_.begin() + attribute_start, buffer_.begin() + attribute_end);
      method_visitor.visit_attribute(attribute_name, data);
    }
    cursor_ = attribute_end;
  }
  method_visitor.visit_end();
}
void class_reader::read_field(class_visitor& visitor) {
  const auto access_flags = read_u16();
  const auto name = get_string(read_u16());
  const auto descriptor = get_string(read_u16());
  auto& field_visitor = visitor.visit_field(access_flags, name, descriptor);
  const auto attributes_count = read_u16();
  std::vector<std::pair<size_t, label>> dummy_labels;
  for (auto i = 0; i < attributes_count; i++) {
    const auto attribute_name = get_string(read_u16());
    const auto attribute_length = read_u32();
    const auto attribute_start = cursor_;
    const auto attribute_end = cursor_ + attribute_length;
    try {
      if (attribute_name == "ConstantValue") {
        field_visitor.visit_constant_value(get_constant(read_u16()));
      } else if (attribute_name == "Synthetic") {
        field_visitor.visit_synthetic(true);
      } else if (attribute_name == "Deprecated") {
        field_visitor.visit_deprecated(true);
      } else if (attribute_name == "Signature") {
        field_visitor.visit_signature(get_string(read_u16()));
      } else if (attribute_name == "RuntimeVisibleAnnotations") {
        const auto num_annotations = read_u16();
        for (auto j = 0; j < num_annotations; j++) {
          field_visitor.visit_visible_annotation(read_annotation());
        }
      } else if (attribute_name == "RuntimeInvisibleAnnotations") {
        const auto num_annotations = read_u16();
        for (auto j = 0; j < num_annotations; j++) {
          field_visitor.visit_invisible_annotation(read_annotation());
        }
      } else if (attribute_name == "RuntimeVisibleTypeAnnotations") {
        const auto num_annotations = read_u16();
        for (auto j = 0; j < num_annotations; j++) {
          field_visitor.visit_visible_type_annotation(read_type_annotation(dummy_labels));
        }
      } else if (attribute_name == "RuntimeInvisibleTypeAnnotations") {
        const auto num_annotations = read_u16();
        for (auto j = 0; j < num_annotations; j++) {
          field_visitor.visit_invisible_type_annotation(read_type_annotation(dummy_labels));
        }
      }
    } catch (const std::exception&) {
      std::vector data(buffer_.begin() + attribute_start, buffer_.begin() + attribute_end);
      field_visitor.visit_attribute(attribute_name, data);
    }
    cursor_ = attribute_end;
  }
  field_visitor.visit_end();
}
void class_reader::read_record(class_visitor& visitor) {
  const auto component_count = read_u16();
  std::vector<std::pair<size_t, label>> dummy_labels;
  for (auto i = 0; i < component_count; i++) {
    const auto name = get_string(read_u16());
    const auto descriptor = get_string(read_u16());
    auto& record_component_visitor = visitor.visit_record_component(name, descriptor);
    const auto attribute_count = read_u16();
    for (auto j = 0; j < attribute_count; j++) {
      const auto attribute_name = get_string(read_u16());
      const auto attribute_length = read_u32();
      const auto attribute_start = cursor_;
      const auto attribute_end = cursor_ + attribute_length;
      try {
        if (attribute_name == "Signature") {
          record_component_visitor.visit_signature(get_string(read_u16()));
        } else if (attribute_name == "RuntimeVisibleAnnotations") {
          const auto num_annotations = read_u16();
          for (auto k = 0; k < num_annotations; k++) {
            record_component_visitor.visit_visible_annotation(read_annotation());
          }
        } else if (attribute_name == "RuntimeInvisibleAnnotations") {
          const auto num_annotations = read_u16();
          for (auto k = 0; k < num_annotations; k++) {
            record_component_visitor.visit_invisible_annotation(read_annotation());
          }
        } else if (attribute_name == "RuntimeVisibleTypeAnnotations") {
          const auto num_annotations = read_u16();
          for (auto k = 0; k < num_annotations; k++) {
            record_component_visitor.visit_visible_type_annotation(read_type_annotation(dummy_labels));
          }
        } else if (attribute_name == "RuntimeInvisibleTypeAnnotations") {
          const auto num_annotations = read_u16();
          for (auto k = 0; k < num_annotations; k++) {
            record_component_visitor.visit_invisible_type_annotation(read_type_annotation(dummy_labels));
          }
        }
      } catch (const std::exception&) {
        std::vector data(buffer_.begin() + attribute_start, buffer_.begin() + attribute_end);
        record_component_visitor.visit_attribute(attribute_name, data);
      }
      cursor_ = attribute_end;
    }
    record_component_visitor.visit_end();
  }
}
void class_reader::read_header() {
  if (const auto magic = read_u32(); magic != 0xcafebabe) {
    throw std::runtime_error("Invalid magic number, expected 0xcafebabe got " + std::to_string(magic));
  }

  const auto minor_version = read_u16();
  const auto major_version = read_u16();

  class_version_ = major_version << 16 | minor_version;
  const auto constant_pool_count = read_u16();

  pool_.reserve(constant_pool_count);
  pool_.emplace_back(cp::pad_info{});
  for (auto i = 1; i < constant_pool_count; i++) {
    switch (const auto tag = read_u8(); tag) {
      case cp::utf8_info::tag:
        pool_.emplace_back(cp::utf8_info{read_utf()});
        break;
      case cp::integer_info::tag:
        pool_.emplace_back(cp::integer_info{read_i32()});
        break;
      case cp::float_info::tag:
        pool_.emplace_back(cp::float_info{read_f32()});

        break;
      case cp::long_info::tag:
        pool_.emplace_back(cp::long_info{read_i64()});
        pool_.emplace_back(cp::pad_info{});
        i++;
        break;
      case cp::double_info::tag:
        pool_.emplace_back(cp::double_info{read_f64()});
        pool_.emplace_back(cp::pad_info{});
        i++;
        break;
      case cp::class_info::tag:
        pool_.emplace_back(cp::class_info{read_u16()});
        break;
      case cp::string_info::tag:
        pool_.emplace_back(cp::string_info{read_u16()});
        break;
      case cp::field_ref_info::tag:
        pool_.emplace_back(cp::field_ref_info{read_u16(), read_u16()});

        break;
      case cp::method_ref_info::tag:
        pool_.emplace_back(cp::method_ref_info{read_u16(), read_u16()});

        break;
      case cp::interface_method_ref_info::tag:
        pool_.emplace_back(cp::interface_method_ref_info{read_u16(), read_u16()});

        break;
      case cp::name_and_type_info::tag:
        pool_.emplace_back(cp::name_and_type_info{read_u16(), read_u16()});

        break;
      case cp::method_handle_info::tag:
        pool_.emplace_back(cp::method_handle_info{read_u8(), read_u16()});

        break;
      case cp::method_type_info::tag:
        pool_.emplace_back(cp::method_type_info{read_u16()});

        break;
      case cp::dynamic_info::tag:
        pool_.emplace_back(cp::dynamic_info{read_u16(), read_u16()});

        break;
      case cp::invoke_dynamic_info::tag:
        pool_.emplace_back(cp::invoke_dynamic_info{read_u16(), read_u16()});
        break;
      case cp::module_info::tag:
        pool_.emplace_back(cp::module_info{read_u16()});
        break;
      case cp::package_info::tag:
        pool_.emplace_back(cp::package_info{read_u16()});
        break;
      default:
        throw std::runtime_error("Unknown constant pool_ tag (" + std::to_string(tag) + ")");
    }
  }
}
annotation class_reader::read_annotation() {
  annotation anno(get_string(read_u16()));
  const auto num_element_value_pairs = read_u16();
  for (auto i = 0; i < num_element_value_pairs; i++) {
    const auto element_name = get_string(read_u16());
    const auto value = read_element_value();
    anno.values.emplace_back(element_name, value);
  }
  return anno;
}
type_annotation class_reader::read_type_annotation(std::vector<std::pair<size_t, label>>& labels) {
  const auto target_type = read_u8();
  type_annotation_target target = target::empty{};
  switch (target_type) {
    case 0x00:
    case 0x01:
      target = target::type_parameter{read_u8()};
      break;
    case 0x10:
      target = target::supertype{read_u16()};
      break;
    case 0x11:
    case 0x12:
      target = target::type_parameter_bound{read_u8(), read_u8()};
      break;
    case 0x13:
    case 0x14:
    case 0x15:
      target = target::empty();
      break;
    case 0x16:
      target = target::formal_parameter{read_u8()};
      break;
    case 0x17:
      target = target::throws{read_u16()};
      break;
    case 0x40:
    case 0x41: {
      const auto table_length = read_u16();
      std::vector<target::local> table;
      table.reserve(table_length);
      for (auto i = 0; i < table_length; i++) {
        const auto start_pc = read_u16();
        const auto end_pc = read_u16() + start_pc;
        const auto start = get_label(labels, start_pc);
        const auto end = get_label(labels, end_pc);
        const auto index = read_u16();
        table.emplace_back(start, end, index);
      }
      target = target::localvar{table};
      break;
    }
    case 0x42:
      target = target::catch_target{read_u16()};
      break;
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46: {
      const auto offset = get_label(labels, read_u16());
      target = target::offset_target{offset};
      break;
    }
    case 0x47:
    case 0x48:
    case 0x49:
    case 0x4A:
    case 0x4B: {
      const auto offset = get_label(labels, read_u16());
      const auto index = read_u8();
      target = target::type_argument{offset, index};
      break;
    }
    default:
      throw std::runtime_error("Unknown target type (" + std::to_string(target_type) + ")");
  }
  const auto target_path_length = read_u8();
  std::vector<std::pair<uint8_t, uint8_t>> path;
  path.reserve(target_path_length);
  for (auto i = 0; i < target_path_length; i++) {
    const auto type_kind = read_u8();
    const auto type_index = read_u8();
    path.emplace_back(type_kind, type_index);
  }
  const auto descriptor = get_string(read_u16());
  type_annotation anno(target_type, target, type_path(path), descriptor);
  const auto num_element_value_pairs = read_u16();
  for (auto i = 0; i < num_element_value_pairs; i++) {
    const auto element_name = get_string(read_u16());
    const auto value = read_element_value();
    anno.values.emplace_back(element_name, value);
  }
  return anno;
}
element_value class_reader::read_element_value() {
  switch (const auto tag = read_u8(); tag) {
    case 'B':
    case 'C':
    case 'I':
    case 'S':
    case 'Z':
      return {get_int(read_u16())};
    case 'D':
      return {get_double(read_u16())};
    case 'F':
      return {get_float(read_u16())};
    case 'J':
      return {get_long(read_u16())};
    case 's':
      return {get_string(read_u16())};
    case 'e':
      return {std::make_pair(get_string(read_u16()), get_string(read_u16()))};
    case 'c':
      return {class_value(get_string(read_u16()))};
    case '@':
      return {read_annotation()};
    case '[': {
      const auto num_values = read_u16();
      std::vector<element_value> values;
      values.reserve(num_values);
      for (auto i = 0; i < num_values; i++) {
        values.emplace_back(read_element_value());
      }
      return {values};
    }
    default:
      throw std::runtime_error("Unknown element value tag (" + std::to_string(tag) + ")");
  }
}
frame_var class_reader::read_frame_var(std::vector<std::pair<size_t, label>>& labels) {
  switch (const auto tag = read_u8(); tag) {
    case 0:
      return top_var();
    case 1:
      return int_var();
    case 2:
      return float_var();
    case 5:
      return null_var();
    case 6:
      return uninitialized_this_var();
    case 7:
      return object_var(get_string(read_u16()));
    case 8:
      return uninitialized_var(get_label(labels, read_u16()));
    case 4:
      return long_var();
    case 3:
      return double_var();
    default:
      throw std::runtime_error("Unknown frame var type (" + std::to_string(tag) + ")");
  }
}
label class_reader::get_label(std::vector<std::pair<size_t, label>>& labels, size_t offset) {
  for (const auto& [pos, lbl] : labels) {
    if (pos == offset) {
      return lbl;
    }
  }

  label l;
  label_count_++;
  std::string debug_name;
  auto i = label_count_;
  while (i > 0) {
    i--;
    const auto letter = static_cast<char>('A' + (i % 26));
    debug_name.insert(0, 1, letter);
    i /= 26;
  }
  l.debug_name = debug_name;
  labels.emplace_back(offset, l);
  return l;
}
std::optional<label> class_reader::get_label_opt(std::vector<std::pair<size_t, label>>& labels, size_t offset) {
  for (const auto& [pos, lbl] : labels) {
    if (pos == offset) {
      return lbl;
    }
  }
  return std::nullopt;
}
std::string class_reader::get_string(uint16_t index) {
  if (index == 0 || index >= pool_.size()) {
    return "";
  }
  if (const auto utf8 = std::get_if<cp::utf8_info>(&pool_.at(index))) {
    return utf8->value;
  }
  if (const auto string = std::get_if<cp::string_info>(&pool_.at(index))) {
    if (const auto utf8 = std::get_if<cp::utf8_info>(&pool_.at(string->string_index))) {
      return utf8->value;
    }
  }
  if (const auto class_info = std::get_if<cp::class_info>(&pool_.at(index))) {
    if (const auto utf8 = std::get_if<cp::utf8_info>(&pool_.at(class_info->name_index))) {
      return utf8->value;
    }
  }
  if (const auto module_info = std::get_if<cp::module_info>(&pool_.at(index))) {
    if (const auto utf8 = std::get_if<cp::utf8_info>(&pool_.at(module_info->name_index))) {
      return utf8->value;
    }
  }
  if (const auto package_info = std::get_if<cp::package_info>(&pool_.at(index))) {
    if (const auto utf8 = std::get_if<cp::utf8_info>(&pool_.at(package_info->name_index))) {
      return utf8->value;
    }
  }
  return "";
}
std::pair<std::string, std::string> class_reader::get_name_and_type(uint16_t index) {
  if (const auto name_and_type = std::get_if<cp::name_and_type_info>(&pool_.at(index))) {
    return {get_string(name_and_type->name_index), get_string(name_and_type->descriptor_index)};
  }
  return {"", ""};
}
std::tuple<std::string, std::string, std::string> class_reader::get_ref(uint16_t index) {
  if (const auto method = std::get_if<cp::method_ref_info>(&pool_.at(index))) {
    const auto [name, descriptor] = get_name_and_type(method->name_and_type_index);
    return {get_string(method->class_index), name, descriptor};
  }
  if (const auto field = std::get_if<cp::field_ref_info>(&pool_.at(index))) {
    const auto [name, descriptor] = get_name_and_type(field->name_and_type_index);
    return {get_string(field->class_index), name, descriptor};
  }
  if (const auto interface_method = std::get_if<cp::interface_method_ref_info>(&pool_.at(index))) {
    const auto [name, descriptor] = get_name_and_type(interface_method->name_and_type_index);
    return {get_string(interface_method->class_index), name, descriptor};
  }
  return {"", "", ""};
}
method_handle class_reader::get_method_handle(uint16_t index) {
  if (const auto mh = std::get_if<cp::method_handle_info>(&pool_.at(index))) {
    const auto [owner, name, descriptor] = get_ref(mh->reference_index);
    return {mh->reference_kind, owner, name, descriptor};
  }
  return {};
}
value class_reader::get_constant(uint16_t index) {
  if (const auto int_info = std::get_if<cp::integer_info>(&pool_.at(index))) {
    return int_info->value;
  }
  if (const auto float_info = std::get_if<cp::float_info>(&pool_.at(index))) {
    return float_info->value;
  }
  if (const auto long_info = std::get_if<cp::long_info>(&pool_.at(index))) {
    return long_info->value;
  }
  if (const auto double_info = std::get_if<cp::double_info>(&pool_.at(index))) {
    return double_info->value;
  }
  if (const auto cls = std::get_if<cp::class_info>(&pool_.at(index))) {
    return class_value(get_string(cls->name_index));
  }
  if (const auto str = std::get_if<cp::string_info>(&pool_.at(index))) {
    return get_string(str->string_index);
  }
  if (const auto mh = std::get_if<cp::method_handle_info>(&pool_.at(index))) {
    const auto [owner, name, descriptor] = get_ref(mh->reference_index);
    return method_handle(mh->reference_kind, owner, name, descriptor);
  }
  if (const auto mt = std::get_if<cp::method_type_info>(&pool_.at(index))) {
    return method_type(get_string(mt->descriptor_index));
  }
  if (const auto dyn = std::get_if<cp::dynamic_info>(&pool_.at(index))) {
    const auto curr = cursor_;
    cursor_ = bootstrap_methods_[dyn->bootstrap_method_attr_index];
    const auto handle = get_method_handle(read_u16());
    const auto arg_count = read_u16();
    std::vector<uint16_t> arg_indices;
    arg_indices.reserve(arg_count);
    for (auto i = 0; i < arg_count; i++) {
      arg_indices.emplace_back(read_u16());
    }
    cursor_ = curr;
    std::vector<value> args;
    args.reserve(arg_count);
    for (const auto arg_index : arg_indices) {
      args.emplace_back(get_constant(arg_index));
    }
    const auto [name, descriptor] = get_name_and_type(dyn->name_and_type_index);
    return dynamic(name, descriptor, handle, args);
  }
  throw std::runtime_error("Invalid constant pool_ index: " + std::to_string(index));
}
int32_t class_reader::get_int(uint16_t index) const {
  if (const auto int_info = std::get_if<cp::integer_info>(&pool_.at(index))) {
    return int_info->value;
  }
  return 0;
}
int64_t class_reader::get_long(uint16_t index) const {
  if (const auto long_info = std::get_if<cp::long_info>(&pool_.at(index))) {
    return long_info->value;
  }
  return 0L;
}
float class_reader::get_float(uint16_t index) const {
  if (const auto float_info = std::get_if<cp::float_info>(&pool_.at(index))) {
    return float_info->value;
  }
  return 0.0F;
}
double class_reader::get_double(uint16_t index) const {
  if (const auto double_info = std::get_if<cp::double_info>(&pool_.at(index))) {
    return double_info->value;
  }
  return 0.0;
}
uint8_t class_reader::read_u8() {
  if (cursor_ >= buffer_.size()) {
    throw std::runtime_error("Unexpected end of input");
  }
  return static_cast<uint8_t>(buffer_[cursor_++]);
}
int8_t class_reader::read_i8() {
  if (cursor_ >= buffer_.size()) {
    throw std::runtime_error("Unexpected end of input");
  }
  return buffer_[cursor_++];
}
uint16_t class_reader::read_u16() {
  return static_cast<uint16_t>(read_u8()) << 8 | read_u8();
}
int16_t class_reader::read_i16() {
  return static_cast<int16_t>(read_u16());
}
uint32_t class_reader::read_u32() {
  return static_cast<uint32_t>(read_u8()) << 24 | static_cast<uint32_t>(read_u8()) << 16 |
         static_cast<uint32_t>(read_u8()) << 8 | read_u8();
}
int32_t class_reader::read_i32() {
  return static_cast<int32_t>(read_u32());
}
uint64_t class_reader::read_u64() {
  return static_cast<uint64_t>(read_u8()) << 56 | static_cast<uint64_t>(read_u8()) << 48 |
         static_cast<uint64_t>(read_u8()) << 40 | static_cast<uint64_t>(read_u8()) << 32 |
         static_cast<uint64_t>(read_u8()) << 24 | static_cast<uint64_t>(read_u8()) << 16 |
         static_cast<uint64_t>(read_u8()) << 8 | read_u8();
}
int64_t class_reader::read_i64() {
  return static_cast<int64_t>(read_u64());
}
float class_reader::read_f32() {
  const auto i = read_i32();
  float f;
  std::memcpy(&f, &i, sizeof(float));
  return f;
}
double class_reader::read_f64() {
  const auto i = read_i64();
  double d;
  std::memcpy(&d, &i, sizeof(double));
  return d;
}
std::string class_reader::read_utf() {
  const auto utflen = read_u16();
  if (byte_cache_.capacity() < utflen) {
    byte_cache_.reserve(utflen);
  }
  byte_cache_.clear();
  for (auto i = 0; i < utflen; i++) {
    byte_cache_.emplace_back(read_i8());
  }
  std::string char_cache;
  char_cache.reserve(utflen);
  for (int i = 0; i < utflen; ++i) {
    if (const uint8_t byte1 = byte_cache_[i]; (byte1 & 0x80) == 0) {
      char_cache.push_back(static_cast<char>(byte1));
    } else if ((byte1 & 0xE0) == 0xC0) {
      if (i + 1 >= utflen) {
        throw std::runtime_error("Unexpected end of input during 2-byte encoding");
      }
      if (const uint8_t byte2 = byte_cache_[++i]; byte1 == 0xC0 && byte2 == 0x80) {
        char_cache.push_back('\0');
      } else {
        char_cache.push_back(static_cast<char>(byte1));
        char_cache.push_back(static_cast<char>(byte2));
      }
    } else if ((byte1 & 0xF0) == 0xE0) {
      if (i + 2 >= utflen) {
        throw std::runtime_error("Unexpected end of input during 3-byte encoding");
      }
      const uint8_t byte2 = byte_cache_[++i];
      const uint8_t byte3 = byte_cache_[++i];
      if (i + 3 < utflen && byte1 == 0xED && (byte2 & 0xF0) == 0xA0) {
        const uint8_t byte4 = byte_cache_[i + 1];
        const uint8_t byte5 = byte_cache_[i + 2];
        const uint8_t byte6 = byte_cache_[i + 3];
        if (byte4 == 0xED && (byte5 & 0xF0) == 0xB0) {
          i += 3;
          const uint32_t bits = ((byte2 & 0x0F) + 1) << 16 | (byte3 & 0x3F) << 10 | (byte5 & 0x0F) << 6 | byte6 & 0x3F;
          char_cache.push_back(static_cast<char>(0xF0 + (bits >> 18 & 0x07)));
          char_cache.push_back(static_cast<char>(0x80 + (bits >> 12 & 0x3F)));
          char_cache.push_back(static_cast<char>(0x80 + (bits >> 6 & 0x3F)));
          char_cache.push_back(static_cast<char>(0x80 + (bits & 0x3F)));
          continue;
        }
      }
      char_cache.push_back(static_cast<char>(byte1));
      char_cache.push_back(static_cast<char>(byte2));
      char_cache.push_back(static_cast<char>(byte3));
    } else {
      throw std::runtime_error("Invalid UTF-8 encoding");
    }
  }

  return char_cache;
}
} // namespace cafe
