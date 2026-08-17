#pragma once

#include <cstdint>

#include "Price.hpp"

using OrderId = std::uint64_t;
using Quantity = std::uint64_t;

enum class Side : std::uint8_t {
  Buy = 0,
  Sell = 1,
  Invalid = 2,
};

struct Order {
  OrderId id = 0;
  Side side = Side::Invalid;
  Quantity quantity = 0;
  Price price{};
};

[[nodiscard]] constexpr Side opposite(Side side) noexcept {
  switch (side) {
    case Side::Buy:
      return Side::Sell;
    case Side::Sell:
      return Side::Buy;
    case Side::Invalid:
      return Side::Invalid;
  }
  return Side::Invalid;
}
