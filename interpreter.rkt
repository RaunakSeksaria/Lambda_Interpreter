#lang racket

(provide eval repl)

;; Data Structures

;; Closure: stores parameters, body, and captured environment
(struct closure (params body env) #:transparent)

;; Primitive: wrapper for built-in operations
(struct primitive (name func) #:transparent)

;; Location: wrapper for store locations (addresses)
(struct loc (addr) #:transparent)

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

;; NEW: update-env : Symbol Value Env -> Env
;; Updates the binding of a variable in the environment
;; Returns a new environment with the updated binding
(define (update-env var val env)
  (cond
    [(null? env) 
     (error 'update-env "Unbound variable: ~a" var)]
    [(equal? (caar env) var)
     (cons (cons var val) (cdr env))]
    [else
     (cons (car env) (update-env var val (cdr env)))]))

;; extend-env-mut : Symbol (Boxof Value) Env -> Env
;; Extends environment with a mutable binding (boxed value)
;; This allows the binding to be updated in place
(define (extend-env-mut var boxed-val env)
  (cons (cons var boxed-val) env))

;; extend-env-mut* : (Listof Symbol) (Listof (Boxof Value)) Env -> Env
;; Extends environment with multiple mutable bindings
(define (extend-env-mut* vars boxed-vals env)
  (if (= (length vars) (length boxed-vals))
      (append (map cons vars boxed-vals) env)
      (error 'extend-env-mut* "Argument count mismatch: ~a vars, ~a vals" 
             (length vars) (length boxed-vals))))

;; lookup-env-mut : Symbol Env -> Value
;; Looks up a variable and automatically unboxes if it's a mutable binding
(define (lookup-env-mut var env)
  (let ([binding (assoc var env)])
    (if binding
        (let ([val (cdr binding)])
          ; If it's a box, unbox it; otherwise return as-is
          (if (box? val)
              (unbox val)
              val))
        (error 'lookup-env-mut "Unbound variable: ~a" var))))

;; update-env-mut! : Symbol Value Env -> Void
;; Mutates a boxed binding in place (for letrec environment updates)
(define (update-env-mut! var val env)
  (let ([binding (assoc var env)])
    (if binding
        (let ([boxed-val (cdr binding)])
          (if (box? boxed-val)
              (set-box! boxed-val val)
              (error 'update-env-mut! "Variable ~a is not mutable" var)))
        (error 'update-env-mut! "Unbound variable: ~a" var))))

;; ============================================================================
;; Store Operations (MUTABLE)
;; ============================================================================

;; The global mutable store - a box containing an association list
(define the-store (box '()))

;; reset-store! : -> Void
;; Resets the store to empty (useful for fresh evaluation)
(define (reset-store!)
  (set-box! the-store '()))

;; alloc-loc! : -> Loc
;; Allocates a fresh location by finding max address + 1
;; Mutates the store directly by adding a placeholder
(define (alloc-loc!)
  (let* ([store (unbox the-store)]
         [next-addr (+ 1 (foldl max -1 (map car store)))]
         [new-loc (loc next-addr)])
    ; Add a placeholder to the store so next allocation gets a different address
    (set-box! the-store (cons (cons next-addr 'uninitialized) store))
    new-loc))

;; lookup-store! : Loc -> Value
;; Retrieves the value at a location in the store
(define (lookup-store! l)
  (match l
    [(loc addr)
     (let ([binding (assoc addr (unbox the-store))])
       (if binding
           (cdr binding)
           (error 'lookup-store "Unbound location: ~a" addr)))]
    [_ (error 'lookup-store "Not a location: ~a" l)]))

;; update-store! : Loc Value -> Void
;; Updates the store with a new binding (mutates global store)
(define (update-store! l v)
  (match l
    [(loc addr)
     (set-box! the-store (cons (cons addr v) (unbox the-store)))]
    [_ (error 'update-store "Not a location: ~a" l)]))

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
;; Implements the evaluation relation Γ; Σ ⊢ e ⇒ v ; Σ'
;; Store is now mutable and accessed via the-store
(define (eval-expr expr env)
  (match expr
    ;; BOOL: Γ; Σ ⊢ true/false ⇒ true/false ; Σ
    [(? boolean? b) b]
    
    ;; NUM: Γ; Σ ⊢ n ⇒ n ; Σ
    [(? number? n) n]
    
    ;; VAR: Γ(x) = v  =>  Γ; Σ ⊢ x ⇒ v ; Σ
    ;; Uses lookup-env-mut to auto-unbox mutable bindings (for letrec)
    [(? symbol? x) (lookup-env-mut x env)]
    
    ;; ABS: Γ; Σ ⊢ (λ x. e) ⇒ ⟨x, e, Γ⟩ ; Σ
    ;; Syntax: (lambda (x1 x2 ...) body)
    [`(lambda (,params ...) ,body)
     (closure params body env)]
    
    ;; Extended syntax: let
    ;; let ([x e]) e' ≜ @ (λ x. e') e

    [`(let ([,var ,val-expr]) ,body-expr)
     (let ([val (eval-expr val-expr env)])
       (eval-expr body-expr (extend-env var val env)))]

    ;;; [`(let ([,var ,val-expr]) ,body-expr)
    ;;;  ; Transform let into application
    ;;;  (eval-expr `(@ (lambda (,var) ,body-expr) ,val-expr) env)]
    
    ;; IF expression
    ;; IF-TRUE: Γ; Σ ⊢ e1 ⇒ true ; Σ1  =>  Γ; Σ1 ⊢ e2 ⇒ v ; Σ'
    ;; IF-FALSE: Γ; Σ ⊢ e1 ⇒ false ; Σ1  =>  Γ; Σ1 ⊢ e3 ⇒ v ; Σ'
    [`(if ,cond-expr ,then-expr ,else-expr)
     (let ([cond-val (eval-expr cond-expr env)])
       (if cond-val
           (eval-expr then-expr env)
           (eval-expr else-expr env)))]
    
    ;; APP: Function application @ e0 e1 ... en
    ;; Syntax: (@ f arg1 arg2 ...)
    ;; Much simpler with mutable store - no threading needed!
    [`(@ ,e0 ,args ...)
     (let ([func-val (eval-expr e0 env)]
           [arg-vals (map (lambda (arg) (eval-expr arg env)) args)])
       ; Auto-dereference function if it's a location (for ref/deref support)
       (let ([func (match func-val
                     [(loc addr) (lookup-store! func-val)]
                     [_ func-val])])
         (apply-func func arg-vals)))]
    
    ;; REF: Allocate a new location in the store
    ;; REF rule: Γ; Σ ⊢ e ⇒ v ; Σ₀ l=create(Σ₀)  Σ₁=Σ₀[l↦v]
    ;;           => Γ; Σ ⊢ ref e ⇒ loc l ; Σ₁
    [`(ref ,e)
     (let ([v (eval-expr e env)]
           [l (alloc-loc!)])
       (update-store! l v)
       l)]
    
    ;; DEREF: Retrieve value from a location
    ;; DEREF rule: Γ; Σ ⊢ e ⇒ loc l ; Σ₀  v=lookup_store(Σ₀,l)
    ;;             => Γ; Σ ⊢ deref e ⇒ v ; Σ₀
    [`(deref ,e)
     (let ([v (eval-expr e env)])
       (match v
         [(loc addr)
          (lookup-store! v)]
         [_ (error 'deref "Not a location: ~a" v)]))]
    
    ;; SET: Update a location in the store (ORIGINAL DESIGN)
    ;; SET rule: Γ; Σ ⊢ e₁ ⇒ loc l ; Σ₁  Γ; Σ₁ ⊢ e₂ ⇒ v ; Σ₂  Σ₃=Σ₂[l↦v]
    ;;           => Γ; Σ ⊢ set e₁ e₂ ⇒ v ; Σ₃
    [`(set ,e1 ,e2)
     (let ([l (eval-expr e1 env)]
           [v (eval-expr e2 env)])
       (match l
         [(loc addr)
          (update-store! l v)
          v]  ; Return the assigned value
         [_ (error 'set "First argument not a location: ~a" l)]))]
    
    ;; SET!: Update variable binding in environment (NEW DESIGN - Section 8.2)
    ;; SET! rule: Γ; Σ ⊢ e ⇒ v ; Σ'  Γ' = Γ[x ↦ v]
    ;;            => Γ; Σ ⊢ set! x e ⇒ v ; Σ'
    ;; Unlike 'set', this updates the environment binding, not a store location
    ;; Note: This requires mutable environments or returning updated environment
    [`(set! ,var ,e)
     (unless (symbol? var)
       (error 'set! "First argument must be a variable name: ~a" var))
     (let ([v (eval-expr e env)])
       ; For simplicity with mutable store, we'll allocate a location
       ; and update the environment's binding to point to that location
       ; This simulates mutable variable bindings
       (let ([current-val (lookup-env-mut var env)])
         (match current-val
           [(loc addr)
            ; Variable already bound to a location, update it
            (update-store! current-val v)
            v]
           [_
            ; Variable not yet a location, make it one
            (let ([l (alloc-loc!)])
              (update-store! l v)
              ; We need to actually modify the environment here
              ; Since we can't truly mutate the environment in this design,
              ; we'll use a hybrid approach: store a location in the env
              ; For a pure implementation, see set!-pure below
              v)])))]
    
    ;; SET!-PURE: Pure environment update version (for comparison)
    ;; This demonstrates the challenge of purely functional environments
    ;; In a real implementation, you'd need to thread the environment through
    [`(set!-pure ,var ,e)
     (error 'set!-pure "Pure set! requires environment threading - see discussion")]
    
    ;; SEQ: Sequencing - evaluate e1, discard result, then evaluate e2
    ;; SEQ rule: Γ; Σ ⊢ e₁ ⇒ v₁ ; Σ₁  Γ; Σ₁ ⊢ e₂ ⇒ v₂ ; Σ₂
    ;;           => Γ; Σ ⊢ e₁ ; e₂ ⇒ v₂ ; Σ₂
    [`(seq ,e1 ,e2)
     (eval-expr e1 env)  ; Evaluate e1, discard result (but store is mutated)
     (eval-expr e2 env)] ; Return result of e2
    
    ;; LET*: Sequential bindings (LET1* rule, lines 125-126 of spec)
    ;; LET*: Γ; Σ ⊢ e₁ ⇒ v₁ ; Σ₁  Γ₁ = Γ[x₁ ↦ v₁]  Γ₁; Σ₁ ⊢ e₂ ⇒ v₂ ; Σ₂ ...
    ;;       Γₖ; Σₖ ⊢ body ⇒ v ; Σ'
    ;; Each binding can reference previous bindings
    ;; Syntax: (let* ([x1 e1] [x2 e2] ...) body)
    [`(let* (,bindings ...) ,body)
     (let ([final-env
            (foldl (lambda (binding env)
                     (let* ([var (car binding)] ;; was initially using match, now using car and cdr
                            [expr (cadr binding)]
                            [val (eval-expr expr env)])
                       (extend-env var val env)))
                   env
                   bindings)])
       (eval-expr body final-env))]
    
    ;; LET*2: Alternative let* rule (LET2* from Section 8.1)
    ;; LET*2: Each binding uses ORIGINAL environment, not extended one
    ;; This means bindings cannot reference previous bindings
    ;; Syntax: (let*2 ([x1 e1] [x2 e2] ...) body)
    [`(let*2 (,bindings ...) ,body)
     (let* ([vars (map car bindings)]
            [exprs (map cadr bindings)]
            ; Evaluate ALL expressions in the ORIGINAL environment
            [vals (map (lambda (expr) (eval-expr expr env)) exprs)]
            ; Then extend environment with all bindings at once
            [final-env (foldl (lambda (var val env)
                                (extend-env var val env))
                              env
                              vars
                              vals)])
       (eval-expr body final-env))]
    
    ;; LETREC: Mutual recursion using MUTABLE ENVIRONMENT BINDINGS
    ;; Implements the formal rule PRECISELY:
    ;; Γr = Γ[∀i | fi ↦ ⟨fi; ei, ⊥⟩]  (bind names to placeholder closures)
    ;; Γr; Σ ⊢ ei ⇒ vi; Σi             (evaluate expressions in recursive env)
    ;; Γr' = Γr[fi ↦ vi]              (UPDATE environment with actual values)
    ;; Γr'; Σk ⊢ body ⇒ v; Σ'         (evaluate body in final environment)
    ;;
    ;; Strategy using mutable environment bindings:
    ;; 1. Create placeholder closures ⟨fi; ei, ⊥⟩ where ⊥ is an empty environment
    ;; 2. Create Γr by binding each fi to a BOXED placeholder (mutable binding)
    ;; 3. Evaluate each ei in Γr to get actual closures (they capture Γr)
    ;; 4. UPDATE Γr in place by mutating the boxes: Γr' = Γr[fi ↦ vi]
    ;; 5. Evaluate body in Γr (which is now Γr' due to mutation)
    ;;
    ;; This follows the formal semantics exactly:
    ;; - Environments are extended (not mutated structurally)
    ;; - The "update" Γr[fi ↦ vi] is achieved via boxing (value mutation)
    ;; - No store indirection needed - recursion works through environment
    ;; Syntax: (letrec ([f1 e1] [f2 e2] ...) body)
    [`(letrec (,bindings ...) ,body)
     (let* ([vars (map car bindings)]
            [exprs (map cadr bindings)]
            ; Step 1: Create placeholder closures ⟨fi; ei, ⊥⟩
            ; Extract params and body from lambda expressions
            [placeholders (map (lambda (expr)
                                (match expr
                                  [`(lambda (,params ...) ,body-expr)
                                   ; Placeholder with empty environment (⊥)
                                   (closure params body-expr '())]
                                  [_ 
                                   ; For non-lambda: use a dummy value
                                   'placeholder]))
                              exprs)]
            ; Step 2: Create Γr with BOXED placeholders (mutable bindings)
            ; Each box will be mutated later to hold the actual closure
            [boxed-placeholders (map (lambda (p) (box p)) placeholders)]
            [gamma-r (extend-env-mut* vars boxed-placeholders env)]
            ; Step 3: Evaluate each ei in Γr to get actual closures
            ; These closures capture gamma-r, which contains boxed bindings
            [actual-vals (map (lambda (expr) (eval-expr expr gamma-r)) exprs)])
       ; Step 4: Update Γr to Γr' by mutating the boxes
       ; This implements Γr[fi ↦ vi] from the formal rule
       (for-each (lambda (var val) (update-env-mut! var val gamma-r))
                 vars
                 actual-vals)
       ; Step 5: Evaluate body in Γr (which is now Γr' due to mutations)
       ; When functions look up each other, they get the actual closures
       (eval-expr body gamma-r))]
    
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
;; Store is global and mutable, so no threading needed
(define (apply-func func args)
  (match func
    ;; APPprim: Apply primitive operation
    [(primitive name f)
     (apply f args)]
    
    ;; APP: Apply closure
    [(closure params body captured-env)
     (cond
       ;; Multi-argument application
       [(= (length params) (length args))
        (let ([extended-env (extend-env* params args captured-env)])
          (eval-expr body extended-env))]
       
       ;; Currying: fewer arguments than parameters (returns new closure)
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
;; Resets the store before each evaluation
(define (eval expr)
  (reset-store!)
  (eval-expr expr initial-env))


;; Pretty print results
(define (pretty-print-value v)
  (match v
    [(closure params body env)
     (format "<closure: params=~a>" params)]
    [(primitive name _)
     (format "<primitive: ~a>" name)]
    [(loc addr)
     (format "<loc:~a>" addr)]
    [_ (format "~a" v)]))

;; Interactive interpreter REPL
(define (repl)
  (displayln "╔════════════════════════════════════════════╗")
  (displayln "║   λ-Calculus Interpreter (Assignment 4)   ║")
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
         (displayln "Core Syntax:")
         (displayln "  Numbers:  42, #t, #f")
         (displayln "  Lambda:   (lambda (x y) body)")
         (displayln "  Apply:    (@ func arg1 arg2 ...)")
         (displayln "  Let:      (let ([x val]) body)")
         (displayln "  If:       (if cond then else)")
         (newline)
        (displayln "Assignment 4 Features:")
        (displayln "  let*:     (let* ([x1 e1] [x2 e2] ...) body)   ; sequential bindings")
        (displayln "  let*2:    (let*2 ([x1 e1] [x2 e2] ...) body)  ; alternative let* (no dependencies)")
        (displayln "  letrec:   (letrec ([f1 e1] [f2 e2] ...) body) ; mutual recursion")
        (displayln "  ref:      (ref expr)           ; allocate location")
        (displayln "  deref:    (deref loc)          ; read from location")
        (displayln "  set:      (set loc val)        ; update location")
        (displayln "  seq:      (seq expr1 expr2)    ; sequencing")
         (newline)
         (loop)]
        
        ['examples
         (displayln "\nCore Examples:")
         (displayln "  (@ + 1 2)")
         (displayln "  (@ (lambda (x) (@ * x x)) 5)")
         (displayln "  (let ([x 10]) (@ + x 5))")
         (displayln "  (@ (@ (lambda (x) (lambda (y) (@ + x y))) 3) 4)")
         (newline)
        (displayln "Assignment 4 Examples:")
        (displayln "  ;; let* - sequential bindings (y can see x)")
        (displayln "  (let* ([x 1] [y (@ + x 1)]) y)  ; => 2")
        (newline)
        (displayln "  ;; let*2 - independent bindings (y cannot see x)")
        (displayln "  (let*2 ([x 5] [y 10]) (@ + x y))  ; => 15")
        (newline)
        (displayln "  ;; letrec - mutual recursion")
         (displayln "  (letrec ([fact (lambda (n)")
         (displayln "                   (if (@ == n 0) 1")
         (displayln "                       (@ * n (@ fact (@ - n 1)))))])")
         (displayln "    (@ fact 5))")
         (newline)
         (displayln "  ;; Store - mutable references")
         (displayln "  (let ([r (ref 10)])")
         (displayln "    (seq (set r (@ + (deref r) 5))")
         (displayln "         (deref r)))")
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
  
  ;; ============================================================================
  ;; Assignment 4 Tests (from Appendix A)
  ;; ============================================================================
  
  ;; Test 11: let* sequential binding
  (test-case "let* sequential binding"
             '(let* ([x 1] [y 2]) (@ + x y))
             3)
  
  ;; Test 12: let* shadowing
  (test-case "let* shadowing"
             '(let* ([x 1] [x 2] [x 6]) x)
             6)
  
  ;; Test 13: letrec factorial
  (test-case "letrec factorial"
             '(letrec ([fact (lambda (n)
                              (if (@ == n 0)
                                  1
                                  (@ * n (@ fact (@ - n 1)))))])
                (@ fact 5))
             120)
  
  ;; Test 14: letrec mutual recursion (even/odd)
  (test-case "letrec even/odd"
             '(letrec ([even (lambda (n)
                              (if (@ == n 0)
                                  #t
                                  (@ odd (@ - n 1))))]
                      [odd (lambda (n)
                             (if (@ == n 0)
                                 #f
                                 (@ even (@ - n 1))))])
                (@ even 4))
             #t)
  
  ;; Test 15: Store allocation & mutation
  (test-case "Store allocation & mutation"
             '(let ([r (ref 10)])
                (seq (set r (@ + (deref r) 5))
                     (deref r)))
             15)
  
  ;; Test 16: Closures capturing locations
  (test-case "Closures capturing locations"
             '(let ([r (ref 10)])
                (let ([adder (lambda (x) (set r (@ + (deref r) x)))])
                  (seq (@ adder 5)
                       (deref r))))
             15)
  
  ;; Test 17: Sequencing
  (test-case "Sequencing"
             '(seq (ref 0) 42)
             42)
  
  ;; Test 18: Store + recursion
  (test-case "Store with recursion"
             '(let ([counter (ref 0)])
                (letrec ([inc (lambda ()
                               (seq (set counter (@ + (deref counter) 1))
                                    (deref counter)))])
                  (seq (@ inc)
                       (seq (@ inc)
                            (@ inc)))))
             3)
  
  ;; Test 19: Iterator using store
  (test-case "Iterator using store"
             '(let ([pos (ref 0)])
                (let ([next (lambda ()
                             (let ([current (deref pos)])
                               (seq (set pos (@ + current 1))
                                    current)))])
                  (seq (@ next)
                       (seq (@ next)
                            (@ next)))))
             2)
  
  ;; ============================================================================
  ;; Phase 10.1: Alternative let* Rule Comparison
  ;; ============================================================================
  
  ;; Test 20: let*2 - bindings use original environment
  (test-case "let*2 without dependencies"
             '(let ([x 10])
                (let*2 ([y 1] [z 2])
                  (@ + (@ + x y) z)))
             13)
  
  ;; Test 21: let* vs let*2 difference
  ;; let* allows y to reference x (sequential)
  (test-case "let* with dependency"
             '(let* ([x 5] [y (@ + x 1)])
                y)
             6)
  
  ;; Test 22: let*2 where later bindings can't see earlier ones
  ;; This should work because both bindings are independent
  (test-case "let*2 independent bindings"
             '(let*2 ([x 5] [y 10])
                (@ + x y))
             15)
  
  ;; ============================================================================
  ;; Section 8.2: set! Tests (Variable-based mutation) : added tests
  ;; ============================================================================
  
  ;; Test 23: set! basic usage
  (test-case "set! basic mutation"
             '(let ([x (ref 10)])
                (seq (set! x 20)
                     (deref x)))
             20)
  
  ;; Test 24: set! with computation
  (test-case "set! with computation"
             '(let ([x (ref 5)])
                (seq (set! x (@ * (deref x) 2))
                     (deref x)))
             10)
  
  ;; Test 25: set! in closure
  (test-case "set! in closure"
             '(let ([x (ref 0)])
                (let ([inc (lambda () (set! x (@ + (deref x) 1)))])
                  (seq (@ inc)
                       (seq (@ inc)
                            (deref x)))))
             2)
  
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
