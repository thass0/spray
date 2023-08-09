(import (chicken string))
(import (srfi-1))
(import (srfi-13))
(import regex)				; `regexp` and `string-search`.
(import format)				; `format`


;;; Token tags.

(define token-tag-whitespace 'tt-whitespace)
(define token-tag-newline 'tt-newline)
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
(define token-tag-comment 'tt-comment)
(define token-tag-uncomment 'tt-uncomment)
(define token-tag-trailing-uncomment 'tt-trailing-uncomment)
(define token-tag-comment-text 'tt-comment-text)

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
(define whitespace-regex (regexp "^[\t\r ]*"))
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
;; A preprocessor directive. Optionally also matches the
;; `<filename>`/`"filename"` part of `#include`s.
(define preproc-directive-regex
  (regexp "^(#[a-z_]+)([ \t]+[<\"]([^>\"\\\\]|\\\\[\\s\\S])*[>\"])?"))
;; Comments must match anything except for newline characters
;; so as to maintain the line numberings.
(define comment-text-regex (regexp "^(\\*(?!\\/)|[^*\n])*"))
(define line-comment-text-regex (regexp "^[^\n]*"))
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
(define C-comment '("/*"))
(define C-uncomment '("*/"))
(define C++-comment '("//"))


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

  (define (starts-with-comment? str)
    (prefix? str C-comment))

  (define (starts-with-line-comment? str)
    (prefix? str C++-comment))

  (define (starts-with-uncomment? str)
    (prefix? str C-uncomment))

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

  (define (starts-with-newline? str)
    (string-prefix? "\n" str))

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


  ;; NOTE: All scan procedures assume that the corresponding
  ;; `starts-with-*?` procedure is called first so as to verify
  ;; that the string actually matches the regex.
  (define (scan-comment code)
    (make-token-list (find-prefix code C-comment)
		     token-tag-comment))

  (define (scan-line-comment code)
    (make-token-list (find-prefix code C++-comment)
		     token-tag-comment))

  (define (scan-line-comment-text code)
    (make-token-list (full-match line-comment-text-regex code)
		     token-tag-comment-text))

  (define (scan-comment-text code)
    (make-token-list (full-match comment-text-regex code)
		     token-tag-comment-text))

  (define (scan-uncomment code)
    (make-token-list (find-prefix code C-uncomment)
		     token-tag-uncomment))

  (define (scan-trailing-uncomment code)
    (make-token-list (find-prefix code C-uncomment)
		     token-tag-trailing-uncomment))

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

  (define (scan-newline code)
    (make-token-list "\n" token-tag-newline))

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

  (define (scan-normal-mode code)
    (cond
     ((starts-with-uncomment? code)
      (scan-trailing-uncomment code))
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
     ((starts-with-newline? code)
      (scan-newline code))
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

;;; Scan the next token in the code.
  (define scan
    (let ((mode 'normal-mode))
      (lambda (code)
	(cond
	 ((string-null? code)
	  '())
	 ((eq? mode 'normal-mode)
	  (cond
	   ((starts-with-comment? code)
	    (begin
	      (set! mode 'comment-mode)
	      (scan-comment code)))
	   ((starts-with-line-comment? code)
	    (begin
	      (set! mode 'line-comment-mode)
	      (scan-line-comment code)))
	   (else
	    ;; Scan normal code.
	    (scan-normal-mode code))))
	 ((eq? mode 'comment-mode)
	  (cond
	   ((starts-with-newline? code)
	    (scan-newline code))
	   ((starts-with-uncomment? code)
	    ;; End the comment
	    (begin
	      (set! mode 'normal-mode)
	      (scan-uncomment code)))
	   (else
	    ;; Eat-up the block comment.
	    (scan-comment-text code))))
	 ((eq? mode 'line-comment-mode)
	  (cond
	   ((starts-with-newline? code)
	    ;; C++ style comments end after a newline. 
	    (begin
	      (set! mode 'normal-mode)
	      (scan-newline code)))
	   (else
	    ;; Eat-up the line comment.
	    (scan-line-comment-text code))))))))

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

  ;;; Sometimes comments begin outside of the given piece of
  ;;; source code. Then there is a trailing `*/` somewhere at
  ;;; the start. This procedure includes anything up to that `*/`
  ;;; in the comment.
  (define (extend-comment tokens)
    (define (cons-comment comment-tokens comment-text)
      (let ((token (if (not (null? comment-tokens))
		       (car comment-tokens)
		       (make-token "" token-tag-other)))) ; Any token tag except newline.
	(if (eq? (token-tag token) token-tag-newline)
	    (cons
	     (make-token comment-text
			 token-tag-comment-text)
	     comment-tokens)
	    ;; Merge `comment-text` into the current comment text token.
	    (cons
	     (make-token (conc (token-text token) comment-text)
			 token-tag-comment-text)
	     (if (null? comment-tokens)
		 '()
		 (cdr comment-tokens))))))
    (define (extend-comment-iter comment-extension comment-text rest-tokens)
      (let ((token (car rest-tokens)))
	(cond ((eq? (token-tag token) token-tag-trailing-uncomment)
	       (append (reverse
			(cons-comment comment-extension
				      comment-text))
		       rest-tokens))
	      ((eq? (token-tag token) token-tag-newline)
	       ;; Copy this newline into the extensoin. The comment
	       ;; text collected up to this point must be added to
	       ;; the comment extension.
	       (extend-comment-iter
		(cons
		 token			; The newline token.
		 (cons-comment comment-extension comment-text))
		""
		(cdr rest-tokens)))
	      (else
	       ;; Add this token's text to the comment.
	       (extend-comment-iter comment-extension
				    (conc comment-text
					  (token-text token))
				    (cdr rest-tokens))))))
    (let ((trailing
	   (find
	    (lambda (token)
	      (eq? (token-tag token)
		   token-tag-trailing-uncomment))
	    tokens)))
      (if trailing
	  (extend-comment-iter '() "" tokens)
	  tokens)))

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
  (extend-comment
   (tokenize-iter code '())))


;;; Print and color `tokens`.
(define (print-tokens tokens start-lineno active-lineno use-color)
  (define (def-color color)
    (string-append "\033[" color "m"))

  (define literal-color (def-color "31"))
  (define type-color (def-color "32"))
  (define operator-color (def-color "33"))
  (define constant-color (def-color "34"))
  (define keyword-color (def-color "35"))
  (define comment-color (def-color "96"))
  (define no-color (def-color "0"))
  (define nothing "")

  (define (comment-tag? tag)
    (or (eq? tag token-tag-comment)
	(eq? tag token-tag-comment-text)
	(eq? tag token-tag-uncomment)
	(eq? tag token-tag-trailing-uncomment)))

  (define (tag-before-color tag)
    (cond ((eq? tag token-tag-keyword) keyword-color)
	  ((eq? tag token-tag-preproc-directive) keyword-color)
	  ((eq? tag token-tag-operator) operator-color)
	  ((eq? tag token-tag-type) type-color)
	  ((eq? tag token-tag-literal) literal-color)
	  ((eq? tag token-tag-constant) constant-color)
	  ((comment-tag? tag) comment-color)
	  (else no-color)))

  (define (before-color tag)
    (if use-color
	(tag-before-color tag)
	nothing))

  (define (after-color)
    (if use-color
	no-color
	nothing))

  (define (default-line-init token) "")

  (define format-token
    (let ((line-init default-line-init)
	  (lineno start-lineno))
      (lambda (token)
	(define (before-token!)
	  (let ((before-line line-init))
	    (set! line-init default-line-init)
	    (conc (before-line token)
		  (before-color (token-tag token)))))

	(define (after-token!)
	  (define (highlight-active-line token)
	    (define (visible-content? token)
	      (and
	       (not (eq? token-tag-newline (token-tag token)))
	       (not (string-null? (token-text token)))))
	    (cond ((= lineno active-lineno)
		   " -> ")
		  ((visible-content? token) ; Only add whitespace to align this line
		   "    ")		    ; if there is visible content on it.
		  (else
		   "")))
	  (if (eq? (token-tag token) token-tag-newline)
	      (set! line-init
		(lambda (next-token)
		  (let ((lineno-str
			 (conc (format #f " ~4d" lineno)
			       (highlight-active-line next-token))))
		    (set! lineno (+ lineno 1))
		    lineno-str))))
	  (after-color))

	(conc (before-token!)
	      (token-text token)
	      (after-token!)))))


  (define (format-tokens tokens)
    ;; Line numbers are added to the output *after* each newline
    ;; token. Since we want a line number on on on the first line
    ;; too, this newline token is added to the start of the tokens.
    (define (init-tokens token)
      (cons (make-token "" token-tag-newline)
	    tokens))
    (define (format-token-iter tokens output)
      (if (null? tokens)
	  output
	  (format-token-iter
	   (cdr tokens)
	   (conc output
		 (format-token (car tokens))))))
    (format-token-iter (init-tokens tokens) ""))

  (display (format-tokens tokens)))
