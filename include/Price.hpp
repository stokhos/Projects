#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iosfwd>
#include <string>
#include <string_view>

class Price {
 private:
  static constexpr std::size_t kFractionalDigits = 4;
  static constexpr std::int64_t kScale = 10'000;
  std::int64_t value_ = 0;
  explicit constexpr Price(std::int64_t scaledValue) : value_(scaledValue) {}

 public:
  constexpr Price() = default;

  [[nodiscard]] static std::expected<Price, std::string> fromScaled(
      std::int64_t scaledValue);

  [[nodiscard]] static std::expected<Price, std::string> parse(
      std::string_view text);

  [[nodiscard]] std::string toString() const;

  [[nodiscard]] constexpr std::int64_t scaledValue() const noexcept {
    return value_;
  }

  [[nodiscard]] friend constexpr bool operator==(Price, Price) = default;
  [[nodiscard]] friend constexpr std::strong_ordering operator<=>(Price,
                                                                   Price) =
      default;
};

std::ostream& operator<<(std::ostream& os, const Price& p);
