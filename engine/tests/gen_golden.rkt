#lang racket
;; gen_golden.rkt — the differential-test ORACLE.
;;
;; Reuses the reference interpreter (../interpreter.rkt, unchanged) to produce
;; the expected result for every case in the corpus. The C++ engine's difftest
;; must reproduce these byte-for-byte.
;;
;; Usage:  racket gen_golden.rkt <corpus-path>   > golden.txt
;;
;; Corpus line format (see tests/corpus.txt):  "<inputs> <expr>"
;;   <inputs> = `-` (none) or comma-separated name=val (int or #t/#f)
;;   <expr>   = the rest of the line, in interpreter.rkt syntax
;; Each input becomes an outer `let*` binding, so free variables in the expr
;; resolve to the supplied values — exactly how the engine treats inputs.

(require "../../interpreter.rkt")  ; provides `eval`

;; parse-value : String -> datum   (#t/#f or an integer)
(define (parse-value s)
  (cond
    [(string=? s "#t") #t]
    [(string=? s "#f") #f]
    [(string->number s) => values]
    [else (error 'gen_golden "bad input value: ~a" s)]))

;; parse-inputs : String -> (Listof (List Symbol datum))
;; "bid=100,ask=101" -> '((bid 100) (ask 101)); "-" -> '()
(define (parse-inputs tok)
  (if (string=? tok "-")
      '()
      (for/list ([pair (string-split tok ",")])
        (define kv (string-split pair "="))
        (list (string->symbol (first kv)) (parse-value (second kv))))))

;; read-expr : String -> datum
(define (read-expr s)
  (with-input-from-string s read))

;; format-result : Value -> String   (must match engine::to_string)
(define (format-result v)
  (cond
    [(exact-integer? v) (number->string v)]
    [(boolean? v) (if v "#t" "#f")]
    [(symbol? v) (symbol->string v)]         ; 'undefined from while
    [else (error 'gen_golden "unformattable result: ~a" v)]))

;; A corpus line is live iff it is non-blank and not a comment.
(define (live-line? line)
  (define t (string-trim line))
  (and (> (string-length t) 0)
       (not (char=? (string-ref t 0) #\#))))

(define (process-line line)
  (define trimmed (string-trim line))
  ;; Split off the first whitespace-delimited token (the inputs field).
  (define idx (for/first ([i (in-range (string-length trimmed))]
                          #:when (char-whitespace? (string-ref trimmed i)))
                i))
  (unless idx (error 'gen_golden "line has no expression: ~a" line))
  (define inputs (parse-inputs (substring trimmed 0 idx)))
  (define expr (read-expr (substring trimmed idx)))
  (define wrapped
    (if (null? inputs)
        expr
        `(let* ,(for/list ([b inputs]) (list (first b) (second b))) ,expr)))
  (displayln (format-result (eval wrapped))))

(module+ main
  (define args (current-command-line-arguments))
  (when (zero? (vector-length args))
    (error 'gen_golden "usage: racket gen_golden.rkt <corpus-path>"))
  (define path (vector-ref args 0))
  (for ([line (file->lines path)] #:when (live-line? line))
    (process-line line)))
