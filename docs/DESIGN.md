# Design Notes

Implementation notes for the interpreter: the store model, the environment machinery behind `letrec`, the semantics of the binding and mutation constructs, and loop design. The evaluation relation throughout is big-step: `Γ; Σ ⊢ e ⇒ v ; Σ'` (environment Γ, store Σ).

## Mutable store

The store is a single global mutable value rather than an immutable store threaded through evaluation:

```racket
(define the-store (box '()))  ; Global mutable store

(define (alloc-loc!)
  (let* ([store (unbox the-store)]
         [next-addr (+ 1 (foldl max -1 (map car store)))])
    (loc next-addr)))

(define (update-store! l v)
  (set-box! the-store (cons (cons (loc-addr l) v) (unbox the-store))))
```

Threading an immutable store means every evaluation rule returns `(values result new-store)` and every call site destructures it with `let-values`. Making the store a global `box` removes that plumbing entirely: mutations propagate automatically and the evaluator reads like the rules it implements. The trade-off is that evaluation order becomes observable through the store — which is fine here, since the language is call-by-value with a fixed left-to-right order.

Each top-level `eval` resets the store, so REPL entries are independent.

## letrec via boxed environment bindings

`letrec` follows the formal update rule Γr[fi ↦ vi]: the environment *structure* stays immutable, but the recursive bindings hold mutable boxes.

1. Extend the environment with boxed placeholder values (`extend-env-mut*`).
2. Evaluate each right-hand side in that extended environment, so the closures capture Γr.
3. Patch the boxes in place with the resulting closures (`update-env-mut!`).
4. Variable lookup (`lookup-env-mut`) auto-unboxes, so the rest of the evaluator never sees a box.

This is what makes mutual recursion work: `even` and `odd` both close over the same Γr, and by the time either is applied, both boxes contain real closures.

## Implicit mutable variables

`set` primarily targets store locations, but it also works on plain variables:

```racket
(let ([n 5]) (set n 10))
```

A global `var-loc-table` maps variables to implicit locations. The first `set` on a variable allocates a location and registers it; subsequent lookups check the table. Bindings are converted from immutable to mutable on demand, without changing the environment representation.

## `let*` vs `let*2`

Two sequential-binding semantics are implemented.

**`let*` (standard):** each binding is evaluated in the environment extended by all previous bindings.

```racket
[`(let* (,bindings ...) ,body)
 (let ([final-env
        (foldl (lambda (binding env)
                 (let ([val (eval-expr expr env)])  ; uses current env
                   (extend-env var val env)))
               env
               bindings)])
   (eval-expr body final-env))]
```

**`let*2` (alternative):** every binding is evaluated in the *original* environment, then all are added at once.

```racket
[`(let*2 (,bindings ...) ,body)
 (let* ([vals (map (lambda (expr) (eval-expr expr env)) exprs)]  ; all use original env
        [final-env (foldl extend-env env vars vals)])
   (eval-expr body final-env))]
```

The difference shows up as soon as a binding refers to an earlier one:

```racket
(let*  ([x 1] [y (@ + x 1)]) y)   ; => 2
(let*2 ([x 1] [y (@ + x 1)]) y)   ; => error: y cannot see x
```

Independent bindings and references to the outer scope behave identically in both. `let*2` amounts to a parallel-binding `let` over multiple names — useful when bindings should be provably independent, since accidental dependencies become errors.

`let*` itself is expressible as nested `let`:

```racket
(let* ([x e1] [y e2]) body)  ≡  (let ([x e1]) (let ([y e2]) body))
```

The dedicated form exists to avoid the deep nesting, not because it adds power.

## `set` vs `set!`

Two mutation designs coexist, and they mutate different things: `set` updates the **store** Σ at a location, `set!` updates the **environment** Γ binding of a variable.

**Location-based `set`:**

```
Γ; Σ  ⊢ e₁ ⇒ loc l ; Σ₁
Γ; Σ₁ ⊢ e₂ ⇒ v ; Σ₂
Σ₃ = Σ₂[l ↦ v]
─────────────────────────────
Γ; Σ  ⊢ set e₁ e₂ ⇒ v ; Σ₃
```

**Variable-based `set!`:**

```
Γ; Σ ⊢ e ⇒ v ; Σ'
Γ' = Γ[x ↦ v]
─────────────────────────────
Γ; Σ ⊢ set! x e ⇒ v ; Σ'
```

With immutable environments, `set!` is simulated: look up the variable, find (or allocate) its backing location, and update the store there. This gives the convenient syntax without making environments actually mutable.

The semantic gap is aliasing. Locations are first-class values, so two variables can share one:

```racket
(let ([r (ref 0)])
  (let ([counter1 r]
        [counter2 r])
    (seq (set counter1 (@ + (deref counter1) 1))
         (deref counter2))))   ; => 1 — counter2 sees counter1's write
```

Variable bindings can't be shared that way: `set!` on `counter1` changes *its* binding, and `counter2` keeps its own. The same distinction appears with closures — a getter and setter closing over the same location observably share state, while closures over separate bindings may not. And only locations can be returned from a function as a mutable handle; a variable exists only in its lexical scope.

| Capability | `set` (location) | `set!` (variable) |
|------------|------------------|-------------------|
| Aliasing / sharing | yes | no |
| First-class references | yes | no |
| Return a mutable handle | yes | no |
| Syntax overhead | needs `ref`/`deref` | direct |
| Environment purity | preserved | conceptually broken |

## Loops

### `while`: primitive, not sugar

`while` is encodable with `letrec`:

```racket
(while e_cond e_body)
≡
(letrec ([loop (lambda (_)
                 (if e_cond
                     (seq e_body (@ loop 'unit))
                     'undefined))])
  (@ loop 'unit))
```

The encoding keeps the core small, but it costs a closure application per iteration and — more importantly — closes the door on loop-level control flow (`break`/`continue` have no loop boundary to target in the desugared form). The interpreter implements `while` directly in the evaluator instead: direct recursion, better errors, and room for control-flow extensions.

### `do-while`

Body first, then the condition:

```
Γ; Σ  ⊢ e_body ⇒ v₁ ; Σ₁
Γ; Σ₁ ⊢ e_cond ⇒ v_cond ; Σ₂
(v_cond = true  ⟹  Γ; Σ₂ ⊢ do-while e_cond e_body ⇒ v₂ ; Σ₃)
(v_cond = false ⟹  Σ₃ = Σ₂, v₂ = ⊥)
────────────────────────────────────────────────────────
Γ; Σ  ⊢ do-while e_cond e_body ⇒ v₂ ; Σ₃
```

The body always runs at least once — the shape wanted for input-validation and menu loops.

### `break` and `continue`

Non-local control flow needs either an exception mechanism or tagged return values. The tagged-value design: `break e` evaluates to `(break v)`, `continue` to `(continue)`; these propagate through `seq` and are caught at the loop boundary.

```
WHILE-BREAK:
Γ; Σ  ⊢ e_cond ⇒ true ; Σ₁
Γ; Σ₁ ⊢ e_body ⇒ (break v) ; Σ₂
────────────────────────────────────────────
Γ; Σ  ⊢ while e_cond e_body ⇒ v ; Σ₂

WHILE-CONTINUE:
Γ; Σ  ⊢ e_cond ⇒ true ; Σ₁
Γ; Σ₁ ⊢ e_body ⇒ (continue) ; Σ₂
Γ; Σ₂ ⊢ while e_cond e_body ⇒ v ; Σ₃
────────────────────────────────────────────
Γ; Σ  ⊢ while e_cond e_body ⇒ v ; Σ₃
```

Design choice: `break` carries a value (so search loops can return their result), `continue` returns unit.

### `for` as sugar, and why it resists desugaring

The obvious desugaring

```racket
(for e_init e_cond e_step e_body)  ≡  (seq e_init (while e_cond (seq e_body e_step)))
```

is wrong: a loop variable declared in `e_init` isn't in scope for the condition, step, or body. The initialization has to *wrap* the loop:

```racket
(for ([i (ref 0)]) e_cond e_step e_body)
≡
(let ([i (ref 0)])
  (while e_cond (seq e_body e_step)))
```

This scoping quirk — init creating a scope that encloses the whole loop — is why many languages make `for` a primitive rather than sugar.

## Worked example: `letrec` factorial, with store

```racket
(letrec ([fact (lambda (n)
                 (if (@ == n 0)
                     1
                     (@ * n (@ fact (@ - n 1)))))])
  (@ fact 5))
```

1. Allocate a slot for `fact` and extend: `Γr = Γ[fact ↦ loc(0)]`, `Σ = {}`.
2. Evaluate the lambda in Γr — the closure captures the environment that already contains `fact`: `⟨(n), if…, Γr⟩`.
3. Store it: `Σ = {0 ↦ ⟨(n), if…, Γr⟩}`.
4. Evaluate `(@ fact 5)` in Γr: `fact` resolves to `loc(0)`, is auto-dereferenced in application position, and the closure is applied with `Γ' = Γr[n ↦ 5]`.
5. Each recursive call `(@ fact 4)`, `(@ fact 3)`, … resolves through the same location, down to the base case, giving `5! = 120`.

The store still holds only the one closure at the end — recursion goes through the environment/location indirection, not through store growth.

## REPL

The REPL is a tail-recursive loop dispatching on input with `match`; expression errors are caught with `with-handlers` so a bad input doesn't kill the session:

```racket
(define (repl)
  (let loop ()
    (display "λ-calc> ")
    (flush-output)
    (let ([input (read)])
      (match input
        ['quit (displayln "Goodbye!")]
        ['help (show-help) (loop)]
        ['examples (show-examples) (loop)]
        ['run-tests (run-tests) (loop)]
        [expr
         (with-handlers ([exn:fail? (lambda (e) ...)])
           (displayln (pretty-print-value (eval expr))))
         (loop)]))))
```

Internal values are abstracted for display — `<closure: params=(x y)>`, `<primitive: +>`, `<loc:0>` — and the store is reset before each top-level evaluation so entries don't interfere.
