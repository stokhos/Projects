#include "Application.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <ios>
#include <istream>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "ActivityTable.hpp"
#include "MatchingEngine.hpp"
#include "Message.hpp"

namespace {

[[noreturn]] void throwIoFailure(const char* reason) {
  throw std::ios_base::failure(reason);
}

void writeLine(std::ostream& stream, std::string_view text,
               const char* failureReason) {
  stream << text << '\n';
  if (!stream) {
    throwIoFailure(failureReason);
  }
}

void writeError(std::ostream& error, std::uint64_t lineNumber,
                std::string_view reason) {
  writeLine(error, std::format("ERROR [line {}]: {}", lineNumber, reason),
            "failed while writing stderr");
}

void flush(std::ostream& stream, const char* failureReason) {
  stream.flush();
  if (!stream) {
    throwIoFailure(failureReason);
  }
}

[[nodiscard]] bool inputFailed(const std::istream& input) noexcept {
  return input.bad() || (input.fail() && !input.eof());
}

enum class ReadLineResult {
  Line,     // `line` holds one complete, in-bounds line.
  TooLong,  // The line exceeded the buffer; `line` is empty, caller should
            // report an error and continue at the next physical line.
  End,      // No more input.
};

// Reads one line into a fixed-size stack buffer rather than an unbounded
// std::getline into a std::string, so a hostile or malformed input stream
// can't force an unbounded allocation.
ReadLineResult readLine(std::istream& input, std::string& line) {
  if (input.eof()) {
    return ReadLineResult::End;
  }
  if (inputFailed(input)) {
    throwIoFailure("failed while reading stdin");
  }

  // Two spare characters accommodate CRLF and distinguish an exactly-at-limit
  // line from an overlong one without unbounded allocation.
  std::array<char, app::kMaximumInputLineLength + 3> buffer;
  input.getline(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  const std::streamsize extracted = input.gcount();

  if (input.bad()) {
    throwIoFailure("failed while reading stdin");
  }

  if (input.fail() && !input.eof()) {
    // The fixed buffer filled before the delimiter. Clear only failbit, then
    // discard the remainder so the next read begins at a new physical line.
    input.clear(input.rdstate() & ~std::ios::failbit);
    input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    if (inputFailed(input)) {
      throwIoFailure("failed while reading stdin");
    }
    line.clear();
    return ReadLineResult::TooLong;
  }

  if (extracted == 0 && input.eof()) {
    return ReadLineResult::End;
  }

  // gcount includes a consumed delimiter, but never stores it in the buffer.
  const std::streamsize contentLength = input.eof() ? extracted : extracted - 1;
  line.assign(buffer.data(), static_cast<std::size_t>(contentLength));
  return ReadLineResult::Line;
}

}  // namespace

// Reads one request per line from `input`, runs it through a MatchingEngine,
// and writes results to `output` (protocol mode) or `error` (activity-table
// mode, so recoverable errors stay on stderr either way). A malformed line
// is reported and skipped; only a stream-level failure is fatal.
int app::run(std::istream& input, std::ostream& output, std::ostream& error,
             RunOptions options) {
  std::uint64_t lineNumber = 0;
  const bool showActivityTable =
      options.outputFormat == OutputFormat::ActivityTable;
  ActivityTable activityTable{error};

  const auto reportError = [&error, &activityTable, &lineNumber,
                            showActivityTable](std::string_view reason) {
    if (showActivityTable) {
      activityTable.recordError(lineNumber, reason);
    } else {
      writeError(error, lineNumber, reason);
    }
  };

  MatchingEngine::MatchCallback onMatch;
  if (showActivityTable) {
    onMatch = [&activityTable, &lineNumber](const MatchExecution& execution) {
      activityTable.recordMatch(lineNumber, execution);
    };
  }

  MatchingEngine::OutputCallback onOutput;
  if (showActivityTable) {
    // Activity-table mode reports matches via onMatch (below) instead; the
    // numeric wire protocol is suppressed so stdout stays free for the
    // human-readable table if the caller chooses to also use it.
    onOutput = [](const OutputMessage&) {};
  } else {
    onOutput = [&output](const OutputMessage& message) {
      writeLine(output, formatOutputMessage(message),
                "failed while writing stdout");
    };
  }

  MatchingEngine engine(std::move(onOutput), reportError, {},
                        std::move(onMatch));

  std::string line;
  // A maximum-length CRLF line temporarily includes the trailing '\r'.
  line.reserve(kMaximumInputLineLength + 1);

  while (true) {
    const ReadLineResult readResult = readLine(input, line);
    if (readResult == ReadLineResult::End) {
      break;
    }

    ++lineNumber;

    if (readResult == ReadLineResult::TooLong) {
      reportError(
          std::format("input line exceeds {} bytes", kMaximumInputLineLength));
      continue;
    }

    // std::getline removes '\n' but leaves the '\r' from Windows CRLF input.
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line.size() > kMaximumInputLineLength) {
      reportError(
          std::format("input line exceeds {} bytes", kMaximumInputLineLength));
      continue;
    }

    const auto message = parseInputLine(line);
    if (!message) {
      reportError(message.error());
      continue;
    }

    if (showActivityTable) {
      activityTable.recordInput(lineNumber, *message);
    }
    engine.process(*message);
  }

  if (inputFailed(input)) {
    throwIoFailure("failed while reading stdin");
  }

  activityTable.finish();

  // Surface delayed buffered-write failures before reporting success.
  if (!showActivityTable) {
    flush(output, "failed while flushing stdout");
  }
  flush(error, "failed while flushing stderr");
  return EXIT_SUCCESS;
}
