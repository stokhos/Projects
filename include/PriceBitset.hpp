#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

// Tracks which price-grid indexes currently have at least one resting order,
// packed one bit per index into 64-bit words. Lets the order book answer
// "what's the next occupied price?" by scanning whole words at a time
// instead of checking every index one by one.
class PriceBitset {
 public:
  explicit PriceBitset(std::size_t size)
      : words_(wordCount(size), Word{0}), size_(size) {}

  PriceBitset(const PriceBitset&) = default;
  PriceBitset& operator=(const PriceBitset&) = default;

  PriceBitset(PriceBitset&& other) noexcept
      : words_(std::move(other.words_)), size_(std::exchange(other.size_, 0)) {}

  PriceBitset& operator=(PriceBitset&& other) noexcept {
    if (this != &other) {
      words_ = std::move(other.words_);
      size_ = std::exchange(other.size_, 0);
    }
    return *this;
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }

  void set(std::size_t index) {
    const auto location = checkedLocation(index);
    words_[location.wordIndex] |= location.mask;
  }

  void clear(std::size_t index) {
    const auto location = checkedLocation(index);
    words_[location.wordIndex] &= ~location.mask;
  }

  [[nodiscard]] bool test(std::size_t index) const {
    const auto location = checkedLocation(index);
    return (words_[location.wordIndex] & location.mask) != 0;
  }

  // Finds the lowest set index greater than or equal to start. An out-of-range
  // start has no searchable indexes above it.
  [[nodiscard]] std::optional<std::size_t> findLowestSet(
      std::size_t start = 0) const noexcept {
    if (start >= size_) {
      return std::nullopt;
    }

    std::size_t wordIndex = wordIndexFor(start);
    const std::size_t bitIndex = bitIndexFor(start);

    // Mask off the bits below `start` in its own word so the scan only sees
    // candidates at or after the requested index.
    Word word = words_[wordIndex] & (~Word{0} << bitIndex);

    while (true) {
      if (word != 0) {
        // countr_zero finds the lowest set bit directly, so within a
        // nonzero word the answer is one instruction, not a bit-by-bit scan.
        const auto result = wordIndex * kBitsPerWord +
                            static_cast<std::size_t>(std::countr_zero(word));
        if (result < size_) {
          return result;
        }
        return std::nullopt;
      }
      ++wordIndex;

      if (wordIndex == words_.size()) {
        return std::nullopt;
      }
      word = words_[wordIndex];
    }
  }

  // Finds the highest set index less than or equal to start. An out-of-range
  // start is clamped to the final valid index.
  [[nodiscard]] std::optional<std::size_t> findHighestSet(
      std::size_t start) const noexcept {
    if (size_ == 0) {
      return std::nullopt;
    }

    if (start >= size_) {
      start = size_ - 1;
    }

    std::size_t wordIndex = wordIndexFor(start);
    const std::size_t bitIndex = bitIndexFor(start);

    // Mask off the bits above `start` in its own word (searching downward,
    // so anything past `start` doesn't count). The ternary avoids undefined
    // behavior from shifting a Word by a full kBitsPerWord when bitIndex is
    // the top bit.
    const Word initialMask = bitIndex == kBitsPerWord - 1
                                 ? ~Word{0}
                                 : (Word{1} << (bitIndex + 1)) - Word{1};

    Word word = words_[wordIndex] & initialMask;

    while (true) {
      if (word != 0) {
        // countl_zero finds the highest set bit directly, mirroring
        // findLowestSet's use of countr_zero above.
        const auto highestBit =
            kBitsPerWord - 1 - static_cast<std::size_t>(std::countl_zero(word));
        return wordIndex * kBitsPerWord + highestBit;
      }

      if (wordIndex == 0) {
        return std::nullopt;
      }
      --wordIndex;
      word = words_[wordIndex];
    }
  }

 private:
  using Word = std::uint64_t;
  static constexpr std::size_t kBitsPerWord = std::numeric_limits<Word>::digits;

  static_assert(kBitsPerWord == 64);

  struct Location {
    std::size_t wordIndex;
    Word mask;
  };

  [[nodiscard]] static constexpr std::size_t wordIndexFor(
      std::size_t index) noexcept {
    return index / kBitsPerWord;
  }

  [[nodiscard]] static constexpr std::size_t bitIndexFor(
      std::size_t index) noexcept {
    return index % kBitsPerWord;
  }

  [[nodiscard]] static constexpr Word bitMaskFor(std::size_t index) noexcept {
    return Word{1} << bitIndexFor(index);
  }

  [[nodiscard]] Location checkedLocation(std::size_t index) const {
    if (index >= size_) [[unlikely]] {
      throw std::out_of_range("PriceBitset index out of range");
    }
    return Location{.wordIndex = wordIndexFor(index),
                    .mask = bitMaskFor(index)};
  }

  [[nodiscard]] static constexpr std::size_t wordCount(
      std::size_t size) noexcept {
    return size / kBitsPerWord +
           static_cast<std::size_t>(size % kBitsPerWord != 0);
  }

  std::vector<Word> words_;
  std::size_t size_;
};
