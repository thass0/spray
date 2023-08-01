(load "src/colorize.scm")

(import (chicken foreign)		; define-external, foreign types etc.
	(chicken platform))		; return-to-host

;; `extern void print_colored(char *code);`
(define-external (print_colored (c-string code)
				(unsigned-int start-lineno)
				(unsigned-int active-lineno)) void
  (print-tokens (tokenize code)
		start-lineno
		active-lineno))

(return-to-host)
