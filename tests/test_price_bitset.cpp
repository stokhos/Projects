#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "PriceBitset.hpp"

namespace {

void expectMovedFromBitsetIsEmpty(const PriceBitset& bits) {
  EXPECT_EQ(bits.size(), std::size_t{0});
  EXPECT_EQ(bits.findLowestSet(), std::nullopt);
  EXPECT_EQ(bits.findHighestSet(0), std::nullopt);
  EXPECT_THROW((void)bits.test(0), std::out_of_range);
}

}  // namespace

TEST(PriceBitsetTest, ReportsConfiguredSizeAndStartsEmpty) {
  const PriceBitset bits(70);

  EXPECT_EQ(bits.size(), std::size_t{70});
  for (std::size_t index = 0; index < bits.size(); ++index) {
    EXPECT_FALSE(bits.test(index));
  }
  EXPECT_EQ(bits.findLowestSet(), std::nullopt);
  EXPECT_EQ(bits.findHighestSet(69), std::nullopt);
}

TEST(PriceBitsetTest, SetsTestsAndClearsWordBoundaryIndexes) {
  PriceBitset bits(70);

  for (const std::size_t index :
       {std::size_t{0}, std::size_t{63}, std::size_t{64}, std::size_t{69}}) {
    bits.set(index);
    EXPECT_TRUE(bits.test(index));
  }

  EXPECT_FALSE(bits.test(1));
  EXPECT_FALSE(bits.test(62));
  EXPECT_FALSE(bits.test(65));

  bits.clear(63);
  bits.clear(64);

  EXPECT_TRUE(bits.test(0));
  EXPECT_FALSE(bits.test(63));
  EXPECT_FALSE(bits.test(64));
  EXPECT_TRUE(bits.test(69));
}

TEST(PriceBitsetTest, SetAndClearAreIdempotent) {
  PriceBitset bits(1);

  bits.set(0);
  bits.set(0);
  EXPECT_TRUE(bits.test(0));

  bits.clear(0);
  bits.clear(0);
  EXPECT_FALSE(bits.test(0));
}

TEST(PriceBitsetTest, SearchesReflectClearedExtrema) {
  PriceBitset bits(70);
  bits.set(2);
  bits.set(63);
  bits.set(64);
  bits.set(69);

  bits.clear(69);
  EXPECT_EQ(bits.findHighestSet(69), std::optional<std::size_t>{64});

  bits.clear(2);
  EXPECT_EQ(bits.findLowestSet(), std::optional<std::size_t>{63});

  bits.clear(63);
  bits.clear(64);
  EXPECT_EQ(bits.findLowestSet(), std::nullopt);
  EXPECT_EQ(bits.findHighestSet(69), std::nullopt);
}

TEST(PriceBitsetTest, SearchesAcrossCompletelyEmptyInteriorWords) {
  PriceBitset bits(257);
  bits.set(0);
  bits.set(256);

  EXPECT_EQ(bits.findLowestSet(1), std::optional<std::size_t>{256});
  EXPECT_EQ(bits.findHighestSet(255), std::optional<std::size_t>{0});
}

TEST(PriceBitsetTest, HighestSearchClampsAnOversizedStart) {
  PriceBitset bits(70);
  bits.set(69);

  EXPECT_EQ(bits.findHighestSet(70), std::optional<std::size_t>{69});
  EXPECT_EQ(bits.findHighestSet(std::numeric_limits<std::size_t>::max()),
            std::optional<std::size_t>{69});
}

TEST(PriceBitsetTest, HandlesZeroSizedBitset) {
  PriceBitset bits(0);

  EXPECT_EQ(bits.size(), std::size_t{0});
  EXPECT_EQ(bits.findLowestSet(), std::nullopt);
  EXPECT_EQ(bits.findHighestSet(0), std::nullopt);
  EXPECT_EQ(bits.findHighestSet(std::numeric_limits<std::size_t>::max()),
            std::nullopt);

  EXPECT_THROW(bits.set(0), std::out_of_range);
  EXPECT_THROW(bits.clear(0), std::out_of_range);
  EXPECT_THROW((void)bits.test(0), std::out_of_range);
}

TEST(PriceBitsetTest, HandlesAnExactMultipleOfTheWordSize) {
  PriceBitset bits(128);
  bits.set(127);

  EXPECT_TRUE(bits.test(127));
  EXPECT_EQ(bits.findLowestSet(), std::optional<std::size_t>{127});
  EXPECT_EQ(bits.findHighestSet(127), std::optional<std::size_t>{127});

  bits.clear(127);
  EXPECT_EQ(bits.findLowestSet(), std::nullopt);
  EXPECT_EQ(bits.findHighestSet(127), std::nullopt);
}

TEST(PriceBitsetTest, RejectsIndexesAtAndAboveTheConfiguredSize) {
  PriceBitset bits(70);

  for (const std::size_t index : {bits.size(), bits.size() + 1,
                                  std::numeric_limits<std::size_t>::max()}) {
    SCOPED_TRACE(index);
    EXPECT_THROW(bits.set(index), std::out_of_range);
    EXPECT_THROW(bits.clear(index), std::out_of_range);
    EXPECT_THROW((void)bits.test(index), std::out_of_range);
  }
}

TEST(PriceBitsetTest, SearchesMatchLinearOracleAroundWordBoundaries) {
  constexpr std::array<std::size_t, 9> kSizes{
      1, 2, 63, 64, 65, 70, 127, 128, 129,
  };

  for (const std::size_t size : kSizes) {
    PriceBitset bits(size);
    std::vector<bool> expected(size, false);

    for (std::size_t index = 0; index < size; ++index) {
      if (index == 0 || index + 1 == size || index % 17 == 3 ||
          index % 64 == 63) {
        bits.set(index);
        expected[index] = true;
      }
    }

    for (std::size_t start = 0; start <= size + 1; ++start) {
      std::optional<std::size_t> expectedLowest;
      for (std::size_t index = start; index < size; ++index) {
        if (expected[index]) {
          expectedLowest = index;
          break;
        }
      }

      std::optional<std::size_t> expectedHighest;
      std::size_t index = start >= size ? size - 1 : start;
      while (true) {
        if (expected[index]) {
          expectedHighest = index;
          break;
        }
        if (index == 0) {
          break;
        }
        --index;
      }

      EXPECT_EQ(bits.findLowestSet(start), expectedLowest)
          << "size=" << size << ", start=" << start;
      EXPECT_EQ(bits.findHighestSet(start), expectedHighest)
          << "size=" << size << ", start=" << start;
    }
  }
}

TEST(PriceBitsetTest, CopyConstructionCreatesAnIndependentValue) {
  PriceBitset source(65);
  source.set(0);
  source.set(64);

  PriceBitset copy{source};
  source.clear(64);
  copy.clear(0);

  EXPECT_EQ(copy.size(), std::size_t{65});
  EXPECT_TRUE(source.test(0));
  EXPECT_FALSE(source.test(64));
  EXPECT_FALSE(copy.test(0));
  EXPECT_TRUE(copy.test(64));
}

TEST(PriceBitsetTest, CopyAssignmentReplacesTheDestinationIndependently) {
  PriceBitset source(65);
  source.set(64);
  PriceBitset assigned(1);
  assigned.set(0);

  assigned = source;
  source.clear(64);
  source.set(0);

  EXPECT_EQ(assigned.size(), std::size_t{65});
  EXPECT_FALSE(assigned.test(0));
  EXPECT_TRUE(assigned.test(64));
  EXPECT_TRUE(source.test(0));
  EXPECT_FALSE(source.test(64));
}

TEST(PriceBitsetTest, MoveConstructionResetsTheSourceToAnEmptyState) {
  PriceBitset source(65);
  source.set(64);

  PriceBitset moved{std::move(source)};

  EXPECT_EQ(moved.size(), std::size_t{65});
  EXPECT_FALSE(moved.test(0));
  EXPECT_TRUE(moved.test(64));
  expectMovedFromBitsetIsEmpty(source);
}

TEST(PriceBitsetTest, MoveAssignmentReplacesDestinationAndResetsSource) {
  PriceBitset source(65);
  source.set(64);

  PriceBitset assigned(1);
  assigned.set(0);
  assigned = std::move(source);

  EXPECT_EQ(assigned.size(), std::size_t{65});
  EXPECT_FALSE(assigned.test(0));
  EXPECT_TRUE(assigned.test(64));
  expectMovedFromBitsetIsEmpty(source);
}
