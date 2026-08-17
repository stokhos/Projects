#include "OrderBook.hpp"

#include <stdexcept>
#include <string>
#include <utility>

const OrderBook::SideBook& OrderBook::bookFor(Side side) const {
  switch (side) {
    case Side::Buy:
      return buys_;
    case Side::Sell:
      return sells_;
    [[unlikely]] case Side::Invalid:
      throw std::invalid_argument("invalid order side");
  }

  throw std::invalid_argument("invalid order side");
}

OrderBook::SideBook& OrderBook::bookFor(Side side) {
  return const_cast<SideBook&>(std::as_const(*this).bookFor(side));
}

[[nodiscard]] std::optional<std::size_t> OrderBook::bestPriceIndexFor(
    Side side) const {
  return bookFor(side).bestIndex;
}

const OrderBook::PriceLevel& OrderBook::bestLevelFor(Side side) const {
  const auto& sideBook = bookFor(side);
  if (!sideBook.bestIndex) [[unlikely]] {
    throw std::logic_error("requested side has no best price level");
  }

  const auto& level = sideBook.levels[*sideBook.bestIndex];
  if (level.empty()) [[unlikely]] {
    throw std::logic_error("occupied price level is unexpectedly empty");
  }

  return level;
}

OrderBook::PriceLevel& OrderBook::bestLevelFor(Side side) {
  return const_cast<PriceLevel&>(std::as_const(*this).bestLevelFor(side));
}

OrderBook::OrderBook(PriceGridConfig config)
    : priceGrid_(config),
      buys_(priceGrid_.levelCount()),
      sells_(priceGrid_.levelCount()) {}

// Inserts a resting order at the back of its price level's queue (FIFO -
// time priority), then indexes it so cancelOrder/removeFilledFront can find
// it in O(1) later.
void OrderBook::addOrder(const Order& order) {
  const auto validation = validateOrder(order);
  if (!validation) [[unlikely]] {
    throw std::invalid_argument(validation.error());
  }

  auto& sideBook = bookFor(order.side);

  const auto indexResult = priceGrid_.priceToIndex(order.price);
  if (!indexResult) [[unlikely]] {
    throw std::invalid_argument(indexResult.error());
  }

  const std::size_t priceIndex = *indexResult;
  auto& level = sideBook.levels[priceIndex];
  const bool wasEmpty = level.empty();

  const auto orderIterator = level.insert(level.end(), order);

  try {
    const bool inserted = orderLocations_
                              .emplace(order.id,
                                       OrderLocation{
                                           .side = order.side,
                                           .priceIndex = priceIndex,
                                           .iterator = orderIterator,
                                       })
                              .second;
    if (!inserted) {
      throw std::invalid_argument("duplicate order id");
    }
  } catch (...) {
    // If the locator insert fails (duplicate id or allocation failure),
    // undo the list insertion above so the book doesn't retain an
    // unreachable order with no locator entry.
    level.erase(orderIterator);
    throw;
  }

  if (wasEmpty) {
    sideBook.occupiedPrices.set(priceIndex);
  }

  // A newly added order can only ever *become* the new best by direct
  // comparison against the current best - it never needs the bitset scan
  // that removal (below) sometimes requires.
  if (!sideBook.bestIndex ||
      (order.side == Side::Buy ? priceIndex > *sideBook.bestIndex
                               : priceIndex < *sideBook.bestIndex)) {
    sideBook.bestIndex = priceIndex;
  }

  ++sideBook.orderCount;
}

// Shared removal path for both a canceled order and a fully filled order -
// deliberately the same code either way, so cancellation isn't a
// second-tier operation with weaker guarantees than a fill.
void OrderBook::eraseLocatedOrder(OrderLocationMap::iterator locationIt) {
  const OrderLocation location = locationIt->second;
  auto& sideBook = bookFor(location.side);
  auto& level = sideBook.levels[location.priceIndex];

  // O(1): erasing via a stored list iterator, no scan needed.
  level.erase(location.iterator);
  --sideBook.orderCount;

  if (level.empty()) {
    sideBook.occupiedPrices.clear(location.priceIndex);

    if (sideBook.bestIndex && *sideBook.bestIndex == location.priceIndex) {
      if (sideBook.orderCount == 0) {
        sideBook.bestIndex.reset();
      } else if (location.side == Side::Buy) {
        // Buys want the highest occupied price; search restarts from the
        // top of the whole grid rather than from beside the emptied level,
        // so its cost depends on where the next occupied price sits in the
        // configured range, not on distance from the level just vacated.
        sideBook.bestIndex =
            sideBook.occupiedPrices.findHighestSet(priceGrid_.levelCount() - 1);
      } else {
        // Sells want the lowest occupied price; same reasoning, from the
        // bottom of the grid.
        sideBook.bestIndex = sideBook.occupiedPrices.findLowestSet();
      }
    }
  }

  orderLocations_.erase(locationIt);
}

bool OrderBook::cancelOrder(OrderId orderId) {
  const auto locationIt = orderLocations_.find(orderId);
  if (locationIt == orderLocations_.end()) [[unlikely]] {
    return false;
  }

  eraseLocatedOrder(locationIt);
  return true;
}

bool OrderBook::hasOrder(OrderId orderId) const {
  return orderLocations_.contains(orderId);
}

bool OrderBook::empty(Side side) const { return bookFor(side).orderCount == 0; }

Price OrderBook::bestPrice(Side side) const {
  const auto priceIndex = bestPriceIndexFor(side);
  if (!priceIndex) [[unlikely]] {
    throw std::logic_error("bestPrice called on an empty side");
  }

  return priceGrid_.indexToPrice(*priceIndex);
}

const Order& OrderBook::frontOrder(Side side) const {
  return bestLevelFor(side).front();
}

Quantity OrderBook::reduceFrontQuantity(Side side, Quantity amount) {
  auto& order = bestLevelFor(side).front();

  if (amount == 0) [[unlikely]] {
    throw std::invalid_argument("fill quantity must be nonzero");
  }

  if (amount > order.quantity) [[unlikely]] {
    throw std::invalid_argument(
        "fill quantity exceeds the resting order quantity");
  }

  order.quantity -= amount;
  return order.quantity;
}

// Removes the FIFO-front order of the given side's best price level after
// it has been reduced to zero quantity by a match. The extra checks below
// exist because this is only ever called right after a match, on an order
// the matching engine already believes is the best-price front - if that
// belief is wrong, it means a book invariant broke, so this fails loudly
// rather than silently removing the wrong order.
void OrderBook::removeFilledFront(Side side, Price price) {
  auto& sideBook = bookFor(side);

  const auto indexResult = priceGrid_.priceToIndex(price);
  if (!indexResult) [[unlikely]] {
    throw std::invalid_argument(indexResult.error());
  }

  const std::size_t priceIndex = *indexResult;
  if (!sideBook.bestIndex || *sideBook.bestIndex != priceIndex) [[unlikely]] {
    throw std::logic_error(
        "removeFilledFront price is not the side's best price");
  }

  auto& level = sideBook.levels[priceIndex];
  if (level.empty()) [[unlikely]] {
    throw std::logic_error("removeFilledFront called on an empty price level");
  }

  const auto orderIterator = level.begin();
  const auto& order = *orderIterator;
  if (order.quantity != 0) [[unlikely]] {
    throw std::logic_error("removeFilledFront called for an unfilled order");
  }

  const auto locationIt = orderLocations_.find(order.id);
  if (locationIt == orderLocations_.end()) [[unlikely]] {
    throw std::logic_error("filled front order has no locator");
  }

  const auto& location = locationIt->second;
  if (location.side != side || location.priceIndex != priceIndex ||
      location.iterator != orderIterator) [[unlikely]] {
    throw std::logic_error("filled front order has an inconsistent locator");
  }

  eraseLocatedOrder(locationIt);
}

std::expected<void, std::string> OrderBook::validateOrder(
    const Order& order) const {
  if (order.id == 0) [[unlikely]] {
    return std::unexpected("order id must be nonzero");
  }

  if (order.side != Side::Buy && order.side != Side::Sell) [[unlikely]] {
    return std::unexpected("invalid order side");
  }

  if (order.quantity == 0) [[unlikely]] {
    return std::unexpected("order quantity must be nonzero");
  }

  if (order.price.scaledValue() == 0) [[unlikely]] {
    return std::unexpected("price must be positive");
  }

  const auto priceIndex = priceGrid_.priceToIndex(order.price);
  if (!priceIndex) [[unlikely]] {
    return std::unexpected(priceIndex.error());
  }

  if (orderLocations_.contains(order.id)) [[unlikely]] {
    return std::unexpected("duplicate order id");
  }

  return {};
}
