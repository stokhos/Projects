#pragma once

#include <cstddef>
#include <iosfwd>

namespace app {

inline constexpr std::size_t kMaximumInputLineLength = 1'024;

enum class OutputFormat {
  Protocol,
  ActivityTable,
};

struct RunOptions {
  OutputFormat outputFormat = OutputFormat::Protocol;
};

// Runs one matching-engine session using the supplied streams. Protocol
// messages are written to output by default. ActivityTable mode suppresses
// protocol output and writes one human-readable trace to error so recoverable
// errors remain on stderr. Fatal failures reach main's exception boundary.
int run(std::istream& input, std::ostream& output, std::ostream& error,
        RunOptions options = {});

}  // namespace app
