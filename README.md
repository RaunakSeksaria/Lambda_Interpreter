# λ-Calculus Interpreter with Mutable Store
**Assignment 4 - Principles of Programming Languages**  
**Author:** Raunak Seksaria (2023113019)

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
