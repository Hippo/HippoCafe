#include "cafe/class_tree.hpp"

#include "gen/gen_class_tree.hpp"

namespace cafe {
class_tree::node::node(const std::string_view& name) : name(name), super_class(nullptr) {
}

class_tree::node::node(const std::string_view& name, node* super_class, const std::vector<node*>& interfaces) :
    name(name), super_class(super_class), interfaces(interfaces) {
}
class_tree::class_tree(load_rt_t) {
  put_rt(*this);
}
class_tree::node* class_tree::get_or_create(const std::string& name) {
  if (const auto it = tree_.find(name); it != tree_.end()) {
    return it->second.get();
  }
  if (const auto [it, pass] = tree_.emplace(name, std::make_unique<node>(name)); pass) {
    return it->second.get();
  }
  return nullptr;
}
void class_tree::put(const std::string& name, const std::optional<std::string>& super_name,
                           const std::vector<std::string>& interfaces) {
  const auto node = get_or_create(name);
  if (super_name) {
    node->super_class = get_or_create(*super_name);
  } else {
    node->super_class = nullptr;
  }
  node->interfaces.clear();
  for (const auto& interface : interfaces) {
    node->interfaces.emplace_back(get_or_create(interface));
  }
}
void class_tree::put(const class_file& file) {
  put(file.name, file.super_name, file.interfaces);
}
class_tree::node* class_tree::get(const std::string& name) const {
  const auto it = tree_.find(name);
  return it != tree_.end() ? it->second.get() : nullptr;
}
bool class_tree::is_assignable_from(const std::string& from, const std::string& to) const {
  if (from == to) {
    return true;
  }
  bool to_array = to[0] == '[';
  if (to_array && (from == "java/lang/Object" || from == "java/lang/Cloneable" || from == "java/io/Serializable")) {
    return true;
  }
  bool from_array = from[0] == '[';
  if (from_array && to_array) {
    auto from_sub = from.substr(1);
    auto to_sub = to.substr(1);
    if (is_primitive(from_sub) || is_primitive(to_sub)) {
      return from_sub == to_sub;
    }
    if (from_sub[0] == 'L' && from_sub[from_sub.size() - 1] == ';') {
      from_sub = from_sub.substr(1, from_sub.size() - 2);
    }
    if (to_sub[0] == 'L' && to_sub[to_sub.size() - 1] == ';') {
      to_sub = to_sub.substr(1, to_sub.size() - 2);
    }
    return is_assignable_from(from_sub, to_sub);
  }
  if (!from_array && to_array) {
    return false;
  }
  if (from_array && !to_array) {
    return false;
  }
  const auto to_node = get(to);
  if (to_node == nullptr) {
    return false;
  }
  auto curr = to_node->super_class;
  while (curr != nullptr) {
    if (from == curr->name) {
      return true;
    }
    curr = curr->super_class;
  }

  curr = to_node;
  while (curr != nullptr) {
    for (const auto& interface : curr->interfaces) {
      if (from == interface->name) {
        return true;
      }
      if (is_assignable_from(from, interface->name)) {
        return true;
      }
    }
    curr = curr->super_class;
  }
  return false;
}

bool class_tree::is_primitive(const std::string_view& name) const {
  if (name.empty()) {
    return false;
  }
  switch (name[0]) {
    case 'V':
    case 'Z':
    case 'B':
    case 'C':
    case 'S':
    case 'I':
    case 'J':
    case 'F':
    case 'D':
      return true;
    default:
      return false;
  }
}

size_t class_tree::size() const {
  return tree_.size();
}
class_tree::map_type::const_iterator
class_tree::begin() const {
  return tree_.begin();
}
class_tree::map_type::const_iterator class_tree::end() const {
  return tree_.end();
}
class_tree::map_type::iterator class_tree::begin() {
  return tree_.begin();
}
class_tree::map_type::iterator class_tree::end() {
  return tree_.end();
}
bool class_tree::empty() const {
  return tree_.empty();
}
}