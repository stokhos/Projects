#pragma once

#include <cstddef>
#include <expected>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Order.hpp"
#include "PriceBitset.hpp"
#include "PriceGrid.hpp"

// Holds all live orders for one instrument and the indexes used to access
// them: a FIFO list per price level, an occupancy bitset per side, a cached
// best price per side, and a hash map from order id straight to its list
// location so cancellation never has to scan the book.
class OrderBook {
 public:
  explicit OrderBook(PriceGridConfig config = {});

  OrderBook(const OrderBook&) = delete;
  OrderBook& operator=(const OrderBook&) = delete;

  void addOrder(const Order& order);

  [[nodiscard]] bool cancelOrder(OrderId orderId);
  [[nodiscard]] bool hasOrder(OrderId orderId) const;

  [[nodiscard]] bool empty(Side side) const;
  [[nodiscard]] Price bestPrice(Side side) const;
  [[nodiscard]] const Order& frontOrder(Side side) const;

  // Decrease only the mutable quantity of the best FIFO-front order.
  // Returns that order's remaining quantity.
  [[nodiscard]] Quantity reduceFrontQuantity(Side side, Quantity amount);
  [[nodiscard]] std::expected<void, std::string> validateOrder(
      const Order& order) const;

  void removeFilledFront(Side side, Price price);

 private:
  // A std::list gives O(1) insertion at the tail and O(1) erasure from any
  // position via a stored iterator, without invalidating other orders'
  // iterators - both are needed for FIFO matching and direct cancellation.
  using PriceLevel = std::list<Order>;

  struct SideBook {
    explicit SideBook(std::size_t levelCount)
        : levels(levelCount), occupiedPrices(levelCount) {}

    // One FIFO queue per price-grid index. Indexed directly rather than
    // keyed in a tree, trading memory for the whole configured range up
    // front in exchange for O(1) access to any price level.
    std::vector<PriceLevel> levels;
    PriceBitset occupiedPrices;
    // Cached so "what's the best price on this side" is a field read
    // instead of a search; only recomputed when it empties (see
    // eraseLocatedOrder).
    std::optional<std::size_t> bestIndex;
    std::size_t orderCount{0};
  };

  // Everything needed to erase a live order in O(1): which side/price array
  // it's in, and its exact position within that price level's list.
  struct OrderLocation {
    Side side;
    std::size_t priceIndex;
    PriceLevel::iterator iterator;
  };

  using OrderLocationMap = std::unordered_map<OrderId, OrderLocation>;

  [[nodiscard]] SideBook& bookFor(Side side);
  [[nodiscard]] const SideBook& bookFor(Side side) const;

  [[nodiscard]] std::optional<std::size_t> bestPriceIndexFor(Side side) const;

  [[nodiscard]] PriceLevel& bestLevelFor(Side side);
  [[nodiscard]] const PriceLevel& bestLevelFor(Side side) const;

  void eraseLocatedOrder(OrderLocationMap::iterator locationIt);

  PriceGrid priceGrid_;
  SideBook buys_;
  SideBook sells_;
  OrderLocationMap orderLocations_;
};
