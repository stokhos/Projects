# Order Matching Engine

A single-threaded matching engine: reads `AddOrderRequest`/`CancelOrderRequest`
messages from stdin, matches them price-time priority against an in-memory
limit order book, and writes `TradeEvent`/`OrderFullyFilled`/`OrderPartiallyFilled`
messages to stdout. Malformed input is reported to stderr and otherwise
ignored — no input crashes the program.

## Building

Requires a C++26 compiler — GCC 14+ or Clang 17+ on Linux, or Xcode with
Apple Clang 16+ on macOS — and CMake 3.20+. Linux and macOS are the only
supported platforms; `CMakeLists.txt` rejects any other `CMAKE_SYSTEM_NAME`
up front, and separately checks the compiler version, so a build fails with
a clear message rather than a wall of template errors if either isn't met. No
third-party libraries are used by the application itself. Tests link against
the GoogleTest 1.18.0 source checkout vendored under `third_party/googletest`
— everything needed to build is inside this folder, no network access
required.

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
```

## Running

```bash
./build/match_engine < datasets/spec_example.txt
```

`datasets/spec_example.txt` is the worked example from the assignment spec;
running it prints the exact expected output:

```
2,2,1025
4,1000008,1
3,1000005
2,1,1025
3,1000008
4,1000007,4
```

with `ERROR [line 7]: Unknown message type: BADMESSAGE` on stderr for the one
malformed line. Every rejected input uses the same line-numbered error format.
Successful requests do not write diagnostics, so stdout remains strictly the
wire protocol and stderr is reserved for errors.

For an organized human-readable view, use the opt-in activity table:

```bash
./build/match_engine --pretty < datasets/spec_example.txt
```

```text
|     LINE | EVENT    |             ORDER ID | SIDE |                PRICE |             QUANTITY |            REMAINING | RESULT                           |
|       11 | TRADE    |              1000008 | BUY  |                 1025 |                    2 |                    1 | #1 EXECUTED (aggressor)          |
|       11 | FILL     |              1000008 | BUY  |                 1050 |                    2 |                    1 | #1 PARTIAL (aggressor)           |
|       11 | FILL     |              1000005 | SELL |                 1025 |                    2 |                    0 | #1 FULL (resting)                |
```

Pretty mode suppresses the numeric protocol and writes the complete table to
stderr, keeping erroneous input on the stream required by the assignment. It
is intended for interactive inspection; the no-argument mode remains the
machine-readable submission interface. To save a trace, redirect it with
`2> activity.log`. The `#N` values are display-only sequence numbers; they are
not additional fields in the assignment's wire protocol.

Malformed requests are recoverable and processing continues with the next
line. Blank lines are malformed too. Input lines are limited to 1,024 bytes
(excluding the line ending and the `\r` in a CRLF ending); longer lines are
discarded with one error so processing can resume at the next physical line.
An underlying stdin/stdout/stderr failure, allocation failure, or unexpected
exception is fatal: the application prints a `FATAL: ...` diagnostic to stderr
and exits nonzero. Active output streams are explicitly flushed before a
successful exit, and on Linux a closed output pipe is handled as a reported I/O
failure instead of an unhandled `SIGPIPE` termination.

## Testing

```bash
cd build && ctest
```

90 GoogleTest cases across `Price`, `PriceGrid`, `PriceBitset`, `Order`,
`Message`, `OrderBook`, `MatchingEngine`, and the stream-based application
runner, covering the wire protocol, FIFO/price-time priority, partial and full
fills, cancellation, rejected input, CRLF input, and stream failures.

For manual soak-testing beyond the unit suite, `datasets/generate_dataset.py`
produces a larger randomized dataset — a mix of orders clustered around a
drifting mid-price (so a meaningful fraction actually trade), cancels of
both real and unknown ids, and several categories of malformed lines:

```bash
python3 datasets/generate_dataset.py 2000 42 > datasets/stress_dataset.txt
./build/match_engine < datasets/stress_dataset.txt > /dev/null
```

The checked-in `datasets/stress_dataset.txt` contains 2,000 lines. The generator
accepts both message count and random seed so additional datasets can be made
deterministically.

## Design

Two arrays (bid side, ask side), each indexed directly by price rather than
keyed in a tree, holding one FIFO `std::list<Order>` per occupied price level
for price-time ordering, plus a `PriceBitset` and a cached best-index per
side so "what's the best price" is a field read, not a search. A hash map
from order id to its exact list location makes cancellation direct instead
of a scan. See
[Design and Performance](docs/design-and-performance.md#order-book-internals)
for the full breakdown, a diagram of how those pieces fit together, and the
trade-off between price-range flexibility and speed behind the choice.

The executable entry point only configures process-level I/O and provides the
final exception boundary. The reusable `app::run` layer owns line parsing,
error formatting, stream validation, and engine callbacks; injecting streams
makes the complete stdin/stdout workflow directly unit-testable without
duplicating order-book state in the CLI. Pretty output observes rich match
events emitted directly by `MatchingEngine`; it does not maintain a second
shadow order book.

## Performance

Checking whether an incoming order matches, and removing a filled or
canceled order, are all O(1) in the common case (average-case for the
hash-map lookups); matching an order against several resting orders costs
work proportional to how many fills it produces. The design spends
O(price levels + live orders) memory to make best-price access direct and
cancellation responsive, rather than scanning. The randomized dataset is a
functional soak test, not a measured latency or throughput benchmark.

See [Design and Performance](docs/design-and-performance.md) for the full
cost table, a line-by-line walkthrough against the actual code, and where
the worst cases come from.

## Notable edge-case decisions

The spec doesn't define these; here's what this implementation does and why:

- **Duplicate order id on add:** rejected with an error, not overwritten or
  merged. An id is a stable handle a client can cancel by — silently
  reassigning it would make that handle ambiguous.
- **Cancel of an unknown/already-filled/already-canceled id:** reported to
  stderr, not fatal. A cancel racing a fill it doesn't know about yet is
  routine in any real system; the process shouldn't die over it.
- **Self-trading:** allowed. There's no account/trader concept in the wire
  protocol, so nothing distinguishes "two different ids happen to belong to
  the same participant." Self-trade prevention needs that concept first —
  see "If this were production" below.

## If this were production

The single biggest improvement would be pooling order storage instead of two
separate heap allocations per live order (see
[Design and Performance](docs/design-and-performance.md#performance-in-plain-terms)
for why). After that: remove repeated validation/index conversion, reserve
the ID map, start bitset searches next to the old best price, and replace
allocating text parsing/formatting on the protocol path. Larger-scale work
would shard instruments across single-writer engines, add bounded queues and
backpressure, introduce write-ahead logging plus snapshots, and export
asynchronous latency/allocation metrics. The detailed rationale and
trade-offs are in
[Design and Performance](docs/design-and-performance.md#if-this-went-to-production).
