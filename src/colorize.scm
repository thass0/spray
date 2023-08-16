;;; Token tags.
(module c-tokens *
  (import scheme)
  (import (chicken base))

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
  (define token-tag-whitespace 'tt-whitespace)

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
	(error "token-text, token must be a pair" token))))

(module c-regex *
  (import scheme)
  (import regex)		       ; `regexp` and `string-search`.

;;; Regular expressions for scanning C code. They mostly
;;; resemble what's  used in [this](https://www.lysator.liu.se/c/ANSI-C-grammar-l.html)
;;; scanner although some modifications were made.

  (define literal-regex (regexp "^\"([^\"\\\\]|\\\\[\\s\\S])*\""))
  (define whitespace-regex (regexp "^[\t\r\n ]*"))
  (define identifier-regex (regexp "^[a-zA-Z_][a-zA-Z_0-9]*"))
  (define hex-constant-regex (regexp "^0[xX][a-fA-F0-9]+(u|U|l|L)*"))
  (define octal-constant-regex (regexp "^0[0-7]+(u|U|l|L)*"))
  (define decimal-constant-regex (regexp "^[0-9][0-9]*(u|U|l|L)*"))
  (define char-constant-regex (regexp "^(u|U|l|L)*'(\\\\.|[^\\\\'])+'"))
  (define sci-constant-regex (regexp "^[1-9][0-9]*[Ee][+-]?[0-9]+"))
  ;; Floating point constants requiring fractional part.
  (define float-constant-regex-frac
    (regexp "^[0-9]*\\.[0-9]+([Ee][+-]?[0-9]+)?(f|F|l|L)?"))
  ;; Floating point constants requiring whole number part.
  (define float-constant-regex-whole
    (regexp "^[0-9]+\\.[0-9]*([Ee][+-]?[0-9]+)?(f|F|l|L)?"))
  ;; A preprocessor directive. Optionally also matches the
  ;; `<filename>`/`"filename"` part of `#include`s.
  (define preproc-directive-regex
    (regexp "^(#[a-z_]+)([ \t]+[<\"]([^>\"\\\\]|\\\\[\\s\\S])*[>\"])?"))
  (define comment-text-regex (regexp "^(\\*(?!\\/)|[^*])*"))
  (define line-comment-text-regex (regexp "^[^\n]*"))
  ;; Match anything that's not whitespace.
  ;; Used to recover from invalid pieces of syntax.
  (define any-regex (regexp "^[^ \n\t\r]*"))

;;; Does `str` match `regex`?
  (define (regex-match? regex str)
    (let ((search-result (string-search regex str)))
      (and (pair? search-result)
	   (not (equal? (car search-result) "")))))

;;; Return the full match of `str` and `regex`.
  (define (full-match regex str)
    (car (string-search regex str)))

  ;;; Reexport string-search -- `regex-matches`
  ;;; returns a list of all the matches..
  (define regex-matches string-search))

(module colorizer
    ;; Take a string (C source code) and create a list of tokens
    ;; that represent which color each part of the string should be.
    (colorize
     ;; Turn the given list of colored tokens into a string.
     ;; The string contains ANSI escape codes to represent the
     ;; colors if `use-color` is true.
     colored->string)

  (import scheme)
  (import (chicken base))
  (import (chicken string))
  (import traversal)
  (import (except (srfi-1) assoc member))
  (import (except (srfi-13) string->list string-fill! string-copy))
  (import format)		       ; `format`

  (import c-tokens)
  (import c-regex)


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


;;; Transform `code-lines` into a list of token
;;; streams representing the color of each piece
;;; of code in each line.
  (define (colorize code-lines)
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
      (regex-match? literal-regex str))

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
      (let ((match (regex-matches preproc-directive-regex code)))
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
	(lambda (code new-line?)
	  ;; Implicitly end single-line comment.
	  (if (and (eq? mode 'line-comment-mode)
		   new-line?)
	      (set! mode 'normal-mode))

	  (cond
	   ((string-null? code)
	    '())
	   ((eq? mode 'normal-mode)
	    (cond
	     ((starts-with-comment? code)
	      (begin
		;; Begin block comment.
		(set! mode 'comment-mode)
		(scan-comment code)))
	     ((starts-with-line-comment? code)
	      (begin
		;; Begin single-line comment.
		(set! mode 'line-comment-mode)
		(scan-line-comment code)))
	     (else
	      ;; Scan normal code.
	      (scan-normal-mode code))))
	   ((eq? mode 'comment-mode)
	    (if (starts-with-uncomment? code)
		(begin
		  ;; Explicitly end multi-line comment.
		  (set! mode 'normal-mode)
		  (scan-uncomment code))
		;; Eat-up the block comment.
		(scan-comment-text code)))
	   ((eq? mode 'line-comment-mode)
	    (scan-line-comment-text code))))))

;;; Return the next token in the code.
    (define next-token
      ;; Queue of tokens to be retured before scanning the next token.
      (let ((token-queue '()))
	(lambda (code new-line?)
	  (if (null? token-queue)
	      (let ((new-tokens (scan code new-line?)))
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
    (define (string-cutoff str cutoff)
      (substring str (string-length cutoff) (string-length str)))

;;; Colorize the given piece of code by splitting it into tokens.
    (define (colorize-code code tokens new-line?)
      (let ((token (next-token code new-line?)))
	(if (end-token? token)
	    (reverse tokens)
	    (colorize-code
	     (string-cutoff code (token-text token))
	     (cons token tokens)
	     ;; `new-line?` may only be set to true
	     ;; by an external caller.
	     #f))))

;;; Sometimes comments begin outside of the given piece of
;;; source code. Then there is a trailing `*/` somewhere at
;;; the start. This procedure includes anything up to that `*/`
;;; in the comment.
    (define (wrap-leading-comment token-lines)
      (define (lead-comment? token-lines)
	(find
	 (lambda (tag)
	   (eq? tag token-tag-trailing-uncomment))
	 (map token-tag (flatten token-lines))))

      (define (make-lead-end tokens)
	(cons 'lead-comment-end tokens))

      (define (make-lead-line token)
	(cons 'lead-comment-line token))

      (define (lead-end? lead-line)
	(and (pair? lead-line)
	     (eq? (car lead-line) 'lead-comment-end)))

      (define (lead-tokens lead-line)
	(if (and (pair? lead-line)
		 (or (eq? (car lead-line)
			  'lead-comment-end)
		     (eq? (car lead-line)
			  'lead-comment-line)))
	    (cdr lead-line)
	    (error "lead-line-tokens, not a lead comment line"
		   lead-line)))

      (define (wrap-leading-comment-line line)
	(let tokens-loop ((ext-str "")
			  (rest-tokens line))
	  (cond ((null? rest-tokens)
		 (make-lead-line
		  (make-token-list ext-str token-tag-comment-text)))
		((eq? (token-tag (car rest-tokens))
		      token-tag-trailing-uncomment)
		 (make-lead-end
		  (cons (make-token ext-str token-tag-comment-text)
			rest-tokens)))
		(else
		 (tokens-loop
		  (conc ext-str (token-text (car rest-tokens)))
		  (cdr rest-tokens))))))

      (if (lead-comment? token-lines)
	  (let lines-loop ((ext-lines '())
			   (rest-lines token-lines))
	    ;; Don't have to check if `rest-lines` is null
	    ;; because `wrap-leading-comment-line` will return a
	    ;; pair before `rest-lines` ends if `lead-comment?`
	    ;; was true.
	    (let ((lead-line
		   (wrap-leading-comment-line (car rest-lines))))
	      (if (lead-end? lead-line)
		  (append
		   (reverse
		    (cons (lead-tokens lead-line)
			  ext-lines))
		   (cdr rest-lines))
		  (lines-loop
		   (cons (lead-tokens lead-line)
			 ext-lines)
		   (cdr rest-lines)))))
	  token-lines))

    (wrap-leading-comment
     (map
      (lambda (code-line)
	(colorize-code code-line '() #t))
      code-lines))
    )  ; End procedure colorize.

  (define (colored->string token-lines start-lineno active-lineno use-color)
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

    (define (before-color token)
      (if use-color
	  (tag-before-color
	   (token-tag token))
	  nothing))

    (define (after-color)
      (if use-color
	  no-color
	  nothing))

    (define (format-token token)
      (conc (before-color token)
	    (token-text token)
	    (after-color)))

    (define (accumulate-strings strs)
      (foldr conc "" strs))

    (define (format-tokens tokens)
      (accumulate-strings
       (map format-token tokens)))

;;; Check if `tokens` contains any non-whitespace text.
    (define (visible-content? tokens)
      (find
       (lambda (token-text-chars)
	 (not (null?
	       (filter
		(lambda (char)
		  (not (char-whitespace? char)))
		token-text-chars))))
       (map (lambda (token)
	      (string->list (token-text token)))
	    tokens)))

    (define (format-lineno tokens offset)
      (let ((current-lineno (+ offset start-lineno)))
	(define (highlight-active-lineno)
	  (cond ((= current-lineno active-lineno)
		 " -> ")
		((visible-content? tokens)
		 "    ")
		(else
		 "")))

	(conc (format #f " ~4d" current-lineno)
	      (highlight-active-lineno))))
    
    (accumulate-strings
     (map-indexed
      (lambda (token-line idx)
	(conc
	 (format-lineno token-line idx)
	 (format-tokens token-line)
	 "\n"))
      token-lines)))  ; End procedure colored->string.
  )  ; End module colorizer.
