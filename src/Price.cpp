#include "Price.hpp"

#include <cctype>
#include <cstddef>
#include <format>
#include <limits>
#include <ostream>

// Wraps an already-scaled integer (e.g. 1234500 for "123.45") with no text
// parsing involved. Used when a caller already has the fixed-point value.
std::expected<Price, std::string> Price::fromScaled(std::int64_t scaledValue) {
  if (scaledValue < 0) [[unlikely]] {
    return std::unexpected("price cannot be negative");
  }
  return Price{scaledValue};
}

// Parses decimal text like "123.45" into a fixed-point integer scaled by
// kScale, e.g. "123.45" -> 1234500. Storing prices as scaled integers
// (instead of float/double) avoids binary floating-point rounding, so a
// price always compares and adds exactly.
std::expected<Price, std::string> Price::parse(std::string_view text) {
  // Largest `whole` for which `whole * kScale` still fits in int64_t,
  // leaving headroom for the fractional part added on top.
  constexpr std::int64_t kMaxWhole =
      std::numeric_limits<std::int64_t>::max() / kScale;
  constexpr std::int64_t kMaxFractionAtMaxWhole =
      std::numeric_limits<std::int64_t>::max() % kScale;

  if (text.empty()) [[unlikely]] {
    return std::unexpected("empty price");
  }

  std::size_t pos = 0;
  bool negative = false;
  if (text[pos] == '-') {
    negative = true;
    ++pos;
  }
  if (pos >= text.size()) [[unlikely]] {
    return std::unexpected(std::format("price has no digits: '{}'", text));
  }

  // Read the digits before the decimal point into `whole`, one at a time.
  std::int64_t whole = 0;
  std::size_t digitsBeforeDot = 0;
  while (pos < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[pos]))) {
    if (whole > kMaxWhole) [[unlikely]] {
      return std::unexpected(std::format("price is too large: '{}'", text));
    }
    whole = whole * 10 + (text[pos] - '0');
    ++pos;
    ++digitsBeforeDot;
  }

  // Read the digits after the decimal point (if any) into `fraction`. Extra
  // digits beyond kFractionalDigits are still consumed here so the "trailing
  // garbage" check below can tell "too many decimal places" apart from
  // "junk after the number" - but only the first kFractionalDigits count
  // toward the value; the precision check further down rejects the rest.
  std::int64_t fraction = 0;
  std::size_t fractionDigits = 0;
  if (pos < text.size() && text[pos] == '.') {
    ++pos;
    while (pos < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[pos]))) {
      if (fractionDigits < kFractionalDigits) {
        fraction = fraction * 10 + (text[pos] - '0');
      }
      ++fractionDigits;
      ++pos;
    }
  }

  if (digitsBeforeDot == 0 && fractionDigits == 0) [[unlikely]] {
    return std::unexpected(std::format("price has no digits: '{}'", text));
  }
  if (pos != text.size()) [[unlikely]] {
    return std::unexpected(
        std::format("trailing garbage in price: '{}'", text));
  }
  if (fractionDigits > kFractionalDigits) [[unlikely]] {
    return std::unexpected(
        std::format("price supports at most {} fractional digits: '{}'",
                    kFractionalDigits, text));
  }
  if (negative) [[unlikely]] {
    return std::unexpected(std::format("price cannot be negative: '{}'", text));
  }
  // The in-loop check above stops 'whole' from growing without bound, but its
  // very last digit can still land just past kMaxWhole; catch that before
  // 'whole * kScale' below would silently overflow.

  // Right-pad the fraction to exactly kFractionalDigits, e.g. "45" (2 digits
  // read) becomes 4500 (4 digits), so it lines up with kScale below.
  for (std::size_t i = fractionDigits; i < kFractionalDigits; ++i) {
    fraction *= 10;
  }

  if (whole > kMaxWhole ||
      (whole == kMaxWhole && fraction > kMaxFractionAtMaxWhole)) [[unlikely]] {
    return std::unexpected(std::format("price is too large: '{}'", text));
  }

  // Combine whole and fractional parts into one scaled integer, e.g.
  // whole=123, fraction=4500 -> 123 * 10000 + 4500 = 1234500.
  return Price{whole * kScale + fraction};
}

// Reverses parse(): splits the scaled integer back into whole and
// fractional parts and formats them as decimal text, dropping trailing
// zeros (so 1234500 prints as "123.45", not "123.4500").
std::string Price::toString() const {
  const std::int64_t whole = value_ / kScale;
  const std::int64_t fraction = value_ % kScale;

  if (fraction == 0) {
    return std::format("{}", whole);
  }

  std::string result =
      std::format("{}.{:0{}}", whole, fraction, kFractionalDigits);
  while (result.back() == '0') {
    result.pop_back();
  }

  return result;
}

std::ostream& operator<<(std::ostream& os, const Price& p) {
  return os << p.toString();
}
