(load "src/tokenize.scm")
(load "src/c-syntax.scm")

(import test
	c-types
	tokenizer)

(test-group "(c-types)"
  (test-assert "check finds single struct type"
    (is-type-in-env? (make-types-env (tokenize (list "struct a {};"))) "a"))
  (test-assert "check finds single union type"
    (is-type-in-env? (make-types-env (tokenize (list "union a {};"))) "a"))
  (test-assert "check finds single enum type"
    (is-type-in-env? (make-types-env (tokenize (list "enum a {};"))) "a"))

  (test "single struct type"
    (list '*env* "a")
    (make-types-env (tokenize (list "struct a {};"))))
  (test "single union type"
    (list '*env* "a")
    (make-types-env (tokenize (list "union a {};"))))
  (test "single enum type"
    (list '*env* "a")
    (make-types-env (tokenize (list "enum a {};"))))
  
  (test-assert "finds in multiple types"
    (is-type-in-env?
     (make-types-env (tokenize (list "enum a {}; union b {}; struct c {}'")))
     "c"))
  (test "multiple types"
	(list '*env* "a" "b" "c")
	(make-types-env (tokenize (list "enum a {}; union b {}; struct c {}'"))))
  ;; End of test-group c-types
  )

(test-exit)
