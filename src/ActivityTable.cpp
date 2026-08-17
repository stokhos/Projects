#include "ActivityTable.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <ios>
#include <ostream>
#include <string>
#include <type_traits>
#include <variant>

namespace {

constexpr std::size_t kResultWidth = 32;
constexpr std::string_view kBorder =
    "+----------"
    "+----------"
    "+----------------------"
    "+------"
    "+----------------------"
    "+----------------------"
    "+----------------------"
    "+----------------------------------+";

[[nodiscard]] std::string sanitize(std::string_view text) {
  constexpr char kHexDigits[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(text.size());

  for (const char rawCharacter : text) {
    const auto character = static_cast<unsigned char>(rawCharacter);
    switch (character) {
      case '\\':
        result += "\\\\";
        break;
      case '|':
        result += "\\x7C";
        break;
      case '\t':
        result += "\\t";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      default:
        if (character >= 0x20U && character <= 0x7EU) {
          result.push_back(static_cast<char>(character));
        } else {
          result += "\\x";
          result.push_back(kHexDigits[(character >> 4U) & 0x0FU]);
          result.push_back(kHexDigits[character & 0x0FU]);
        }
    }
  }
  return result;
}

[[nodiscard]] constexpr std::string_view sideName(Side side) noexcept {
  switch (side) {
    case Side::Buy:
      return "BUY";
    case Side::Sell:
      return "SELL";
    case Side::Invalid:
      return "-";
  }
  return "-";
}

[[nodiscard]] constexpr std::string_view fillStatus(
    Quantity remaining) noexcept {
  return remaining == 0 ? "FULL" : "PARTIAL";
}

}  // namespace

ActivityTable::ActivityTable(std::ostream& stream) noexcept : stream_(stream) {}

void ActivityTable::writeRawLine(std::string_view line) {
  stream_ << line << '\n';
  if (!stream_) {
    throw std::ios_base::failure("failed while writing stderr");
  }
}

void ActivityTable::writePhysicalRow(
    std::string_view line, std::string_view event, std::string_view orderId,
    std::string_view side, std::string_view price, std::string_view quantity,
    std::string_view remaining, std::string_view result) {
  writeRawLine(std::format(
      "| {:>8} | {:<8} | {:>20} | {:<4} | {:>20} | {:>20} | {:>20} | "
      "{:<32} |",
      line, event, orderId, side, price, quantity, remaining, result));
}

void ActivityTable::ensureStarted() {
  if (started_) {
    return;
  }

  writeRawLine("ORDER MATCHING ENGINE ACTIVITY");
  writeRawLine(kBorder);
  writePhysicalRow("LINE", "EVENT", "ORDER ID", "SIDE", "PRICE", "QUANTITY",
                   "REMAINING", "RESULT");
  writeRawLine(kBorder);
  started_ = true;
}

void ActivityTable::writeRow(std::string_view line, std::string_view event,
                             std::string_view orderId, std::string_view side,
                             std::string_view price, std::string_view quantity,
                             std::string_view remaining,
                             std::string_view result) {
  ensureStarted();

  if (result.empty()) {
    result = "-";
  }
  const std::string sanitizedResult = sanitize(result);
  result = sanitizedResult;

  bool firstLine = true;
  do {
    std::size_t chunkLength = std::min(kResultWidth, result.size());
    if (chunkLength < result.size()) {
      const std::size_t space = result.rfind(' ', chunkLength);
      if (space != std::string_view::npos && space != 0) {
        chunkLength = space;
      }
    }

    const std::string_view chunk = result.substr(0, chunkLength);
    writePhysicalRow(firstLine ? line : "", firstLine ? event : "",
                     firstLine ? orderId : "", firstLine ? side : "",
                     firstLine ? price : "", firstLine ? quantity : "",
                     firstLine ? remaining : "", chunk);

    result.remove_prefix(chunkLength);
    while (!result.empty() && result.front() == ' ') {
      result.remove_prefix(1);
    }
    firstLine = false;
  } while (!result.empty());
}

void ActivityTable::recordInput(std::uint64_t lineNumber,
                                const InputMessage& message) {
  std::visit(
      [this, lineNumber](const auto& request) {
        using Request = std::remove_cvref_t<decltype(request)>;
        if constexpr (std::is_same_v<Request, AddOrderRequest>) {
          writeRow(std::format("{}", lineNumber), "ADD",
                   std::format("{}", request.orderId), sideName(request.side),
                   request.price.toString(),
                   std::format("{}", request.quantity), "-", "REQUEST (input)");
        } else {
          writeRow(std::format("{}", lineNumber), "CANCEL",
                   std::format("{}", request.orderId), "-", "-", "-", "-",
                   "REQUEST (input)");
        }
      },
      message);
}

void ActivityTable::recordError(std::uint64_t lineNumber,
                                std::string_view reason) {
  writeRow(std::format("{}", lineNumber), "ERROR", "-", "-", "-", "-", "-",
           std::format("REJECTED: {}", reason));
}

void ActivityTable::recordMatch(std::uint64_t lineNumber,
                                const MatchExecution& execution) {
  const std::string line = std::format("{}", lineNumber);
  const std::string tradeNumber = std::format("{}", nextTradeNumber_++);
  const std::string quantity = std::format("{}", execution.executedQuantity);

  writeRow(line, "TRADE", std::format("{}", execution.aggressorOrderId),
           sideName(execution.aggressorSide),
           execution.executionPrice.toString(), quantity,
           std::format("{}", execution.aggressorRemaining),
           std::format("#{} EXECUTED (aggressor)", tradeNumber));

  writeRow(line, "FILL", std::format("{}", execution.aggressorOrderId),
           sideName(execution.aggressorSide),
           execution.aggressorLimitPrice.toString(), quantity,
           std::format("{}", execution.aggressorRemaining),
           std::format("#{} {} (aggressor)", tradeNumber,
                       fillStatus(execution.aggressorRemaining)));

  writeRow(line, "FILL", std::format("{}", execution.restingOrderId),
           sideName(execution.restingSide),
           execution.restingLimitPrice.toString(), quantity,
           std::format("{}", execution.restingRemaining),
           std::format("#{} {} (resting)", tradeNumber,
                       fillStatus(execution.restingRemaining)));
}

void ActivityTable::finish() {
  if (!started_ || finished_) {
    return;
  }
  writeRawLine(kBorder);
  finished_ = true;
}
