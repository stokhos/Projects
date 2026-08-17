#include <gtest/gtest.h>

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "MatchingEngine.hpp"

namespace {

constexpr PriceGridConfig kTestGrid{
    .minimumScaledPrice = 900'000,
    .maximumScaledPrice = 1'100'000,
    .tickSize = 100,
};

[[nodiscard]] Price price(std::string_view text) {
  const auto parsed = Price::parse(text);
  if (!parsed) {
    throw std::invalid_argument(parsed.error());
  }
  return *parsed;
}

class EngineHarness {
 public:
  explicit EngineHarness(PriceGridConfig config = kTestGrid)
      : engine_(
            [this](const OutputMessage& output) {
              outputs.push_back(formatOutputMessage(output));
            },
            [this](std::string_view error) { errors.emplace_back(error); },
            config) {}

  void add(OrderId orderId, Side side, Quantity quantity, Price orderPrice) {
    engine_.process(InputMessage{AddOrderRequest{
        .orderId = orderId,
        .side = side,
        .quantity = quantity,
        .price = orderPrice,
    }});
  }

  void add(const AddOrderRequest& request) {
    engine_.process(InputMessage{request});
  }

  void cancel(OrderId orderId) {
    engine_.process(InputMessage{CancelOrderRequest{.orderId = orderId}});
  }

  std::vector<std::string> outputs;
  std::vector<std::string> errors;

 private:
  MatchingEngine engine_;
};

}  // namespace

TEST(MatchingEngineTest, ExactMatchesAreSymmetricAndUseTheRestingPrice) {
  struct ExactMatchCase {
    Side restingSide;
    std::string_view restingPrice;
    Side incomingSide;
    std::string_view incomingPrice;
  };

  constexpr std::array<ExactMatchCase, 2> kCases{
      ExactMatchCase{Side::Sell, "100", Side::Buy, "105"},
      ExactMatchCase{Side::Buy, "105", Side::Sell, "100"},
  };

  for (const auto& testCase : kCases) {
    SCOPED_TRACE(testCase.restingPrice);
    EngineHarness harness;

    harness.add(10, testCase.restingSide, 5, price(testCase.restingPrice));
    EXPECT_TRUE(harness.outputs.empty());

    harness.add(20, testCase.incomingSide, 5, price(testCase.incomingPrice));

    const std::vector<std::string> expected{
        std::string{"2,5,"} + std::string{testCase.restingPrice},
        "3,20",
        "3,10",
    };
    EXPECT_EQ(harness.outputs, expected);
    EXPECT_TRUE(harness.errors.empty());
  }
}

TEST(MatchingEngineTest, MatchObserverReceivesRichExecutionContext) {
  std::vector<MatchExecution> executions;
  std::vector<std::string> errors;
  MatchingEngine engine(
      [](const OutputMessage&) {},
      [&errors](std::string_view error) { errors.emplace_back(error); },
      kTestGrid,
      [&executions](const MatchExecution& execution) {
        executions.push_back(execution);
      });

  engine.process(InputMessage{AddOrderRequest{
      .orderId = 10,
      .side = Side::Sell,
      .quantity = 2,
      .price = price("100"),
  }});
  engine.process(InputMessage{AddOrderRequest{
      .orderId = 11,
      .side = Side::Sell,
      .quantity = 5,
      .price = price("101"),
  }});
  engine.process(InputMessage{AddOrderRequest{
      .orderId = 20,
      .side = Side::Buy,
      .quantity = 3,
      .price = price("105"),
  }});

  ASSERT_EQ(executions.size(), 2U);

  const MatchExecution& first = executions[0];
  EXPECT_EQ(first.aggressorOrderId, 20U);
  EXPECT_EQ(first.aggressorSide, Side::Buy);
  EXPECT_EQ(first.aggressorLimitPrice, price("105"));
  EXPECT_EQ(first.restingOrderId, 10U);
  EXPECT_EQ(first.restingSide, Side::Sell);
  EXPECT_EQ(first.restingLimitPrice, price("100"));
  EXPECT_EQ(first.executionPrice, price("100"));
  EXPECT_EQ(first.executedQuantity, 2U);
  EXPECT_EQ(first.aggressorRemaining, 1U);
  EXPECT_EQ(first.restingRemaining, 0U);

  const MatchExecution& second = executions[1];
  EXPECT_EQ(second.aggressorOrderId, 20U);
  EXPECT_EQ(second.aggressorSide, Side::Buy);
  EXPECT_EQ(second.aggressorLimitPrice, price("105"));
  EXPECT_EQ(second.restingOrderId, 11U);
  EXPECT_EQ(second.restingSide, Side::Sell);
  EXPECT_EQ(second.restingLimitPrice, price("101"));
  EXPECT_EQ(second.executionPrice, price("101"));
  EXPECT_EQ(second.executedQuantity, 1U);
  EXPECT_EQ(second.aggressorRemaining, 0U);
  EXPECT_EQ(second.restingRemaining, 4U);
  EXPECT_TRUE(errors.empty());
}

TEST(MatchingEngineTest, IncomingRemainderRestsAndCanMatchLater) {
  EngineHarness harness;

  harness.add(10, Side::Sell, 5, price("100"));
  harness.add(20, Side::Buy, 8, price("105"));

  const std::vector<std::string> firstMatch{
      "2,5,100",
      "4,20,3",
      "3,10",
  };
  EXPECT_EQ(harness.outputs, firstMatch);

  harness.outputs.clear();
  harness.add(30, Side::Sell, 3, price("105"));

  const std::vector<std::string> secondMatch{
      "2,3,105",
      "3,30",
      "3,20",
  };
  EXPECT_EQ(harness.outputs, secondMatch);
  EXPECT_TRUE(harness.errors.empty());
}

TEST(MatchingEngineTest, RestingRemainderStaysAvailableAfterPartialFill) {
  EngineHarness harness;

  harness.add(10, Side::Sell, 8, price("100"));
  harness.add(20, Side::Buy, 5, price("105"));

  const std::vector<std::string> firstMatch{
      "2,5,100",
      "3,20",
      "4,10,3",
  };
  EXPECT_EQ(harness.outputs, firstMatch);

  harness.outputs.clear();
  harness.add(21, Side::Buy, 3, price("100"));

  const std::vector<std::string> secondMatch{
      "2,3,100",
      "3,21",
      "3,10",
  };
  EXPECT_EQ(harness.outputs, secondMatch);
  EXPECT_TRUE(harness.errors.empty());
}

TEST(MatchingEngineTest, SweepsBestPricesAndPreservesFifoWithinAPrice) {
  EngineHarness harness;

  harness.add(10, Side::Sell, 1, price("101"));
  harness.add(11, Side::Sell, 1, price("100"));
  harness.add(12, Side::Sell, 1, price("100"));
  harness.add(20, Side::Buy, 3, price("102"));

  const std::vector<std::string> expected{
      "2,1,100", "4,20,2",  "3,11", "2,1,100", "4,20,1",
      "3,12",    "2,1,101", "3,20", "3,10",
  };
  EXPECT_EQ(harness.outputs, expected);
  EXPECT_TRUE(harness.errors.empty());
}

TEST(MatchingEngineTest, NonCrossingOrdersRestUntilTheirPricesCross) {
  EngineHarness harness;

  harness.add(10, Side::Sell, 1, price("101"));
  harness.add(20, Side::Buy, 1, price("100"));
  EXPECT_TRUE(harness.outputs.empty());

  harness.add(21, Side::Buy, 1, price("101"));
  harness.add(22, Side::Sell, 1, price("100"));

  const std::vector<std::string> expected{
      "2,1,101", "3,21", "3,10", "2,1,100", "3,22", "3,20",
  };
  EXPECT_EQ(harness.outputs, expected);
  EXPECT_TRUE(harness.errors.empty());
}

TEST(MatchingEngineTest, CancellationRemovesOrdersAndReportsUnknownIds) {
  EngineHarness harness;

  harness.add(10, Side::Buy, 2, price("100"));
  harness.cancel(10);
  EXPECT_TRUE(harness.outputs.empty());
  EXPECT_TRUE(harness.errors.empty());

  harness.cancel(10);
  ASSERT_EQ(harness.errors.size(), 1U);
  EXPECT_EQ(harness.errors.front(), "unknown order id");

  harness.add(20, Side::Sell, 2, price("90"));
  EXPECT_TRUE(harness.outputs.empty());
  harness.add(30, Side::Buy, 2, price("90"));

  const std::vector<std::string> expected{
      "2,2,90",
      "3,30",
      "3,20",
  };
  EXPECT_EQ(harness.outputs, expected);
}

TEST(MatchingEngineTest, RejectedAggressorsCannotTradeOrMutateTheBook) {
  struct RejectedCase {
    AddOrderRequest request;
    std::string_view expectedError;
  };

  const std::array<RejectedCase, 2> kCases{
      RejectedCase{AddOrderRequest{.orderId = 10,
                                   .side = Side::Buy,
                                   .quantity = 5,
                                   .price = price("105")},
                   "duplicate order id"},
      RejectedCase{AddOrderRequest{.orderId = 20,
                                   .side = Side::Buy,
                                   .quantity = 5,
                                   .price = price("105.0001")},
                   "price is not aligned with the grid tick size"},
  };

  for (const auto& testCase : kCases) {
    SCOPED_TRACE(testCase.expectedError);
    EngineHarness harness;
    harness.add(10, Side::Sell, 5, price("100"));

    harness.add(testCase.request);

    EXPECT_TRUE(harness.outputs.empty());
    ASSERT_EQ(harness.errors.size(), 1U);
    EXPECT_EQ(harness.errors.front(), testCase.expectedError);

    harness.add(30, Side::Buy, 5, price("105"));
    const std::vector<std::string> expected{
        "2,5,100",
        "3,30",
        "3,10",
    };
    EXPECT_EQ(harness.outputs, expected);
  }
}
