// Merges two redundant UDP feeds (A and B) carrying identical, sequence-
// numbered traffic into a single gap-free, strictly ordered stream. Packets
// may arrive out of order or duplicated across either feed; this assumes
// logical reliability, meaning at least one copy of every sequence number
// eventually arrives across the two feeds combined.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <print>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

class Sequencer
{
  private:
    // The feed sequence starts at 1, so the first expected message is sequence 1.
    std::uint64_t next_seq_ = 1;

  public:
    [[nodiscard]] std::uint64_t getNextSeq() const { return next_seq_; }
    [[nodiscard]] std::uint64_t incrementSeq()
    {
        next_seq_++;
        return next_seq_;
    }
};

class UdpBuffer
{
  public:
    static constexpr std::size_t Capacity = 1024;
    static constexpr std::size_t MaxPayloadSize = 2048;

    struct PacketView
    {
        char feed;
        std::span<const char> payload;
    };

    struct PacketInput
    {
        char feed;
        std::uint64_t seq;
        const char *data;
        std::size_t len;
    };

    enum class AddResult : u_int8_t
    {
        stored,
        duplicate,
        out_of_window,
        payload_too_large,
        invalid_data,
        slot_collision
    };

    UdpBuffer() : payload_storage_(Capacity * MaxPayloadSize) {}

    UdpBuffer(const UdpBuffer &) = delete;
    UdpBuffer &operator=(const UdpBuffer &) = delete;
    UdpBuffer(UdpBuffer &&) = default;
    UdpBuffer &operator=(UdpBuffer &&) = default;

    [[nodiscard]] AddResult addPacket(std::uint64_t next_expected,
                                      const PacketInput &packet) noexcept
    {
        if (packet.data == nullptr && packet.len != 0)
        {
            return AddResult::invalid_data;
        }
        if (packet.len > MaxPayloadSize)
        {
            return AddResult::payload_too_large;
        }
        const auto seq = packet.seq;
        if (seq <= next_expected || seq - next_expected > static_cast<std::uint64_t>(Capacity))
        {
            return AddResult::out_of_window;
        }

        const auto index = indexFor(seq);
        auto &slot = slots_[index];
        if (slot.occupied)
        {
            return slot.seq == seq ? AddResult::duplicate : AddResult::slot_collision;
        }

        if (packet.len != 0)
        {
            std::memcpy(payloadData(index), packet.data, packet.len);
        }
        slot.seq = seq;
        slot.len = packet.len;
        slot.feed = packet.feed;
        slot.occupied = true;
        return AddResult::stored;
    }

    // Consume directly from owned ring storage, then release the slot. The merger
    // is deliberately single-threaded and the callback must not retain the view.
    template <typename Consumer>
    [[nodiscard]] bool consumePacket(std::uint64_t seq, Consumer &&consume)
    {
        const auto index = indexFor(seq);
        auto &slot = slots_[index];
        if (!slot.occupied || slot.seq != seq)
        {
            return false;
        }

        const PacketView packet{.feed = slot.feed,
                                .payload = std::span<const char>{payloadData(index), slot.len}};
        consume(packet);
        slot.occupied = false;
        return true;
    }

  private:
    struct Slot
    {
        std::uint64_t seq = 0;
        std::size_t len = 0;
        char feed = 0;
        bool occupied = false;
    };

    static_assert((Capacity & (Capacity - 1)) == 0, "Ring capacity must be a power of two");

    [[nodiscard]] static constexpr std::size_t indexFor(std::uint64_t seq) noexcept
    {
        return static_cast<std::size_t>(seq & static_cast<std::uint64_t>(Capacity - 1));
    }

    [[nodiscard]] char *payloadData(std::size_t index) noexcept
    {
        return payload_storage_.data() + index * MaxPayloadSize;
    }

    std::array<Slot, Capacity> slots_{};
    std::vector<char> payload_storage_;
};

class UdpMergeConsumer
{
    Sequencer sequencer_;
    UdpBuffer buffer_;

    void deliver(char feed, std::uint64_t seq, const char *data, std::size_t len)
    {
        // std::cout << "Downstream got message\n";
        const std::string_view payload =
            len == 0 ? std::string_view{} : std::string_view{data, len};
        std::println("Feed: {}, Seq: {}, data: {}", feed, seq, payload);
    }

  public:
    void handlePacket(char feed, std::uint64_t seq, const char *data, std::size_t len)
    {
        if (len > UdpBuffer::MaxPayloadSize)
        {
            throw std::length_error("UDP payload exceeds buffer capacity");
        }
        if (data == nullptr && len != 0)
        {
            throw std::invalid_argument("non-empty packet has null data");
        }

        const auto next_seq = sequencer_.getNextSeq();
        if (seq < next_seq)
        {
            return; // Late duplicate from the redundant feed.
        }

        if (seq > next_seq)
        {
            const UdpBuffer::PacketInput packet{.feed = feed, .seq = seq, .data = data, .len = len};
            switch (buffer_.addPacket(next_seq, packet))
            {
            case UdpBuffer::AddResult::stored:
            case UdpBuffer::AddResult::duplicate:
                return;
            case UdpBuffer::AddResult::out_of_window:
                throw std::overflow_error("UDP reorder window exceeded");
            case UdpBuffer::AddResult::payload_too_large:
                throw std::length_error("UDP payload exceeds buffer capacity");
            case UdpBuffer::AddResult::invalid_data:
                throw std::invalid_argument("non-empty packet has null data");
            case UdpBuffer::AddResult::slot_collision:
                throw std::logic_error("UDP ring slot collision");
            }
        }

        // The expected packet is delivered directly without copying.
        deliver(feed, seq, data, len);
        auto expected = sequencer_.incrementSeq();

        while (buffer_.consumePacket(
            expected, [this, expected](const UdpBuffer::PacketView &packet)
            { deliver(packet.feed, expected, packet.payload.data(), packet.payload.size()); }))
        {
            expected = sequencer_.incrementSeq();
        }
    }
};

int main()
{
    UdpMergeConsumer m;
    const auto send = [&m](char feed, std::uint64_t seq, std::string_view message)
    { m.handlePacket(feed, seq, message.data(), message.size()); };

    send('A', 1, "Message 1");
    send('B', 1, "Message 1");
    send('B', 2, "Message 2");
    send('A', 2, "Message 2");
    send('B', 5, "Message 5");
    send('A', 6, "Message 6");
    send('A', 4, "Message 4");
    send('A', 4, "Message 4");
    send('A', 3, "Message 3");
    send('A', 5, "Message 5");
    send('B', 3, "Message 3");
    send('B', 4, "Message 4");
    return 0;
}
