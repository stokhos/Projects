#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string_view>

#include "Price.hpp"

namespace {

struct ParsedPriceCase {
  std::string_view text;
  std::int64_t expectedScaledValue;
};

struct FormattedPriceCase {
  std::int64_t scaledValue;
  std::string_view expectedText;
};

}  // namespace

TEST(PriceTest, DefaultConstructsToZero) {
  const Price price;

  EXPECT_EQ(price.scaledValue(), 0);
}

TEST(PriceTest, ConstructsFromScaledValuesWithoutChangingRepresentation) {
  constexpr std::array<std::int64_t, 3> scaledValues{
      0,
      12'345,
      std::numeric_limits<std::int64_t>::max(),
  };

  for (const auto scaledValue : scaledValues) {
    SCOPED_TRACE(scaledValue);
    const auto price = Price::fromScaled(scaledValue);

    ASSERT_TRUE(price.has_value());
    EXPECT_EQ(price->scaledValue(), scaledValue);
  }
}

TEST(PriceTest, RejectsNegativeScaledValue) {
  const auto price = Price::fromScaled(-1);

  ASSERT_FALSE(price.has_value());
  EXPECT_FALSE(price.error().empty());
}

TEST(PriceTest, ParsesValidDecimalFormsToScaledValues) {
  constexpr std::array<ParsedPriceCase, 8> cases{{
      {.text = "0", .expectedScaledValue = 0},
      {.text = "1250", .expectedScaledValue = 12'500'000},
      {.text = "1250.5", .expectedScaledValue = 12'505'000},
      {.text = "1250.0001", .expectedScaledValue = 12'500'001},
      {.text = "1250.0000", .expectedScaledValue = 12'500'000},
      {.text = "1000.2500", .expectedScaledValue = 10'002'500},
      {.text = ".5", .expectedScaledValue = 5'000},
      {.text = "1.", .expectedScaledValue = 10'000},
  }};

  for (const auto& testCase : cases) {
    SCOPED_TRACE(testCase.text);
    const auto price = Price::parse(testCase.text);

    ASSERT_TRUE(price.has_value());
    EXPECT_EQ(price->scaledValue(), testCase.expectedScaledValue);
  }
}

TEST(PriceTest, FormatsScaledValuesCanonically) {
  constexpr std::array<FormattedPriceCase, 7> cases{{
      {.scaledValue = 0, .expectedText = "0"},
      {.scaledValue = 1, .expectedText = "0.0001"},
      {.scaledValue = 12'345, .expectedText = "1.2345"},
      {.scaledValue = 12'500'000, .expectedText = "1250"},
      {.scaledValue = 10'002'500, .expectedText = "1000.25"},
      {.scaledValue = 5'000, .expectedText = "0.5"},
      {.scaledValue = std::numeric_limits<std::int64_t>::max(),
       .expectedText = "922337203685477.5807"},
  }};

  for (const auto& testCase : cases) {
    SCOPED_TRACE(testCase.scaledValue);
    const auto price = Price::fromScaled(testCase.scaledValue);

    ASSERT_TRUE(price.has_value());
    EXPECT_EQ(price->toString(), testCase.expectedText);
  }
}

TEST(PriceTest, RejectsMalformedDecimalText) {
  constexpr std::array<std::string_view, 11> malformedInputs{
      "",     "-",      ".",     "+1",      " 1",   "1 ",
      "12a5", "125.5x", "1.2.5", "1.23456", "-1.5",
  };

  for (const auto input : malformedInputs) {
    SCOPED_TRACE(input);
    const auto price = Price::parse(input);

    ASSERT_FALSE(price.has_value());
    EXPECT_FALSE(price.error().empty());
  }
}

TEST(PriceTest, RejectsValuesThatWouldOverflow) {
  const auto price = Price::parse("99999999999999999999");

  ASSERT_FALSE(price.has_value());
  EXPECT_FALSE(price.error().empty());
}

TEST(PriceTest, ParsesLargestRepresentableValue) {
  const auto price = Price::parse("922337203685477.5807");

  ASSERT_TRUE(price.has_value());
  EXPECT_EQ(price->scaledValue(), std::numeric_limits<std::int64_t>::max());
}

TEST(PriceTest, RejectsValueJustAboveLargestRepresentableValue) {
  const auto price = Price::parse("922337203685477.5808");

  ASSERT_FALSE(price.has_value());
  EXPECT_FALSE(price.error().empty());
}

TEST(PriceTest, RejectsOverflowAfterFractionPadding) {
  const auto price = Price::parse("922337203685477.6");

  ASSERT_FALSE(price.has_value());
  EXPECT_FALSE(price.error().empty());
}

TEST(PriceTest, ComparisonOperatorsRespectOrderingAndEquality) {
  const auto low = Price::fromScaled(1'000'000);
  const auto high = Price::fromScaled(1'000'001);
  const auto same = Price::fromScaled(1'000'000);
  ASSERT_TRUE(low.has_value());
  ASSERT_TRUE(high.has_value());
  ASSERT_TRUE(same.has_value());

  EXPECT_LT(*low, *high);
  EXPECT_FALSE(*high < *low);
  EXPECT_FALSE(*low < *same);

  EXPECT_LE(*low, *high);
  EXPECT_LE(*low, *same);
  EXPECT_FALSE(*high <= *low);

  EXPECT_GT(*high, *low);
  EXPECT_FALSE(*low > *high);
  EXPECT_FALSE(*low > *same);

  EXPECT_GE(*high, *low);
  EXPECT_GE(*low, *same);
  EXPECT_FALSE(*low >= *high);

  EXPECT_NE(*low, *high);
  EXPECT_FALSE(*low != *same);
  EXPECT_EQ(*low, *same);
  EXPECT_FALSE(*low == *high);
}

TEST(PriceTest, StreamOperatorUsesCanonicalFormatting) {
  const auto price = Price::fromScaled(425'000);
  ASSERT_TRUE(price.has_value());

  std::ostringstream os;
  os << *price;

  EXPECT_EQ(os.str(), "42.5");
  EXPECT_EQ(os.str(), price->toString());
}
