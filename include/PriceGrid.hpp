#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

#include "Price.hpp"

// Defines the finite price universe used by the flat-array order book.
// Price stores four decimal places, so a scaled tick size of 100 represents
// $0.01.
struct PriceGridConfig {
  std::int64_t minimumScaledPrice = 0;
  std::int64_t maximumScaledPrice = 20'000'000;
  std::int64_t tickSize = 100;
};

// Converts between domain Prices and dense indexes suitable for vectors and
// PriceBitset. Keep all range and tick-policy logic here so OrderBook only has
// to work with validated indexes.
class PriceGrid {
 public:
  explicit PriceGrid(PriceGridConfig config = {});

  [[nodiscard]] std::size_t levelCount() const noexcept;

  // Returns an error when price is below/above the configured range or does
  // not lie exactly on a configured tick.
  [[nodiscard]] std::expected<std::size_t, std::string> priceToIndex(
      Price price) const;

  // Treat an index outside [0, levelCount()) as a programming error.
  [[nodiscard]] Price indexToPrice(std::size_t index) const;

  [[nodiscard]] const PriceGridConfig& config() const noexcept;

 private:
  // Validate the configuration and return:
  //   (maximumScaledPrice - minimumScaledPrice) / tickSize + 1
  [[nodiscard]] static std::size_t validateAndCountLevels(
      const PriceGridConfig& config);

  PriceGridConfig config_;
  std::size_t levelCount_;
};
