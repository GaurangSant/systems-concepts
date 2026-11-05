# bpsim — Branch Predictor Simulator (COL 7001)

bpsim replays a branch trace through your predictor and reports accuracy and
MPKI (mispredictions per 1,000 instructions).

## Quick start

```
make                       # builds ./bpsim
./bpsim -l                 # lists predictors and their storage
./bpsim -s last_outcome    # storage breakdown of one predictor
./bpsim -p always_taken traces/*.gz
python3 tools/check_rules.py predictors/*.cpp   # run before submitting
```

## What you write

Only the files in `predictors/`. Each predictor is one class that inherits
`BranchPredictor` (`src/predictor.h`) and fills in two functions:

| Function | Called | Your job |
|---|---|---|
| `predict(pc)` | before the outcome is known | return true for taken |
| `update(pc, taken)` | right after, with the real outcome | train tables, shift history |

Every `predict()` is followed by exactly one `update()` for the same branch,
with nothing in between, so `update()` can simply recompute what `predict()`
looked up.

Fill in:

- Part A: `onebit.cpp`, `twobit.cpp`, `gshare.cpp`, `tournament.cpp`
- Part B: `mine.cpp`

Read the two worked examples first: `always_taken.cpp` (no state) and
`last_outcome.cpp` (one register).

## Storage: use only `src/storage.h`

All state your predictor keeps between calls must live in these four
structures, declared as members of your class:

| Structure | Shape | Bits counted |
|---|---|---|
| `Register(bits)` | one W-bit value | W |
| `Table(entries, bits)` | N entries, indexed directly, no tags | N × W |
| `DirectMappedCache(entries, tagBits, dataBits)` | N entries of valid + tag + data | N × (1 + T + D) |
| `SetAssocCache(sets, ways, tagBits, dataBits, LRU or Manual)` | S sets × K ways of valid + tag + data | S × K × (1 + T + D + LRU bits) |

LRU costs ceil(log2 K) bits per entry. With `Manual` you choose the victim way
yourself and pay no LRU bits.

bpsim adds these up for you. The total must be at most **65,536 bits (64 Kbit)**.
You never report storage yourself.

Each field is 1–64 bits wide, unsigned by default or two's-complement `Signed`
(useful for perceptron weights). `inc()` and `dec()` saturate at the field's
limits. Storing a value that does not fit, or using an index out of range,
stops the run with an error. Tags are truncated to `tagBits` automatically.

**Allowed:** the four structures as members; `static constexpr` constants;
helper functions; local variables inside a single `predict()`/`update()` call.

**Not allowed:** any other member or global variable (`int`, `bool`, arrays,
structs, pointers), `static` variables, containers, `new`/`malloc`, files,
randomness, or anything that reaches around the library. bpsim stops if
storage is created outside your constructor. `tools/check_rules.py` flags the
rest, and every submission is also reviewed by hand.

## Rules

- Do not modify anything in `src/`. It is replaced during grading.
- Your predictor must be deterministic.

## Traces

Format: one line per executed conditional branch, `<pc in hex> <0|1>`, plus a
`# instructions: N` header used for MPKI. Files may be gzipped.

The traces in `traces/` are synthetic, built from branches with known behaviour
(loops, alternating, biased, correlated, random). Use them to check that your
predictors behave as theory says. Final grading uses hidden traces from real
programs, so do not tune only to these. Regenerate with `make traces`.
