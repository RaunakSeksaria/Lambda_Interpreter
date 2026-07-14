#lang racket
;; bench_oracle.rkt — reference-interpreter baseline for the benchmark.
;;
;; Times the Racket tree-walker (../../interpreter.rkt, the differential-test
;; oracle) on the same three signals the C++ engine benchmarks, so the engine's
;; speedup can be sized against the naive interpreter it was derived from.
;;
;; This is an honest before->after: a fresh AST walk with an assoc-list
;; environment and boxed values (Racket) vs a compiled bytecode VM (C++) — not a
;; comparison against a fast baseline. Each `eval` resets the store, matching the
;; engine's per-tick semantics.
;;
;;   racket bench/bench_oracle.rkt

(require "../../interpreter.rkt")  ; provides `eval`

(define N 20000)

(define (rand lo hi) (+ lo (random (add1 (- hi lo)))))

;; Each signal: name, expression datum, and (input lo hi) generators — matching
;; engine/bench/bench_vm.cpp exactly.
(define signals
  (list
   (list "light"
         '(let* ([mid (@ / (@ + bid ask) 2)] [spread (@ - ask bid)])
            (if (@ and (@ > spread 0) (@ < spread 10)) mid 0))
         '((bid 100 1000) (ask 100 1000)))
   (list "branchy"
         '(if (@ > a b) (if (@ < c d) (@ + a c) (@ - a c))
              (if (@ > c d) (@ * b 2) (@ + b d)))
         '((a 0 1000) (b 0 1000) (c 0 1000) (d 0 1000)))
   (list "heavy"
         '(let* ([acc (ref 0)] [i (ref 32)])
            (seq (while (@ > (deref i) 0)
                   (seq (set acc (@ + (deref acc) (@ * (deref i) px)))
                        (set i (@ - (deref i) 1))))
                 (@ + (deref acc) qty)))
         '((px 1 1000) (qty 1 100)))))

;; Wrap an expression in a let* that binds its inputs to random values.
(define (make-datum expr inspecs)
  `(let* ,(for/list ([s inspecs]) (list (first s) (rand (second s) (third s))))
     ,expr))

(define (bench name expr inspecs)
  (define datums (for/list ([_ (in-range N)]) (make-datum expr inspecs)))
  (for ([d (in-list datums)]) (eval d))  ; warm up
  (define t0 (current-inexact-monotonic-milliseconds))
  (for ([d (in-list datums)]) (eval d))
  (define t1 (current-inexact-monotonic-milliseconds))
  (define ns-per (/ (* (- t1 t0) 1e6) N))
  (printf "  ~a: ~a ns/eval\n" name (~r ns-per #:precision 1)))

(module+ main
  (printf "=== racket oracle (reference tree-walker), N=~a ===\n" N)
  (for ([s (in-list signals)])
    (bench (first s) (second s) (third s))))
