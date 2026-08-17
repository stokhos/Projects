#include <gtest/gtest.h>

#include <array>

#include "Order.hpp"

TEST(OrderTest, DefaultConstructionCannotDescribeAValidOrder) {
  const Order order;

  EXPECT_EQ(order.id, OrderId{0});
  EXPECT_EQ(order.side, Side::Invalid);
  EXPECT_EQ(order.quantity, Quantity{0});
  EXPECT_EQ(order.price.scaledValue(), 0);
}

TEST(OrderTest, OppositeMapsValidSidesAndPreservesInvalidity) {
  struct SideCase {
    Side input;
    Side expected;
  };
  constexpr std::array cases{
      SideCase{Side::Buy, Side::Sell},
      SideCase{Side::Sell, Side::Buy},
      SideCase{Side::Invalid, Side::Invalid},
      SideCase{static_cast<Side>(99), Side::Invalid},
  };

  for (const auto& testCase : cases) {
    SCOPED_TRACE(static_cast<unsigned>(testCase.input));
    EXPECT_EQ(opposite(testCase.input), testCase.expected);
  }
}
