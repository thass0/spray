(import (chicken string))
(import (srfi-1))
(import (srfi-13))
(import regex)


;;; Token tags.

(define token-tag-whitespace 'tt-whitespace)
(define token-tag-other 'tt-other)
(define token-tag-keyword 'tt-keyword)
(define token-tag-operator 'tt-operator)
(define token-tag-special-symbol 'tt-special-symbol)
(define token-tag-constant 'tt-constant)
(define token-tag-literal 'tt-literal)
(define token-tag-identifier 'tt-identifier)
(define token-tag-type' tt-type)
(define token-tag-preproc-directive 'tt-preproc)
(define token-tag-include-filepath 'tt-include-filepath)

(define (make-token text token-tag)
  (cons token-tag text))

(define (make-token-list text token-tag)
  (list (make-token text token-tag)))

(define (make-end-token)
  (make-token "" 'tt-end))

(define (end-token? token)
  (and (equal? (token-text token) "")
       (eq? (token-tag token) 'tt-end)))

(define (token-tag token)
  (if (pair? token)
      (car token)
      (error "token-tag, token must be a pair" token)))

(define (token-text token)
  (if (pair? token)
      (cdr token)
      (error "token-text, token must be a pair" token)))


;;; Regular expressions for scanning C code. They mostly
;;; resemble what's  used in [this](https://www.lysator.liu.se/c/ANSI-C-grammar-l.html)
;;; scanner although some modifications were made.

(define literal-regex (regexp "^\"([^\"\\\\]|\\\\[\\s\\S])*\""))
(define whitespace-regex (regexp "^[\t\n \r]*"))
(define identifier-regex (regexp "^[a-zA-Z_][a-zA-Z_0-9]*"))
(define hex-constant-regex (regexp "^0[xX][a-fA-F0-9]+(u|U|l|L)*"))
(define octal-constant-regex (regexp "^0[0-7]+(u|U|l|L)*"))
(define decimal-constant-regex (regexp "^[0-9][0-9]*(u|U|l|L)*"))
(define char-constant-regex (regexp "^(u|U|l|L)*'(\\\\.|[^\\\\'])+'"))
(define sci-constant-regex (regexp "^[1-9][0-9]*[Ee][+-]?[0-9]+"))
;; Floating point constants requiring fractional part.
(define float-constant-regex-frac (regexp "^[0-9]*\\.[0-9]+([Ee][+-]?[0-9]+)?(f|F|l|L)?"))
;; Floating point constants requiring whole number part.
(define float-constant-regex-whole (regexp "^[0-9]+\\.[0-9]*([Ee][+-]?[0-9]+)?(f|F|l|L)?"))
;; A preprocessor directive. Optionally also matches the `<filename>`/`"filename"` part of `#include`s.
(define preproc-directive-regex (regexp "^(#[a-z_]+)([ \t]+[<\"]([^\"\\\\]|\\\\[\\s\\S])*[>\"])?"))
;; Match anything that's not whitespace. Used to recover from invalid pieces of syntax.
(define any-regex (regexp "^[^ \n\t\r]*"))

;;; Does `str` match `regex`?
(define (regex-match? regex str)
  (let ((search-result (string-search regex str)))
  (and (pair? search-result)
       (not (equal? (car search-result) "")))))

;;; Return the full match of `str` and `regex`.
(define (full-match regex str)
  (car (string-search regex str)))


 ;;; Lists of meaningful string literals in C sources.
(define C-keywords '("case" "default" "if" "else" "switch" "while"
		       "do" "for" "goto" "continue" "break" "return"
		       "struct" "union" "enum" "typedef" "extern"
		       "static" "register" "auto" "const" "volatile"
		       "restrict"))
(define C-operators '(">>=" "<<=" "+=" "-=" "*=" "/=" "%=" "&=" "^=" "|="
			">>" "<<" "++" "--" "->" "&&" "||" "<=" ">=" "==" "!="
			"=" "." "&" "!" "~" "-" "+" "*" "/" "%" "<" ">" "^"
			"|" "?" ":" "sizeof"))
(define C-builtin-types '("char" "short" "int" "long" "signed"
		    "unsigned" "float" "double" "void"))
(define C-special-symbols '("(" ")" "[" "]" "{" "}" "," ";" "..."))


;;; Tokenize `code`.
(define (tokenize code)
  ;; Does `given-str` start with any of the prefixes in `possible-prefixes`?
  (define (find-prefix given-str possible-prefixes)
    (find
     (lambda (possible-prefix)
       (string-prefix? possible-prefix given-str))
     possible-prefixes))

  ;; Predicate for `find-prefix`.
  (define (prefix? given-str possible-prefixes)
    (if (find-prefix given-str possible-prefixes)
	#t #f))


  (define (starts-with-keyword? str)
    (prefix? str C-keywords))

  (define (starts-with-operator? str)
    (prefix? str C-operators))

  (define (starts-with-special-symbol? str)
    (prefix? str C-special-symbols))

  (define (starts-with-literal? str)
    (if (string-search literal-regex str)
	#t #f))

  (define (starts-with-whitespace? str)
    (regex-match? whitespace-regex str))

  (define (starts-with-identifier? str)
    (regex-match? identifier-regex str))

  (define (starts-with-constant? str)
    (or (regex-match? hex-constant-regex str)
	(regex-match? octal-constant-regex str)
	(regex-match? decimal-constant-regex str)
	(regex-match? char-constant-regex str)
	(regex-match? sci-constant-regex str)
	(regex-match? float-constant-regex-frac str)
	(regex-match? float-constant-regex-whole str)))

  (define (starts-with-preproc? str)
    (regex-match? preproc-directive-regex str))

  (define (starts-with-any? str)
    (regex-match? any-regex str))


  ;;; NOTE: All scan procedures assume that the corresponding
  ;;; `starts-with-*?` procedure is called first so as to verify
  ;;; that the string actually matches the regex.
  (define (scan-keyword code)
    (make-token-list (find-prefix code C-keywords)
		     token-tag-keyword))

  (define (scan-operator code)
    (make-token-list (find-prefix code C-operators)
		     token-tag-operator))

  (define (scan-special-symbol code)
    (make-token-list (find-prefix code C-special-symbols)
		     token-tag-special-symbol))

  (define (scan-literal code)
    (make-token-list (full-match literal-regex code)
		     token-tag-literal))

  (define (scan-whitespace code)
    (make-token-list (full-match whitespace-regex code)
		     token-tag-whitespace))

  (define (scan-identifier code)
    ;; Check if `identifier` is the identifier of a type.
    (define (type-identifier? identifier)
      (find
       (lambda (type)
	 (string=? type identifier))
       C-builtin-types))
    (let ((match (full-match identifier-regex code)))
      (make-token-list
       match
       (if (type-identifier? match)
	   token-tag-type
	   token-tag-identifier))))

  (define (scan-constant code)
    (make-token-list
     (cond ((regex-match? hex-constant-regex code)
	    (full-match hex-constant-regex code))
	   ((regex-match? octal-constant-regex code)
	    (full-match octal-constant-regex code))
	   ((regex-match? decimal-constant-regex code)
	    (full-match decimal-constant-regex code))
	   ((regex-match? char-constant-regex code)
	    (full-match char-constant-regex code))
	   ((regex-match? sci-constant-regex code)
	    (full-match sci-constant-regex code))
	   ((regex-match? float-constant-regex-frac code)
	    (full-match float-constant-regex-frac code))
	   ((regex-match? float-constant-regex-whole code)
	    (full-match float-constant-regex-whole code))
	   (else
	    (error "scan-constant, expected to find match" code)))
     token-tag-constant))

  (define (scan-preproc code)
    (let ((match (string-search preproc-directive-regex code)))
      (if (caddr match)
	  (list (make-token (cadr match)
			    token-tag-preproc-directive)
		(make-token (caddr match)
			    token-tag-include-filepath))
	  (make-token-list (cadr match)
			   token-tag-preproc-directive))))

  (define (scan-any code)
    (make-token-list (full-match any-regex code)
		     token-tag-other))


  ;;; Scan the next token in the code.
  (define (scan code)
    (cond ((string-null? code)
	   '())
	  ((starts-with-keyword? code)
	   (scan-keyword code))
	  ((starts-with-operator? code)
	   (scan-operator code))
	  ((starts-with-special-symbol? code)
	   (scan-special-symbol code))
	  ((starts-with-literal? code)
	   (scan-literal code))
	  ((starts-with-whitespace? code)
	   (scan-whitespace code))
	  ((starts-with-identifier? code)
	   (scan-identifier code))
	  ((starts-with-constant? code)
	   (scan-constant code))
	  ((starts-with-preproc? code)
	   (scan-preproc code))
	  ((starts-with-any? code)
	   (scan-any code))
	  (else
	   (error "scan, invalid input" code))))

  ;;; Return the next token in the code.
  (define next-token
    ;; Queue of tokens to be retured before scanning the next token.
    (let ((token-queue '()))
      (lambda (code)
	(if (null? token-queue)
	    (let ((new-tokens (scan code)))
	      (if (null? new-tokens)
		  (make-end-token)	; Signal that input is over.
		  (begin
		    (set! token-queue (cdr new-tokens))
		    (car new-tokens))))
	    (let ((this-token (car token-queue)))
	      (set! token-queue (cdr token-queue))
	      this-token)))))

  ;;; Return the rest of `str` after removing
  ;;; `(string-length cutoff)` characters from its start.
  (define (string-rest str cutoff)
    (substring str (string-length cutoff) (string-length str)))

  ;; Tokenize the code.
  (define (tokenize-iter code tokens)
    (let ((token (next-token code)))
      (if (end-token? token)
	  (reverse tokens)
	  (tokenize-iter
	   (string-rest code (token-text token))
	   (cons token tokens)))))
  (tokenize-iter code '()))


;;; Print and color `tokens`.
(define (print-tokens tokens)
  (define (def-color color)
    (string-append "\033[" color "m"))

  (define keyword-color (def-color "35"))
  (define operator-color (def-color "33"))
  (define type-color (def-color "32"))
  (define literal-color (def-color "31"))
  (define constant-color (def-color "34"))
  (define no-color (def-color "0"))

  (define (tag-color tag)
    (cond ((eq? tag token-tag-keyword) keyword-color)
	  ((eq? tag token-tag-preproc-directive) keyword-color)
	  ((eq? tag token-tag-operator) operator-color)
	  ((eq? tag token-tag-type) type-color)
	  ((eq? tag token-tag-literal) literal-color)
	  ((eq? tag token-tag-constant) constant-color)
	  (else no-color)))

  (for-each
   (lambda (token)
     (display (tag-color (token-tag token)))
     (display (token-text token))
     (display no-color))
   tokens))
