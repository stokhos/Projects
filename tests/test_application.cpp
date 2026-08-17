#include <gtest/gtest.h>

#include <cstdlib>
#include <ios>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

#include "Application.hpp"

namespace {

class FailingWriteBuffer : public std::streambuf {
 protected:
  std::streamsize xsputn(const char*, std::streamsize) override { return 0; }

  int_type overflow(int_type = traits_type::eof()) override {
    return traits_type::eof();
  }

  int sync() override { return 0; }
};

class FailingFlushBuffer : public std::stringbuf {
 protected:
  int sync() override { return -1; }
};

class FailingInputBuffer : public std::streambuf {
 public:
  explicit FailingInputBuffer(std::string data) : data_(std::move(data)) {
    setg(data_.data(), data_.data(), data_.data() + data_.size());
  }

 protected:
  int_type underflow() override {
    throw std::runtime_error("synthetic input failure");
  }

 private:
  std::string data_;
};

template <typename Function>
void expectIoFailure(Function&& function, std::string_view expectedContext) {
  try {
    std::forward<Function>(function)();
    FAIL() << "expected std::ios_base::failure";
  } catch (const std::ios_base::failure& error) {
    EXPECT_NE(std::string{error.what()}.find(expectedContext),
              std::string::npos)
        << error.what();
  } catch (const std::exception& error) {
    FAIL() << "unexpected exception: " << error.what();
  } catch (...) {
    FAIL() << "unexpected non-standard exception";
  }
}

}  // namespace

TEST(ApplicationTest, SeparatesProtocolOutputFromLineNumberedErrors) {
  std::istringstream input{
      "0,1,1,2,100\n"
      "BADMESSAGE\n"
      "0,2,0,1,100\n"
      "\n"
      "1,999\n"
      "0,3,0,1,100\n"};
  std::ostringstream output;
  std::ostringstream error;

  EXPECT_EQ(app::run(input, output, error), EXIT_SUCCESS);
  EXPECT_EQ(output.str(),
            "2,1,100\n3,2\n4,1,1\n"
            "2,1,100\n3,3\n3,1\n");
  EXPECT_EQ(error.str(),
            "ERROR [line 2]: Unknown message type: BADMESSAGE\n"
            "ERROR [line 4]: empty input line\n"
            "ERROR [line 5]: unknown order id\n");
}

TEST(ApplicationTest, AcceptsCrLfAndFinalLineWithoutNewline) {
  std::istringstream input{"0,10,1,1,100\r\nBADMESSAGE\r\n0,11,0,1,100"};
  std::ostringstream output;
  std::ostringstream error;

  EXPECT_EQ(app::run(input, output, error), EXIT_SUCCESS);
  EXPECT_EQ(output.str(), "2,1,100\n3,11\n3,10\n");
  EXPECT_EQ(error.str(), "ERROR [line 2]: Unknown message type: BADMESSAGE\n");
}

TEST(ApplicationTest, PrettyModeWritesAnOrganizedActivityTableToStderr) {
  std::istringstream input{
      "0,10,1,2,100\n"
      "BADMESSAGE\n"
      "0,20,0,3,105\n"};
  std::ostringstream output;
  std::ostringstream error;

  const app::RunOptions options{
      .outputFormat = app::OutputFormat::ActivityTable,
  };
  EXPECT_EQ(app::run(input, output, error, options), EXIT_SUCCESS);
  EXPECT_TRUE(output.str().empty());
  EXPECT_EQ(error.str(),
            R"(ORDER MATCHING ENGINE ACTIVITY
+----------+----------+----------------------+------+----------------------+----------------------+----------------------+----------------------------------+
|     LINE | EVENT    |             ORDER ID | SIDE |                PRICE |             QUANTITY |            REMAINING | RESULT                           |
+----------+----------+----------------------+------+----------------------+----------------------+----------------------+----------------------------------+
|        1 | ADD      |                   10 | SELL |                  100 |                    2 |                    - | REQUEST (input)                  |
|        2 | ERROR    |                    - | -    |                    - |                    - |                    - | REJECTED: Unknown message type:  |
|          |          |                      |      |                      |                      |                      | BADMESSAGE                       |
|        3 | ADD      |                   20 | BUY  |                  105 |                    3 |                    - | REQUEST (input)                  |
|        3 | TRADE    |                   20 | BUY  |                  100 |                    2 |                    1 | #1 EXECUTED (aggressor)          |
|        3 | FILL     |                   20 | BUY  |                  105 |                    2 |                    1 | #1 PARTIAL (aggressor)           |
|        3 | FILL     |                   10 | SELL |                  100 |                    2 |                    0 | #1 FULL (resting)                |
+----------+----------+----------------------+------+----------------------+----------------------+----------------------+----------------------------------+
)");
}

TEST(ApplicationTest, PrettyModeKeepsMaximumNumericValuesAligned) {
  std::istringstream input{
      "0,18446744073709551615,1,18446744073709551615,100\n"
      "0,1,0,1,922337203685477.5807\n"};
  std::ostringstream output;
  std::ostringstream error;
  const app::RunOptions options{
      .outputFormat = app::OutputFormat::ActivityTable,
  };

  EXPECT_EQ(app::run(input, output, error, options), EXIT_SUCCESS);
  EXPECT_TRUE(output.str().empty());
  EXPECT_NE(error.str().find("18446744073709551615"), std::string::npos);
  EXPECT_NE(error.str().find("922337203685477.5807"), std::string::npos);

  std::istringstream rendered{error.str()};
  std::string row;
  ASSERT_TRUE(static_cast<bool>(std::getline(rendered, row)));  // Title.
  ASSERT_TRUE(static_cast<bool>(std::getline(rendered, row)));
  const std::size_t tableWidth = row.size();
  while (std::getline(rendered, row)) {
    EXPECT_EQ(row.size(), tableWidth) << row;
  }
}

TEST(ApplicationTest, PrettyModeEscapesUntrustedDiagnosticText) {
  std::string inputText{"BAD|TYPE\tX\rY"};
  inputText.push_back('\x1b');
  inputText += "[2J\n";
  std::istringstream input{inputText};
  std::ostringstream output;
  std::ostringstream error;
  const app::RunOptions options{
      .outputFormat = app::OutputFormat::ActivityTable,
  };

  EXPECT_EQ(app::run(input, output, error, options), EXIT_SUCCESS);
  EXPECT_TRUE(output.str().empty());
  EXPECT_NE(error.str().find(R"(BAD\x7CTYPE\tX\rY\x1B[2J)"), std::string::npos);
  EXPECT_EQ(error.str().find('\t'), std::string::npos);
  EXPECT_EQ(error.str().find('\r'), std::string::npos);
  EXPECT_EQ(error.str().find('\x1b'), std::string::npos);
}

TEST(ApplicationTest, PrettyModeShowsRequestsBeforeEngineErrors) {
  std::istringstream input{
      "0,1,0,2,100\n"
      "0,1,1,1,101\n"
      "1,1\n"
      "1,1\n"};
  std::ostringstream output;
  std::ostringstream error;
  const app::RunOptions options{
      .outputFormat = app::OutputFormat::ActivityTable,
  };

  EXPECT_EQ(app::run(input, output, error, options), EXIT_SUCCESS);
  EXPECT_TRUE(output.str().empty());

  const std::string table = error.str();
  const std::size_t duplicateRequest = table.find("|        2 | ADD");
  const std::size_t duplicateError = table.find("|        2 | ERROR");
  const std::size_t successfulCancel = table.find("|        3 | CANCEL");
  const std::size_t unknownCancel = table.find("|        4 | CANCEL");
  const std::size_t unknownCancelError = table.find("|        4 | ERROR");

  ASSERT_NE(duplicateRequest, std::string::npos);
  ASSERT_NE(duplicateError, std::string::npos);
  ASSERT_NE(successfulCancel, std::string::npos);
  ASSERT_NE(unknownCancel, std::string::npos);
  ASSERT_NE(unknownCancelError, std::string::npos);
  EXPECT_LT(duplicateRequest, duplicateError);
  EXPECT_LT(duplicateError, successfulCancel);
  EXPECT_LT(successfulCancel, unknownCancel);
  EXPECT_LT(unknownCancel, unknownCancelError);
  EXPECT_NE(table.find("REJECTED: duplicate"), std::string::npos);
  EXPECT_NE(table.find("REJECTED: unknown order"), std::string::npos);
}

TEST(ApplicationTest, RejectsAnOverlongLineAndContinuesProcessing) {
  std::string inputText(app::kMaximumInputLineLength * 4, 'X');
  inputText += "\n0,20,1,1,100\n0,21,0,1,100\n";
  std::istringstream input{inputText};
  std::ostringstream output;
  std::ostringstream error;

  EXPECT_EQ(app::run(input, output, error), EXIT_SUCCESS);
  EXPECT_EQ(error.str(), "ERROR [line 1]: input line exceeds 1024 bytes\n");
  EXPECT_EQ(output.str(), "2,1,100\n3,21\n3,20\n");
}

TEST(ApplicationTest, AcceptsALineAtTheMaximumLength) {
  std::string inputText(app::kMaximumInputLineLength, ',');
  inputText += "\r\n";
  std::istringstream input{inputText};
  std::ostringstream output;
  std::ostringstream error;

  EXPECT_EQ(app::run(input, output, error), EXIT_SUCCESS);
  EXPECT_TRUE(output.str().empty());
  EXPECT_EQ(error.str(), "ERROR [line 1]: empty input line\n");
}

TEST(ApplicationTest, RejectsAnOverlongFinalLineWithoutNewline) {
  std::string inputText(app::kMaximumInputLineLength + 1, 'X');
  std::istringstream input{inputText};
  std::ostringstream output;
  std::ostringstream error;

  EXPECT_EQ(app::run(input, output, error), EXIT_SUCCESS);
  EXPECT_TRUE(output.str().empty());
  EXPECT_EQ(error.str(), "ERROR [line 1]: input line exceeds 1024 bytes\n");
}

TEST(ApplicationTest, ReportsAnUnderlyingInputFailure) {
  FailingInputBuffer inputBuffer{
      "0,25,1,1,100\n"
      "0,26,0,1,100\n"};
  std::istream input{&inputBuffer};
  std::ostringstream output;
  std::ostringstream error;

  expectIoFailure([&] { (void)app::run(input, output, error); },
                  "reading stdin");
  EXPECT_EQ(output.str(), "2,1,100\n3,26\n3,25\n");
  EXPECT_TRUE(error.str().empty());
}

TEST(ApplicationTest, ReportsProtocolOutputFailure) {
  std::istringstream input{"0,30,1,1,100\n0,31,0,1,100\n"};
  FailingWriteBuffer outputBuffer;
  std::ostream output{&outputBuffer};
  std::ostringstream error;

  expectIoFailure([&] { (void)app::run(input, output, error); },
                  "writing stdout");
}

TEST(ApplicationTest, ReportsDelayedProtocolFlushFailure) {
  std::istringstream input;
  FailingFlushBuffer outputBuffer;
  std::ostream output{&outputBuffer};
  std::ostringstream error;

  expectIoFailure([&] { (void)app::run(input, output, error); },
                  "flushing stdout");
}

TEST(ApplicationTest, ReportsDiagnosticOutputFailure) {
  std::istringstream input{"BADMESSAGE\n"};
  std::ostringstream output;
  FailingWriteBuffer errorBuffer;
  std::ostream error{&errorBuffer};

  expectIoFailure([&] { (void)app::run(input, output, error); },
                  "writing stderr");
}

TEST(ApplicationTest, ReportsDelayedDiagnosticFlushFailure) {
  std::istringstream input;
  std::ostringstream output;
  FailingFlushBuffer errorBuffer;
  std::ostream error{&errorBuffer};

  expectIoFailure([&] { (void)app::run(input, output, error); },
                  "flushing stderr");
}
