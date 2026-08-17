#include "PriceGrid.hpp"

#include <stdexcept>
#include <utility>

PriceGrid::PriceGrid(PriceGridConfig config)
    : config_(config), levelCount_(validateAndCountLevels(config_)) {}

std::size_t PriceGrid::levelCount() const noexcept { return levelCount_; }

// Converts a price to a dense array index. Dividing by tickSize (rather than
// using the raw scaled price as the index) is what keeps the array sized by
// the number of legal tick-aligned levels instead of by Price's full scaled
// range - e.g. a $0.01 tick over $0-$2000 needs 200,001 slots, not
// 20,000,001.
std::expected<std::size_t, std::string> PriceGrid::priceToIndex(
    Price price) const {
  const auto scaledPrice = price.scaledValue();
  if (scaledPrice < config_.minimumScaledPrice) {
    return std::unexpected("price is below the grid minimum");
  }
  if (scaledPrice > config_.maximumScaledPrice) {
    return std::unexpected("price is above the grid maximum");
  }
  const auto offset = scaledPrice - config_.minimumScaledPrice;
  // A price must land exactly on a configured tick; anything in between is
  // rejected rather than rounded to the nearest valid price.
  if (offset % config_.tickSize != 0) {
    return std::unexpected("price is not aligned with the grid tick size");
  }
  return static_cast<std::size_t>(offset / config_.tickSize);
}

Price PriceGrid::indexToPrice(std::size_t index) const {
  if (index >= levelCount_) [[unlikely]] {
    throw std::out_of_range("price grid index is out of range");
  }
  const auto scaledPrice = config_.minimumScaledPrice +
                           static_cast<std::int64_t>(index) * config_.tickSize;

  const auto price = Price::fromScaled(scaledPrice);
  if (!price) [[unlikely]] {
    throw std::logic_error("validated price grid produced an invalid price: " +
                           price.error());
  }

  return *price;
}

const PriceGridConfig& PriceGrid::config() const noexcept { return config_; }

std::size_t PriceGrid::validateAndCountLevels(const PriceGridConfig& config) {
  const auto require = [](bool condition, const char* message) {
    if (!condition) [[unlikely]] {
      throw std::invalid_argument(message);
    }
  };

  require(config.minimumScaledPrice >= 0, "minimum price cannot be negative");

  require(config.maximumScaledPrice >= config.minimumScaledPrice,
          "maximum price cannot be below minimum price");

  require(config.tickSize > 0, "tick size must be positive");

  const auto span = config.maximumScaledPrice - config.minimumScaledPrice;

  require(span % config.tickSize == 0,
          "price range must be divisible by tick size");

  // +1 because both the minimum and maximum price are valid, inclusive
  // endpoints - e.g. 3 ticks spanning $0.00-$0.03 gives 4 levels (0,1,2,3).
  const auto intervalCount = static_cast<std::uint64_t>(span / config.tickSize);
  const auto levelCount = intervalCount + 1;

  require(std::in_range<std::size_t>(levelCount),
          "price grid has too many levels");

  return static_cast<std::size_t>(levelCount);
}
