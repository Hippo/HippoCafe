#pragma once

#include <deque>
#include <unordered_map>
#include <unordered_set>

#include "class_file.hpp"
#include "class_tree.hpp"

namespace cafe {

class CAFE_API basic_block {
public:
  std::string debug_name;
  explicit basic_block(const code& code);
  ~basic_block() = default;
  basic_block(const basic_block&) = delete;
  basic_block(basic_block&&) noexcept = default;
  basic_block& operator=(const basic_block&) = delete;
  basic_block& operator=(basic_block&&) noexcept = delete;

  void compute_maxes();
  void compute_frames(const std::string_view& class_name);
  bool merge(const class_tree& tree, const basic_block& other, const std::vector<std::string>& tcb_types);

  std::string to_string() const;
  std::string to_string(const std::string_view& delim) const;

  std::vector<frame_var> locals() const;
  std::vector<frame_var> stack() const;

  friend class basic_block_graph;
private:
  const code& code_;
  std::vector<code::const_iterator> instructions_;
  std::unordered_map<basic_block*, std::vector<std::string>> successors_;
  std::unordered_set<label_id> handlers_;
  bool needs_frame_ = false;

  int32_t max_locals_{};
  int32_t input_max_stack_ = -1;
  int32_t output_max_stack_{};
  int32_t max_stack_{};
  std::vector<std::optional<frame_var>> input_locals_;
  std::vector<std::optional<frame_var>> output_locals_;
  std::deque<std::optional<frame_var>> input_stack_;
  std::deque<std::optional<frame_var>> output_stack_;
};

class CAFE_API frame_compute_result {
public:
  frame_compute_result(uint16_t max_locals, uint16_t max_stack);
  frame_compute_result(std::vector<std::pair<code::const_iterator, label>>&& labels, std::vector<std::pair<label, frame>>&& frames, uint16_t max_locals, uint16_t max_stack);
  ~frame_compute_result() = default;
  frame_compute_result(const frame_compute_result&) = default;
  frame_compute_result(frame_compute_result&&) noexcept = default;
  frame_compute_result& operator=(const frame_compute_result&) = default;
  frame_compute_result& operator=(frame_compute_result&&) noexcept = default;

  const std::vector<std::pair<code::const_iterator, label>>& labels() const;
  const std::vector<std::pair<label, frame>>& frames() const;
  uint16_t max_locals() const;
  uint16_t max_stack() const;
private:
  std::vector<std::pair<code::const_iterator, label>> inject_labels_;
  std::vector<std::pair<label, frame>> frames_;
  uint16_t max_locals_;
  uint16_t max_stack_;
};

class CAFE_API basic_block_graph {
public:
  explicit basic_block_graph(const code& code);
  ~basic_block_graph() = default;
  basic_block_graph(const basic_block_graph&) = delete;
  basic_block_graph(basic_block_graph&&) noexcept = default;
  basic_block_graph& operator=(const basic_block_graph&) = delete;
  basic_block_graph& operator=(basic_block_graph&&) noexcept = default;

  frame_compute_result compute_frames(const class_tree& tree, const std::string_view& class_name, const std::vector<std::optional<frame_var>>& start_locals);

  void compute_maxes(method& method);
  std::pair<uint16_t, uint16_t> compute_maxes(uint16_t start_locals);

  static uint16_t get_start_locals(const method& method);
  static std::vector<std::optional<frame_var>> get_start_locals(const std::string_view& class_name, const method& method);

  const std::list<basic_block>& blocks() const;
private:
  std::list<basic_block> blocks_;
};

}