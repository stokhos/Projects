// Filters a batch of trades down to the ones matching a set of Ids. The
// output is a vector of pointers into the source vector rather than copies,
// since Trade is large (an 8KB payload) and the filtered set is meant to be
// consumed immediately by the caller, which still owns and outlives `trades`.

#include <array>
#include <print>
#include <span>
#include <unordered_set>
#include <vector>

struct Trade
{
    int Id = 0;
    double Px = 0.;
    double Qty = 0.;
    std::array<char, 8192> Payload;
};

namespace
{
void filter_by_id(std::span<const Trade> trades, const std::unordered_set<int> &ids,
                  std::vector<const Trade *> &filtered_trades)
{
    filtered_trades.clear();

    for (const auto &trade : trades)
    {
        if (ids.contains(trade.Id))
        {
            filtered_trades.push_back(&trade);
        }
    }
}

void deliver(std::span<const Trade *const> filtered)
{
    for (const auto trade : filtered)
    {
        std::println("Trade Id: {}, Px: {:.5f}, Qty: {:.0f}", trade->Id, trade->Px, trade->Qty);
    };
    // If you assume anything about how the consumer holds/uses the filtered trades,
    // note it here (e.g., ownership, lifetime, copying vs views).
};
} // namespace

int main()
{
    std::vector<Trade> trades;
    trades.push_back({1, 1.23456, 1000000});
    trades.push_back({2, 1.23456, 1000000});
    trades.push_back({2, 1.23455, 2000000});
    trades.push_back({3, 1.23454, 1000000});
    trades.push_back({1, 1.23453, 2000000});
    trades.push_back({3, 1.23453, 2000000});

    // Write code here to filter `trades` by Id.
    std::unordered_set<int> filter_ids = {3};
    std::vector<const Trade *> filtered_trades;

    filtered_trades.reserve(trades.size());

    filter_by_id(trades, filter_ids, filtered_trades);
    deliver(filtered_trades);

    return 0;
}
