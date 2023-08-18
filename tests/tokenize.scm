(load "src/tokenize.scm")

(import test
	c-regex
	tokenizer)

(test-group "(test-regex)"
  (test-group "(test-literal-regex)"
    (test "match a string made up of only a string literal"
	  "\"blah\""
	  (full-match literal-regex "\"blah\""))
    (test "match a string starting with a string literal"
	  "\"blah\""
	  (full-match  literal-regex "\"blah\" ... some more lame text"))
    (test-assert "don't match a string now starting with a string literal"
      (not (regex-match? literal-regex "blah ... invalid text \"string literal\"")))
    ;; After removing the backslashes to embed this string in this source,
    ;; the string below looks like this: "\\ \" ... blah "
    (test "match escaped backslashes and quotation marks"
	  "\"\\\\ \\\" ... blah \""
	  (full-match literal-regex "\"\\\\ \\\" ... blah \"")))

  (test-group "(test-whitespace-regex)"
    (test "only whitespace"
	   " \t\t\r  "
	  (full-match whitespace-regex  " \t\t\r  "))
    (test-assert "beginning whitespace"
      (regex-match? whitespace-regex "\n \t  blah"))
    (test-assert "no beginning whitespace"
      (not (regex-match? whitespace-regex "blah \n  "))))

  (test-group "(test-identifier-regex)"
    (test-assert "identifier starting with numbers"
      (not (regex-match? identifier-regex "98blah")))
    (test "identifier including numbers and under scores"
	  "blah_984baz"
	  (full-match identifier-regex "blah_984baz"))
    (test-assert "identifier starting with whitespace"
      (not (regex-match? identifier-regex "  \n blah"))))

  (test-group "(test-hex-constant-regex)"
    (test-assert "hex constant"
      (regex-match? hex-constant-regex "0x81babe"))
    (test-assert "no hex prefix"
      (not (regex-match? hex-constant-regex "92873")))
    (test-assert "type postfixes"
      (regex-match? hex-constant-regex "0xbad1dealueLU")))

  (test-group "(test-octal-constant-regex)"
    (test-assert "octal constant"
      (regex-match? octal-constant-regex "01543672"))
    (test-assert "out of octal range"
      (not (regex-match? octal-constant-regex "09082"))))

  (test-group "(test-decimal-constant-regex)"
    (test-assert "decimal constant"
      (regex-match? decimal-constant-regex "780934lu"))
    (test-assert "single zero as decimal"
      (regex-match? decimal-constant-regex "0")))

  (test-group "(test-char-constant-regex)"
    (test-assert "character constant"
      (regex-match? char-constant-regex "'a'"))
    (test-assert "multi-character constant"
      (regex-match? char-constant-regex "'abc\\n'"))
    (test-assert (not (regex-match? char-constant-regex "blha 'a'"))))

  (test-group "(test-sci-constant-regex)"
    (test-assert "scientific notation constant"
      (regex-match? sci-constant-regex "81e-2")))

  (test-group "(test-float-constant-regex-frac)"
    (test-assert "fraction floating point constant without whole part"
      (regex-match? float-constant-regex-frac ".024E-3F"))
    (test-assert "fraction floating point constant with whole part"
      (regex-match? float-constant-regex-frac "0184.708e+9fl"))
    (test-assert "fraction floating point constant without fraction part"
      (not (regex-match? float-constant-regex-frac "98.e+085L"))))

  (test-group "(test-float-constant-regex-whole)"
    (test-assert "whole floating point constant without fraction part"
      (regex-match? float-constant-regex-whole "983.E-3F"))
    (test-assert "whole floating point constant with fraction part"
      (regex-match? float-constant-regex-whole "0184.708e+9fl"))
    (test-assert "whole floating point constant without whole part"
      (not (regex-match? float-constant-regex-whole ".41e+085L"))))

  (test-group "(test-preproc-directive-regex)"
    (test-assert "include directive"
      (regex-match? preproc-directive-regex "#include"))
    (test-assert "undef directive"
      (regex-match? preproc-directive-regex "#undef"))
    (test-assert "include_next directive"
      (regex-match? preproc-directive-regex "#include_next"))
    (test "include directive with angle-brackets path"
	  "#include <blah.h>"
	  (full-match preproc-directive-regex "#include <blah.h> int main ..."))
    (test-assert "include directive with quotes path"
      (regex-match? preproc-directive-regex "#import \"blah.h\"")))

  (test-group "(test-comment-text-regex)"
    (test "block comment regex"
	  "blah "
	  (full-match comment-text-regex "blah */ asdf"))
    (test "block comment regex with star and newline"
	  "blah \n * asdf "
	  (full-match comment-text-regex "blah \n * asdf */"))
    (test "line comment containing block comment"
	  "blah /* hey */ wow"
	  (full-match line-comment-text-regex "blah /* hey */ wow\n asdf")))
  ;; End of test-group (test-regex).
  )

(test-group "(test-tokenize)"
  (test "basic code example"
	(tokenize (list "int main(void) {" "    int i = 0;" "    for (; i < 91; i++) {" "        printf(\"Blah: %d\" i);" "    }"))
	'(((tt-type . "int") (tt-whitespace . " ") (tt-identifier . "main") (tt-special-symbol . "(") (tt-type . "void") (tt-special-symbol . ")") (tt-whitespace . " ") (tt-special-symbol . "{")) ((tt-whitespace . "    ") (tt-type . "int") (tt-whitespace . " ") (tt-identifier . "i") (tt-whitespace . " ") (tt-operator . "=") (tt-whitespace . " ") (tt-constant . "0") (tt-special-symbol . ";")) ((tt-whitespace . "    ") (tt-keyword . "for") (tt-whitespace . " ") (tt-special-symbol . "(") (tt-special-symbol . ";") (tt-whitespace . " ") (tt-identifier . "i") (tt-whitespace . " ") (tt-operator . "<") (tt-whitespace . " ") (tt-constant . "91") (tt-special-symbol . ";") (tt-whitespace . " ") (tt-identifier . "i") (tt-operator . "++") (tt-special-symbol . ")") (tt-whitespace . " ") (tt-special-symbol . "{")) ((tt-whitespace . "        ") (tt-identifier . "printf") (tt-special-symbol . "(") (tt-literal . "\"Blah: %d\"") (tt-whitespace . " ") (tt-identifier . "i") (tt-special-symbol . ")") (tt-special-symbol . ";")) ((tt-whitespace . "    ") (tt-special-symbol . "}"))))

  (test "error recovery at whitespace"
	(tokenize  (list "int Äpfel = (6 + 4) * 9;"))
	'(((tt-type . "int") (tt-whitespace . " ") (tt-other . "Äpfel") (tt-whitespace . " ") (tt-operator . "=") (tt-whitespace . " ") (tt-special-symbol . "(") (tt-constant . "6") (tt-whitespace . " ") (tt-operator . "+") (tt-whitespace . " ") (tt-constant . "4") (tt-special-symbol . ")") (tt-whitespace . " ") (tt-operator . "*") (tt-whitespace . " ") (tt-constant . "9") (tt-special-symbol . ";"))))

  (test "single C-style comments"
	(tokenize (list "int main(void) {" "    /* blah */" "    printf(\"blah\");" "}"))
	'(((tt-type . "int") (tt-whitespace . " ") (tt-identifier . "main") (tt-special-symbol . "(") (tt-type . "void") (tt-special-symbol . ")") (tt-whitespace . " ") (tt-special-symbol . "{")) ((tt-whitespace . "    ") (tt-comment . "/*") (tt-comment-text . " blah ") (tt-uncomment . "*/")) ((tt-whitespace . "    ") (tt-identifier . "printf") (tt-special-symbol . "(") (tt-literal . "\"blah\"") (tt-special-symbol . ")") (tt-special-symbol . ";")) ((tt-special-symbol . "}"))))

  (test "multi-line C-style comments"
	(tokenize (list "int a = 2;" "/*blah" "asdf */" "int b = 4;"))
	'(((tt-type . "int") (tt-whitespace . " ") (tt-identifier . "a") (tt-whitespace . " ") (tt-operator . "=") (tt-whitespace . " ") (tt-constant . "2") (tt-special-symbol . ";")) ((tt-comment . "/*") (tt-comment-text . "blah")) ((tt-comment-text . "asdf ") (tt-uncomment . "*/")) ((tt-type . "int") (tt-whitespace . " ") (tt-identifier . "b") (tt-whitespace . " ") (tt-operator . "=") (tt-whitespace . " ") (tt-constant . "4") (tt-special-symbol . ";"))))

  (test "multi-line C-style comment without end"
	(tokenize (list "int blah = 5;" "/* I don't end," "But this is still me"))
	'(((tt-type . "int") (tt-whitespace . " ") (tt-identifier . "blah") (tt-whitespace . " ") (tt-operator . "=") (tt-whitespace . " ") (tt-constant . "5") (tt-special-symbol . ";")) ((tt-comment . "/*") (tt-comment-text . " I don't end,")) ((tt-comment-text . "But this is still me"))))

  (test "C-style comment without beginning wraps to start"
	(tokenize (list "int main(void) */ {int a = 0;"))
	'(((tt-comment-text . "int main(void) ") (tt-trailing-uncomment . "*/") (tt-whitespace . " ") (tt-special-symbol . "{") (tt-type . "int") (tt-whitespace . " ") (tt-identifier . "a") (tt-whitespace . " ") (tt-operator . "=") (tt-whitespace . " ") (tt-constant . "0") (tt-special-symbol . ";"))))

  (test "code can be commented-out"
	(tokenize (list "int" "a" "=" "2;" " */ /* another comment */"))
	'(((tt-comment-text . "int")) ((tt-comment-text . "a")) ((tt-comment-text . "=")) ((tt-comment-text . "2;")) ((tt-comment-text . " ") (tt-trailing-uncomment . "*/") (tt-whitespace . " ") (tt-comment . "/*") (tt-comment-text . " another comment ") (tt-uncomment . "*/"))))

  (test "C++ style comments can contain block comments"
	(tokenize (list "int a = 7;  // This C++ style comment can contain this */ or that /*.""// It even continues on the next line!"))
	'(((tt-type . "int") (tt-whitespace . " ") (tt-identifier . "a") (tt-whitespace . " ") (tt-operator . "=") (tt-whitespace . " ") (tt-constant . "7") (tt-special-symbol . ";") (tt-whitespace . "  ") (tt-comment . "//") (tt-comment-text . " This C++ style comment can contain this */ or that /*.")) ((tt-comment . "//") (tt-comment-text . " It even continues on the next line!"))))
  ;; End test-group (test-tokenize).
  )

(test-exit)
