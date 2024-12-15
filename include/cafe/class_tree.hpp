#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "apidef.hpp"
#include "class_file.hpp"

namespace cafe {

struct CAFE_API load_rt_t {};
inline constexpr load_rt_t load_rt{};

class CAFE_API class_tree {
public:
  class node {
  public:
    std::string name;
    node* super_class;
    std::vector<node*> interfaces;
    explicit node(const std::string_view& name);
    node(const std::string_view& name, node* super_class, const std::vector<node*>& interfaces);
    ~node() = default;
    node(const node&) = default;
    node(node&&) noexcept = default;
    node& operator=(const node&) = default;
    node& operator=(node&&) noexcept = default;
  };
  using map_type = std::unordered_map<std::string, std::unique_ptr<node>>;
  
  class_tree() = default;
  class_tree(load_rt_t);
  ~class_tree() = default;
  class_tree(const class_tree&) = delete;
  class_tree(class_tree&&) noexcept = default;
  class_tree& operator=(const class_tree&) = delete;
  class_tree& operator=(class_tree&&) noexcept = default;

  node* get_or_create(const std::string& name);
  void put(const std::string& name, const std::optional<std::string>& super_name, const std::vector<std::string>& interfaces);
  void put(const class_file& file);
  template<typename Iterator>
  void put_all(const Iterator& it) {
    for (const auto& file : it) {
      put(file);
    }
  }
  node* get(const std::string& name) const;
  bool is_assignable_from(const std::string& from, const std::string& to) const;
  size_t size() const;
  map_type::const_iterator begin() const;
  map_type::const_iterator end() const;
  map_type::iterator begin();
  map_type::iterator end();
  bool empty() const;

private:
  map_type tree_;

  bool is_primitive(const std::string_view& name) const;
};

}