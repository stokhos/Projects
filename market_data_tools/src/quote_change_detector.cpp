// Marks each quote in a snapshot as ShouldPublish when its price has changed
// since the last snapshot seen at the same Size.

#include <print>
#include <unordered_map>
#include <vector>

struct Quote
{
    double Px = 0.;
    double Size = 0.;
    bool ShouldPublish = false;
};

struct Consumer
{
    std::unordered_map<double, double> prev_quote_by_size;
    void onUpdate(std::vector<Quote> &snapshot)
    {
        for (auto &quote : snapshot)
        {
            auto [it, inserted] = prev_quote_by_size.try_emplace(quote.Size, quote.Px);
            quote.ShouldPublish = inserted || it->second != quote.Px;
            auto prevPx = it->second;
            it->second = quote.Px;

            std::println(
                "Should Publish: {:>5} | Size: {:>10.0f} | Prev Px: {:>8.5f} | Px: {:>8.5f}",
                quote.ShouldPublish, quote.Size, prevPx, quote.Px);
        }
    }
};

int main()
{
    Consumer c;
    {
        std::vector<Quote> snapshot;
        snapshot.push_back({1.10987, 250000});  // ShouldPublish = true
        snapshot.push_back({1.10985, 750000});  // ShouldPublish = true
        snapshot.push_back({1.10983, 1250000}); // ShouldPublish = true
        c.onUpdate(snapshot);
    }

    {
        std::vector<Quote> snapshot;
        snapshot.push_back({1.10987, 250000});  // ShouldPublish = false
        snapshot.push_back({1.10983, 750000});  // ShouldPublish = true
        snapshot.push_back({1.10981, 1250000}); // ShouldPublish = true
        c.onUpdate(snapshot);
    }

    return 0;
}
