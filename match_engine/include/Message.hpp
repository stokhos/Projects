#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <variant>

#include "Order.hpp"
#include "Price.hpp"

// Input (from stdin, one per line, comma-separated):
//   0,orderid,side,quantity,price   -> AddOrderRequest
//   1,orderid                       -> CancelOrderRequest
struct AddOrderRequest {
  OrderId orderId = 0;
  Side side = Side::Invalid;
  Quantity quantity = 0;
  Price price{};
};

struct CancelOrderRequest {
  OrderId orderId = 0;
};

// Output (to stdout):
//   2,quantity,price          -> TradeEvent
//   3,orderid                 -> OrderFullyFilled
//   4,orderid,quantity        -> OrderPartiallyFilled
struct TradeEvent {
  Quantity quantity = 0;
  Price price{};
};

struct OrderFullyFilled {
  OrderId orderId = 0;
};

struct OrderPartiallyFilled {
  OrderId orderId = 0;
  Quantity remainingQuantity = 0;
};

using InputMessage = std::variant<AddOrderRequest, CancelOrderRequest>;
using OutputMessage =
    std::variant<TradeEvent, OrderFullyFilled, OrderPartiallyFilled>;

[[nodiscard]] std::expected<InputMessage, std::string> parseInputLine(
    std::string_view line);

[[nodiscard]] std::string formatOutputMessage(const OutputMessage& message);
