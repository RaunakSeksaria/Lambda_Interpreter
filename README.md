# λ-Calculus Interpreter with Mutable Store
**Assignment 4 - Principles of Programming Languages**  
**Author:** Raunak Seksaria (2023113019)

## Overview

This is a feature-rich λ-calculus interpreter implemented in Racket with:
- **Sequential bindings** (`let*`)
- **Mutual recursion** (`letrec`)
- **Mutable store** (`ref`, `deref`, `set`)
- **Booleans** (`#t`, `#f`)
- **Sequencing** (`seq`)
- **Alternative let* rule** (`let*2`) for exploration

## Features

### Core Lambda Calculus
- **Lambda expressions:** `(lambda (x y) body)`
- **Function application:** `(@ func arg1 arg2 ...)`
- **Let bindings:** `(let ([x val]) body)`
- **Conditionals:** `(if cond then else)`
- **Arithmetic:** `+`, `-`, `*`, `/`
- **Comparisons:** `==`, `<`, `>`, `<=`, `>=`
- **Booleans:** `and`, `or`, `not`

### Assignment 4 Extensions
- **let*:** Sequential bindings where later bindings can reference earlier ones
- **let*2:** Alternative let* where bindings are independent
- **letrec:** Mutual recursion for defining recursive functions
- **ref:** Allocate a mutable location with initial value
- **deref:** Read value from a location
- **set:** Update value at a location
- **seq:** Evaluate expressions in sequence

## Installation

### Prerequisites
- Racket 8.x or higher (tested on Racket 8.12)

### Setup
No installation needed! Just have Racket installed.

```bash
# Test if Racket is installed
racket --version
```

## Usage

### Interactive REPL

Start the interactive Read-Eval-Print Loop:

```bash
racket interpreter.rkt
```

**Commands:**
- `help` - Show help message with syntax
- `examples` - Show example expressions
- `run-tests` - Run all 22 test cases
- `clear` - Clear the screen
- `quit` - Exit the interpreter

### Running Tests

Run all test cases:

```bash
racket interpreter.rkt <<< 'run-tests
quit'
```

Or pipe test commands:

```bash
echo "run-tests" | racket interpreter.rkt
```

### Running Single Expressions

From command line:

```bash
racket -e '(require "./interpreter.rkt") (displayln (eval '\''(@ + 1 2)))'
# Output: 3
```

### Example Session

```
$ racket interpreter.rkt
╔════════════════════════════════════════════╗
║   λ-Calculus Interpreter (Assignment 4)   ║
║   Author: Raunak Seksaria (2023113019)    ║
╚════════════════════════════════════════════╝

Commands: 'help', 'examples', 'run-tests', 'quit'

λ-calc> (@ + 1 2)
=> 3

λ-calc> (let* ([x 1] [y (@ + x 1)]) (@ + x y))
=> 3

λ-calc> (letrec ([fact (lambda (n) (if (@ == n 0) 1 (@ * n (@ fact (@ - n 1)))))]) (@ fact 5))
=> 120

λ-calc> (let ([r (ref 10)]) (seq (set r (@ + (deref r) 5)) (deref r)))
=> 15

λ-calc> quit
Goodbye!
```

## Examples

### Basic Arithmetic
```racket
(@ + 1 2)                                    ; => 3
(@ * (@ + 2 3) (@ - 10 5))                   ; => 25
```

### Lambda and Application
```racket
(@ (lambda (x) (@ * x x)) 5)                 ; => 25
(let ([square (lambda (x) (@ * x x))])
  (@ square 7))                               ; => 49
```

### Sequential Bindings (let*)
```racket
(let* ([x 1]
       [y (@ + x 1)]
       [z (@ + y 1)])
  z)                                          ; => 3
```

### Alternative let*2 (Independent Bindings)
```racket
; This works - bindings don't depend on each other
(let*2 ([x 5] [y 10])
  (@ + x y))                                  ; => 15

; This would error - y can't see x in let*2
; (let*2 ([x 5] [y (@ + x 1)]) y)
```

### Mutual Recursion (letrec)
```racket
; Factorial
(letrec ([fact (lambda (n)
                 (if (@ == n 0)
                     1
                     (@ * n (@ fact (@ - n 1)))))])
  (@ fact 5))                                 ; => 120

; Even/Odd mutual recursion
(letrec ([even (lambda (n)
                 (if (@ == n 0)
                     #t
                     (@ odd (@ - n 1))))]
         [odd (lambda (n)
                (if (@ == n 0)
                    #f
                    (@ even (@ - n 1))))])
  (@ even 4))                                 ; => #t
```

### Mutable Store
```racket
; Simple mutation
(let ([r (ref 10)])
  (seq (set r 20)
       (deref r)))                            ; => 20

; Counter with closure
(let ([counter (ref 0)])
  (let ([inc (lambda ()
              (seq (set counter (@ + (deref counter) 1))
                   (deref counter)))])
    (seq (@ inc)
         (seq (@ inc)
              (@ inc)))))                     ; => 3

; Closures capturing locations
(let ([r (ref 10)])
  (let ([adder (lambda (x) (set r (@ + (deref r) x)))])
    (seq (@ adder 5)
         (deref r))))                         ; => 15
```

### Currying
```racket
(@ (@ (lambda (x) (lambda (y) (@ + x y))) 3) 4)   ; => 7
```

## Implementation Details

### Design Choices

1. **Mutable Store**: Uses a global mutable store (`box`) instead of threading store through function calls
   - Simpler code
   - No complex `let-values` needed
   - Cleaner semantics

2. **Store-Based letrec**: Functions are stored at locations to enable mutual recursion
   - Locations allocated for each function
   - Auto-dereference in function position
   - Transparent recursion

3. **Selective Auto-Dereference**: Only dereferences locations when used as functions
   - Allows `ref` to work (variables can hold locations)
   - Enables `letrec` (functions stored as locations)

### Operational Semantics

The interpreter implements the operational semantics:
```
Γ; Σ ⊢ e ⇒ v ; Σ'
```

Where:
- `Γ` is the environment (variable bindings)
- `Σ` is the store (location → value mapping)
- `e` is the expression to evaluate
- `v` is the resulting value
- `Σ'` is the updated store

## Testing

The interpreter includes 22 comprehensive test cases covering:

1. **Core lambda calculus** (Tests 1-10)
   - Basic arithmetic
   - Lambda and application
   - Let bindings
   - Conditionals
   - Closures

2. **Sequential bindings** (Tests 11-12)
   - let* with dependencies
   - Shadowing

3. **Mutual recursion** (Tests 13-14)
   - Factorial with letrec
   - Even/odd mutual recursion

4. **Mutable store** (Tests 15-19)
   - Basic ref/deref/set
   - Closures capturing locations
   - Sequencing
   - Store with recursion
   - Iterator pattern

5. **Alternative let* rule** (Tests 20-22)
   - let*2 comparison
   - Independent bindings

**Run all tests:**
```bash
racket interpreter.rkt <<< 'run-tests
quit'
```

**Expected output:**
```
Test 1: Simple addition ... ✓ PASSED (=> 3)
...
Test 22: let*2 independent bindings ... ✓ PASSED (=> 15)

╔════════════════════════════════════════════╗
║  Results: 22/22 tests passed               ║
║  Status: ✓ ALL TESTS PASSED               ║
╚════════════════════════════════════════════╝
```

## Project Structure

```
POPL_A4/
├── interpreter.rkt      # Main interpreter implementation
├── REPORT.md           # Comprehensive report with design choices
├── README.md           # This file
└── POPL_A4.pdf         # Assignment specification
```

## Documentation

See `REPORT.md` for:
- Detailed implementation choices
- Annotated AST examples
- Primitives documentation
- Phase 10 explorations (alternative let* rule, minimalism, set! design)
- Challenges and solutions

## Key Features

### Turing Complete
The interpreter is Turing complete with:
- Lambda abstraction and application
- Recursion via `letrec`
- Conditional branching via `if`
- Mutable state via `ref`/`deref`/`set`

### Error Handling
- Type checking for primitives
- Unbound variable detection
- Division by zero errors
- Location type checking for deref/set

### Pretty Printing
Values are displayed in readable format:
- Numbers: `42`
- Booleans: `#t`, `#f`
- Closures: `<closure:(x y)>`
- Primitives: `<primitive:+>`
- Locations: `<loc:0>`

## Troubleshooting

### Common Issues

**Issue:** `Unbound variable: x`
- **Solution:** Make sure variable is bound in current scope. Use `let*` for sequential dependencies.

**Issue:** `deref: Not a location`
- **Solution:** Ensure you're dereferencing a value created with `ref`.

**Issue:** `Cannot apply non-function`
- **Solution:** Check that you're applying a lambda or primitive function.

## References

- Assignment specification: `POPL_A4.pdf`
- Racket documentation: https://docs.racket-lang.org/
- Lambda calculus: Church, A. (1941). The Calculi of Lambda-Conversion

## License

Academic project for educational purposes.

## Contact

Raunak Seksaria - 2023113019
