# Hot-Path Expression Engine

A zero-allocation C++ expression-evaluation engine: it compiles small
S-expression "signals" (predicates / rules over named inputs) to bytecode and
runs them on a stack virtual machine designed for the hot path — the kind of
configurable per-tick rule evaluation a feed handler, risk check, or strategy
gate does in a low-latency trading system.

It is built on top of the Racket λ-calculus interpreter in the parent repo,
which is kept **unchanged** and reused as a **differential-test oracle**: every
optimization is proven to preserve the reference semantics.

```
(if (@ < bid ask) (@ - ask bid) 0)      ; a "signal" over inputs bid, ask
```

## Quickstart

```bash
make            # build lambda_eval + difftest (strict, -Werror)
make test       # differential test vs the Racket oracle (needs racket)
make bench      # latency benchmark: switch vs computed-goto, + baselines
make sanitize   # ASan + UBSan over the differential test
make lint       # clang-tidy + cppcheck (when installed)

./build/lambda_eval "(if (@ < bid ask) (@ - ask bid) 0)" bid=100 ask=101   # => 1
```

## Architecture

```
source  ──lexer──▶ tokens ──parser──▶ AST ──compiler──▶ Program ──▶ VM ──▶ Value
                                             (bytecode)         (stack machine)
```

The design decisions are the point:

- **Compile-time lexical addressing.** The compiler resolves every variable to
  an integer index — an input slot or a local frame slot — so the hot loop never
  does a name lookup. This replaces the reference interpreter's assoc-list
  `lookup-env` (O(n) string compares on every access) with an O(1) array index.
- **≤16-byte tagged `Value`.** A trivially-copyable `{tag, int64}` union — no heap
  graph, register/cache friendly.
- **Zero allocation on the hot path.** The operand stack, local frame, and
  `ref`/`set` store are all sized by the compiler (`max_stack`, `n_locals`,
  `max_store`) and preallocated once; each evaluation resets a counter. No
  `malloc` per tick.
- **Two dispatch strategies from one source.** The VM handler bodies are shared
  by a `switch` build and a computed-goto (threaded) build via macros, compiled
  into separate objects so the two can be benchmarked head-to-head.

Correctness is gated by **differential testing** against the Racket oracle
(`make test`), and the build runs under strict warnings + `-Werror`, ASan/UBSan,
clang-tidy/cppcheck, and CI.

## Results

Measured on a 13th-Gen Intel Core i5-1340P, thread-pinned, TSC ≈ 2.19 GHz.
Numbers are illustrative of the shape, not absolutes — reproduce with `make bench`.
"eval" = one signal evaluation (one tick). Three signals: **light** (straight-line
mid/spread predicate), **branchy** (data-dependent nested branches), **heavy**
(32-iteration store/loop accumulation).

**Engine vs baselines** (computed-goto build):

| signal   | ns/eval | cyc/eval | vs Racket tree-walker | vs hand-written native C++ |
|----------|--------:|---------:|----------------------:|---------------------------:|
| light    |      34 |       74 |                  ~36× |                       ~25× |
| branchy  |      26 |       58 |                  ~32× |                        ~8× |
| heavy    |     865 |     1892 |                  ~33× |          *(see note)*      |

- **~30× faster than the reference tree-walker** it was derived from — the
  project's real before→after, from compiled bytecode + lexical addressing + no
  boxing.
- **~8–25× the cost of hand-coded native C++** — the honest price of a
  *configurable* engine. (The `heavy` native ratio is omitted: the optimizer
  collapses that signal's loop-invariant sum to a closed form, so native does O(1)
  while the engine actually loops — an unfair overhead number.)

**Dispatch study** — switch vs computed-goto (cycles/eval, and retired
instructions/eval from `perf_event_open`):

| signal   | switch cyc | goto cyc |     Δ | switch insns | goto insns |
|----------|-----------:|---------:|------:|-------------:|-----------:|
| light    |         85 |       74 |  ~noise | 330 | 257 |
| branchy  |         58 |       58 |  ~noise | 215 | 171 |
| heavy    |       2402 |     1892 |  −21% | 11113 | 7504 |

The interesting finding is the *non-result*: **computed-goto is not a universal
win.** On the short signals it is within run-to-run noise; it only clearly wins
(~21%) on the long loop. The counters show that win comes from **~33% fewer
retired instructions** (no per-op range-check + jump-table indirection), **not**
from fewer branch misses — computed-goto often has a *higher* miss rate here.
This reproduces the modern result (Rohou et al., 2015) that threaded dispatch's
historical edge has largely eroded on CPUs with good indirect-branch predictors.
Measuring that beats repeating the folklore.

## Language subset

First-order only (the hot-path subset of the reference interpreter's grammar):
int/bool literals, input variables, `let` / `let*`, `if`, `ref` / `deref` / `set`,
`seq`, `while`, and primitives `+ - * / == < > <= >= not and or`. Closures,
`letrec`, and `set!` are intentionally excluded — they force heap-allocated
closures that fight the zero-allocation design and are not what a first-order rule
evaluator needs. Unsupported forms are rejected at compile time with a clear error.

## Layout

```
include/engine/   value.hpp · opcode.hpp · bytecode.hpp   (public core types)
src/              lexer · parser · compiler · vm · main   (pipeline + CLI)
tests/            corpus.txt · gen_golden.rkt (oracle) · difftest.cpp · golden.txt
bench/            bench_vm.cpp (latency) · bench_oracle.rkt (Racket baseline)
Makefile          .clang-format · .clang-tidy
```
