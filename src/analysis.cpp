#include "cafe/analysis.hpp"

#include <deque>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "cafe/constants.hpp"

namespace cafe {
basic_block::basic_block(const code& code) : code_(code) {
}
void basic_block::compute_maxes() {
  output_max_stack_ = input_max_stack_;
  const auto check_stack = [&]() {
    if (output_max_stack_ > max_stack_) {
      max_stack_ = output_max_stack_;
    }
  };
  const auto check_local = [&](uint16_t index, uint16_t size) {
    index += size;
    if (index >= max_locals_) {
      max_locals_ = index;
    }
  };
  const auto push = [&](uint16_t size) {
    output_max_stack_ += size;
    check_stack();
  };
  const auto pop = [&](uint16_t size) {
    output_max_stack_ -= size;
    check_stack();
  };
  for (const auto& it : instructions_) {
    const auto& in = *it;
    std::visit(
        [&](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, insn>) {
            switch (arg.opcode) {
              case op::aaload:
              case op::baload:
              case op::caload:
              case op::faload:
              case op::iaload:
              case op::saload:
              case op::areturn:
              case op::ireturn:
              case op::freturn:
              case op::d2f:
              case op::d2i:
              case op::fadd:
              case op::fcmpg:
              case op::fcmpl:
              case op::fdiv:
              case op::fmul:
              case op::frem:
              case op::fsub:
              case op::iadd:
              case op::iand:
              case op::idiv:
              case op::imul:
              case op::ior:
              case op::irem:
              case op::ishl:
              case op::ishr:
              case op::isub:
              case op::iushr:
              case op::ixor:
              case op::l2f:
              case op::l2i:
              case op::lshl:
              case op::lshr:
              case op::lushr:
              case op::monitorenter:
              case op::monitorexit:
              case op::pop:
              case op::athrow:
                pop(1);
                break;
              case op::aastore:
              case op::bastore:
              case op::castore:
              case op::fastore:
              case op::iastore:
              case op::sastore:
              case op::dcmpg:
              case op::dcmpl:
              case op::lcmp:
                pop(3);
                break;
              case op::aconst_null:
              case op::dup:
              case op::dup_x1:
              case op::dup_x2:
              case op::f2d:
              case op::f2l:
              case op::fconst_0:
              case op::fconst_1:
              case op::fconst_2:
              case op::i2d:
              case op::i2l:
              case op::iconst_m1:
              case op::iconst_0:
              case op::iconst_1:
              case op::iconst_2:
              case op::iconst_3:
              case op::iconst_4:
              case op::iconst_5:
                push(1);
                break;
              case op::aload_0:
              case op::aload_1:
              case op::aload_2:
              case op::aload_3:
                push(1);
                check_local(arg.opcode - op::aload_0, 1);
                break;
              case op::lreturn:
              case op::dreturn:
              case op::dadd:
              case op::ddiv:
              case op::dmul:
              case op::drem:
              case op::dsub:
              case op::ladd:
              case op::land:
              case op::ldiv:
              case op::lmul:
              case op::lor:
              case op::lrem:
              case op::lsub:
              case op::lxor:
              case op::pop2:
                pop(2);
                break;
              case op::astore_0:
              case op::astore_1:
              case op::astore_2:
              case op::astore_3:
                pop(1);
                check_local(arg.opcode - op::astore_0, 1);
                break;
              case op::dastore:
              case op::lastore:
                pop(4);
                break;
              case op::dconst_0:
              case op::dconst_1:
              case op::dup2:
              case op::dup2_x1:
              case op::dup2_x2:
              case op::lconst_0:
              case op::lconst_1:
                push(2);
                break;
              case op::dload_0:
              case op::dload_1:
              case op::dload_2:
              case op::dload_3:
                push(2);
                check_local(arg.opcode - op::dload_0, 2);
                break;
              case op::dstore_0:
              case op::dstore_1:
              case op::dstore_2:
              case op::dstore_3:
                pop(2);
                check_local(arg.opcode - op::dstore_0, 2);
                break;
              case op::fload_0:
              case op::fload_1:
              case op::fload_2:
              case op::fload_3:
                push(1);
                check_local(arg.opcode - op::fload_0, 1);
                break;
              case op::fstore_0:
              case op::fstore_1:
              case op::fstore_2:
              case op::fstore_3:
                pop(1);
                check_local(arg.opcode - op::fstore_0, 1);
                break;
              case op::iload_0:
              case op::iload_1:
              case op::iload_2:
              case op::iload_3:
                push(1);
                check_local(arg.opcode - op::iload_0, 1);
                break;
              case op::istore_0:
              case op::istore_1:
              case op::istore_2:
              case op::istore_3:
                pop(1);
                check_local(arg.opcode - op::istore_0, 1);
                break;
              case op::lload_0:
              case op::lload_1:
              case op::lload_2:
              case op::lload_3:
                push(2);
                check_local(arg.opcode - op::lload_0, 2);
                break;
              case op::lstore_0:
              case op::lstore_1:
              case op::lstore_2:
              case op::lstore_3:
                pop(2);
                check_local(arg.opcode - op::lstore_0, 2);
                break;
              default:
                break;
            }
          } else if constexpr (std::is_same_v<T, var_insn>) {
            if (arg.is_load()) {
              push(arg.is_wide() ? 2 : 1);
            } else if (arg.opcode != op::ret) {
              pop(arg.is_wide() ? 2 : 1);
            }
            check_local(arg.index, arg.is_wide() ? 2 : 1);
          } else if constexpr (std::is_same_v<T, push_insn>) {
            push(arg.is_wide() ? 2 : 1);
          } else if constexpr (std::is_same_v<T, field_insn>) {
            const type type(arg.desc);
            auto start = output_max_stack_;
            switch (arg.opcode) {
              case op::getstatic:
                push(type.size());
                break;
              case op::putstatic:
                pop(type.size());
                break;
              case op::getfield:
                push(type.size() - 1);
                break;
              case op::putfield:
                pop(type.size() + 1);
                break;
              default:
                break;
            }
          } else if constexpr (std::is_same_v<T, branch_insn>) {
            switch (arg.opcode) {
              case op::if_acmpeq:
              case op::if_acmpne:
              case op::if_icmpeq:
              case op::if_icmpne:
              case op::if_icmplt:
              case op::if_icmpge:
              case op::if_icmpgt:
              case op::if_icmple:
                pop(2);
                break;
              case op::ifeq:
              case op::ifne:
              case op::iflt:
              case op::ifge:
              case op::ifgt:
              case op::ifle:
              case op::ifnonnull:
              case op::ifnull:
                pop(1);
                break;
              case op::jsr:
              case op::jsr_w:
                push(1);
                break;
              default:
                break;
            }
          } else if constexpr (std::is_same_v<T, iinc_insn>) {
            check_local(arg.index, 1);
          } else if constexpr (std::is_same_v<T, invoke_dynamic_insn>) {
            const type type(arg.desc);
            auto size = static_cast<int32_t>(type.return_type().size());
            for (const auto& p : type.parameter_types()) {
              size -= p.size();
            }
            output_max_stack_ += size;
            check_stack();
          } else if constexpr (std::is_same_v<T, method_insn>) {
            const type type(arg.desc);
            auto start = output_max_stack_;
            auto size = static_cast<int32_t>(type.return_type().size()) - (arg.opcode != op::invokestatic);
            for (const auto& p : type.parameter_types()) {
              size -= p.size();
            }
            output_max_stack_ += size;
            check_stack();
          } else if constexpr (std::is_same_v<T, lookup_switch_insn>) {
            pop(1);
          } else if constexpr (std::is_same_v<T, table_switch_insn>) {
            pop(1);
          } else if constexpr (std::is_same_v<T, multi_array_insn>) {
            pop(arg.dims == 0 ? 0 : arg.dims - 1);
          } else if constexpr (std::is_same_v<T, type_insn>) {
            if (arg.opcode == op::new_) {
              push(1);
            }
          } else if constexpr (std::is_same_v<T, label>) {
            if (handlers_.find(arg.id()) != handlers_.end()) {
              output_max_stack_ = 1;
              check_stack();
            }
          }
        },
        in);
  }
}
void basic_block::compute_frames(const std::string_view& class_name) {
  output_locals_ = input_locals_;
  output_stack_ = input_stack_;
  output_max_stack_ = input_max_stack_;
  const auto is_wide = [](const std::optional<frame_var>& var) -> bool {
    return var && (std::holds_alternative<double_var>(*var) || std::holds_alternative<long_var>(*var));
  };
  const auto push = [&](const std::optional<frame_var>& var) {
    output_max_stack_++;
    if (output_max_stack_ > max_stack_) {
      max_stack_ = output_max_stack_;
    }
    output_stack_.emplace_front(var);
  };
  const auto pop = [&]() -> std::optional<frame_var> {
    output_max_stack_--;
    if (output_stack_.empty()) {
      return std::nullopt;
    }
    const auto var = output_stack_.front();
    output_stack_.pop_front();
    return var;
  };
  const auto store = [&](uint16_t index, const std::optional<frame_var>& var) {
    if (index >= output_locals_.size()) {
      output_locals_.resize(index + 1);
    }
    output_locals_[index] = var;
    index++;
    if (index > max_locals_) {
      max_locals_ = index;
    }
  };
  const auto load = [&](uint16_t index) -> const std::optional<frame_var>& {
    if (index + 1 > max_locals_) {
      max_locals_ = index + 1;
    }
    if (index >= output_locals_.size()) {
      output_locals_.resize(index + 1);
    }
    return output_locals_[index];
  };
  const auto to_frame_var = [](const type& type) -> frame_var {
    switch (type.kind()) {
      case type_kind::boolean:
      case type_kind::byte:
      case type_kind::char_:
      case type_kind::short_:
      case type_kind::int_:
        return int_var();
      case type_kind::long_:
        return long_var();
      case type_kind::double_:
        return double_var();
      case type_kind::float_:
        return float_var();
      case type_kind::array:
        return object_var(type.get());
      case type_kind::object:
        return object_var(type.internal());
      default:
        return top_var();
    }
  };
  uint32_t insn_count = 0;
  uint32_t uninitialized_count = 0;
  std::vector<std::string> uninitialized;
  for (const auto& it : instructions_) {
    const auto& in = *it;
    std::visit(
        [&](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, insn>) {
            switch (arg.opcode) {
              case op::aaload: {
                pop();
                const auto array = pop();
                if (array && std::holds_alternative<object_var>(*array)) {
                  const type type(std::get<object_var>(*array).type);
                  push(object_var(type.array_type().internal()));
                } else {
                  push(std::nullopt);
                }
                break;
              }
              case op::baload:
              case op::caload:
              case op::saload:
              case op::iaload:
                pop();
                pop();
                push(int_var());
                break;
              case op::faload:
                pop();
                pop();
                push(float_var());
                break;
              case op::aastore:
              case op::bastore:
              case op::castore:
              case op::sastore:
              case op::fastore:
              case op::iastore:
                pop();
                pop();
                pop();
                break;
              case op::lastore:
                case op::dastore:
              pop();
              pop();
              pop();
              pop();
              break;
              case op::aconst_null:
                push(null_var());
                break;
              case op::areturn:
              case op::ireturn:
              case op::freturn:
              case op::athrow:
              case op::monitorenter:
              case op::monitorexit:
              case op::pop:
                pop();
                break;
              case op::fconst_0:
              case op::fconst_1:
              case op::fconst_2:
                push(float_var());
                break;
              case op::iconst_m1:
              case op::iconst_1:
              case op::iconst_2:
              case op::iconst_3:
              case op::iconst_4:
              case op::iconst_5:
                push(int_var());
                break;
              case op::lconst_0:
              case op::lconst_1:
                push(long_var());
                push(top_var());
                break;
              case op::dreturn:
              case op::lreturn:
              case op::pop2:
                pop();
                pop();
                break;
              case op::dadd:
              case op::ddiv:
              case op::dmul:
              case op::drem:
              case op::dsub:
                pop();
                pop();
                pop();
                pop();
                push(double_var());
                push(top_var());
                break;
              case op::ladd:
              case op::land:
              case op::ldiv:
              case op::lmul:
              case op::lor:
              case op::lrem:
              case op::lsub:
              case op::lxor:
                pop();
                pop();
                pop();
                pop();
                push(long_var());
                push(top_var());
                break;
              case op::dcmpg:
              case op::dcmpl:
              case op::lcmp:
                pop();
                pop();
                pop();
                pop();
                push(int_var());
                break;
              case op::dup: {
                const auto var = pop();
                push(var);
                push(var);
                break;
              }
              case op::dup_x1: {
                const auto var1 = pop();
                const auto var2 = pop();
                push(var1);
                push(var2);
                push(var1);
                break;
              }
              case op::dup_x2: {
                const auto var1 = pop();
                const auto var2 = pop();
                const auto var3 = pop();
                push(var1);
                push(var3);
                push(var2);
                push(var1);
                break;
              }
              case op::dup2: {
                const auto var1 = pop();
                const auto var2 = pop();
                push(var2);
                push(var1);
                push(var2);
                push(var1);
                break;
              }
              case op::dup2_x1: {
                const auto var1 = pop();
                const auto var2 = pop();
                const auto var3 = pop();
                push(var2);
                push(var1);
                push(var3);
                push(var2);
                push(var1);
                break;
              }
              case op::dup2_x2: {
                const auto var1 = pop();
                const auto var2 = pop();
                const auto var3 = pop();
                const auto var4 = pop();
                push(var2);
                push(var1);
                push(var4);
                push(var3);
                push(var2);
                push(var1);
                break;
              }
              case op::swap: {
                const auto var1 = pop();
                const auto var2 = pop();
                push(var1);
                push(var2);
              }
              case op::dconst_0:
              case op::dconst_1:
                push(double_var());
                push(top_var());
                break;
              case op::f2i:
              case op::arraylength:
                pop();
                push(int_var());
                break;
              case op::f2l:
              case op::i2l:
                pop();
                push(long_var());
                push(top_var());
                break;
              case op::l2f:
              case op::d2f:
              case op::fadd:
              case op::fdiv:
              case op::fmul:
              case op::frem:
              case op::fsub:
                pop();
                pop();
                push(float_var());
                break;
              case op::daload:
              case op::l2d:
                pop();
                pop();
                push(double_var());
                push(top_var());
                break;
              case op::l2i:
              case op::d2i:
              case op::fcmpg:
              case op::fcmpl:
              case op::iadd:
              case op::iand:
              case op::idiv:
              case op::imul:
              case op::ior:
              case op::irem:
              case op::ishl:
              case op::ishr:
              case op::isub:
              case op::iushr:
              case op::ixor:
                pop();
                pop();
                push(int_var());
                break;
              case op::lshl:
              case op::lshr:
              case op::lushr:
                pop();
                pop();
                pop();
                push(long_var());
                push(top_var());
                break;
              case op::f2d:
              case op::i2d:
                pop();
                push(double_var());
                push(top_var());
                break;
              case op::i2f:
                pop();
                push(float_var());
                break;
              case op::laload:
              case op::d2l:
                pop();
                pop();
                push(long_var());
                push(top_var());
                break;
              case op::aload_0:
              case op::aload_1:
              case op::aload_2:
              case op::aload_3:
                push(load(arg.opcode - op::aload_0));
                break;
              case op::astore_0:
              case op::astore_1:
              case op::astore_2:
              case op::astore_3:
                store(arg.opcode - op::astore_0, pop());
                break;
              case op::iload_0:
              case op::iload_1:
              case op::iload_2:
              case op::iload_3:
                push(load(arg.opcode - op::iload_0));
                break;
              case op::istore_0:
              case op::istore_1:
              case op::istore_2:
              case op::istore_3:
                store(arg.opcode - op::istore_0, pop());
                break;
              case op::fstore_0:
              case op::fstore_1:
              case op::fstore_2:
              case op::fstore_3:
                store(arg.opcode - op::fstore_0, pop());
                break;
              case op::fload_0:
              case op::fload_1:
              case op::fload_2:
              case op::fload_3:
                push(load(arg.opcode - op::fload_0));
                break;
              case op::dload_0:
              case op::dload_1:
              case op::dload_2:
              case op::dload_3:
                push(load(arg.opcode - op::dload_0));
                push(top_var());
                break;
              case op::dstore_0:
              case op::dstore_1:
              case op::dstore_2:
              case op::dstore_3:
                pop();
                store(arg.opcode - op::dstore_0, pop());
                store(arg.opcode - op::dstore_0 + 1, top_var());
                break;
              case op::lload_0:
              case op::lload_1:
              case op::lload_2:
              case op::lload_3:
                push(load(arg.opcode - op::lload_0));
                push(top_var());
                break;
              case op::lstore_0:
              case op::lstore_1:
              case op::lstore_2:
              case op::lstore_3:
                pop();
                store(arg.opcode - op::lstore_0, pop());
                store(arg.opcode - op::lstore_0 + 1, top_var());
                break;
              default:
                break;
            }
          } else if constexpr (std::is_same_v<T, var_insn>) {
            if (arg.is_load()) {
              const auto& var = load(arg.index);
              push(var);
              if (arg.is_wide()) {
                push(top_var());
              }
            } else if (arg.opcode != op::ret) {
              if (arg.is_wide()) {
                pop();
              }
              const auto var = pop();
              store(arg.index, var);
              if (arg.is_wide()) {
                store(arg.index + 1, top_var());
              }
            }
          } else if constexpr (std::is_same_v<T, array_insn>) {
            pop();
            if (const auto object_type = std::get_if<std::string>(&arg.type)) {
              std::string array_desc = "[" + (object_type->at(0) != '[' ? "L" + *object_type + ";" : *object_type);
              push(object_var(array_desc));
            } else if (const auto primitive_type = std::get_if<uint8_t>(&arg.type)) {
              switch (*primitive_type) {
                case array::t_boolean:
                  push(object_var("[Z"));
                  break;
                case array::t_char:
                  push(object_var("[C"));
                  break;
                case array::t_float:
                  push(object_var("[F"));
                case array::t_double:
                  push(object_var("[D"));
                  break;
                case array::t_byte:
                  push(object_var("[B"));
                  break;
                case array::t_short:
                  push(object_var("[S"));
                  break;
                case array::t_int:
                  push(object_var("[I"));
                  break;
                case array::t_long:
                  push(object_var("[J"));
                  break;
                default:
                  break;
              }
            }
          } else if constexpr (std::is_same_v<T, push_insn>) {
            auto start = output_max_stack_;
            std::visit(
                [&](auto&& varg) {
                  using V = std::decay_t<decltype(varg)>;
                  if constexpr (std::is_same_v<V, int32_t>) {
                    push(int_var());
                  } else if constexpr (std::is_same_v<V, float>) {
                    push(float_var());
                  } else if constexpr (std::is_same_v<V, int64_t>) {
                    push(long_var());
                    push(top_var());
                  } else if constexpr (std::is_same_v<V, double>) {
                    push(double_var());
                    push(top_var());
                  } else if constexpr (std::is_same_v<V, type>) {
                    push(object_var("java/lang/Class"));
                  } else if constexpr (std::is_same_v<V, std::string>) {
                    push(object_var("java/lang/String"));
                  } else if constexpr (std::is_same_v<V, method_handle>) {
                    push(object_var("java/lang/invoke/MethodHandle"));
                  } else if constexpr (std::is_same_v<V, method_type>) {
                    push(object_var("java/lang/invoke/MethodType"));
                  } else if constexpr (std::is_same_v<V, dynamic>) {
                    const type type(varg.desc);
                    push(to_frame_var(type));
                    if (type.size() == 2) {
                      push(top_var());
                    }
                  }
                },
                arg.value);
          } else if constexpr (std::is_same_v<T, type_insn>) {
            if (arg.opcode == op::checkcast) {
              pop();
              push(object_var(arg.type));
            } else if (arg.opcode == op::instanceof) {
              pop();
              push(int_var());
            } else if (arg.opcode == op::new_) {
              auto distance = static_cast<uint32_t>(std::distance(code_.begin(), it));
              label_id id = static_cast<uint64_t>(distance) << 32 | uninitialized_count++;
              uninitialized.emplace_back(arg.type);
              push(uninitialized_var(label(id)));
            }
          } else if constexpr (std::is_same_v<T, field_insn>) {
            const type type(arg.desc);
            auto start = output_max_stack_;
            switch (arg.opcode) {
              case op::getfield:
                pop();
                push(to_frame_var(type));
                if (type.size() == 2) {
                  push(top_var());
                }
                break;
              case op::getstatic:
                push(to_frame_var(type));
                if (type.size() == 2) {
                  push(top_var());
                }
                break;
              case op::putfield:
                pop();
                if (type.size() == 2) {
                  pop();
                }
                pop();
                break;
              case op::putstatic:
                pop();
                if (type.size() == 2) {
                  pop();
                }
                break;
              default:
                break;
            }
          } else if constexpr (std::is_same_v<T, branch_insn>) {
            switch (arg.opcode) {
              case op::if_acmpeq:
              case op::if_acmpne:
              case op::if_icmpeq:
              case op::if_icmpne:
              case op::if_icmplt:
              case op::if_icmpge:
              case op::if_icmpgt:
              case op::if_icmple:
                pop();
                pop();
                break;
              case op::ifeq:
              case op::ifne:
              case op::iflt:
              case op::ifge:
              case op::ifgt:
              case op::ifle:
              case op::ifnonnull:
              case op::ifnull:
                pop();
                break;
              default:
                break;
            }
          } else if constexpr (std::is_same_v<T, iinc_insn>) {
            if (arg.index + 1 > max_locals_) {
              max_locals_ = arg.index + 1;
            }
          } else if constexpr (std::is_same_v<T, invoke_dynamic_insn>) {
            const type type(arg.desc);
            for (const auto& p : type.parameter_types()) {
              pop();
              if (p.size() == 2) {
                pop();
              }
            }
            if (const auto return_type = type.return_type(); return_type.kind() != type_kind::void_) {
              push(to_frame_var(return_type));
                if (return_type.size() == 2) {
                    push(top_var());
                }
            }
          } else if constexpr (std::is_same_v<T, method_insn>) {
            const type type(arg.desc);
            auto start = output_max_stack_;
            if (arg.opcode != op::invokestatic) {
              pop();
            }
            for (const auto& p : type.parameter_types()) {
              pop();
              if (p.size() == 2) {
                pop();
              }
            }
            if (const auto return_type = type.return_type(); return_type.kind() != type_kind::void_) {
              push(to_frame_var(return_type));
              if (return_type.size() == 2) {
                push(top_var());
              }
            }
            if (arg.opcode == op::invokespecial && arg.name == "<init>") {
              if (!output_stack_.empty() && output_stack_.front()) {
                if (const auto uninit = std::get_if<uninitialized_var>(&*output_stack_.front())) {
                  const auto id = uninit->offset.id();
                  const auto uninit_index = id & 0xFFFFFFFF;
                  std::string uninit_type =
                      uninit_index < uninitialized.size() ? uninitialized[uninit_index] : arg.owner;
                  pop();
                  push(object_var(uninit_type));
                  return;
                }
              }
              if (!output_locals_.empty() && output_locals_[0]) {
                if (std::holds_alternative<uninitialized_this_var>(*output_locals_[0])) {
                  output_locals_[0] = object_var(class_name);
                }
              }
            }
          } else if constexpr (std::is_same_v<T, lookup_switch_insn>) {
            pop();
          } else if constexpr (std::is_same_v<T, table_switch_insn>) {
            pop();
          } else if constexpr (std::is_same_v<T, multi_array_insn>) {
            for (auto i = 0; i < arg.dims; i++) {
              pop();
            }
            push(object_var(arg.desc));
          } else if constexpr (std::is_same_v<T, label>) {
            if (handlers_.find(arg.id()) != handlers_.end()) {
              output_max_stack_ = 1;
              output_stack_.resize(1);
              if (output_max_stack_ > max_stack_) {
                max_stack_ = output_max_stack_;
              }
            }
          }
        },
        in);
    insn_count++;
  }
}
bool basic_block::merge(const class_tree& tree, const basic_block& other, const std::vector<std::string>& tcb_types) {
  const auto merge_type = [&](const std::optional<frame_var>& var1, const std::optional<frame_var>& var2) -> std::optional<frame_var> {
    if (!var1) {
      return var2;
    }
    if (!var2) {
      return var1;
    }
    if (*var1 == *var2) {
      return var1;
    }
    if (std::holds_alternative<null_var>(*var1) && std::holds_alternative<object_var>(*var2)) {
      return var2;
    }
    if (std::holds_alternative<null_var>(*var2) && std::holds_alternative<object_var>(*var1)) {
      return var1;
    }
    if (const auto obj1 = std::get_if<object_var>(&*var1)) {
      if (const auto obj2 = std::get_if<object_var>(&*var2)) {
        if (tree.is_assignable_from(obj1->type, obj2->type)) {
          return var1;
        }
        if (tree.is_assignable_from(obj2->type, obj1->type)) {
          return var2;
        }
        auto node = tree.get(obj1->type);
        do {
          if (node != nullptr) {
            node = node->super_class;
          }
        } while (node != nullptr && !tree.is_assignable_from(node->name, obj2->type));
        return object_var(node == nullptr ? "java/lang/Object" : node->name);
      }
    }
    return top_var();
  };
  bool changed = false;
  if (!tcb_types.empty()) {
    std::optional<frame_var> tcb_type = input_stack_.empty() ? std::nullopt : input_stack_[0];
    for (const auto& type : tcb_types) {
      if (!tcb_type) {
        tcb_type = object_var(type);
      } else {
        tcb_type = merge_type(tcb_type, object_var(type));
      }
    }
   if (input_stack_.size() != 1 || input_stack_[0] != tcb_type) {
     input_stack_.clear();
     input_stack_.emplace_back(tcb_type);
     input_max_stack_ = 1;
     changed = true;
   }
  } else if (input_stack_.size() < other.output_stack_.size()) {
    input_stack_.resize(other.output_stack_.size());
    changed = true;
  }
  if (input_locals_.size() < other.output_locals_.size()) {
    input_locals_.resize(other.output_locals_.size());
    changed = true;
  }
  if (tcb_types.empty()) {
    for (size_t i = 0; i < other.output_stack_.size(); i++) {
      const auto var = merge_type(input_stack_[i], other.output_stack_[i]);
      if (input_stack_[i] != var) {
        input_stack_[i] = var;
        changed = true;
      }
    }
  }
  for (size_t i = 0; i < other.output_locals_.size(); i++) {
    const auto var = merge_type(input_locals_[i], other.output_locals_[i]);
    if (input_locals_[i] != var) {
      input_locals_[i] = var;
      changed = true;
    }
  }
  return changed;
}
std::string basic_block::to_string() const {
  return to_string(", ");
}
std::string basic_block::to_string(const std::string_view& delim) const {
  std::ostringstream oss;
  oss << "basic_block(" << debug_name << ") {";
  bool first = true;
  for (const auto& it : instructions_) {
    if (!first) {
      oss << delim;
    }
    first = false;
    oss << cafe::to_string(*it);
  }
  oss << "} -> (";
  first = true;
  for (const auto& [succ, tcb_type] : successors_) {
    if (!first) {
      oss << ", ";
    }
    first = false;
    oss << succ->debug_name;
  }
  oss << ")";
  return oss.str();
}
std::vector<frame_var> basic_block::locals() const {
  std::vector<frame_var> locals;
  auto num_local = 0;
  auto top_trail = 0;
  for (auto i = 0; i < input_locals_.size(); i++) {
    auto& local = input_locals_[i];
    if (!local) {
      continue;
    }
    if (std::holds_alternative<double_var>(*local) || std::holds_alternative<long_var>(*local)) {
      i += 1;
    }
    if (std::holds_alternative<top_var>(*local)) {
      top_trail++;
    } else {
      num_local += top_trail + 1;
      top_trail = 0;
    }
  }
  locals.reserve(num_local);
  auto i = 0;
  while (num_local-- > 0) {
    auto& local = input_locals_[i];
    if (!local) {
      continue;
    }
    if (std::holds_alternative<double_var>(*local) || std::holds_alternative<long_var>(*local)) {
      i += 2;
    } else {
      i++;
    }
    locals.emplace_back(*local);
  }
  return locals;
}
std::vector<frame_var> basic_block::stack() const {
  std::vector<frame_var> stack;
  stack.reserve(input_stack_.size());
  for (auto it = input_stack_.rbegin(); it != input_stack_.rend(); ++it) {
    const auto& var = *it;
    if (!var) {
      continue;
    }
    if (std::holds_alternative<double_var>(*var) || std::holds_alternative<long_var>(*var)) {
      ++it;
    }
    stack.emplace_back(*var);
  }
  return stack;
}
frame_compute_result::frame_compute_result(uint16_t max_locals, uint16_t max_stack) : max_locals_(max_locals), max_stack_(max_stack) {
}
frame_compute_result::frame_compute_result(std::vector<std::pair<code::const_iterator, label>>&& labels,
                                           std::vector<std::pair<label, frame>>&& frames, uint16_t max_locals, uint16_t max_stack) : inject_labels_(labels), frames_(frames), max_locals_(max_locals), max_stack_(max_stack) {
}
const std::vector<std::pair<code::const_iterator, label>>& frame_compute_result::labels() const {
  return inject_labels_;
}
const std::vector<std::pair<label, frame>>& frame_compute_result::frames() const {
  return frames_;
}
uint16_t frame_compute_result::max_locals() const {
  return max_locals_;
}
uint16_t frame_compute_result::max_stack() const {
  return max_stack_;
}
basic_block_graph::basic_block_graph(const code& code) {
  if (code.empty()) {
    return;
  }
  std::unordered_set<label_id> starting_labels;
  std::unordered_set<label_id> starting_tcbs;
  std::unordered_set<const instruction*> fallthroughs;
  for (auto it = code.begin(); it != code.end(); ++it) {
    auto* in = &*it;
     if (const auto branch = std::get_if<branch_insn>(in)) {
      starting_labels.emplace(branch->target.id());
      if (const auto next = std::next(it); next != code.end()) {
        fallthroughs.emplace(&*next);
      }
    } else if (const auto table_switch = std::get_if<table_switch_insn>(in)) {
      for (const auto& target : table_switch->targets) {
        starting_labels.emplace(target.id());
      }
      starting_labels.emplace(table_switch->default_target.id());
      if (const auto next = std::next(it); next != code.end()) {
        fallthroughs.emplace(&*next);
      }
    } else if (const auto lookup_switch = std::get_if<lookup_switch_insn>(in)) {
      for (const auto& [key, target] : lookup_switch->targets) {
        starting_labels.emplace(target.id());
      }
      starting_labels.emplace(lookup_switch->default_target.id());
      if (const auto next = std::next(it); next != code.end()) {
        fallthroughs.emplace(&*next);
      }
    } else if (const auto ins = std::get_if<insn>(in)) {
      switch (ins->opcode) {
        case op::return_:
        case op::areturn:
        case op::ireturn:
        case op::freturn:
        case op::dreturn:
        case op::lreturn:
        case op::athrow:
          if (auto next = std::next(it); next != code.end()) {
            fallthroughs.emplace(&*next);
          }
          break;
        default:
          break;
      }
    }
  }
  for (const auto& tcb : code.tcbs) {
    starting_labels.emplace(tcb.start.id());
    starting_labels.emplace(tcb.handler.id());
    starting_labels.emplace(tcb.end.id());
    starting_tcbs.emplace(tcb.start.id());
  }
  blocks_.emplace_back(code);
  basic_block* current = &blocks_.back();
  std::unordered_map<const instruction*, basic_block*> block_map;
  const auto get_block = [&](const instruction* in) -> basic_block* {
    auto it = block_map.find(in);
    if (it == block_map.end()) {
      blocks_.emplace_back(code);
      it = block_map.emplace(in, &blocks_.back()).first;
    }
    return it->second;
  };
  bool tcb_flag = false;
  for (auto it = code.begin(); it != code.end(); ++it) {
    auto* in = &*it;
    const auto lbl = std::get_if<label>(in);
    if (tcb_flag) {
      current = get_block(in);
      tcb_flag = false;
    } else if (!current->instructions_.empty()) {
      if (fallthroughs.find(in) != fallthroughs.end()) {
        current = get_block(in);
      } else if (lbl) {
        if (starting_labels.find(lbl->id()) != starting_labels.end()) {
          current = get_block(in);
        }
      }
    }
    current->instructions_.emplace_back(it);
    block_map[in] = current;
    if (lbl && starting_tcbs.find(lbl->id()) != starting_tcbs.end()) {
      tcb_flag = true;
    }
  }
  for (auto it = code.begin(); it != code.end(); ++it) {
    auto* in = &*it;
    current = get_block(in);
    bool link_next = true;
    if (const auto branch = std::get_if<branch_insn>(in)) {
      link_next = branch->opcode != op::goto_ && branch->opcode != op::goto_w && branch->opcode != op::jsr &&
                  branch->opcode != op::jsr_w;
      if (const auto target = code.find_label(branch->target); target != code.end()) {
        auto target_block = get_block(&*target);
        target_block->needs_frame_ = true;
        current->successors_[target_block] = {};
      }
    } else if (const auto table_switch = std::get_if<table_switch_insn>(in)) {
      link_next = false;
      for (const auto& branch_target : table_switch->targets) {
        if (const auto target = code.find_label(branch_target); target != code.end()) {
          auto target_block = get_block(&*target);
          target_block->needs_frame_ = true;
          current->successors_[target_block] = {};
        }
      }
      if (const auto target = code.find_label(table_switch->default_target); target != code.end()) {
        auto target_block = get_block(&*target);
        target_block->needs_frame_ = true;
        current->successors_[target_block] = {};
      }
    } else if (const auto lookup_switch = std::get_if<lookup_switch_insn>(in)) {
      link_next = false;
      for (const auto& [key, branch_target] : lookup_switch->targets) {
        if (const auto target = code.find_label(branch_target); target != code.end()) {
          auto target_block = get_block(&*target);
          target_block->needs_frame_ = true;
          current->successors_[target_block] = {};
        }
      }
      if (const auto target = code.find_label(lookup_switch->default_target); target != code.end()) {
        auto target_block = get_block(&*target);
        target_block->needs_frame_ = true;
        current->successors_[target_block] = {};
      }
    } else if (const auto ins = std::get_if<insn>(in)) {
      switch (ins->opcode) {
        case op::return_:
        case op::areturn:
        case op::ireturn:
        case op::freturn:
        case op::dreturn:
        case op::lreturn:
        case op::athrow:
          link_next = false;
          break;
        default:
          break;
      }
    }

    if (link_next) {
      if (const auto next = std::next(it); next != code.end()) {
        if (auto succ = get_block(&*next); current != succ) {
          current->successors_[succ] = {};
        }
      }
    }
  }

  for (const auto& tcb : code.tcbs) {
    if (const auto handler = code.find_label(tcb.handler); handler != code.end()) {
      auto sucessor = get_block(&*handler);
      sucessor->needs_frame_ = true;
      const auto tcb_type = tcb.type ? *tcb.type : "java/lang/Throwable";
      sucessor->handlers_.emplace(tcb.handler.id());
      for (auto it = code.find_label(tcb.start); it != code.find_label(tcb.end); ++it) {
        auto* in = &*it;
        auto* block = get_block(in);
        auto sit = block->successors_.find(sucessor);
        if (sit == block->successors_.end()) {
          block->successors_[sucessor] = {tcb_type};
        } else {
          sit->second.emplace_back(tcb_type);
        }
      }
    }
  }

  auto count = 1;
  for (auto& b : blocks_) {
    auto i = count;
    while (i > 0) {
      i--;
      const auto letter = static_cast<char>('A' + (i % 26));
      b.debug_name.insert(0, 1, letter);
      i /= 26;
    }
    count++;
  }
}
frame_compute_result basic_block_graph::compute_frames(const class_tree& tree, const std::string_view& class_name, const std::vector<std::optional<frame_var>>& start_locals) {
  uint16_t max_locals = 0;
  for (const auto& local : start_locals) {
    if (local && (std::holds_alternative<long_var>(*local) || std::holds_alternative<double_var>(*local))) {
      max_locals += 2;
    } else {
      max_locals++;
    }
  }
  if (blocks_.empty()) {
    return {max_locals, 0};
  }
  // A single pass is sufficient to find max locals
  for (auto& block : blocks_) {
    block.max_locals_ = 0;
    block.input_max_stack_ = -1;
    block.output_max_stack_ = 0;
    block.max_stack_ = 0;
    block.compute_maxes();
    if (block.max_locals_ > max_locals) {
      max_locals = block.max_locals_;
    }
    block.max_locals_ = 0;
    block.input_max_stack_ = -1;
    block.output_max_stack_ = 0;
    block.max_stack_ = 0;
  }
  std::vector<std::optional<frame_var>> input_locals = start_locals;
  while (input_locals.size() < max_locals) {
    input_locals.emplace_back(top_var());
  }
  std::deque<basic_block*> worklist;
  auto* head = &blocks_.front();
  head->input_max_stack_ = 0;
  head->max_locals_ = max_locals;
  head->input_locals_ = std::move(input_locals);
  worklist.emplace_back(head);

  while (!worklist.empty()) {
    auto* current = worklist.front();
    worklist.pop_front();
    current->compute_frames(class_name);
    for (auto& [succ, tcb_type] : current->successors_) {
      if (succ->merge(tree, *current, tcb_type) || (succ->input_max_stack_ < current->output_max_stack_)) {
        succ->max_locals_ = current->max_locals_;
        succ->input_max_stack_ = current->output_max_stack_;
        if (std::find(worklist.begin(), worklist.end(), succ) == worklist.end()) {
          worklist.emplace_back(succ);
        }
      }
    }
  }

  std::vector<std::pair<code::const_iterator, label>> inject_labels;

  for (auto& block : blocks_) {
    for (auto& var : block.input_stack_) {
      if (!var) {
        continue;
      }
      if (const auto uninit = std::get_if<uninitialized_var>(&*var)) {
        const auto it_index = static_cast<size_t>((uninit->offset.id() >> 32) & 0xFFFFFFFF);
        if (it_index < block.code_.size()) {
          auto it = block.code_.begin();
          std::advance(it, it_index);
          auto maybe_label = it == block.code_.begin() ? it : std::prev(it);
          if (const auto lbl = std::get_if<label>(&*maybe_label)) {
            var = uninitialized_var(*lbl);
          } else {
            label l;
            inject_labels.emplace_back(it, l);
            var = uninitialized_var(l);
          }
        }
      }
    }

    for (auto& var : block.input_locals_) {
      if (!var) {
        continue;
      }
      if (const auto uninit = std::get_if<uninitialized_var>(&*var)) {
        const auto it_index = static_cast<size_t>((uninit->offset.id() >> 32) & 0xFFFFFFFF);
        if (it_index < block.code_.size()) {
          auto it = block.code_.begin();
          std::advance(it, it_index);
          if (const auto lbl = std::get_if<label>(&*it)) {
            var = uninitialized_var(*lbl);
          } else {
            label l;
            inject_labels.emplace_back(it, l);
            var = uninitialized_var(l);
          }
        }
      }
    }
  }

  std::vector<frame_var> locals;
  std::vector<frame_var> stack;
  for (const auto& local : head->locals()) {
    locals.emplace_back(local);
  }

  std::vector<std::pair<label, frame>> frames;
  std::unordered_map<const basic_block*, label> block_labels;

  const auto get_label = [&](const basic_block* block) -> label {
    auto it = block_labels.find(block);
    if (it == block_labels.end()) {
      const auto in = block->instructions_.front();
      if (const auto lbl = std::get_if<label>(&*in)) {
        it = block_labels.emplace(block, *lbl).first;
      } else {
        label l;
        inject_labels.emplace_back(in, l);
        it = block_labels.emplace(block, l).first;
      }
    }
    return it->second;
  };

  for (const auto& block : blocks_) {
    if (!block.needs_frame_) {
      continue;
    }
    const auto block_locals = block.locals();
    const auto block_stack = block.stack();
    bool written = false;
    if (block_stack.empty()) {
      if (block_locals.size() > locals.size() && block_locals.size() - locals.size() <= 3) {
        std::vector<frame_var> append_locals;
        bool same = true;
        auto j = 0;
        for (const auto& local : block_locals) {
          if (j < locals.size()) {
            if (local != locals[j]) {
              same = false;
              break;
            }
          } else {
            append_locals.emplace_back(local);
          }
          j++;
        }
        if (same) {
          frames.emplace_back(get_label(&block), append_frame(append_locals));
          written = true;
        }
      } else if (locals.size() > block_locals.size() && locals.size() - block_locals.size() <= 3) {
        bool same = true;
        for (auto j = 0; j < block_locals.size(); j++) {
          if (locals[j] != block_locals[j]) {
            same = false;
            break;
          }
        }
        if (same) {
          frames.emplace_back(get_label(&block), chop_frame(static_cast<uint8_t>(locals.size() - block_locals.size())));
          written = true;
        }
      } else if (locals.size() == block_locals.size()) {
        bool same = true;
        for (auto j = 0; j < locals.size(); j++) {
          if (locals[j] != block_locals[j]) {
            same = false;
            break;
          }
        }
        if (same) {
          frames.emplace_back(get_label(&block), same_frame());
          written = true;
        }
      }
    } else if (block_stack.size() == 1 && locals == block_locals) {
      frames.emplace_back(get_label(&block), same_frame(block_stack[0]));
      written = true;
    }
    if (!written) {
      frames.emplace_back(get_label(&block), full_frame(block_locals, block_stack));
    }
    locals = block_locals;
    stack = block_stack;
  }
  uint16_t max_stack = 0;
  for (const auto& b : blocks_) {
    if (b.max_locals_ > max_locals) {
      max_locals = b.max_locals_;
    }
    if (b.max_stack_ > max_stack) {
      max_stack = b.max_stack_;
    }
  }
  return {std::move(inject_labels), std::move(frames), max_locals, max_stack};
}
std::pair<uint16_t, uint16_t> basic_block_graph::compute_maxes(uint16_t start_locals) {
  if (blocks_.empty()) {
    return {start_locals, 0};
  }
  for (auto& block : blocks_) {
    block.max_locals_ = 0;
    block.input_max_stack_ = -1;
    block.output_max_stack_ = 0;
    block.max_stack_ = 0;
  }
  std::deque<basic_block*> worklist;
  auto* head = &blocks_.front();
  head->max_locals_ = start_locals;
  head->input_max_stack_ = 0;
  worklist.emplace_back(head);

  while (!worklist.empty()) {
    auto* current = worklist.front();
    worklist.pop_front();
    current->compute_maxes();
    for (const auto& [succ, tcb_type] : current->successors_) {
      if (succ->input_max_stack_ < current->output_max_stack_) {
        succ->max_locals_ = current->max_locals_;
        succ->input_max_stack_ = current->output_max_stack_;
        if (std::find(worklist.begin(), worklist.end(), succ) == worklist.end()) {
          worklist.emplace_back(succ);
        }
      }
    }
  }
  uint16_t max_stack = 0;
  uint16_t max_locals = start_locals;
  for (const auto& b : blocks_) {
    if (b.max_stack_ > max_stack) {
      max_stack = b.max_stack_;
    }
    if (b.max_locals_ > max_locals) {
      max_locals = b.max_locals_;
    }
  }
  return {max_locals, max_stack};
}
uint16_t basic_block_graph::get_start_locals(const method& method) {
  const type type(method.desc);
  auto start_locals = (method.access_flags & access_flag::acc_static) != 0 ? 0 : 1;
  for (const auto& p : type.parameter_types()) {
    start_locals += p.size();
  }
  return start_locals;
}
std::vector<std::optional<frame_var>> basic_block_graph::get_start_locals(const std::string_view& class_name, const method& method) {
  std::vector<std::optional<frame_var>> locals;
  const type type(method.desc);

  if ((method.access_flags & access_flag::acc_static) == 0) {
    if (method.name == "<init>") {
      locals.emplace_back(uninitialized_this_var());
    } else {
      locals.emplace_back(object_var(class_name));
    }
  }

  for (const auto& p : type.parameter_types()) {
    switch (p.kind()) {
      case type_kind::boolean:
        case type_kind::byte:
      case type_kind::char_:
      case type_kind::int_:
      locals.emplace_back(int_var());
      break;
      case type_kind::long_:
        locals.emplace_back(long_var());
      locals.emplace_back(top_var());
      break;
      case type_kind::double_:
        locals.emplace_back(double_var());
      locals.emplace_back(top_var());
      break;
      case type_kind::float_:
        locals.emplace_back(float_var());
      break;
      case type_kind::array:
        locals.emplace_back(object_var(p.get()));
      break;
      case type_kind::object:
        locals.emplace_back(object_var(p.internal()));
      break;
      default: break;
    }
  }

  return locals;
}
void basic_block_graph::compute_maxes(method& method) {
  const auto [max_locals, max_stack] = compute_maxes(get_start_locals(method));
  method.code.max_locals = max_locals;
  method.code.max_stack = max_stack;
}
const std::list<basic_block>& basic_block_graph::blocks() const {
  return blocks_;
}
} // namespace cafe
