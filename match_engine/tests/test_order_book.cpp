#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <map>
#include <random>
#include <string_view>
#include <type_traits>
#include <utility>

#include "OrderBook.hpp"

namespace {

constexpr PriceGridConfig kOrderBookGrid{
    .minimumScaledPrice = 100,
    .maximumScaledPrice = 500,
    .tickSize = 100,
};

class OrderBookTest : public ::testing::Test {
 protected:
  [[nodiscard]] static Price price(std::int64_t scaledValue) {
    return Price::fromScaled(scaledValue).value();
  }

  [[nodiscard]] static Order order(OrderId id, Side side,
                                   std::int64_t scaledPrice,
                                   Quantity quantity = 1) {
    return Order{
        .id = id,
        .side = side,
        .quantity = quantity,
        .price = price(scaledPrice),
    };
  }

  void add(OrderId id, Side side, std::int64_t scaledPrice,
           Quantity quantity = 1) {
    book_.addOrder(order(id, side, scaledPrice, quantity));
  }

  void expectBest(Side side, std::int64_t scaledPrice, OrderId frontId) const {
    EXPECT_FALSE(book_.empty(side));
    EXPECT_EQ(book_.bestPrice(side), price(scaledPrice));
    EXPECT_EQ(book_.frontOrder(side).id, frontId);
  }

  OrderBook book_{kOrderBookGrid};
};

static_assert(std::is_same_v<
              decltype(std::declval<const OrderBook&>().frontOrder(Side::Buy)),
              const Order&>);
static_assert(!std::is_copy_constructible_v<OrderBook>);
static_assert(!std::is_copy_assignable_v<OrderBook>);
static_assert(!std::is_move_constructible_v<OrderBook>);
static_assert(!std::is_move_assignable_v<OrderBook>);

TEST_F(OrderBookTest, ReducesOnlyTheFrontQuantityAndRejectsInvalidAmounts) {
  add(1, Side::Buy, 300, 10);

  EXPECT_THROW((void)book_.reduceFrontQuantity(Side::Buy, 0),
               std::invalid_argument);
  EXPECT_THROW((void)book_.reduceFrontQuantity(Side::Buy, 11),
               std::invalid_argument);
  EXPECT_EQ(book_.frontOrder(Side::Buy).quantity, 10);

  EXPECT_EQ(book_.reduceFrontQuantity(Side::Buy, 4), 6);
  const auto& reduced = book_.frontOrder(Side::Buy);
  EXPECT_EQ(reduced.id, 1);
  EXPECT_EQ(reduced.side, Side::Buy);
  EXPECT_EQ(reduced.price, price(300));
  EXPECT_EQ(reduced.quantity, 6);

  EXPECT_EQ(book_.reduceFrontQuantity(Side::Buy, 6), 0);
  EXPECT_THROW((void)book_.reduceFrontQuantity(Side::Buy, 1),
               std::invalid_argument);
  EXPECT_TRUE(book_.hasOrder(1));

  EXPECT_TRUE(book_.cancelOrder(1));
  EXPECT_TRUE(book_.empty(Side::Buy));
}

TEST_F(OrderBookTest, EmptySideOperationsReportTheirPreconditionFailures) {
  EXPECT_TRUE(book_.empty(Side::Buy));
  EXPECT_TRUE(book_.empty(Side::Sell));
  EXPECT_THROW((void)book_.bestPrice(Side::Buy), std::logic_error);
  EXPECT_THROW((void)book_.frontOrder(Side::Sell), std::logic_error);
  EXPECT_THROW((void)book_.reduceFrontQuantity(Side::Buy, 1), std::logic_error);
  EXPECT_THROW(book_.removeFilledFront(Side::Sell, price(300)),
               std::logic_error);
}

TEST_F(OrderBookTest, InvalidSidesAreRejectedWithoutChangingLiveOrders) {
  add(1, Side::Buy, 300);
  add(2, Side::Sell, 400);

  constexpr std::array invalidSides{
      Side::Invalid,
      static_cast<Side>(99),
  };

  for (const Side side : invalidSides) {
    SCOPED_TRACE(static_cast<unsigned>(side));
    EXPECT_THROW((void)book_.empty(side), std::invalid_argument);
    EXPECT_THROW((void)book_.bestPrice(side), std::invalid_argument);
    EXPECT_THROW((void)book_.frontOrder(side), std::invalid_argument);
    EXPECT_THROW((void)book_.reduceFrontQuantity(side, 1),
                 std::invalid_argument);
    EXPECT_THROW(book_.removeFilledFront(side, price(300)),
                 std::invalid_argument);

    expectBest(Side::Buy, 300, 1);
    expectBest(Side::Sell, 400, 2);
  }
}

TEST_F(OrderBookTest, InvalidOrdersAreRejectedWithoutChangingLiveOrders) {
  struct InvalidOrderCase {
    std::string_view name;
    Order value;
  };

  add(1, Side::Buy, 300, 10);
  const std::array cases{
      InvalidOrderCase{"zero id", order(0, Side::Buy, 500)},
      InvalidOrderCase{"invalid side", order(2, Side::Invalid, 500)},
      InvalidOrderCase{"unknown side", order(3, static_cast<Side>(99), 500)},
      InvalidOrderCase{"zero quantity", order(4, Side::Buy, 500, 0)},
      InvalidOrderCase{"below grid", order(5, Side::Buy, 50)},
      InvalidOrderCase{"above grid", order(6, Side::Buy, 600)},
      InvalidOrderCase{"off tick", order(7, Side::Buy, 150)},
  };

  for (const auto& testCase : cases) {
    SCOPED_TRACE(testCase.name);
    EXPECT_THROW(book_.addOrder(testCase.value), std::invalid_argument);

    expectBest(Side::Buy, 300, 1);
    EXPECT_EQ(book_.frontOrder(Side::Buy).quantity, 10);
    EXPECT_TRUE(book_.empty(Side::Sell));
    EXPECT_TRUE(book_.hasOrder(1));
    if (testCase.value.id != 0) {
      EXPECT_FALSE(book_.hasOrder(testCase.value.id));
    }
  }
}

TEST_F(OrderBookTest, ZeroPriceIsRejectedEvenWhenTheGridContainsZero) {
  constexpr PriceGridConfig zeroBasedGrid{
      .minimumScaledPrice = 0,
      .maximumScaledPrice = 100,
      .tickSize = 100,
  };
  OrderBook book{zeroBasedGrid};

  EXPECT_THROW(book.addOrder(order(1, Side::Buy, 0)), std::invalid_argument);
  EXPECT_TRUE(book.empty(Side::Buy));
  EXPECT_FALSE(book.hasOrder(1));
}

TEST_F(OrderBookTest, SupportsMaximumOrderIdAndQuantityWithoutNarrowing) {
  constexpr OrderId maximumId = std::numeric_limits<OrderId>::max();
  constexpr Quantity maximumQuantity = std::numeric_limits<Quantity>::max();

  add(maximumId, Side::Sell, 300, maximumQuantity);

  EXPECT_TRUE(book_.hasOrder(maximumId));
  expectBest(Side::Sell, 300, maximumId);
  EXPECT_EQ(book_.frontOrder(Side::Sell).quantity, maximumQuantity);

  EXPECT_EQ(book_.reduceFrontQuantity(Side::Sell, maximumQuantity), 0);
  book_.removeFilledFront(Side::Sell, price(300));

  EXPECT_FALSE(book_.hasOrder(maximumId));
  EXPECT_TRUE(book_.empty(Side::Sell));
}

TEST_F(OrderBookTest, DuplicateIdsRollBackWithoutLeavingUnindexedOrders) {
  struct DuplicateCase {
    std::string_view name;
    Side side;
    std::int64_t scaledPrice;
  };
  constexpr std::array cases{
      DuplicateCase{"same occupied level", Side::Buy, 300},
      DuplicateCase{"empty better level", Side::Buy, 500},
      DuplicateCase{"opposite side", Side::Sell, 400},
  };

  for (const auto& testCase : cases) {
    SCOPED_TRACE(testCase.name);
    OrderBook book{kOrderBookGrid};
    book.addOrder(order(1, Side::Buy, 300, 10));

    EXPECT_THROW(
        book.addOrder(order(1, testCase.side, testCase.scaledPrice, 20)),
        std::invalid_argument);

    EXPECT_TRUE(book.hasOrder(1));
    EXPECT_EQ(book.bestPrice(Side::Buy), price(300));
    EXPECT_EQ(book.frontOrder(Side::Buy).id, 1);
    EXPECT_EQ(book.frontOrder(Side::Buy).quantity, 10);
    EXPECT_TRUE(book.empty(Side::Sell));

    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_TRUE(book.empty(Side::Buy));
    EXPECT_TRUE(book.empty(Side::Sell));
    EXPECT_FALSE(book.hasOrder(1));

    book.addOrder(order(1, testCase.side, testCase.scaledPrice, 20));
    EXPECT_TRUE(book.hasOrder(1));
    EXPECT_EQ(book.bestPrice(testCase.side), price(testCase.scaledPrice));
    EXPECT_EQ(book.frontOrder(testCase.side).quantity, 20);
  }
}

TEST_F(OrderBookTest, UnknownAndAlreadyRemovedCancellationsDoNotMutateBook) {
  add(1, Side::Buy, 300);
  add(2, Side::Buy, 200);

  EXPECT_FALSE(book_.cancelOrder(0));
  EXPECT_FALSE(book_.cancelOrder(999));
  expectBest(Side::Buy, 300, 1);
  EXPECT_TRUE(book_.hasOrder(1));
  EXPECT_TRUE(book_.hasOrder(2));

  EXPECT_TRUE(book_.cancelOrder(2));
  EXPECT_FALSE(book_.cancelOrder(2));
  expectBest(Side::Buy, 300, 1);
  EXPECT_TRUE(book_.hasOrder(1));
}

TEST_F(OrderBookTest, CancelingHeadMiddleAndTailPreservesFifoAndPricePriority) {
  add(1, Side::Buy, 300);
  add(2, Side::Buy, 300);
  add(3, Side::Buy, 300);
  add(4, Side::Buy, 200);

  EXPECT_TRUE(book_.cancelOrder(2));
  expectBest(Side::Buy, 300, 1);

  EXPECT_TRUE(book_.cancelOrder(1));
  expectBest(Side::Buy, 300, 3);

  EXPECT_TRUE(book_.cancelOrder(3));
  expectBest(Side::Buy, 200, 4);

  EXPECT_TRUE(book_.cancelOrder(4));
  EXPECT_TRUE(book_.empty(Side::Buy));
  EXPECT_THROW((void)book_.bestPrice(Side::Buy), std::logic_error);
}

TEST_F(OrderBookTest, ReusedCanceledIdGetsFreshLocatorAtFifoTail) {
  add(1, Side::Buy, 300);
  add(2, Side::Buy, 300);
  add(3, Side::Buy, 300);

  EXPECT_TRUE(book_.cancelOrder(2));
  EXPECT_FALSE(book_.hasOrder(2));

  add(2, Side::Buy, 300);
  EXPECT_TRUE(book_.hasOrder(2));
  expectBest(Side::Buy, 300, 1);

  EXPECT_TRUE(book_.cancelOrder(1));
  expectBest(Side::Buy, 300, 3);

  EXPECT_TRUE(book_.cancelOrder(3));
  expectBest(Side::Buy, 300, 2);

  EXPECT_TRUE(book_.cancelOrder(2));
  EXPECT_TRUE(book_.empty(Side::Buy));
}

TEST_F(OrderBookTest, ReusedFilledIdGetsFreshLocatorAtFifoTail) {
  add(1, Side::Sell, 300);
  add(2, Side::Sell, 300);

  EXPECT_EQ(book_.reduceFrontQuantity(Side::Sell, 1), 0);
  book_.removeFilledFront(Side::Sell, price(300));
  EXPECT_FALSE(book_.hasOrder(1));
  expectBest(Side::Sell, 300, 2);

  add(1, Side::Sell, 300);
  EXPECT_TRUE(book_.hasOrder(1));
  expectBest(Side::Sell, 300, 2);

  EXPECT_EQ(book_.reduceFrontQuantity(Side::Sell, 1), 0);
  book_.removeFilledFront(Side::Sell, price(300));
  expectBest(Side::Sell, 300, 1);

  EXPECT_TRUE(book_.cancelOrder(1));
  EXPECT_TRUE(book_.empty(Side::Sell));
}

TEST_F(OrderBookTest, BestPricesTrackOutOfOrderAddsAndCancellationsBySide) {
  add(1, Side::Buy, 200);
  add(2, Side::Buy, 400);
  add(3, Side::Buy, 300);
  add(4, Side::Sell, 400);
  add(5, Side::Sell, 200);
  add(6, Side::Sell, 300);

  expectBest(Side::Buy, 400, 2);
  expectBest(Side::Sell, 200, 5);

  EXPECT_TRUE(book_.cancelOrder(1));
  EXPECT_TRUE(book_.cancelOrder(4));
  expectBest(Side::Buy, 400, 2);
  expectBest(Side::Sell, 200, 5);

  EXPECT_TRUE(book_.cancelOrder(2));
  EXPECT_TRUE(book_.cancelOrder(5));
  expectBest(Side::Buy, 300, 3);
  expectBest(Side::Sell, 300, 6);
}

TEST_F(OrderBookTest, ReactivatedNonBestLevelIsPromotedAfterBestIsRemoved) {
  struct SideCase {
    Side side;
    std::int64_t bestPrice;
    std::int64_t nonBestPrice;
  };
  constexpr std::array cases{
      SideCase{Side::Buy, 400, 200},
      SideCase{Side::Sell, 200, 400},
  };

  for (const auto& testCase : cases) {
    SCOPED_TRACE(static_cast<unsigned>(testCase.side));
    OrderBook book{kOrderBookGrid};
    book.addOrder(order(1, testCase.side, testCase.bestPrice));
    book.addOrder(order(2, testCase.side, testCase.nonBestPrice));

    EXPECT_TRUE(book.cancelOrder(2));
    EXPECT_EQ(book.bestPrice(testCase.side), price(testCase.bestPrice));
    EXPECT_FALSE(book.hasOrder(2));

    book.addOrder(order(3, testCase.side, testCase.nonBestPrice));
    EXPECT_EQ(book.bestPrice(testCase.side), price(testCase.bestPrice));
    EXPECT_TRUE(book.hasOrder(3));

    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_EQ(book.bestPrice(testCase.side), price(testCase.nonBestPrice));
    EXPECT_EQ(book.frontOrder(testCase.side).id, 3);
  }
}

TEST_F(OrderBookTest, BestPricesTransitionAcrossBitsetWordBoundariesAndGaps) {
  constexpr PriceGridConfig wideGrid{
      .minimumScaledPrice = 100,
      .maximumScaledPrice = 13000,
      .tickSize = 100,
  };
  constexpr std::array<std::size_t, 5> insertionOrder{64, 0, 129, 63, 127};
  OrderBook book{wideGrid};

  const auto priceAt = [](std::size_t index) {
    return price(100 + static_cast<std::int64_t>(index) * 100);
  };
  for (const std::size_t index : insertionOrder) {
    book.addOrder(order(100 + index, Side::Buy, priceAt(index).scaledValue()));
    book.addOrder(
        order(1'000 + index, Side::Sell, priceAt(index).scaledValue()));
  }

  EXPECT_EQ(book.bestPrice(Side::Buy), priceAt(129));
  EXPECT_EQ(book.bestPrice(Side::Sell), priceAt(0));

  EXPECT_TRUE(book.cancelOrder(100 + 64));
  EXPECT_TRUE(book.cancelOrder(1'000 + 63));
  EXPECT_EQ(book.bestPrice(Side::Buy), priceAt(129));
  EXPECT_EQ(book.bestPrice(Side::Sell), priceAt(0));

  constexpr std::array<std::size_t, 4> buyRemovalOrder{129, 127, 63, 0};
  constexpr std::array<std::size_t, 3> buyNextBest{127, 63, 0};
  for (std::size_t i = 0; i < buyRemovalOrder.size(); ++i) {
    EXPECT_TRUE(book.cancelOrder(100 + buyRemovalOrder[i]));
    if (i < buyNextBest.size()) {
      EXPECT_EQ(book.bestPrice(Side::Buy), priceAt(buyNextBest[i]));
    }
  }
  EXPECT_TRUE(book.empty(Side::Buy));

  constexpr std::array<std::size_t, 4> sellRemovalOrder{0, 64, 127, 129};
  constexpr std::array<std::size_t, 3> sellNextBest{64, 127, 129};
  for (std::size_t i = 0; i < sellRemovalOrder.size(); ++i) {
    EXPECT_TRUE(book.cancelOrder(1'000 + sellRemovalOrder[i]));
    if (i < sellNextBest.size()) {
      EXPECT_EQ(book.bestPrice(Side::Sell), priceAt(sellNextBest[i]));
    }
  }
  EXPECT_TRUE(book.empty(Side::Sell));
}

TEST_F(OrderBookTest, FilledRemovalPreservesFifoThenAdvancesAndClearsEachSide) {
  struct SideCase {
    Side side;
    std::int64_t bestPrice;
    std::int64_t nextPrice;
  };
  constexpr std::array cases{
      SideCase{Side::Buy, 300, 200},
      SideCase{Side::Sell, 300, 400},
  };

  for (const auto& testCase : cases) {
    SCOPED_TRACE(static_cast<unsigned>(testCase.side));
    OrderBook book{kOrderBookGrid};
    book.addOrder(order(1, testCase.side, testCase.bestPrice, 2));
    book.addOrder(order(2, testCase.side, testCase.bestPrice, 3));
    book.addOrder(order(3, testCase.side, testCase.nextPrice, 1));

    EXPECT_EQ(book.reduceFrontQuantity(testCase.side, 1), 1);
    EXPECT_THROW(
        book.removeFilledFront(testCase.side, price(testCase.bestPrice)),
        std::logic_error);
    EXPECT_EQ(book.frontOrder(testCase.side).id, 1);

    EXPECT_EQ(book.reduceFrontQuantity(testCase.side, 1), 0);
    book.removeFilledFront(testCase.side, price(testCase.bestPrice));
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_EQ(book.bestPrice(testCase.side), price(testCase.bestPrice));
    EXPECT_EQ(book.frontOrder(testCase.side).id, 2);

    EXPECT_EQ(book.reduceFrontQuantity(testCase.side, 3), 0);
    book.removeFilledFront(testCase.side, price(testCase.bestPrice));
    EXPECT_FALSE(book.hasOrder(2));
    EXPECT_EQ(book.bestPrice(testCase.side), price(testCase.nextPrice));
    EXPECT_EQ(book.frontOrder(testCase.side).id, 3);

    EXPECT_EQ(book.reduceFrontQuantity(testCase.side, 1), 0);
    book.removeFilledFront(testCase.side, price(testCase.nextPrice));
    EXPECT_FALSE(book.hasOrder(3));
    EXPECT_TRUE(book.empty(testCase.side));
    EXPECT_THROW((void)book.bestPrice(testCase.side), std::logic_error);
  }
}

TEST_F(OrderBookTest, MixedOperationsStayConsistentWithReferenceModel) {
  struct ModelOrder {
    OrderId id;
    Quantity quantity;
  };
  struct ModelLocation {
    Side side;
    std::int64_t scaledPrice;
  };
  using ModelLevels = std::map<std::int64_t, std::deque<ModelOrder>>;

  ModelLevels modelBuys;
  ModelLevels modelSells;
  std::map<OrderId, ModelLocation> modelLocations;
  OrderId nextId = 1;
  std::mt19937_64 random{0xB357CA5EULL};

  const auto levelsFor = [&](Side side) -> ModelLevels& {
    return side == Side::Buy ? modelBuys : modelSells;
  };
  const auto bestLevelFor = [&](Side side) {
    auto& levels = levelsFor(side);
    return side == Side::Buy ? std::prev(levels.end()) : levels.begin();
  };
  const auto eraseModelOrder = [&](OrderId id) {
    const auto locationIt = modelLocations.find(id);
    ASSERT_NE(locationIt, modelLocations.end());

    auto& levels = levelsFor(locationIt->second.side);
    const auto levelIt = levels.find(locationIt->second.scaledPrice);
    ASSERT_NE(levelIt, levels.end());

    const auto erased = std::erase_if(
        levelIt->second,
        [id](const ModelOrder& modelOrder) { return modelOrder.id == id; });
    ASSERT_EQ(erased, 1U);
    if (levelIt->second.empty()) {
      levels.erase(levelIt);
    }
    modelLocations.erase(locationIt);
  };
  const auto verifySide = [&](Side side) {
    const auto& levels = levelsFor(side);
    if (levels.empty()) {
      EXPECT_TRUE(book_.empty(side));
      return;
    }

    ASSERT_FALSE(book_.empty(side));
    const auto levelIt = bestLevelFor(side);
    const auto& expectedFront = levelIt->second.front();
    const auto& actualFront = book_.frontOrder(side);

    EXPECT_EQ(book_.bestPrice(side).scaledValue(), levelIt->first);
    EXPECT_EQ(actualFront.id, expectedFront.id);
    EXPECT_EQ(actualFront.side, side);
    EXPECT_EQ(actualFront.quantity, expectedFront.quantity);
    EXPECT_EQ(actualFront.price.scaledValue(), levelIt->first);
  };
  const auto verifyModel = [&] {
    verifySide(Side::Buy);
    verifySide(Side::Sell);
    for (OrderId id = 1; id < nextId; ++id) {
      EXPECT_EQ(book_.hasOrder(id), modelLocations.contains(id)) << "id=" << id;
    }
  };
  const auto randomActiveId = [&] {
    auto locationIt = modelLocations.begin();
    std::advance(locationIt, random() % modelLocations.size());
    return locationIt->first;
  };

  constexpr std::size_t kOperationCount = 2'000;
  for (std::size_t step = 0; step < kOperationCount; ++step) {
    SCOPED_TRACE(step);
    const auto action = random() % 100;

    if (modelLocations.empty() || action < 45) {
      const Side side = (random() & 1U) == 0 ? Side::Buy : Side::Sell;
      const auto scaledPrice =
          static_cast<std::int64_t>(1 + random() % 5) * 100;
      const Quantity quantity = 1 + random() % 20;
      const OrderId id = nextId++;

      book_.addOrder(order(id, side, scaledPrice, quantity));
      levelsFor(side)[scaledPrice].push_back(ModelOrder{id, quantity});
      modelLocations.emplace(id, ModelLocation{side, scaledPrice});
    } else if (action < 70) {
      const bool cancelExisting = random() % 5 != 0;
      const OrderId id = cancelExisting ? randomActiveId()
                                        : nextId + 10'000 + random() % 1'000;
      const bool expectedCanceled = modelLocations.contains(id);

      EXPECT_EQ(book_.cancelOrder(id), expectedCanceled);
      if (expectedCanceled) {
        eraseModelOrder(id);
      }
    } else if (action < 90) {
      Side side = (random() & 1U) == 0 ? Side::Buy : Side::Sell;
      if (levelsFor(side).empty()) {
        side = opposite(side);
      }

      auto levelIt = bestLevelFor(side);
      auto& expectedFront = levelIt->second.front();
      const Quantity amount =
          expectedFront.quantity == 1 || (random() & 1U) == 0
              ? expectedFront.quantity
              : 1 + random() % (expectedFront.quantity - 1);
      const Quantity expectedRemaining = expectedFront.quantity - amount;

      EXPECT_EQ(book_.reduceFrontQuantity(side, amount), expectedRemaining);
      expectedFront.quantity = expectedRemaining;
      if (expectedRemaining == 0) {
        const OrderId filledId = expectedFront.id;
        const auto scaledPrice = levelIt->first;
        book_.removeFilledFront(side, price(scaledPrice));
        levelIt->second.pop_front();
        if (levelIt->second.empty()) {
          levelsFor(side).erase(levelIt);
        }
        modelLocations.erase(filledId);
      }
    } else {
      const OrderId duplicateId = randomActiveId();
      const Side side = (random() & 1U) == 0 ? Side::Buy : Side::Sell;
      const auto scaledPrice =
          static_cast<std::int64_t>(1 + random() % 5) * 100;

      EXPECT_THROW(book_.addOrder(order(duplicateId, side, scaledPrice)),
                   std::invalid_argument);
    }

    verifyModel();
  }

  while (!modelLocations.empty()) {
    const OrderId id = modelLocations.begin()->first;
    EXPECT_TRUE(book_.cancelOrder(id));
    eraseModelOrder(id);
    verifyModel();
  }
  EXPECT_TRUE(book_.empty(Side::Buy));
  EXPECT_TRUE(book_.empty(Side::Sell));
}

TEST_F(OrderBookTest, InvalidFilledRemovalRequestsLeaveOrdersUnchanged) {
  add(1, Side::Buy, 200);
  EXPECT_EQ(book_.reduceFrontQuantity(Side::Buy, 1), 0);
  add(2, Side::Buy, 300);

  EXPECT_THROW(book_.removeFilledFront(Side::Buy, price(200)),
               std::logic_error);
  EXPECT_THROW(book_.removeFilledFront(Side::Buy, price(150)),
               std::invalid_argument);
  EXPECT_THROW(book_.removeFilledFront(Side::Buy, price(600)),
               std::invalid_argument);
  EXPECT_THROW(book_.removeFilledFront(Side::Sell, price(300)),
               std::logic_error);

  EXPECT_TRUE(book_.hasOrder(1));
  EXPECT_TRUE(book_.hasOrder(2));
  expectBest(Side::Buy, 300, 2);

  EXPECT_TRUE(book_.cancelOrder(2));
  expectBest(Side::Buy, 200, 1);
  book_.removeFilledFront(Side::Buy, price(200));
  EXPECT_TRUE(book_.empty(Side::Buy));
}

}  // namespace
