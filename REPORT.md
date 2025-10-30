# Assignment 4: λ-Calculus Interpreter Report
**Author:** Raunak Seksaria (2023113019)

## Table of Contents
1. [Overview](#overview)
2. [Implementation Choices](#implementation-choices)
3. [Annotated AST Example](#annotated-ast-example)
4. [Primitives Documentation](#primitives-documentation)
5. [Phase 10 Explorations](#phase-10-explorations)
6. [Challenges and Solutions](#challenges-and-solutions)
7. [How to Run](#how-to-run)
---

## Overview

This interpreter extends a basic λ-calculus with:
- **Sequential bindings** (`let*`)
- **Mutual recursion** (`letrec`)
- **Mutable store** (`ref`, `deref`, `set`)
- **Booleans** (`#t`, `#f`)
- **Sequencing** (`seq`)

**Key Decision:** We implemented a **mutable store** approach instead of immutable store threading, which greatly simplified the codebase.

---

## Implementation Choices

### 1. Mutable Store Strategy

**Choice:** Global mutable store using Racket's `box`

**Rationale:**
- **Simpler code:** No need to thread store through every function call
- **No `let-values`:** Eliminated complex destructuring everywhere
- **Automatic propagation:** Store mutations happen automatically
- **Cleaner:** Code reads like imperative languages

**Implementation:**
```racket
(define the-store (box '()))  ; Global mutable store

(define (alloc-loc!)
  (let* ([store (unbox the-store)]
         [next-addr (+ 1 (foldl max -1 (map car store)))])
    (loc next-addr)))

(define (update-store! l v)
  (set-box! the-store (cons (cons (loc-addr l) v) (unbox the-store))))
```

### 2. Store-Based letrec

**Choice:** Use store locations instead of environment-based recursion

**Rationale:**
- Breaks circular dependencies naturally
- Functions are stored at locations
- Auto-dereference in function position enables transparent recursion

**Implementation Strategy:**
1. Allocate a location for each recursive binding
2. Bind variable names to these locations in the environment
3. Evaluate function bodies (they capture environment with locations)
4. Store actual closures at their locations
5. When functions are called, locations auto-dereference to closures

### 3. Selective Auto-Dereference

**Choice:** Only auto-dereference locations in function application position

**Rationale:**
- Allows `ref` to work (variables can hold locations)
- Enables `letrec` (functions stored as locations are called transparently)
- Best of both worlds

```racket
;; In APP case:
(let ([func-val (eval-expr e0 env)])
  (let ([func (match func-val
                [(loc addr) (lookup-store! func-val)]  ; Deref if location
                [_ func-val])])
    (apply-func func arg-vals)))
```

### 4. set Returns Value

**Choice:** `set` returns the assigned value

**Rationale:**
- Matches assignment semantics in most languages
- Enables chaining: `(seq (set r 5) (deref r))`
- Per specification line 120: SET returns `v`

---

## Annotated AST Example

### Example: letrec factorial

**Expression:**
```racket
(letrec ([fact (lambda (n)
                 (if (@ == n 0)
                     1
                     (@ * n (@ fact (@ - n 1)))))])
  (@ fact 5))
```

### Step-by-Step Execution with Store Snapshots

**Initial State:**
```
Γ = {+, -, *, /, ==, ...}  ; Initial environment with primitives
Σ = {}                      ; Empty store
```

**Step 1: Allocate location for `fact`**
```
l₀ = alloc-loc!()
Σ = {}                      ; Store unchanged yet
```

**Step 2: Extend environment with location binding**
```
Γ_rec = Γ[fact ↦ loc(0)]
```

**Step 3: Evaluate lambda in recursive environment**
```
val = ⟨(n), if..., Γ_rec⟩   ; Closure captures Γ_rec (which has fact ↦ loc(0))
```

**Step 4: Store closure at location**
```
update-store!(loc(0), val)
Σ = {0 ↦ ⟨(n), if..., Γ_rec⟩}
```

**Step 5: Evaluate body `(@ fact 5)` in Γ_rec**
```
fact → loc(0)  [lookup in Γ_rec]
→ auto-dereference in APP
→ ⟨(n), if..., Γ_rec⟩  [lookup in Σ]
```

**Step 6: Apply closure to 5**
```
Γ' = Γ_rec[n ↦ 5]
eval (if (@ == n 0) 1 (@ * n (@ fact (@ - n 1)))) in Γ'
```

**Step 7: Recursive call**
```
(@ fact 4) → fact → loc(0) → ⟨(n), if..., Γ_rec⟩ → apply to 4
(@ fact 3) → ...
(@ fact 2) → ...
(@ fact 1) → ...
(@ fact 0) → returns 1 (base case)
```

**Final computation:**
```
0! = 1
1! = 1 * 1 = 1
2! = 2 * 1 = 2
3! = 3 * 2 = 6
4! = 4 * 6 = 24
5! = 5 * 24 = 120
```

**Store remains:**
```
Σ = {0 ↦ ⟨(n), if..., Γ_rec⟩}
```

---

## Primitives Documentation

### Arithmetic Operations
| Primitive | Arity | Type Signature | Behavior | Error Handling |
|-----------|-------|----------------|----------|----------------|
| `+` | 2 | `number × number → number` | Addition | Type error on non-numbers |
| `-` | 2 | `number × number → number` | Subtraction | Type error on non-numbers |
| `*` | 2 | `number × number → number` | Multiplication | Type error on non-numbers |
| `/` | 2 | `number × number → number` | Integer division (quotient) | Type error, division by zero |

### Comparison Operations
| Primitive | Arity | Type Signature | Behavior | Error Handling |
|-----------|-------|----------------|----------|----------------|
| `==` | 2 | `number × number → boolean` | Equality test | Type error on non-numbers |
| `<` | 2 | `number × number → boolean` | Less than | Type error on non-numbers |
| `>` | 2 | `number × number → boolean` | Greater than | Type error on non-numbers |
| `<=` | 2 | `number × number → boolean` | Less than or equal | Type error on non-numbers |
| `>=` | 2 | `number × number → boolean` | Greater than or equal | Type error on non-numbers |

### Boolean Operations
| Primitive | Arity | Type Signature | Behavior | Error Handling |
|-----------|-------|----------------|----------|----------------|
| `not` | 1 | `any → boolean` | Logical negation | Racket's `not` (treats #f as false, else true) |
| `and` | 2 | `any × any → any` | Logical AND | Short-circuits |
| `or` | 2 | `any × any → any` | Logical OR | Short-circuits |

### Store Operations
| Construct | Arity | Type Signature | Behavior | Error Handling |
|-----------|-------|----------------|----------|----------------|
| `ref` | 1 | `value → location` | Allocates location, stores value | - |
| `deref` | 1 | `location → value` | Reads value from location | Error if not a location |
| `set` | 2 | `location × value → value` | Updates location, returns value | Error if first arg not location |

---

## Phase 10 Explorations

### 10.1: Alternative let* Rule (LET2*)

#### Implementation

We implemented **two versions** of `let*`:

**LET1* (Standard - `let*`):** Sequential bindings where each can reference previous ones
```racket
[`(let* (,bindings ...) ,body)
 (let ([final-env
        (foldl (lambda (binding env)
                 (let ([val (eval-expr expr env)])  ; Uses current env
                   (extend-env var val env)))      ; Extends env
               env
               bindings)])
   (eval-expr body final-env))]
```

**LET2* (Alternative - `let*2`):** All bindings use original environment
```racket
[`(let*2 (,bindings ...) ,body)
 (let* ([vals (map (lambda (expr) (eval-expr expr env)) exprs)]  ; ALL use original env
        [final-env (foldl extend-env env vars vals)])
   (eval-expr body final-env))]
```

#### Behavioral Differences

**Example 1: Dependency**
```racket
; LET1* (standard):
(let* ([x 1] [y (@ + x 1)]) y)  
; => 2 (y can see x)

; LET2*:
(let*2 ([x 1] [y (@ + x 1)]) y) 
; => ERROR! (y cannot see x from bindings)
```

**Example 2: Independent bindings**
```racket
; Both work the same:
(let* ([x 5] [y 10]) (@ + x y))   ; => 15
(let*2 ([x 5] [y 10]) (@ + x y))  ; => 15
```

**Example 3: Outer scope**
```racket
; Both can see outer scope:
(let ([z 100])
  (let* ([x 1] [y 2]) (@ + (@ + z x) y)))   ; => 103
(let ([z 100])
  (let*2 ([x 1] [y 2]) (@ + (@ + z x) y)))  ; => 103
```

#### Use Cases

**LET1* is better when:**
- Bindings build on each other
- Computing derived values
- Traditional let* semantics needed

**LET2* is better when:**
- All bindings are independent
- Parallel evaluation is desired (conceptually)
- Want to prevent accidental dependencies

---

### 10.2: Minimalism Discussion

Can we encode the new constructs using simpler primitives?

#### Encoding let* as nested let

**Encoding:**
```racket
(let* ([x e1] [y e2] [z e3]) body)
≡
(let ([x e1])
  (let ([y e2])
    (let ([z e3])
      body)))
```

**Trade-offs:**
- ✓ No new construct needed
- ✓ Semantics identical
- ✗ Verbose for many bindings
- ✗ Deep nesting hurts readability

#### Encoding letrec using ref/deref/set

**Encoding (single function):**
```racket
(letrec ([f e]) body)
≡
(let ([f-loc (ref '<undef>)])
  (seq (set f-loc (lambda ...(deref f-loc)...))
       (let ([f (deref f-loc)])
         body)))
```

**Challenges:**
- Function bodies need explicit `deref` calls
- Mutual recursion requires multiple locations
- Verbose and error-prone
- **Our implementation handles this automatically!**

**Why our store-based letrec is better:**
- Transparent: no manual deref needed
- Auto-dereference in function position
- Cleaner syntax for users

#### Encoding seq as let

**Encoding:**
```racket
(seq e1 e2)
≡
(let ([_ e1]) e2)
```

**Trade-offs:**
- ✓ Simple encoding
- ✓ Semantically equivalent
- ✓ Could eliminate `seq` from core
- ✗ Underscore convention not enforced
- ✗ Less explicit intent

#### Encoding if as lambda (Church encodings)

**Encoding:**
```racket
(if cond then else)
≡
(@ (@ (@ cond (lambda () then)) (lambda () else)))
```
Where `#t = (lambda (t f) (@ t))` and `#f = (lambda (t f) (@ f))`

**Trade-offs:**
- ✓ Possible in pure lambda calculus
- ✗ Extremely inefficient
- ✗ No short-circuit evaluation
- ✗ Requires Church boolean encoding
- **Direct implementation is far better**

#### Conclusion

While most constructs *can* be encoded, **direct implementation provides:**
- Better performance
- Clearer semantics
- Better error messages
- More intuitive for users

Our mutable store-based approach particularly shines with `letrec`, providing transparent recursion that would be cumbersome to encode manually.

---

### 10.3: Alternative set! Design

#### Current Design: Explicit Locations

**Syntax:** `(set location-expr value-expr)`

```racket
(let ([r (ref 10)])
  (set r 20)
  (deref r))  ; => 20
```

**Pros:**
- ✓ Explicit about what's mutable
- ✓ Supports aliasing (multiple variables reference same location)
- ✓ First-class locations can be passed around
- ✓ Clear separation: variables vs. locations

**Cons:**
- ✗ More verbose
- ✗ Need explicit `ref` and `deref`
- ✗ Two-level indirection

#### Alternative: Variable-Based set!

**Syntax:** `(set! variable-name value-expr)`

```racket
(let ([x 10])
  (set! x 20)
  x)  ; => 20
```

**Implementation sketch:**
```racket
[`(set! ,var ,expr)
 ; Would need to modify environment entry in-place
 ; OR store variables in store and track in environment
 ...]
```

**Pros:**
- ✓ Simpler syntax
- ✓ Familiar to Scheme/Lisp users
- ✓ No explicit ref/deref needed

**Cons:**
- ✗ No aliasing support
- ✗ Can't pass mutable references
- ✗ Requires mutable environments or environment-as-store
- ✗ Less explicit about mutation

#### Comparison Example

**Aliasing with explicit locations:**
```racket
(let ([r (ref 0)])
  (let ([a r]
        [b r])
    (seq (set a 5)
         (deref b))))  ; => 5 (b sees change through shared location)
```

**Would NOT work with set!:**
```racket
(let ([x 0])
  (let ([a x]  ; a gets VALUE 0
        [b x]) ; b gets VALUE 0
    (seq (set! a 5)
         b)))  ; => 0 (b doesn't see change, they're independent)
```

#### Our Choice: Explicit Locations

We chose **explicit locations** because:
1. **Full expressiveness:** Supports aliasing and first-class references
2. **Clear semantics:** Easy to understand when mutation occurs
3. **Matches assignment:** Spec uses ref/deref/set
4. **More powerful:** Can implement set! using ref/deref, but not vice versa

---

## Challenges and Solutions

### Challenge 1: Store Threading Complexity

**Problem:** Initial immutable store approach required threading store through every function call, using complex `let-values` and `foldl` with cons pairs.

**Solution:** Switched to mutable global store using `box`. This eliminated:
- All `let-values` destructuring
- Complex accumulator patterns in `foldl`
- Manual store threading

**Result:** Code became 50% shorter and much more readable.

### Challenge 2: letrec Circular Dependencies

**Problem:** Functions need to reference themselves and each other, but closures capture environment at creation time.

**Solution:** Store-based letrec strategy:
1. Allocate locations for functions
2. Bind names to locations (not closures)
3. Evaluate lambdas (they capture env with locations)
4. Store closures at their locations
5. Auto-dereference in function position

**Result:** Transparent mutual recursion without manual fixed-point combinators.

### Challenge 3: Selective Auto-Dereference

**Problem:** Auto-dereferencing all variable lookups broke `ref` (couldn't hold locations in variables). Not auto-dereferencing broke `letrec` (couldn't call functions).

**Solution:** Only auto-dereference in function application position:
```racket
;; In APP case:
(let ([func-val (eval-expr e0 env)])
  (let ([func (match func-val
                [(loc addr) (lookup-store! func-val)]
                [_ func-val])])
    (apply-func func arg-vals)))
```

**Result:** Both `ref` and `letrec` work perfectly.

### Challenge 4: foldl Ordering Bug in letrec

**Problem:** `foldl` builds environment in reverse order, but `map` evaluates expressions in forward order. This caused functions to be stored at wrong locations.

**Solution:** Reverse the `vals` list before storing:
```racket
(for-each (lambda (loc val) (update-store! loc val)) 
          locs 
          (reverse vals))
```

**Result:** Functions correctly bound to their intended locations.

---

## How to Run
### Running the Interpreter

**Interactive REPL:**
```bash
racket interpreter.rkt
```

**Run specific expression:**
```bash
racket -e '(require "./interpreter.rkt") (displayln (eval '\''(expr)))'
```

### Running Tests

**All tests:**
```bash
racket interpreter.rkt <<< 'run-tests
quit'
```



# 8.2: set!

Notes, rules, pros/cons, and comparison (short):

Operational rule for set! (informal):

Evaluate e to v (using current store Σ).
Find binding cell b = assoc(var, Γ). If b exists, mutate its cdr to v (set-cdr! b v). Result value v, store Σ unchanged.
Error if var unbound.
Pros of set! (variable-based):

Simple syntax for reassigning variables.
Can mutate captured variables (closures see updated value) without explicit refs.
Matches many high-level languages (e.g., Scheme set!).
Cons of set! (variable-based):

Mutation targets env binding cells, not a separate heap—aliasing semantics differ.
Harder to create true shared mutable references between variables unless you store a location value.
Mutating a binding affects every place that shares the same cons cell — which is subtle and depends on how envs are constructed.
Pros of set (location-based / store-based):

Explicit heap; references can be first-class and shared (multiple variables can hold the same loc).
Clear separation between environment and heap; predictable aliasing.
Useful for implementing data structures with shared mutable state.
Cons of set (location-based):

Requires explicit ref/deref syntax to create and access references.
Verbose when you only want to mutate a local variable captured by closures.
Behavior possible in one design but not the other (example):

Shared aliasing between variables without locations:
With location-based set/ref you can do:
```racket
(let ([r (ref 0)])
(let ([a r] [b r])
(set a 5)
(deref b))) => 5
```
With variable-based set! you cannot create the same "shared cell" between two distinct variable bindings a and b without explicitly using a location value; set! mutates the individual binding cell for the name, not some separately allocated shared cell.
Conversely, variable-based set! lets you write succinctly:
```racket
(let ([x 1])
(let ([f (lambda () x)])
(set! x 2)
(@ f)) => 2
```
Achieves the same with locations only by making x a ref and using deref in the closure.