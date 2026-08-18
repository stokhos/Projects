# Market Data Tools

Three small, self-contained C++26 programs, each a standalone exercise in a
piece of market-data plumbing: deduplicating/filtering a trade batch,
resequencing a redundant UDP feed pair, and detecting which quotes in a
snapshot actually changed. Each lives in its own `.cpp` file with a `main()`
driving a few hardcoded scenarios — there's no shared library or CLI
argument parsing between them.

## Building

Requires a C++26 compiler — GCC 14+ or Clang 17+ on Linux, or Xcode with
Apple Clang 16+ on macOS — and CMake 3.20+.

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
```

## Running

```bash
./build/trade_id_filter
./build/udp_feed_merger
./build/quote_change_detector
```

Each prints its scenario's output directly to stdout; there's no input file
or argument parsing to configure.

### `trade_id_filter`

Filters a batch of `Trade` records down to the ones matching a set of ids,
returning pointers into the source vector rather than copies since each
`Trade` carries an 8KB payload.

### `udp_feed_merger`

Merges two redundant, sequence-numbered UDP feeds (A and B) into a single
gap-free, strictly ordered stream. Out-of-order and duplicate packets across
either feed are buffered in a fixed-capacity ring and released in sequence
once the gap closes. See
[docs/udp-feed-merger-anatomy.html](docs/udp-feed-merger-anatomy.html) for a
walkthrough of the buffering logic.

### `quote_change_detector`

Marks each quote in a snapshot as `ShouldPublish` when its price has changed
since the last snapshot seen at the same size, using a size-keyed map of the
last published price.

## Project layout

```
src/    the three tools, one file each
docs/   supporting write-ups
```
