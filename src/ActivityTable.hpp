#pragma once

#include <cstdint>
#include <iosfwd>
#include <string_view>

#include "MatchingEngine.hpp"
#include "Message.hpp"

// Human-readable, aligned rendering of requests and match executions.
// This is deliberately an observer: it never duplicates or mutates book state.
class ActivityTable {
 public:
  explicit ActivityTable(std::ostream& stream) noexcept;

  void recordInput(std::uint64_t lineNumber, const InputMessage& message);
  void recordError(std::uint64_t lineNumber, std::string_view reason);
  void recordMatch(std::uint64_t lineNumber, const MatchExecution& execution);
  void finish();

 private:
  void ensureStarted();
  void writeRawLine(std::string_view line);
  void writePhysicalRow(std::string_view line, std::string_view event,
                        std::string_view orderId, std::string_view side,
                        std::string_view price, std::string_view quantity,
                        std::string_view remaining, std::string_view result);
  void writeRow(std::string_view line, std::string_view event,
                std::string_view orderId, std::string_view side,
                std::string_view price, std::string_view quantity,
                std::string_view remaining, std::string_view result);

  std::ostream& stream_;
  std::uint64_t nextTradeNumber_ = 1;
  bool started_ = false;
  bool finished_ = false;
};
