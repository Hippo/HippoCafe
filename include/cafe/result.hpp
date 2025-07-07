#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace cafe {

class error {
public:
  explicit error(const std::string& message);
  explicit error(std::string&& message);
  ~error() = default;
  error(const error& other) = default;
  error(error&& other) noexcept = default;
  error& operator=(const error& other) = default;
  error& operator=(error&& other) noexcept = default;

  const std::string& message() const;

private:
  std::string message_;
};


template<typename T>
class result {
public:
  using value_type = T;

  result(const T& value) : value_(value) {
  }
  result(T&& value) : value_(std::move(value)) {
  }
  result(const error& value) : value_(value) {
  }
  result(error&& value) : value_(std::move(value)) {
  }
  ~result() = default;
  result(const result& other) = default;
  result(result&& other) noexcept = default;
  result& operator=(const result& other) = default;
  result& operator=(result&& other) noexcept = default;

  bool is_error() const {
    return std::holds_alternative<cafe::error>(value_);
  }
  bool is_ok() const {
    return !is_error();
  }
  explicit operator bool() const {
    return is_ok();
  }

  const T& value() const {
    return std::get<T>(value_);
  }
  T& value() {
    return std::get<T>(value_);
  }
  const error& err() const {
    return std::get<cafe::error>(value_);
  }

  const std::string& error_message() const {
    return err().message();
  }

  template<typename F, typename... Ts>
  auto lift(F&& f, const Ts&... ts) const
      -> result<decltype(f(std::declval<T>(), std::declval<typename Ts::value_type>()...))> {
    using U = decltype(f(std::declval<T>(), std::declval<typename Ts::value_type>()...));
    if (is_error()) {
      return err();
    }
    const cafe::error* e = nullptr;
    auto check_error = [&](const auto& r) {
      if (!e && r.is_error()) {
        e = &r.err();
      }
    };
    (check_error(ts), ...);
    if (e) {
      return *e;
    }
    if constexpr (std::is_void_v<U>) {
      f(value(), ts.value()...);
      return result<U>();
    } else {
      return f(value(), ts.value()...);
    }
  }

  template<typename F, typename... Ts>
  auto zip(F&& f, const Ts&... ts) const -> decltype(f(std::declval<T>(), std::declval<typename Ts::value_type>()...)) {
    if (is_error()) {
      return err();
    }
    const cafe::error* e = nullptr;
    auto check_error = [&](const auto& r) {
      if (!e && r.is_error()) {
        e = &r.err();
      }
    };
    (check_error(ts), ...);
    if (e) {
      return *e;
    }
    return f(value(), ts.value()...);
  }

  template<typename F>
  auto and_then(F&& f) const -> decltype(f(std::declval<T>())) {
    if (is_error()) {
      return err();
    }
    return f(value());
  }

  template<typename F>
  auto map(F&& f) const -> result<decltype(f(std::declval<T>()))> {
    using U = decltype(f(std::declval<T>()));
    if (is_error()) {
      return err();
    }
    if constexpr (std::is_void_v<U>) {
      f(value());
      return result<U>();
    } else {
      return f(value());
    }
  }

  const T& operator*() const {
    return value();
  }
  T& operator*() {
    return value();
  }
  const T* operator->() const {
    return &value();
  }
  T* operator->() {
    return &value();
  }

private:
  std::variant<T, cafe::error> value_;
};

template<>
class result<void> {
public:
  using value_type = void;
  result() = default;
  result(const error& error) : error_(error) {
  }
  result(error&& error) : error_(std::move(error)) {
  }
  ~result() = default;
  result(const result& other) = default;
  result(result&& other) noexcept = default;
  result& operator=(const result& other) = default;
  result& operator=(result&& other) noexcept = default;

  bool is_error() const {
    return error_.has_value();
  }
  bool is_ok() const {
    return !is_error();
  }
  explicit operator bool() const {
    return is_ok();
  }
  const error& err() const {
    return error_.value();
  }

private:
  std::optional<cafe::error> error_;
};

} // namespace cafe
