#lang racket

(provide eval repl)

;; Data Structures

;; Closure: stores parameters, body, and captured environment
(struct closure (params body env) #:transparent)

;; Primitive: wrapper for built-in operations
(struct primitive (name func) #:transparent)

;; ============================================================================
;; Environment Operations
;; ============================================================================

;; empty-env : Env
(define empty-env '())

;; extend-env : Symbol Value Env -> Env
(define (extend-env var val env)
  (cons (cons var val) env))

;; extend-env* : (Listof Symbol) (Listof Value) Env -> Env
(define (extend-env* vars vals env)
  (if (= (length vars) (length vals))
      (append (map cons vars vals) env)
      (error 'extend-env* "Argument count mismatch: ~a vars, ~a vals" 
             (length vars) (length vals))))

;; lookup-env : Symbol Env -> Value
(define (lookup-env var env)
  (let ([binding (assoc var env)])
    (if binding
        (cdr binding)
        (error 'lookup-env "Unbound variable: ~a" var))))

;; ============================================================================
;; Primitive Operations
;; ============================================================================

(define primitives
  (list
   (cons '+ (primitive '+ +))
   (cons '- (primitive '- -))
   (cons '* (primitive '* *))
   (cons '/ (primitive '/ (lambda (x y) (quotient x y))))
   (cons '== (primitive '== =))
   (cons '< (primitive '< <))
   (cons '> (primitive '> >))
   (cons '<= (primitive '<= <=))
   (cons '>= (primitive '>= >=))
   (cons 'not (primitive 'not not))
   (cons 'and (primitive 'and (lambda (x y) (and x y))))
   (cons 'or (primitive 'or (lambda (x y) (or x y))))))

;; initial-env : Env
(define initial-env primitives)

;; ============================================================================
;; Evaluator
;; ============================================================================

;; eval-expr : Expr Env -> Value
;; Implements the evaluation relation Γ ⊢ e ⇒ v
(define (eval-expr expr env)
  (match expr
    ;; NUM: Γ ⊢ n ⇒ n
    [(? number? n) n]
    
    ;; ID: Γ(x) = v  =>  Γ ⊢ x ⇒ v
    [(? symbol? x) (lookup-env x env)]
    
    ;; ABS: Γ ⊢ (λ x. e) ⇒ ⟨x, e, Γ⟩
    ;; Syntax: (lambda (x1 x2 ...) body)
    [`(lambda (,params ...) ,body)
     (closure params body env)]
    
    ;; Extended syntax: let
    ;; let ([x e]) e' ≜ @ (λ x. e') e

    ;[`(let ([,var ,val-expr]) ,body-expr)
     ;(let ([val (eval-expr val-expr env)])
      ; (eval-expr body-expr (extend-env var val env)))]

    [`(let ([,var ,val-expr]) ,body-expr)
    ; Transform let into application: same as the one above, its just more direct to the specification given
     (eval-expr `(@ (lambda (,var) ,body-expr) ,val-expr) env)]
    
    ;; IF expression (for bonus recursive test)
    [`(if ,cond-expr ,then-expr ,else-expr)
     (let ([cond-val (eval-expr cond-expr env)])
       (if cond-val
           (eval-expr then-expr env)
           (eval-expr else-expr env)))]
    
    ;; APP: Function application @ e0 e1 ... en
    ;; Syntax: (@ f arg1 arg2 ...)
    [`(@ ,e0 ,args ...)
     (let ([func (eval-expr e0 env)]
           [arg-vals (map (lambda (arg) (eval-expr arg env)) args)])
       (apply-func func arg-vals))]
    
    ;; The following commented lines were a convenience feature which wasnt given in the assignment
    ;; what it did was it allowed (+ 1 2) instead of (@ + 1 2) as well
    ;; Default: list form application (alternative syntax)
    ;;[`(,e0 ,args ...)
    ;; (let ([func (eval-expr e0 env)]
    ;;       ; this eval-expr on e0 is okay because func evaluation doesnt exactly evaluate the function in the env, returns a closure instead, which evaluates only in apply func
    ;;       [arg-vals (map (lambda (arg) (eval-expr arg env)) args)])
    ;;   (apply-func func arg-vals))]
    
    [_ (error 'eval-expr "Unknown expression: ~a" expr)]))

;; apply-func : Value (Listof Value) -> Value
;; Applies a function (closure or primitive) to arguments
(define (apply-func func args)
  (match func
    ;; APPprim: Apply primitive operation
    [(primitive name f)
     (apply f args)]
    
    ;; APP: Apply closure
    ;; Γ ⊢ e0 ⇒ ⟨x, e, Γ'⟩  Γ ⊢ ei ⇒ vi  α = { xi ↦ vi }  αΓ' ⊢ e ⇒ v
    [(closure params body captured-env)
     (cond
       ;; Multi-argument application
       [(= (length params) (length args))
        (let ([extended-env (extend-env* params args captured-env)])
          (eval-expr body extended-env))]
       
       ;; Currying: fewer arguments than parameters
       [(< (length args) (length params))
        (let* ([used-params (take params (length args))]
               [remaining-params (drop params (length args))]
               [extended-env (extend-env* used-params args captured-env)])
          (closure remaining-params body extended-env))]
       
       ;; Too many arguments: apply in stages
       [(> (length args) (length params))
        (let* ([first-args (take args (length params))]
               [remaining-args (drop args (length params))]
               [result (apply-func func first-args)])
          (apply-func result remaining-args))])]
    
    [_ (error 'apply-func "Cannot apply non-function: ~a" func)]))

;; ============================================================================
;; Main Interface
;; ============================================================================

;; eval : Expr -> Value
;; Evaluates an expression in the initial environment
(define (eval expr)
  (eval-expr expr initial-env))


;; Pretty print results
(define (pretty-print-value v)
  (match v
    [(closure params body env)
     (format "<closure: params=~a>" params)]
    [(primitive name _)
     (format "<primitive: ~a>" name)]
    [_ (format "~a" v)]))

;; Interactive interpreter REPL
(define (repl)
  (displayln "╔════════════════════════════════════════════╗")
  (displayln "║   λ-Calculus Interpreter (Assignment 3)   ║")
  (displayln "║   Author: Raunak Seksaria (2023113019)    ║")
  (displayln "╚════════════════════════════════════════════╝")
  (displayln "\nCommands: 'help', 'examples', 'run-tests', 'quit'")
  (newline)
  
  (let loop ()
    (display "λ-calc> ")
    (flush-output)
    (let ([input (read)])
      (match input
        [(? eof-object?) (displayln "\nGoodbye!")]
        ['quit (displayln "Goodbye!")]
        
        ['help 
         (displayln "\nAvailable Commands:")
         (displayln "  help       - Show this help message")
         (displayln "  examples   - Show example expressions")
         (displayln "  run-tests  - Run all test cases")
         (displayln "  clear      - Clear screen")
         (displayln "  quit       - Exit interpreter")
         (newline)
         (displayln "Syntax:")
         (displayln "  Numbers:  42")
         (displayln "  Lambda:   (lambda (x y) body)")
         (displayln "  Apply:    (@ func arg1 arg2 ...)")
         (displayln "  Let:      (let ([x val]) body)")
         (displayln "  If:       (if cond then else)")
         (newline)
         (loop)]
        
        ['examples
         (displayln "\nExample Expressions:")
         (displayln "  (@ + 1 2)")
         (displayln "  (@ (lambda (x) (@ * x x)) 5)")
         (displayln "  (let ([x 10]) (@ + x 5))")
         (displayln "  (@ (@ (lambda (x) (lambda (y) (@ + x y))) 3) 4)")
         (displayln "  (let ([f (lambda (x) (lambda (y) (@ + x y)))]) (@ (@ f 2) 3))")
         (newline)
         (loop)]
        
        ['run-tests
         (newline)
         (run-tests)
         (newline)
         (loop)]
        
        ['clear
         (cond
           [(equal? (system-type 'os) 'windows)
            (system "cls")]
           [else
            (system "clear")])
         (loop)]
        
        [expr
         (with-handlers ([exn:fail? 
                         (lambda (e) 
                           (displayln (format "✗ Error: ~a" (exn-message e))))])
           (let ([result (eval expr)])
             (displayln (format "=> ~a" (pretty-print-value result)))))
         (loop)]))))

;; Run REPL by default
(module+ main
  (repl))

;; ============================================================================
;; Example Usage
;; ============================================================================

;; Report example without using the repl: Higher-order function with environment tracking
(define report-example
  '(let ([f (lambda (x) (lambda (y) (@ + x y)))])
     (let ([g (lambda (z) (@ f z 10))])
       (@ g 5))))

(module+ main
  (newline)
  (displayln "=== Report Example ===")
  (displayln (format "Result: ~a" (eval report-example))) ; eval is used to evaluate the report-example
  (displayln "Expected: 15"))

;; ============================================================================
;; Test Suite
;; ============================================================================

(define (run-tests)
  (displayln "╔════════════════════════════════════════════╗")
  (displayln "║          Running Test Suite                ║")
  (displayln "╚════════════════════════════════════════════╝")
  (newline)
  
  (define tests-passed 0)
  (define tests-total 0)
  
  (define (test-case name expr expected)
    (set! tests-total (+ tests-total 1))
    (display (format "Test ~a: ~a ... " tests-total name))
    (with-handlers ([exn:fail? 
                     (lambda (e) 
                       (displayln (format "✗ FAILED"))
                       (displayln (format "  Error: ~a" (exn-message e))))])
      (let ([result (eval expr)])
        (if (equal? result expected)
            (begin
              (set! tests-passed (+ tests-passed 1))
              (displayln (format "✓ PASSED (=> ~a)" result)))
            (begin
              (displayln (format "✗ FAILED"))
              (displayln (format "  Expected: ~a" expected))
              (displayln (format "  Got:      ~a" result)))))))
  
  ;; Test 1: Numeric literal
  (test-case "Numeric literal" 
             42 
             42)
  
  ;; Test 2: Simple function
  (test-case "Simple function"
             '(@ (lambda (x) (@ + x 1)) 5)
             6)
  
  ;; Test 3: Nested closure
  (test-case "Nested closure"
             '(@ (@ (lambda (x) (lambda (y) (@ + x y))) 3) 4)
             7)
  
  ;; Test 4: Higher-order function
  (test-case "Higher-order function"
             '(let ([apply (lambda (f) (lambda (x) (@ f x)))])
                (@ apply (lambda (n) (@ * n 2)) 5))
             10)
  
  ;; Test 5: Captured environment
  (test-case "Captured environment"
             '(let ([adder (lambda (x) (lambda (y) (@ + x y)))])
                (let ([inc (@ adder 1)])
                  (@ inc 9)))
             10)
  
  ;; Test 6: Shadowing and lexical scoping
  (test-case "Lexical scoping"
             '(let ([x 10])
                (let ([f (lambda (y) (@ + x y))])
                  (let ([x 100])
                    (@ f 5))))
             15)
  
  ;; Test 7: Multi-argument function via currying
  (test-case "Currying"
             '(@ (@ (lambda (x) (lambda (y) (@ - x y))) 10) 3)
             7)
  
  ;; Test 8: Primitive combination
  (test-case "Primitive combination"
             '(@ + (@ * 2 3) (@ / 8 4))
             8)
  
  ;; Test 9: Recursive function (bonus)
  (test-case "Recursive factorial (Y combinator)"
             '(let ([fact (lambda (f) 
                            (lambda (n)
                              (if (@ == n 0)
                                  1
                                  (@ * n (@ (@ f f) (@ - n 1))))))])
                (@ (@ fact fact) 5))
             120)
  
  ;; Report example from assignment
  (test-case "Report example"
             '(let ([f (lambda (x) (lambda (y) (@ + x y)))])
                (let ([g (lambda (z) (@ f z 10))])
                  (@ g 5)))
             15)
  
  (newline)
  (displayln "╔════════════════════════════════════════════╗")
  (displayln (format "║  Results: ~a/~a tests passed~a║" 
                     tests-passed 
                     tests-total
                     (make-string (max 0 (- 19 
                                           (string-length (format "~a/~a tests passed" 
                                                                 tests-passed 
                                                                 tests-total)))) 
                                 #\space)))
  (if (= tests-passed tests-total)
      (displayln "║  Status: ✓ ALL TESTS PASSED               ║")
      (displayln (format "║  Status: ✗ ~a test(s) failed~a║" 
                        (- tests-total tests-passed)
                        (make-string (max 0 (- 20 
                                              (string-length (format "~a test(s) failed" 
                                                                    (- tests-total tests-passed))))) 
                                    #\space))))
  (displayln "╚════════════════════════════════════════════╝"))
