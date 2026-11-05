#!/usr/bin/env python3
"""Generate small synthetic branch traces for bpsim.

Each trace mimics a program made of static branches with known behaviour,
so students can check that each predictor wins and loses where theory says:

  loop        taken N-1 times, then not taken (loop back-edge, trip count N)
  alternate   T, N, T, N, ...            (defeats 1-bit, local history wins)
  biased      taken with probability p   (bimodal is near-optimal)
  correlated  same outcome as an earlier branch (needs global history)
  random      taken with probability 0.5 (nothing helps)

These are for development only. Grading uses real-program traces.

usage: gen_trace.py <out_dir> [--branches N] [--seed S]
"""
import argparse
import gzip
import os
import random


class Branch:
    def __init__(self, pc, kind, rng, **kw):
        self.pc, self.kind, self.rng, self.kw = pc, kind, rng, kw
        self.count = 0
        self.last = False

    def outcome(self):
        k = self.kind
        if k == "loop":
            n = self.kw["trip"]
            t = (self.count % n) != n - 1
        elif k == "alternate":
            t = self.count % 2 == 0
        elif k == "biased":
            t = self.rng.random() < self.kw["p"]
        elif k == "correlated":
            src = self.kw["src"]
            t = src.last if not self.kw.get("invert") else not src.last
        else:  # random
            t = self.rng.random() < 0.5
        self.count += 1
        self.last = t
        return t


def make_program(mix, n_static, rng):
    """Build n_static branches; mix = {kind: weight}."""
    kinds = list(mix)
    weights = [mix[k] for k in kinds]
    branches = []
    for i in range(n_static):
        pc = 0x400000 + 4 * rng.randrange(1, 1 << 20)
        kind = rng.choices(kinds, weights)[0]
        kw = {}
        if kind == "loop":
            kw["trip"] = rng.choice([3, 4, 5, 8, 10, 16, 33])
        elif kind == "biased":
            kw["p"] = rng.choice([0.05, 0.1, 0.9, 0.95, 0.99])
        elif kind == "correlated":
            if not branches:
                kind, kw = "biased", {"p": 0.5}
            else:
                kw["src"] = branches[-1]  # previous branch in program order
                kw["invert"] = rng.random() < 0.5
        branches.append(Branch(pc, kind, rng, **kw))
    return branches


def write_trace(path, branches, n_dyn, rng):
    # Program runs its static branches in order, round and round, like a
    # loop body. Average ~5 instructions between branches (about 20% branches,
    # matching midterm Q1).
    instructions = 0
    lines = []
    i = 0
    while len(lines) < n_dyn:
        b = branches[i % len(branches)]
        lines.append(f"{b.pc:x} {int(b.outcome())}\n")
        instructions += rng.randint(2, 8)
        i += 1
    with gzip.open(path, "wt") as f:
        f.write(f"# instructions: {instructions}\n")
        f.writelines(lines)


TRACES = {
    # name: (mix, number of static branches)
    "loops":      ({"loop": 1}, 40),
    "alternate":  ({"alternate": 1, "biased": 1}, 40),
    "biased":     ({"biased": 1}, 200),
    "correlated": ({"correlated": 2, "random": 1}, 60),
    "mixed":      ({"loop": 3, "biased": 4, "correlated": 2, "alternate": 1, "random": 1}, 300),
    "large":      ({"loop": 3, "biased": 4, "correlated": 2, "alternate": 1, "random": 1}, 20000),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("--branches", type=int, default=1_000_000)
    ap.add_argument("--seed", type=int, default=7001)
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)
    for name, (mix, n_static) in TRACES.items():
        rng = random.Random(f"{a.seed}-{name}")
        prog = make_program(mix, n_static, rng)
        path = os.path.join(a.out_dir, f"{name}.trace.gz")
        write_trace(path, prog, a.branches, rng)
        print("wrote", path)


if __name__ == "__main__":
    main()
