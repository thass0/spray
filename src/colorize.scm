(import (chicken string))
(import (srfi-1))
(import (srfi-13))
(import regex)

(define token-tag-whitespace 'tt-whitespace)
(define token-tag-other 'tt-other)
(define token-tag-keyword 'tt-keyword)
(define token-tag-operator 'tt-operator)
(define token-tag-special-symbol 'tt-special-symbol)
(define token-tag-constant 'tt-constant)
(define token-tag-literal 'tt-literal)
(define token-tag-identifier 'tt-identifier)
(define token-tag-type' tt-type)

(define (make-token text token-tag)
  (cons token-tag text))

(define (token-tag token)
  (if (pair? token)
      (car token)
      (error "token-tag, token must be a pair" token)))

(define (token-text token)
  (if (pair? token)
      (cdr token)
      (error "token-text, token must be a pair" token)))

;;; Regular expressions for scanning C code. They mostly
;;; resenble what's  used in [this](https://www.lysator.liu.se/c/ANSI-C-grammar-l.html)
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

(define (regex-match? regex str)
  (let ((search-result (string-search regex str)))
  (and (pair? search-result)
       (not (equal? (car search-result) "")))))

(define (tokenize code)
  (define (find-start given-str possible-prefixes)
    (find
     (lambda (possible-prefix)
       (string-prefix? possible-prefix given-str))
     possible-prefixes))
  (define (starts-with-some? given-str possible-prefixes)
    (if (find-start given-str possible-prefixes)
	#t #f))
  (define (find-start-or-error given-str possible-prefixes)
    (let ((start (find-start given-str possible-prefixes)))
      (if (not start)
	  (error "find-start-or-error, failed to find start" given-str possible-prefixes)
	  start)))

  (define keyword-strs '("case" "default" "if" "else" "switch" "while"
			 "do" "for" "goto" "continue" "break" "return"
			 "struct" "union" "enum" "typedef" "extern"
			 "static" "register" "auto" "const" "volatile"
			 "restrict"))
  (define operator-strs '(">>=" "<<=" "+=" "-=" "*=" "/=" "%=" "&=" "^=" "|="
			  ">>" "<<" "++" "--" "->" "&&" "||" "<=" ">=" "==" "!="
			  "=" "." "&" "!" "~" "-" "+" "*" "/" "%" "<" ">" "^"
			  "|" "?" ":" "sizeof"))
  (define type-strs '("char" "short" "int" "long" "signed"
		      "unsigned" "float" "double" "void"))

  (define special-symbol-strs '("(" ")" "[" "]" "{" "}" "," ";" "..."))

  (define (starts-with-keyword? str)
    (starts-with-some? str keyword-strs))
  (define (starts-with-operator? str)
    (starts-with-some? str operator-strs))
  (define (starts-with-special-symbol? str)
    (starts-with-some? str special-symbol-strs))
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

  (define (start-keyword str)
    (find-start-or-error str keyword-strs))
  (define (start-operator str)
    (find-start-or-error str operator-strs))
  (define (start-special-symbol str)
    (find-start-or-error str special-symbol-strs))
  (define (start-literal str)
    (if (regex-match? literal-regex str)
	(car (string-search literal-regex str))
	(error "start-literal, expected to find match" str)))
  (define (start-whitespace str)
    (if (regex-match? whitespace-regex str)
	(car (string-search whitespace-regex str))
	(error "start-whitespace, expected to find match" str)))
  (define (start-identifier str)
      (if (regex-match? identifier-regex str)
	  (car (string-search identifier-regex str))
	  (error "start-identifier, expected to find match" str)))
  (define (start-constant str)
    (cond ((regex-match? hex-constant-regex str)
	   (car (string-search hex-constant-regex str)))
	  ((regex-match? octal-constant-regex str)
	   (car (string-search octal-constant-regex str)))
	  ((regex-match? decimal-constant-regex str)
	   (car (string-search decimal-constant-regex str)))
	  ((regex-match? char-constant-regex str)
	   (car (string-search char-constant-regex str)))
	  ((regex-match? sci-constant-regex str)
	   (car (string-search sci-constant-regex str)))
	  ((regex-match? float-constant-regex-frac str)
	   (car (string-search float-constant-regex-frac str)))
	  ((regex-match? float-constant-regex-whole str)
	   (car (string-search float-constant-regex-whole str)))
	  (else
	   (error "start-constant, expected to find match" str))))

  (define (string-rest str cutoff)
    (substring str (string-length cutoff) (string-length str)))

  (define (type-identifier? identifier)
    (find
     (lambda (type)
       (string=? type identifier))
     type-strs))

  (define (tokenize-iter str tokens)
    (cond ((string-null? str)
	   (reverse tokens))
	  ((starts-with-keyword? str)
	   (let ((keyword (start-keyword str)))
	     (tokenize-iter
	      (string-rest str keyword)
	      (cons (make-token keyword token-tag-keyword)
		    tokens))))
	  ((starts-with-operator? str)
	   (let ((operator (start-operator str)))
	     (tokenize-iter
	      (string-rest str operator)
	      (cons (make-token operator token-tag-operator)
		    tokens))))
	  ((starts-with-special-symbol? str)
	   (let ((special-symbol (start-special-symbol str)))
	     (tokenize-iter
	      (string-rest str special-symbol)
	      (cons (make-token special-symbol token-tag-special-symbol)
		    tokens))))
	  ((starts-with-literal? str)
	   (let ((literal (start-literal str)))
	     (tokenize-iter
	      (string-rest str literal)
	      (cons (make-token literal token-tag-literal)
		    tokens))))
	  ((starts-with-whitespace? str)
	   (let ((whitespace (start-whitespace str)))
	     (tokenize-iter
	      (string-rest str whitespace)
	      (cons (make-token whitespace token-tag-whitespace)
		    tokens))))
	  ((starts-with-identifier? str)
	   (let ((identifier (start-identifier str)))
	     (tokenize-iter
	      (string-rest str identifier)
	      (cons (make-token identifier
				(if (type-identifier? identifier)
				    token-tag-type
				    token-tag-identifier))
		    tokens))))
	  ((starts-with-constant? str)
	   (let ((constant (start-constant str)))
	     (tokenize-iter
	      (string-rest str constant)
	      (cons (make-token constant token-tag-constant)
		    tokens))))
	  (else
	   (tokenize-iter
	    ""
	    (cons (make-token str token-tag-other)
		  tokens)))))
  (tokenize-iter code '()))

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
