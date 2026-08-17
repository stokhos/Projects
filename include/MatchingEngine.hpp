#pragma once

#include <functional>
#include <string_view>

#include "Message.hpp"
#include "OrderBook.hpp"

// Rich, non-wire execution context for diagnostics and observability. The
// protocol messages remain intentionally minimal and are emitted separately.
// Field order is deliberate: grouping the two 1-byte Side fields together
// (instead of each splitting a run of 8-byte fields) shrinks this from 80 to
// 72 bytes by needing one alignment gap instead of two.
struct MatchExecution {
  OrderId aggressorOrderId = 0;
  OrderId restingOrderId = 0;
  Price aggressorLimitPrice{};
  Price restingLimitPrice{};
  Price executionPrice{};
  Quantity executedQuantity = 0;
  Quantity aggressorRemaining = 0;
  Quantity restingRemaining = 0;
  Side aggressorSide = Side::Invalid;
  Side restingSide = Side::Invalid;
};

// An incoming order matches resting orders on the opposite side, best price
// first, oldest order first at a given price, trading the smaller of the
// two quantities each time, until either the incoming order is exhausted or
// nothing left crosses. Leftover quantity rests in the book.
//
// For every matched pair, emit in this exact order: TradeEvent, the
// incoming order's fill message, then the resting order's fill message —
// emitted as each pair matches, not batched at the end.
class MatchingEngine {
 public:
  using OutputCallback = std::function<void(const OutputMessage&)>;
  using ErrorCallback = std::function<void(std::string_view)>;
  using MatchCallback = std::function<void(const MatchExecution&)>;

  explicit MatchingEngine(OutputCallback onOutput, ErrorCallback onError,
                          PriceGridConfig config = {},
                          MatchCallback onMatch = {});

  ~MatchingEngine() = default;
  MatchingEngine(const MatchingEngine&) = delete;
  MatchingEngine& operator=(const MatchingEngine&) = delete;
  MatchingEngine(MatchingEngine&&) = delete;
  MatchingEngine& operator=(MatchingEngine&&) = delete;

  void process(const InputMessage& message);

 private:
  void handleAdd(AddOrderRequest request);
  void handleCancel(const CancelOrderRequest& request);

  [[nodiscard]] static Order makeOrder(const AddOrderRequest& request) noexcept;
  [[nodiscard]] bool validateIncoming(const Order& order);
  [[nodiscard]] bool tryMatchBestOrder(Order& incoming, Side restingSide);

  [[nodiscard]] static bool crosses(Side incomingSide, Price incomingPrice,
                                    Price restingPrice) noexcept;
  void emitFill(OrderId orderId, Quantity remainingQuantity);

  OrderBook orderBook_;
  OutputCallback onOutput_;
  ErrorCallback onError_;
  MatchCallback onMatch_;
};
