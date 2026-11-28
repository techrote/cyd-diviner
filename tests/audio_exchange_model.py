#!/usr/bin/env python3
"""Independent bounded interleaving model for the three-slot Latest protocol.

Assumptions: exactly one non-reentrant producer and consumer; strong CAS has no
spurious failure; no endpoint reset while either endpoint runs. CAS and the
following private-index bookkeeping are one model step: the other endpoint
cannot observe that bookkeeping. Payload copies are modeled as one memory
event, with exclusive slot ownership checked at every step.

This explores scheduling/ownership, including dirty-token reuse. It is not a
C++ weak-memory proof, target assembly check, sanitizer run or hardware timing
measurement. Production release/acquire operations are reviewed separately.
"""
import argparse
from collections import deque
import json
from pathlib import Path


def require(condition, message, state):
    if not condition:
        raise RuntimeError(f"{message}: {state}")


def explore(publications, consumptions):
    # middle, writer, reader, payload revisions, producer pc/count/expected,
    # consumer pc/count/expected, last observed revision, observed retries.
    initial = (1, 2, 0, (0, 0, 0), 0, 0, 0, 0, 0, 0, 0, 0)
    pending, seen = deque([initial]), {initial}
    terminals = collisions = 0
    while pending:
        state = pending.pop()
        middle, writer, reader, slots, pp, pn, pe, cp, cn, ce, last, retries = state
        require(len({middle & 3, writer, reader}) == 3, "Slot ownership collision", state)
        if pn == publications and cn == consumptions:
            # Once both endpoints stop, the final revision is either already
            # consumed or is still available in the dirty middle slot.
            retained = slots[middle & 3] if middle & 4 else last
            require(retained == publications, "Final publication lost", state)
            terminals += 1
            continue
        successors = []
        if pn < publications:
            if pp == 0:
                updated = list(slots)
                updated[writer] = pn + 1
                successors.append((middle, writer, reader, tuple(updated), 1, pn, pe,
                                   cp, cn, ce, last, retries))
            elif pp == 1:
                successors.append((middle, writer, reader, slots, 2, pn, middle,
                                   cp, cn, ce, last, retries))
            elif middle == pe:
                successors.append((writer | 4, middle & 3, reader, slots, 0, pn + 1, pe,
                                   cp, cn, ce, last, retries))
            else:
                require(pp == 2, "Second strong producer CAS failed", state)
                collisions += 1
                successors.append((middle, writer, reader, slots, 3, pn, middle,
                                   cp, cn, ce, last, retries + 1))
        if cn < consumptions:
            if cp == 0:
                if middle & 4:
                    successors.append((middle, writer, reader, slots, pp, pn, pe,
                                       1, cn, middle, last, retries))
                else:
                    successors.append((middle, writer, reader, slots, pp, pn, pe,
                                       0, cn + 1, middle, last, retries))
            elif cp == 1:
                if middle == ce:
                    successors.append((reader, writer, middle & 3, slots, pp, pn, pe,
                                       2, cn, ce, last, retries))
                else:
                    successors.append((middle, writer, reader, slots, pp, pn, pe,
                                       0, cn + 1, middle, last, retries))
            else:
                value = slots[reader]
                require(last < value <= pn, "Stale or unpublished payload consumed", state)
                successors.append((middle, writer, reader, slots, pp, pn, pe,
                                   0, cn + 1, ce, value, retries))
        for following in successors:
            if following not in seen:
                require(len(seen) < 500000, "Model state budget exceeded", state)
                seen.add(following)
                pending.append(following)
    return {"status": "PASS", "publications": publications, "consume_calls": consumptions,
            "reachable_states": len(seen), "terminal_states": terminals,
            "first_cas_collisions": collisions, "second_cas_failures": 0,
            "producer_attempt_bound": 2, "consumer_attempt_bound": 1}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = {"scope": "Abstract SPSC scheduling/ownership; no target or weak-memory execution",
              "checks": ["three exclusive slot owners", "strictly newer consumed revisions",
                         "two-attempt publisher bound", "final publication retained"], "cases": []}
    try:
        for publications, consumptions in ((3, 4), (4, 5)):
            report["cases"].append(explore(publications, consumptions))
        report["status"] = "PASS"
    except Exception as error:
        report.update(status="FAIL", error=str(error))
    text = json.dumps(report, indent=2)
    print(text)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n")
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
