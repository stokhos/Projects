#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <ios>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string_view>

#include "Application.hpp"

namespace {

// Decouples C++ streams from C stdio and removes the automatic flush-before-
// read tie, which would otherwise force a stdout/stderr flush before every
// stdin read - meaningful overhead on the input-per-line hot path.
void configureProcessIo() {
  std::ios_base::sync_with_stdio(false);
  std::cin.tie(nullptr);
  std::cerr.tie(nullptr);

  // Convert a closed Linux output pipe into an I/O error that can pass through
  // the normal fatal-error boundary.
#ifdef SIGPIPE
  if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    throw std::runtime_error("failed to configure broken-pipe handling");
  }
#endif
}

int reportFatal(const char* summary, const char* detail = nullptr) noexcept {
  if (detail == nullptr) {
    std::fprintf(stderr, "FATAL: %s\n", summary);
  } else {
    std::fprintf(stderr, "FATAL: %s: %s\n", summary, detail);
  }
  return EXIT_FAILURE;
}

}  // namespace

// The final exception boundary: nothing below main is allowed to let an
// exception escape the process, so every failure mode - bad input aside,
// which is handled and reported without throwing - ends up here as one
// clearly labeled FATAL line on stderr and a nonzero exit code.
int main(int argc, char* argv[]) noexcept {
  try {
    configureProcessIo();

    app::RunOptions options;
    if (argc == 2 && std::string_view{argv[1]} == "--pretty") {
      options.outputFormat = app::OutputFormat::ActivityTable;
    } else if (argc != 1) {
      return reportFatal("invalid arguments", "usage: match_engine [--pretty]");
    }

    return app::run(std::cin, std::cout, std::cerr, options);
  } catch (const std::bad_alloc&) {
    return reportFatal("insufficient memory");
  } catch (const std::ios_base::failure& error) {
    return reportFatal("I/O failure", error.what());
  } catch (const std::exception& error) {
    return reportFatal("unexpected failure", error.what());
  } catch (...) {
    return reportFatal("unknown unexpected failure");
  }
}
