# Order Matching Engine: Design and Performance

## What it does

A single-threaded, in-memory limit order book. It reads `AddOrderRequest` and
`CancelOrderRequest` messages from stdin and matches orders by price-time
priority (best price first, then the earliest order at that price). For every
match, it outputs, in order: a `TradeEvent`, the fill message of the
aggressive order, then the fill message of the resting order.

Single-threaded is a deliberate choice: it makes the order of processing
predictable, without needing locks.

The numbers in this document are **complexity estimates from reading the
code**, not measured benchmarks — there is no benchmark harness yet.

## How it is built

```text
stdin -> line reader -> CSV parser -> MatchingEngine -> OrderBook -> stdout/stderr
```

- **Application**: reads input, handles errors, chooses output mode.
- **Message**: parses and formats the wire protocol.
- **MatchingEngine**: validates requests and applies the matching rules.
- **OrderBook**: owns all order data and the lookup structures.

## Order book internals

- **Prices** are stored as integers (scaled by 10,000) to avoid float
  rounding — this can represent up to 4 decimal places (down to $0.0001).
  The default *configured* grid uses $0.00–$2,000.00 in $0.01 steps
  (200,001 levels); tick size and range can be configured differently, for
  example a $0.0001 tick over the same range gives 20,000,001 levels. A price
  outside the configured range or off the tick is rejected.
- **One FIFO queue per price level** (a linked list) — new orders are added
  at the end, matching happens from the front, so time priority is kept
  automatically.
- **Best price is cached** on each side, so checking "does this order cross"
  is only reading a field, not searching. A bitset keeps track of which price
  levels are occupied, so finding the next-best price after one becomes empty
  is still fast.
- **Order-ID lookup table** maps every live order directly to where it is
  (side, price, list position), so cancelling an order never needs to scan
  the book.

One side of the book (buy or sell), with 5 price levels for illustration —
index 4 is the highest price, and this side is buys, so `bestIndex` points
there:

```text
price index                0        1        2        3        4
                           |        |                          |
occupiedPrices (bitset)    1        1        0        0        1
                           |        |                          |
levels (vector)           [0]      [1]      [2]      [3]      [4]
                           |        |        (empty)  (empty)  |
                           v        v                          v
                       Order 7  Order 3                    Order 9 -> Order 2 -> Order 5
                                                             (oldest ----------> newest)
                                                                          bestIndex --^

orderLocations_ (unordered_map<OrderId, OrderLocation>)
  7 -> side=Buy, priceIndex=0  ---> the "Order 7" node above
  3 -> side=Buy, priceIndex=1  ---> the "Order 3" node above
  9 -> side=Buy, priceIndex=4  ---> the "Order 9" node above
  2 -> side=Buy, priceIndex=4  ---> the "Order 2" node above
  5 -> side=Buy, priceIndex=4  ---> the "Order 5" node above
```

Reading the diagram: the **vector** gives O(1) access from a price index
straight to that level's queue. Each queue is a **linked list**, so adding to
the back or removing any node is O(1) without shifting anything else. The
**bitset** is a same-sized shadow of the vector — one bit per index — so
scanning for the next occupied price after one empties only touches this
compact row of bits, not the (possibly much larger) vector of lists itself.
The **hash map** is the odd one out: it doesn't sit spatially next to the
other three, it points directly *into* a specific node inside one specific
list, which is what lets cancelling order 2 (say) jump straight there instead
of walking `levels[4]` from the front to find it.

## Performance, in plain terms

| Operation | Typical cost | Worst case |
|---|---|---|
| Check if an order would match | O(1) | O(1) |
| Add a resting (non-matching) order | O(1) | O(N) on hash rehash |
| Process a matching order (T fills) | ~O(T) | worse if best-price search has to scan far |
| Cancel an order | O(1) | O(N) in rare bad-hashing cases |
| Remove a filled order | O(1) | O(N) in rare bad-hashing cases |

`N` = number of live orders. The "worst case" rows only happen when the hash
table used for order lookup has bad luck, or is deliberately attacked — this
is not expected during normal operation.

**Memory**: roughly `O(P + N)` — the price grid (`P` = 200,001 levels by
default) is allocated at the start no matter how many levels are actually
used, plus a per-order cost (below). This is the main cost of choosing
instant price lookups instead of a more memory-efficient sparse structure.

Every live order costs two separate heap allocations right now — one
`std::list` node and one hash-map node — and each one goes through the
general-purpose allocator and lands wherever it finds space, so related
orders end up scattered instead of sitting near each other in memory.
Pooling order storage — grabbing one big block up front and handing out
fixed-size slots from it — is what would actually move the needle: fewer
calls into the allocator, and better cache locality when walking a price
level or the order-id table.

## Performance, in detail

Going through each row against the actual code.

**Check if an order would match — O(1), always.**
`tryMatchBestOrder` ([MatchingEngine.cpp:96-104](../src/MatchingEngine.cpp#L96-L104))
does exactly this: check `orderCount == 0` for emptiness
([OrderBook.cpp:160](../src/OrderBook.cpp#L160)), read the cached `bestIndex`
field and index directly into `levels[bestIndex]`, then take `.front()` of
that price level's list ([OrderBook.cpp:29-41](../src/OrderBook.cpp#L29-L41)),
then one price comparison. No searching happens in any case — this is why it
stays O(1) both typically and in the worst case.

**Add a resting order — O(1) typical, O(N) worst.**
`addOrder` ([OrderBook.cpp:55-108](../src/OrderBook.cpp#L55-L108)): `priceToIndex`
is only arithmetic (subtract/divide/modulo); `levels[priceIndex]` is a direct
array index; `level.insert(level.end(), order)` is O(1) because inserting
into a `std::list` never shifts other elements. The one hash-table insert
(`orderLocations_.emplace`) is O(1) on average; the O(N) worst case comes
from an occasional rehash (which touches every existing entry), or from many
order IDs landing in the same hash bucket (only realistic if someone chooses
the IDs on purpose to cause this). Updating `bestIndex` is one comparison
against the new price
([OrderBook.cpp:101-105](../src/OrderBook.cpp#L101-L105)) — adding an order never
needs the bitset scan described below, because a new order can only become
the best price by direct comparison, it never needs to search for it.

**Process a matching order with T fills — ~O(T) typical.**
The loop inside `handleAdd`
([MatchingEngine.cpp:165](../src/MatchingEngine.cpp#L165)) runs once for each
resting order it consumes. Each step is O(1) — reduce quantity plus three
O(1) output calls (`TradeEvent`, fill of the incoming order, fill of the
resting order) — *except*
when a fill empties a price level that was the best on that side. In that
case `removeFilledFront` has to find the next best occupied price using
`PriceBitset::findHighestSet`/`findLowestSet`
([PriceBitset.hpp:54-127](../src/PriceBitset.hpp#L54-L127)), which checks
64-bit occupancy words one by one until a nonzero one is found — fast when
the next occupied price is close, up to `W = P/64` words in the worst case.
One thing to note: this scan restarts from the edge of the whole grid,
not from next to the level that just emptied (`findHighestSet(levelCount() - 1)`
/ `findLowestSet()` starting at 0 —
[OrderBook.cpp:134](../src/OrderBook.cpp#L134),
[:138](../src/OrderBook.cpp#L138)). So the cost of a scan depends on *where
the next occupied price is in the whole configured range*, not on how far it
is from the level just emptied.

**Cancel an order — O(1) typical, O(N) worst.**
`cancelOrder` looks up the ID in the hash map (O(1) on average), then
`eraseLocatedOrder` ([OrderBook.cpp:113-144](../src/OrderBook.cpp#L113-L144))
erases the list node directly through the stored iterator — O(1), which is
the reason the locator stores an iterator instead of an ID to search for —
flips one occupancy bit, and only runs the bitset scan above if this emptied
the cached best level. An unknown ID simply misses the hash lookup and
nothing is changed.

**Remove a filled order — same path as cancel.**
`removeFilledFront` ([OrderBook.cpp:197-234](../src/OrderBook.cpp#L197-L234))
does a few extra O(1) consistency checks (re-validates the price index,
re-checks that the locator matches) and then calls the same
`eraseLocatedOrder` as cancellation (see "Design trade-offs" below for why).

**Why the worst-case rows are called "not expected."** Every O(N) case above
has the same reason: `std::unordered_map` only guarantees *average* O(1) per
operation, not worst case, mainly because of occasional rehashing as the map
grows.

## Design trade-offs

- Fast matching and cancellation were chosen over memory efficiency and
  flexibility in price range.
- Cancels and fills use the same code path, so cancellation is not treated
  as less important.
- Single-threaded means no lock contention, but also no way to use multiple
  cores for one order book.

## Known limits (expected for a take-home scope)

- Handles one instrument, on one thread, with a fixed price range set at
  startup.
- Everything happens one step after another — if writing output is slow,
  matching becomes slow too.
- No persistence: all data lives only in memory, nothing is saved, so a
  crash loses everything.
- A match with several fills is not all-or-nothing — if writing the output
  fails partway through, the fills that already happened cannot be undone.

## If this went to production

Roughly in order of importance:

1. **Change how order memory is allocated — the single biggest improvement
   available.** (See the "Memory" section above for why: two heap
   allocations per live order, scattered rather than packed together.)
   Pooling order storage — one upfront allocation, handed out and reclaimed
   in fixed-size slots — would cut allocator overhead and improve cache
   locality, which matters far more than any struct field reordering or
   search-algorithm tweak.
2. **Make the most-used code faster in other ways** — a smarter way to
   search for the next best price (fast even with 200,000 price levels), and
   fewer repeated allocations in parsing/formatting.
3. **Choose the right price structure for each instrument** — a fixed grid
   works well for a narrow price range; a different structure (for example a
   tree) would work better for a wide or unusual price range.
4. **Use more threads by splitting the work, not by sharing one order book**
   — give each instrument its own thread and its own order book, with
   requests sent to the correct thread through a queue. This keeps the order
   of matching correct for each instrument, while still using more than one
   CPU core overall.
5. **Add limits and a way to slow down under heavy load** — set maximum
   numbers for orders, queue sizes, and request rate, so that too much load
   causes controlled rejection instead of unpredictable behavior.
6. **Add a way to recover after a crash** — write every request to a log
   before applying it, and save snapshots regularly, so the book can be
   rebuilt after a crash without losing or reordering trades.
7. **Add monitoring** — counts and timing measurements (typical and
   worst-case latency, not just the average) for every step: parsing,
   matching, logging, and sending output.
8. **Actually measure performance** — replace these estimates with real
   measurements before trying to optimize further.
