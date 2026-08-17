#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
#include <variant>

#include "Message.hpp"

namespace {

struct ValidAddOrderCase {
  std::string_view input;
  OrderId orderId;
  Side side;
  Quantity quantity;
  std::int64_t scaledPrice;
};

struct InvalidInputCase {
  std::string_view input;
  std::string_view expectedError;
};

}  // namespace

TEST(MessageTest, ParsesAddOrders) {
  constexpr std::array<ValidAddOrderCase, 3> kCases{
      ValidAddOrderCase{"0,123,0,9,1000.25", 123, Side::Buy, 9, 10'002'500},
      ValidAddOrderCase{"0,456,1,12,999.5", 456, Side::Sell, 12, 9'995'000},
      ValidAddOrderCase{"0,18446744073709551615,0,18446744073709551615,1",
                        std::numeric_limits<OrderId>::max(), Side::Buy,
                        std::numeric_limits<Quantity>::max(), 10'000},
  };

  for (const auto& testCase : kCases) {
    SCOPED_TRACE(testCase.input);
    const auto parsed = parseInputLine(testCase.input);

    ASSERT_TRUE(parsed.has_value());
    const auto* request = std::get_if<AddOrderRequest>(&*parsed);
    ASSERT_NE(request, nullptr);

    EXPECT_EQ(request->orderId, testCase.orderId);
    EXPECT_EQ(request->side, testCase.side);
    EXPECT_EQ(request->quantity, testCase.quantity);
    EXPECT_EQ(request->price.scaledValue(), testCase.scaledPrice);
  }
}

TEST(MessageTest, ParsesCancelOrders) {
  struct ValidCancelOrderCase {
    std::string_view input;
    OrderId orderId;
  };
  constexpr std::array<ValidCancelOrderCase, 2> kCases{
      ValidCancelOrderCase{"1,123", 123},
      ValidCancelOrderCase{"1,18446744073709551615",
                           std::numeric_limits<OrderId>::max()},
  };

  for (const auto& testCase : kCases) {
    SCOPED_TRACE(testCase.input);
    const auto parsed = parseInputLine(testCase.input);

    ASSERT_TRUE(parsed.has_value());
    const auto* request = std::get_if<CancelOrderRequest>(&*parsed);
    ASSERT_NE(request, nullptr);
    EXPECT_EQ(request->orderId, testCase.orderId);
  }
}

TEST(MessageTest, RejectsUnknownMessageTypeWithClearError) {
  const auto parsed = parseInputLine("BADMESSAGE");

  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error(), "Unknown message type: BADMESSAGE");
}

TEST(MessageTest, RejectsMalformedInput) {
  constexpr std::array<InvalidInputCase, 25> kCases{
      InvalidInputCase{"", "empty input line"},
      InvalidInputCase{"0,1,0,5",
                       "AddOrderRequest expects exactly 5 fields, got 4"},
      InvalidInputCase{"0,1,0,5,100,extra",
                       "AddOrderRequest expects exactly 5 fields, got 6"},
      InvalidInputCase{"1",
                       "CancelOrderRequest expects exactly 2 fields, got 1"},
      InvalidInputCase{"1,1,extra",
                       "CancelOrderRequest expects exactly 2 fields, got 3"},
      InvalidInputCase{"0,0,0,5,100", "orderid must be positive"},
      InvalidInputCase{"1,0", "orderid must be positive"},
      InvalidInputCase{"0,,0,5,100", "orderid is empty"},
      InvalidInputCase{"0,abc,0,5,100", "orderid is not a valid integer"},
      InvalidInputCase{"0,12x,0,5,100", "orderid is not a valid integer"},
      InvalidInputCase{"0,-1,0,5,100", "orderid is not a valid integer"},
      InvalidInputCase{"1,18446744073709551616",
                       "orderid is not a valid integer"},
      InvalidInputCase{"0,1,2,5,100", "side must be 0 (Buy) or 1 (Sell)"},
      InvalidInputCase{"0,1,,5,100", "side must be 0 (Buy) or 1 (Sell)"},
      InvalidInputCase{"0,1,0,0,100", "quantity must be positive"},
      InvalidInputCase{"0,1,0,,100", "quantity is empty"},
      InvalidInputCase{"0,1,0,-1,100", "quantity is not a valid integer"},
      InvalidInputCase{"0,1,0,abc,100", "quantity is not a valid integer"},
      InvalidInputCase{"0,1,0,12x,100", "quantity is not a valid integer"},
      InvalidInputCase{"0,1,0,18446744073709551616,100",
                       "quantity is not a valid integer"},
      InvalidInputCase{"0,1,0,5,0", "price must be positive"},
      InvalidInputCase{"0,1,0,5,", "empty price"},
      InvalidInputCase{"0,1,0,5,-100", "price cannot be negative: '-100'"},
      InvalidInputCase{"0,1,0,5,not-a-price",
                       "price has no digits: 'not-a-price'"},
      InvalidInputCase{"0,1,0,5,100,",
                       "AddOrderRequest expects exactly 5 fields, got 6"},
  };

  for (const auto& testCase : kCases) {
    SCOPED_TRACE(testCase.input);
    const auto parsed = parseInputLine(testCase.input);

    if (parsed.has_value()) {
      ADD_FAILURE() << "malformed input was accepted";
      continue;
    }
    EXPECT_EQ(parsed.error(), testCase.expectedError);
  }
}

TEST(MessageTest, FormatsTradeEvent) {
  const auto price = Price::fromScaled(10'252'500);
  ASSERT_TRUE(price.has_value());

  const OutputMessage message = TradeEvent{
      .quantity = 2,
      .price = *price,
  };

  EXPECT_EQ(formatOutputMessage(message), "2,2,1025.25");
}

TEST(MessageTest, FormatsOrderFullyFilled) {
  const OutputMessage message = OrderFullyFilled{
      .orderId = 1000005,
  };

  EXPECT_EQ(formatOutputMessage(message), "3,1000005");
}

TEST(MessageTest, FormatsOrderPartiallyFilled) {
  const OutputMessage message = OrderPartiallyFilled{
      .orderId = 1000008,
      .remainingQuantity = 1,
  };

  EXPECT_EQ(formatOutputMessage(message), "4,1000008,1");
}
