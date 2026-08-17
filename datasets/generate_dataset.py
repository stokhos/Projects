#!/usr/bin/env python3
"""Generates a randomized, deterministic dataset of order-matching messages
for soak-testing the `match_engine` binary. Not used by the GoogleTest
suite, which relies on small hand-checked cases instead.

Produces a mix of:
  - AddOrderRequests clustered around a slowly drifting mid-price (so a
    meaningful fraction actually cross and trade, not just rest),
  - CancelOrderRequests against real, currently-resting order ids, plus a
    few against unknown ids (to exercise the "unknown order id" error path),
  - occasional malformed lines, to exercise the parser's error handling.

Prices stay within this project's default PriceGrid bounds ($0.00-$2,000.00,
$0.01 ticks) so runs aren't dominated by rejected out-of-range orders.

Usage:
    python3 generate_dataset.py [num_messages] [seed] > stress_dataset.txt
"""

import random
import sys


def generate(num_messages: int, seed: int) -> list[str]:
    rng = random.Random(seed)
    lines: list[str] = []
    live_order_ids: list[int] = []
    next_order_id = 1
    mid_price_cents = 100_00  # $100.00, in integer cents

    for _ in range(num_messages):
        roll = rng.random()

        if roll < 0.08 and live_order_ids:
            # Cancel a real, currently-resting order.
            order_id = rng.choice(live_order_ids)
            lines.append(f"1,{order_id}")
            continue

        if roll < 0.10:
            # Cancel a bogus/unknown id, to exercise the error path.
            lines.append(f"1,{rng.randint(10**7, 10**8)}")
            continue

        if roll < 0.12:
            # Deliberately malformed line.
            bad_variants = [
                "NOTAMESSAGE",
                "0,1,2",  # wrong field count
                f"0,{next_order_id},0,-5,100",  # negative quantity
                f"0,{next_order_id},2,5,100",  # invalid side
                f"0,{next_order_id},0,5,abc",  # invalid price
                f"0,{next_order_id},0,5,3000",  # price above the $2,000 grid max
                f"0,{next_order_id},0,5,10.005",  # finer than the $0.01 tick
            ]
            lines.append(rng.choice(bad_variants))
            continue

        # A normal AddOrderRequest, priced within a few ticks of the
        # drifting mid-price so a good fraction of orders actually cross.
        mid_price_cents += rng.randint(-5, 5)
        mid_price_cents = min(max(mid_price_cents, 100), 199_900)
        side = rng.choice([0, 1])
        offset_cents = rng.randint(-20, 20)
        price_cents = min(max(mid_price_cents + offset_cents, 1), 200_000)
        price = f"{price_cents // 100}.{price_cents % 100:02d}"
        quantity = rng.randint(1, 500)

        order_id = next_order_id
        next_order_id += 1
        live_order_ids.append(order_id)
        if len(live_order_ids) > 500:
            live_order_ids.pop(0)

        lines.append(f"0,{order_id},{side},{quantity},{price}")

    return lines


if __name__ == "__main__":
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 42
    for line in generate(count, seed):
        print(line)
