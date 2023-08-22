(module c-tokens *
  (import scheme
	  (chicken base))

;;; Tokens in C source code.
  (define token-tag-other 'tt-other)
  (define token-tag-keyword 'tt-keyword)
  (define token-tag-operator 'tt-operator)
  (define token-tag-special-symbol 'tt-special-symbol)
  (define token-tag-constant 'tt-constant)
  (define token-tag-literal 'tt-literal)
  (define token-tag-identifier 'tt-identifier)
  (define token-tag-prim-type 'tt-type)
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
	(error "token-text, token must be a pair" token)))

  
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

  ;; End of module c-tokens.
  )


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
    (regexp "^(#[a-z_]+)([ \t]*)([<\"]([^>\"\\\\]|\\\\[\\s\\S])*[>\"])?"))
  (define comment-text-regex (regexp "^(\\*(?!\\/)|[^*])*"))
  (define line-comment-text-regex (regexp "^[^\n]*"))
  ;; Match anything that's not whitespace.
  ;; Used to recover from invalid pieces of syntax.
  (define any-regex (regexp "^[^ \n\t\r]*"))
  ;; Check that the given string starts with a keyword and
  ;; that the keyword is terminated by whitespace (i.e.
  ;; the given string doesn't continue with more characters).
  (define keyword-regex (regexp "^(case|default|if|else|switch|while|do|for|goto|continue|break|return|struct|union|enum|typedef|extern|static|register|auto|const|volatile|restrict)[\n\t ]"))

;;; Does `str` match `regex`?
  (define (regex-match? regex str)
    (let ((search-result (string-search regex str)))
      (and (pair? search-result)
	   (not (equal? (car search-result) "")))))

;;; Return the full match of `str` and `regex`.
  (define (full-match regex str)
    (car (string-search regex str)))

;;; Reexport string-search -- `regex-matches`
;;; returns a list of all the matches.
  (define regex-matches string-search)

  ;; End of module c-regex.
  )


(module c-types (make-types-env
		 is-type-in-env?)
  (import scheme
	  (chicken base)
	  (srfi-69)
	  (only (srfi-1) find)
	  c-tokens)

  ;; From the C reference grammar (https://www.lysator.liu.se/c/ANSI-C-grammar-y.html):
  ;; 
  ;; type_specifier
  ;; 	: VOID
  ;;      ...
  ;; 	| UNSIGNED
  ;; 	| struct_or_union_specifier
  ;; 	| enum_specifier
  ;; 	| TYPE_NAME
  ;; 	;
  ;;
  ;; struct_or_union_specifier
  ;; 	: struct_or_union IDENTIFIER '{' struct_declaration_list '}'
  ;; 	| struct_or_union '{' struct_declaration_list '}'
  ;; 	| struct_or_union IDENTIFIER
  ;; 	;
  ;;
  ;; struct_or_union
  ;; 	: STRUCT
  ;; 	| UNION
  ;; 	;
  ;;  ...
  ;; enum_specifier
  ;; 	: ENUM '{' enumerator_list '}'
  ;; 	| ENUM IDENTIFIER '{' enumerator_list '}'
  ;; 	| ENUM IDENTIFIER
  ;; 	;
  ;;
  ;; This tells us that every time we see the any of the keywords
  ;; struct, union or enum, we just  need to check if the next token
  ;; is an identifier or a opening bracket. If it's an identifier, we
  ;; can store that as the name of a type. Otherwise the type
  ;; being declared is anonymous, so we can ignore it.

;;; Is `token` a keyword token that's a type specifier?
  (define (type-spec-keyword? token)
    (and (eq? (token-tag token)
	      token-tag-keyword)
	 (let ((keyword (token-text token)))
	   (or (equal? keyword "struct")
	       (equal? keyword "enum")
	       (equal? keyword "union")))))

;;; Store the identifier of the first type specification
;;; found in `tokens` in `env`.
  (define (store-type-spec! env tokens)
    (let loop ((tokens tokens))
      (if (pair? tokens)
	  (cond ((eq? (token-tag (car tokens))
		      token-tag-identifier)
		 ;; Save the type's identifier.
		 (set-cdr! env (cons (token-text (car tokens))
				     (cdr env))))
		((eq? (token-tag (car tokens))
		      token-tag-whitespace)
		 ;; Skip whitespace.
		 (loop (cdr tokens)))
		((type-spec-keyword? (car tokens))
		 ;; Skip type specifiers.
		 (loop (cdr tokens)))))))

  (define (store-typedef! env tokens)
    ;; TODO
    '())

;;; Return a list of sublists extracted from `tokens`
;;; that begin with a keyword token.
  (define (filter-keywords tokens)
    (if (null? tokens)
	'()
	(let loop ((tokens tokens)
		   (keyword-tokens '()))
	  (if (null? tokens)
	      keyword-tokens
	      (loop
	       (cdr tokens)
	       (if (eq? (token-tag (car tokens))
			token-tag-keyword)
		   (cons tokens keyword-tokens)
		   keyword-tokens))))))

  (define (make-types-env token-lines . token-lines-lst)
    (define (extend-types-env env token-lines)
      (for-each
       (lambda (tokens)
	 (let ((token (car tokens)))
	   (cond ((type-spec-keyword? token)
		  (store-type-spec! env tokens))
		 ((equal? (token-text token)
			  "typedef")
		  (store-typedef! env tokens)))))
       (filter-keywords (flatten token-lines))))
    (let ((env (list '*env*)))
      (extend-types-env env token-lines)
      (for-each
       (lambda (token-lines)
	 (extend-types-env env token-lines))
       token-lines-lst)
      env))

  (define (types-env? env)
    (and (pair? env)
	 (eq? '*env* (car env))))

  (define (is-type-in-env? env type)
    (if (types-env? env)
	(find (lambda (type-identifier)
		(equal? type-identifier type))
	      (cdr env))
	#f))
  ;; End of module c-types.
  )
