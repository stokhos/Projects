#include "MatchingEngine.hpp"

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

MatchingEngine::MatchingEngine(OutputCallback onOutput, ErrorCallback onError,
                               PriceGridConfig config, MatchCallback onMatch)
    : orderBook_(config),
      onOutput_(std::move(onOutput)),
      onError_(std::move(onError)),
      onMatch_(std::move(onMatch)) {
  if (!onOutput_) [[unlikely]] {
    throw std::invalid_argument("output callback must not be empty");
  }

  if (!onError_) [[unlikely]] {
    throw std::invalid_argument("error callback must not be empty");
  }
}

void MatchingEngine::process(const InputMessage& message) {
  std::visit(
      [this](const auto& request) {
        using Request = std::remove_cvref_t<decltype(request)>;
        if constexpr (std::is_same_v<Request, AddOrderRequest>) {
          handleAdd(request);
        } else {
          handleCancel(request);
        }
      },
      message);
}

void MatchingEngine::handleCancel(const CancelOrderRequest& request) {
  if (!orderBook_.cancelOrder(request.orderId)) [[unlikely]] {
    onError_("unknown order id");
  }
}

Order MatchingEngine::makeOrder(const AddOrderRequest& request) noexcept {
  return Order{
      .id = request.orderId,
      .side = request.side,
      .quantity = request.quantity,
      .price = request.price,
  };
}

bool MatchingEngine::validateIncoming(const Order& order) {
  const auto validation = orderBook_.validateOrder(order);
  if (!validation) [[unlikely]] {
    onError_(validation.error());
    return false;
  }

  return true;
}

bool MatchingEngine::crosses(Side incomingSide, Price incomingPrice,
                             Price restingPrice) noexcept {
  switch (incomingSide) {
    case Side::Buy:
      return incomingPrice >= restingPrice;
    case Side::Sell:
      return incomingPrice <= restingPrice;
    [[unlikely]] case Side::Invalid:
      return false;
  }

  return false;
}

// A zero remaining quantity means fully filled; anything else is a partial
// fill still resting in (or about to rest in) the book. Both the incoming
// and resting order can be fully filled by the same trade (equal
// quantities), so this is checked independently for each side.
void MatchingEngine::emitFill(OrderId orderId, Quantity remainingQuantity) {
  if (remainingQuantity == 0) {
    onOutput_(OutputMessage{OrderFullyFilled{.orderId = orderId}});
    return;
  }

  onOutput_(OutputMessage{OrderPartiallyFilled{
      .orderId = orderId,
      .remainingQuantity = remainingQuantity,
  }});
}

// Attempts one match against the best resting order on the opposite side.
// Returns false (no book change) if that side is empty or its best price
// doesn't cross the incoming order - the caller loops this until either the
// incoming order is exhausted or nothing left crosses.
bool MatchingEngine::tryMatchBestOrder(Order& incoming, Side restingSide) {
  if (orderBook_.empty(restingSide)) {
    return false;
  }

  const Order& resting = orderBook_.frontOrder(restingSide);
  if (!crosses(incoming.side, incoming.price, resting.price)) {
    return false;
  }

  // Removing a filled resting order invalidates its reference, so retain the
  // values needed for output before changing the book.
  const OrderId restingId = resting.id;
  const Side restingOrderSide = resting.side;
  const Price tradePrice = resting.price;
  const Quantity tradeQuantity = std::min(incoming.quantity, resting.quantity);

  incoming.quantity -= tradeQuantity;
  const Quantity restingRemaining =
      orderBook_.reduceFrontQuantity(restingSide, tradeQuantity);

  if (restingRemaining == 0) {
    orderBook_.removeFilledFront(restingSide, tradePrice);
  }

  // Book mutation happens above, output below: a match is not atomic. If an
  // output write fails partway through, prior matches already stand (and
  // were already reported) while this one may be left partially reported.
  // See docs/design-and-performance.md for the full discussion.
  onOutput_(OutputMessage{TradeEvent{
      .quantity = tradeQuantity,
      .price = tradePrice,
  }});
  // Required output order for every match: TradeEvent, then the incoming
  // (aggressive) order's fill, then the resting order's fill.
  emitFill(incoming.id, incoming.quantity);
  emitFill(restingId, restingRemaining);

  // Diagnostics observe an already-delivered execution. An observer failure
  // therefore cannot suppress the three required protocol messages.
  if (onMatch_) {
    onMatch_(MatchExecution{
        .aggressorOrderId = incoming.id,
        .restingOrderId = restingId,
        .aggressorLimitPrice = incoming.price,
        .restingLimitPrice = tradePrice,
        .executionPrice = tradePrice,
        .executedQuantity = tradeQuantity,
        .aggressorRemaining = incoming.quantity,
        .restingRemaining = restingRemaining,
        .aggressorSide = incoming.side,
        .restingSide = restingOrderSide,
    });
  }

  return true;
}

// Matches the incoming order against the opposite side, best price then
// oldest order first, one resting order per iteration, until either it's
// fully filled or nothing left on that side crosses. Any leftover quantity
// becomes a new resting order.
void MatchingEngine::handleAdd(AddOrderRequest request) {
  Order incoming = makeOrder(request);
  if (!validateIncoming(incoming)) [[unlikely]] {
    return;
  }

  const Side restingSide = opposite(incoming.side);
  while (incoming.quantity != 0 && tryMatchBestOrder(incoming, restingSide)) {
  }

  if (incoming.quantity != 0) {
    orderBook_.addOrder(incoming);
  }
}
