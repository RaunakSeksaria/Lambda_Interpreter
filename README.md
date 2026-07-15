# Hot-Path Expression Engine (+ λ-Calculus Reference Interpreter)

A zero-allocation **C++ expression-evaluation engine** for hot-path signal/rule
evaluation — the configurable per-tick predicate evaluation a feed handler, risk
check, or strategy gate performs in a low-latency trading system — built on and
**differentially tested against** a Racket λ-calculus interpreter that serves as
the executable reference semantics.

The engine compiles S-expression signals to bytecode and runs them on a stack VM
with compile-time lexical addressing, ≤16-byte tagged values, and no per-tick
allocation. The Racket interpreter (the original project) is kept **unchanged**
and reused as a differential-test **oracle**, so every optimization is proven to
preserve the reference behaviour.

```bash
# the engine
cd engine && make && make test && make bench

# the reference interpreter (oracle)
racket interpreter.rkt
```

## The engine — [`engine/`](engine/)

```bash
cd engine
make            # build (strict, -Werror)
make test       # differential test vs the Racket oracle
make bench      # latency benchmark: switch vs computed-goto + baselines
make sanitize   # ASan + UBSan
make lint       # clang-tidy + cppcheck

./build/lambda_eval "(if (@ < bid ask) (@ - ask bid) 0)" bid=100 ask=101   # => 1
```

**Headline results** (i5-1340P, thread-pinned; reproduce with `make bench`):

| signal (computed-goto) | ns/eval | vs Racket tree-walker | vs native C++ |
|------------------------|--------:|----------------------:|--------------:|
| light (straight-line)  |      34 |                  ~36× |          ~25× |
| branchy (data-dep.)    |      26 |                  ~32× |           ~8× |
| heavy (32-iter loop)   |     865 |                  ~33× |     *(note)*  |

- **~30× faster than the reference tree-walker** (compiled bytecode + lexical
  addressing + no boxing); **~8–25× the cost of hand-coded native** (the price of
  configurability).
- A **switch vs computed-goto dispatch study** shows threaded dispatch is *not* a
  universal win: within noise on short signals, ~21% only on the long loop, and
  that win tracks **retired-instruction count, not branch misses** — reproducing
  the modern finding that its historical edge has eroded on good indirect-branch
  predictors.

Design, full results, and the honest caveats are in **[engine/README.md](engine/README.md)**;
engine internals are in [docs/DESIGN.md](docs/DESIGN.md#c-engine).

## The reference interpreter (oracle) — [`interpreter.rkt`](interpreter.rkt)

An interpreter for an extended λ-calculus with a mutable store, in Racket — the
original project, now the engine's oracle. Core calculus (`lambda`, `@`
application, `let`, `if`) extended with sequential/recursive bindings, first-class
store locations, sequencing, and loops, plus an interactive REPL and a 32-case
test suite.

```bash
racket interpreter.rkt            # REPL (help, examples, run-tests, quit)
echo "run-tests" | racket interpreter.rkt
```

```racket
λ-calc> (@ + 1 2)                                                    ; => 3
λ-calc> (letrec ([fact (lambda (n)
                         (if (@ == n 0) 1 (@ * n (@ fact (@ - n 1)))))])
          (@ fact 5))                                                ; => 120
```

| Construct | Syntax |
|-----------|--------|
| Application / lambda | `(@ f arg ...)`, `(lambda (x y) body)` |
| Bindings | `(let ([x e]) body)`, `let*`, `let*2`, `letrec` |
| Store | `(ref e)`, `(deref l)`, `(set l v)`, `(set! x v)` |
| Control | `(if c t e)`, `(seq e1 e2)`, `(while cond body)` |
| Primitives | `+ - * /` (integer), `== < > <= >=`, `not and or` |

Semantics, formal big-step rules, and the `set` vs `set!` design discussion are in
[docs/DESIGN.md](docs/DESIGN.md). The engine implements the first-order subset of
this grammar (no closures / `letrec` / `set!`).

## Repository layout

```
interpreter.rkt     Racket reference interpreter + REPL + tests (the oracle)
engine/             the C++ hot-path expression engine (see engine/README.md)
docs/DESIGN.md      interpreter semantics + the C++ engine design & results
```
