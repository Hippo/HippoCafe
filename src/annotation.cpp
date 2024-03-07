#include <utility>

#include "cafe/annotation.hpp"

namespace cafe {
annotation::annotation(const std::string_view& descriptor) : descriptor(descriptor) {
}
target::type_parameter::type_parameter(uint8_t index) : index(index) {
}
target::supertype::supertype(const std::string_view& name) : name(name) {
}
target::type_parameter_bound::type_parameter_bound(uint8_t type_parameter_index, uint8_t bound_index) :
    type_parameter_index(type_parameter_index), bound_index(bound_index) {
}
target::formal_parameter::formal_parameter(uint8_t index) : index(index) {
}
target::throws::throws(const std::string_view& name) : name(name) {
}
target::local::local(label start, label end, uint16_t index) :
    start(std::move(start)), end(std::move(end)), index(index) {
}
target::localvar::localvar(const std::vector<local>& table) : table(table) {
}
target::catch_target::catch_target(label handler_label) : handler_label(std::move(handler_label)) {
}
target::offset_target::offset_target(label offset) : offset(std::move(offset)) {
}
target::type_argument::type_argument(label offset, uint8_t index) : offset(std::move(offset)), index(index) {
}
type_path::type_path(const std::vector<std::pair<uint8_t, uint8_t>>& path) : path(path) {
}
type_annotation::type_annotation() : target_info(target::empty()) {
}
type_annotation::type_annotation(uint8_t target_type, type_annotation_target target_info, type_path target_path,
                                 const std::string_view& descriptor) :
    target_type(target_type), target_info(std::move(target_info)), target_path(std::move(target_path)),
    descriptor(descriptor) {
}
} // namespace cafe
