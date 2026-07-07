# λ-Calculus Interpreter

An interpreter for an extended λ-calculus with a mutable store, written in Racket. The core calculus (lambda abstraction, application, `let`, `if`) is extended with sequential and recursive bindings, first-class store locations, sequencing, and loops. Ships with an interactive REPL and a built-in 32-case test suite.

## Features

- **Lambda and application** — explicit application syntax `(@ f x y)`, closures, currying
- **Bindings** — `let`; sequential `let*`; and `let*2`, an alternative semantics where bindings cannot see each other
- **Recursion** — `letrec`, including mutual recursion
- **Mutable store** — first-class locations with `ref` / `deref` / `set`, plus direct variable mutation with `set!`
- **Control flow** — `if` with booleans, `seq` for sequencing, `while` loops
- **Primitives** — arithmetic `+ - * /` (integer division), comparison `== < > <= >=`, boolean `not and or` (short-circuiting), all with type checking

### Syntax at a glance

| Construct | Syntax |
|-----------|--------|
| Application | `(@ f arg ...)` |
| Lambda | `(lambda (x y) body)` |
| Let / sequential | `(let ([x e]) body)`, `(let* ([x e1] [y e2]) body)` |
| Recursion | `(letrec ([f (lambda (n) ...)]) body)` |
| Store | `(ref e)`, `(deref l)`, `(set l v)`, `(set! x v)` |
| Sequencing | `(seq e1 e2)` |
| Loop | `(while cond body)` |

## Getting started

Requires Racket 8.x (tested on 8.12). Start the REPL:

```bash
racket interpreter.rkt
```

```
λ-calc> (@ + 1 2)
=> 3

λ-calc> (letrec ([fact (lambda (n) (if (@ == n 0) 1 (@ * n (@ fact (@ - n 1)))))]) (@ fact 5))
=> 120
```

REPL commands: `help` (syntax reference), `examples` (sample expressions), `run-tests` (test suite), `clear`, `quit`.

To evaluate a single expression from the shell:

```bash
racket -e '(require "./interpreter.rkt") (displayln (eval '\''(@ + 1 2)))'
```

## Examples

Closures and currying:

```racket
(let ([square (lambda (x) (@ * x x))])
  (@ square 7))                                     ; => 49

(@ (@ (lambda (x) (lambda (y) (@ + x y))) 3) 4)     ; => 7
```

Sequential bindings:

```racket
(let* ([x 1]
       [y (@ + x 1)]
       [z (@ + y 1)])
  z)                                                ; => 3
```

Mutual recursion:

```racket
(letrec ([even (lambda (n) (if (@ == n 0) #t (@ odd  (@ - n 1))))]
         [odd  (lambda (n) (if (@ == n 0) #f (@ even (@ - n 1))))])
  (@ even 4))                                       ; => #t
```

Mutable state — a counter closed over a store location:

```racket
(let ([counter (ref 0)])
  (let ([inc (lambda ()
               (seq (set counter (@ + (deref counter) 1))
                    (deref counter)))])
    (seq (@ inc)
         (seq (@ inc)
              (@ inc)))))                           ; => 3
```

Closures sharing a location:

```racket
(let ([r (ref 10)])
  (let ([adder (lambda (x) (set r (@ + (deref r) x)))])
    (seq (@ adder 5)
         (deref r))))                               ; => 15
```

## Testing

Run the 32-case suite (core calculus, bindings, recursion, store operations, loops, and the alternative `let*2` / `set!` semantics):

```bash
echo "run-tests" | racket interpreter.rkt
```

## Design

Evaluation is environment-based with a global mutable store (a Racket `box`), so the store is not threaded through the evaluator. `letrec` is implemented with boxed environment bindings that are patched in place, and `set` works on both explicit locations and plain variables via an implicit location table. Design rationale, formal big-step rules, and the `set` vs `set!` comparison live in [docs/DESIGN.md](docs/DESIGN.md).
