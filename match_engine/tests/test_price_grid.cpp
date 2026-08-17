#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include "PriceGrid.hpp"

namespace {

constexpr PriceGridConfig kTinyGrid{
    .minimumScaledPrice = 1'000'000,
    .maximumScaledPrice = 1'001'000,
    .tickSize = 100,
};

Price priceFromScaled(std::int64_t scaledPrice) {
  return Price::fromScaled(scaledPrice).value();
}

constexpr std::int64_t tinyScaledPriceAt(std::size_t index) {
  return kTinyGrid.minimumScaledPrice +
         static_cast<std::int64_t>(index) * kTinyGrid.tickSize;
}

class TinyPriceGridTest : public ::testing::Test {
 protected:
  PriceGrid grid_{kTinyGrid};
};

}  // namespace

TEST(PriceGridTest, DefaultGridHasExpectedLevelCount) {
  const PriceGrid grid;

  EXPECT_EQ(grid.levelCount(), 200'001U);
}

TEST(PriceGridTest, TinyGridHasExpectedLevelCount) {
  const PriceGrid grid(kTinyGrid);

  EXPECT_EQ(grid.levelCount(), 11U);
}

TEST(PriceGridTest, RetainsItsConfiguration) {
  const PriceGrid grid(kTinyGrid);
  const auto& config = grid.config();

  EXPECT_EQ(config.minimumScaledPrice, kTinyGrid.minimumScaledPrice);
  EXPECT_EQ(config.maximumScaledPrice, kTinyGrid.maximumScaledPrice);
  EXPECT_EQ(config.tickSize, kTinyGrid.tickSize);
}

TEST(PriceGridTest, RejectsNegativeMinimumPrice) {
  const PriceGridConfig config{
      .minimumScaledPrice = -1,
      .maximumScaledPrice = 1'000,
      .tickSize = 100,
  };

  EXPECT_THROW((void)PriceGrid(config), std::invalid_argument);
}

TEST(PriceGridTest, RejectsMaximumBelowMinimumPrice) {
  const PriceGridConfig config{
      .minimumScaledPrice = 1'000,
      .maximumScaledPrice = 900,
      .tickSize = 100,
  };

  EXPECT_THROW((void)PriceGrid(config), std::invalid_argument);
}

TEST(PriceGridTest, RejectsZeroTickSize) {
  const PriceGridConfig config{
      .minimumScaledPrice = 0,
      .maximumScaledPrice = 1'000,
      .tickSize = 0,
  };

  EXPECT_THROW((void)PriceGrid(config), std::invalid_argument);
}

TEST(PriceGridTest, RejectsNegativeTickSize) {
  const PriceGridConfig config{
      .minimumScaledPrice = 0,
      .maximumScaledPrice = 1'000,
      .tickSize = -100,
  };

  EXPECT_THROW((void)PriceGrid(config), std::invalid_argument);
}

TEST(PriceGridTest, RejectsRangeNotDivisibleByTickSize) {
  const PriceGridConfig config{
      .minimumScaledPrice = 0,
      .maximumScaledPrice = 1'001,
      .tickSize = 100,
  };

  EXPECT_THROW((void)PriceGrid(config), std::invalid_argument);
}

TEST_F(TinyPriceGridTest, ConvertsEveryPriceAndIndexUsingIndependentOracles) {
  for (std::size_t index = 0; index < grid_.levelCount(); ++index) {
    SCOPED_TRACE(index);
    const auto scaledPrice = tinyScaledPriceAt(index);
    const auto convertedIndex =
        grid_.priceToIndex(priceFromScaled(scaledPrice));

    ASSERT_TRUE(convertedIndex.has_value());
    EXPECT_EQ(*convertedIndex, index);
    EXPECT_EQ(grid_.indexToPrice(index).scaledValue(), scaledPrice);
  }
}

TEST_F(TinyPriceGridTest, RejectsPriceBelowMinimum) {
  const auto index = grid_.priceToIndex(
      priceFromScaled(kTinyGrid.minimumScaledPrice - kTinyGrid.tickSize));

  EXPECT_FALSE(index.has_value());
}

TEST_F(TinyPriceGridTest, RejectsPriceAboveMaximum) {
  const auto index = grid_.priceToIndex(
      priceFromScaled(kTinyGrid.maximumScaledPrice + kTinyGrid.tickSize));

  EXPECT_FALSE(index.has_value());
}

TEST_F(TinyPriceGridTest, RejectsOffTickPrice) {
  const auto index = grid_.priceToIndex(
      priceFromScaled(kTinyGrid.minimumScaledPrice + kTinyGrid.tickSize / 2));

  EXPECT_FALSE(index.has_value());
}

TEST_F(TinyPriceGridTest, RejectsIndexOutsideGrid) {
  EXPECT_THROW((void)grid_.indexToPrice(grid_.levelCount()), std::out_of_range);
}

TEST(PriceGridTest, SupportsASinglePriceAtTheIntegerLimit) {
  constexpr auto kMaximumPrice = std::numeric_limits<std::int64_t>::max();
  constexpr PriceGridConfig config{
      .minimumScaledPrice = kMaximumPrice,
      .maximumScaledPrice = kMaximumPrice,
      .tickSize = 1,
  };
  const PriceGrid grid(config);

  EXPECT_EQ(grid.levelCount(), 1U);
  EXPECT_EQ(grid.indexToPrice(0).scaledValue(), kMaximumPrice);

  const auto index = grid.priceToIndex(priceFromScaled(kMaximumPrice));
  ASSERT_TRUE(index.has_value());
  EXPECT_EQ(*index, 0U);
}

TEST(PriceGridTest, ConvertsWithoutOverflowAtTheIntegerLimit) {
  constexpr auto kMaximumPrice = std::numeric_limits<std::int64_t>::max();
  constexpr PriceGridConfig config{
      .minimumScaledPrice = 1,
      .maximumScaledPrice = kMaximumPrice,
      .tickSize = kMaximumPrice - 1,
  };
  const PriceGrid grid(config);

  EXPECT_EQ(grid.levelCount(), 2U);
  EXPECT_EQ(grid.indexToPrice(1).scaledValue(), kMaximumPrice);

  const auto index = grid.priceToIndex(priceFromScaled(kMaximumPrice));
  ASSERT_TRUE(index.has_value());
  EXPECT_EQ(*index, 1U);
}

TEST(PriceGridTest, AnchorsTicksToTheConfiguredMinimum) {
  constexpr PriceGridConfig config{
      .minimumScaledPrice = 1'000'050,
      .maximumScaledPrice = 1'000'250,
      .tickSize = 100,
  };
  const PriceGrid grid(config);

  const auto alignedIndex = grid.priceToIndex(
      priceFromScaled(config.minimumScaledPrice + config.tickSize));
  ASSERT_TRUE(alignedIndex.has_value());
  EXPECT_EQ(*alignedIndex, 1U);
  EXPECT_EQ(grid.indexToPrice(1).scaledValue(),
            config.minimumScaledPrice + config.tickSize);

  const auto globallyAlignedButOffGrid = grid.priceToIndex(
      priceFromScaled(config.minimumScaledPrice + config.tickSize / 2));
  EXPECT_FALSE(globallyAlignedButOffGrid.has_value());
}
