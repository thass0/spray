(load "src/colorize.scm")

(import (chicken foreign)		; define-external, foreign types etc.
	(chicken platform))		; return-to-host

;; `extern void print_colored(char *code);`
(define-external (print_colored (c-string code)) void
  (print-tokens (tokenize code)))

(return-to-host)
