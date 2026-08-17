#include "Message.hpp"

#include <charconv>
#include <cstddef>
#include <format>
#include <system_error>
#include <type_traits>
#include <vector>

namespace {

constexpr std::string_view kAddOrderRequestType = "0";
constexpr std::string_view kCancelOrderRequestType = "1";
constexpr std::string_view kTradeEventType = "2";
constexpr std::string_view kOrderFullyFilledType = "3";
constexpr std::string_view kOrderPartiallyFilledType = "4";
constexpr std::string_view kBuySide = "0";
constexpr std::string_view kSellSide = "1";

constexpr std::size_t kAddOrderRequestFieldCount = 5;
constexpr std::size_t kCancelOrderRequestFieldCount = 2;

template <typename T>
constexpr bool kAlwaysFalse = false;

// Splits a line on commas into views into the original string - no
// allocation of the individual fields, just their boundaries.
[[nodiscard]] std::vector<std::string_view> splitCsv(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;

  while (true) {
    const std::size_t comma = line.find(',', start);

    if (comma == std::string_view::npos) {
      fields.push_back(line.substr(start));
      break;
    }

    fields.push_back(line.substr(start, comma - start));
    start = comma + 1;
  }
  return fields;
}

// Parses orderid/quantity fields: strictly positive integers only. Using an
// unsigned T means from_chars itself rejects a leading '-', so negative
// input fails as "not a valid integer" without extra sign handling here.
template <typename T>
[[nodiscard]] std::expected<T, std::string> parsePositiveInteger(
    std::string_view text, std::string_view fieldName) {
  static_assert(std::is_unsigned_v<T>,
                "parsePositiveInteger requires an unsigned integer type");

  if (text.empty()) [[unlikely]] {
    return std::unexpected(std::format("{} is empty", fieldName));
  }
  T value{};
  const char* begin = text.data();
  const char* end = begin + text.size();

  auto [ptr, error] = std::from_chars(begin, end, value);

  if (error != std::errc{} || ptr != end) [[unlikely]] {
    return std::unexpected(std::format("{} is not a valid integer", fieldName));
  }

  if (value == 0) [[unlikely]] {
    return std::unexpected(std::format("{} must be positive", fieldName));
  }

  return value;
}

[[nodiscard]] std::expected<Side, std::string> parseSide(
    std::string_view text) {
  if (text == kBuySide) {
    return Side::Buy;
  }
  if (text == kSellSide) {
    return Side::Sell;
  }
  return std::unexpected("side must be 0 (Buy) or 1 (Sell)");
}

[[nodiscard]] std::expected<InputMessage, std::string> parseAddOrder(
    const std::vector<std::string_view>& fields) {
  if (fields.size() != kAddOrderRequestFieldCount) [[unlikely]] {
    return std::unexpected(
        std::format("AddOrderRequest expects exactly {} fields, got {}",
                    kAddOrderRequestFieldCount, fields.size()));
  }
  const auto orderId = parsePositiveInteger<OrderId>(fields[1], "orderid");
  if (!orderId) [[unlikely]] {
    return std::unexpected(orderId.error());
  }
  const auto side = parseSide(fields[2]);
  if (!side) [[unlikely]] {
    return std::unexpected(side.error());
  }

  const auto quantity = parsePositiveInteger<Quantity>(fields[3], "quantity");
  if (!quantity) [[unlikely]] {
    return std::unexpected(quantity.error());
  }

  const auto price = Price::parse(fields[4]);
  if (!price) [[unlikely]] {
    return std::unexpected(price.error());
  }

  if (*price == Price{}) [[unlikely]] {
    return std::unexpected("price must be positive");
  }
  return InputMessage{AddOrderRequest{.orderId = *orderId,
                                      .side = *side,
                                      .quantity = *quantity,
                                      .price = *price}};
}

[[nodiscard]] std::expected<InputMessage, std::string> parseCancelOrder(
    const std::vector<std::string_view>& fields) {
  if (fields.size() != kCancelOrderRequestFieldCount) [[unlikely]] {
    return std::unexpected(
        std::format("CancelOrderRequest expects exactly {} fields, got {}",
                    kCancelOrderRequestFieldCount, fields.size()));
  }

  const auto orderId = parsePositiveInteger<OrderId>(fields[1], "orderid");

  if (!orderId) [[unlikely]] {
    return std::unexpected(orderId.error());
  }
  return InputMessage{CancelOrderRequest{.orderId = *orderId}};
}

}  // namespace

std::expected<InputMessage, std::string> parseInputLine(std::string_view line) {
  const auto fields = splitCsv(line);

  if (fields.empty() || fields[0].empty()) [[unlikely]] {
    return std::unexpected("empty input line");
  }

  if (fields[0] == kAddOrderRequestType) {
    return parseAddOrder(fields);
  }

  if (fields[0] == kCancelOrderRequestType) {
    return parseCancelOrder(fields);
  }

  return std::unexpected(std::format("Unknown message type: {}", fields[0]));
}

std::string formatOutputMessage(const OutputMessage& message) {
  return std::visit(
      []<typename T>(const T& output) -> std::string {
        if constexpr (std::is_same_v<T, TradeEvent>) {
          return std::format("{},{},{}", kTradeEventType, output.quantity,
                             output.price.toString());
        } else if constexpr (std::is_same_v<T, OrderFullyFilled>) {
          return std::format("{},{}", kOrderFullyFilledType, output.orderId);
        } else if constexpr (std::is_same_v<T, OrderPartiallyFilled>) {
          return std::format("{},{},{}", kOrderPartiallyFilledType,
                             output.orderId, output.remainingQuantity);
        } else {
          static_assert(kAlwaysFalse<T>, "Unhandled OutputMessage type");
        }
      },
      message);
}
