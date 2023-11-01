from subprocess import Popen, PIPE, run
from typing import Optional
import re
import string
import random

DEBUGGER = 'build/spray'
SIMPLE_64BIT_BIN = 'tests/assets/64bit-linux-simple.bin'
NESTED_FUNCTIONS_BIN = 'tests/assets/nested-functions.bin'
FRAME_POINTER_BIN = 'tests/assets/frame-pointer-nested-functions.bin'
NO_FRAME_POINTER_BIN = 'tests/assets/no-frame-pointer-nested-functions.bin'
MULTI_FILE_BIN = 'tests/assets/multi-file.bin'
PRINT_ARGS_BIN = 'tests/assets/print-args.bin'
COMMENTED_BIN = 'tests/assets/commented.bin'
CUSTOM_TYPES_BIN = 'tests/assets/custom-types.bin'
TYPE_EXAMPLES_BIN = 'tests/assets/type-examples.bin'


def random_string() -> str:
    letters = string.ascii_letters
    return ''.join(random.choice(letters) for i in range(8))


def run_cmd(commands: str, debugee: str, flags: list[str], args: list[str]) -> str:
    with Popen([DEBUGGER] + flags + [debugee] + args, stdout=PIPE, stdin=PIPE, stderr=PIPE) as dbg:
        output = dbg.communicate(commands.encode('UTF-8'))
        return output[0].decode('UTF-8')


def assert_lit(cmd: str,
               out: str,
               debugee: Optional[str] = SIMPLE_64BIT_BIN,
               args: Optional[list[str]] = [""]):
    stdout = run_cmd(cmd, debugee, ['--no-color'], args)
    print(stdout)
    assert out in stdout


def assert_ends_with(cmd: str,
                     end: str,
                     debugee: Optional[str] = SIMPLE_64BIT_BIN,
                     args: Optional[list[str]] = [""]):
    stdout = run_cmd(cmd, debugee, ['--no-color'], args)
    pattern = f'{re.escape(end)}$'
    match = re.search(pattern, stdout, re.MULTILINE)
    if not match:
        print(stdout)
    assert match


class TestStepCommands:
    def test_single_step(self):
        assert_lit('b 0x0040116b\nc\ns',
                   'ERR: Failed to find another line to step to')

        assert_ends_with('b 0x0040115d\nc\ns\ns\ns\ns\ns\ns', """\
   12 ->   int c = weird_sum(a, b);
   13      return 0;
   14    }
""")
        assert_ends_with('b 0x0040115d\nc\ns\ns', """\
    1    int weird_sum(int a,
    2                  int b) {
    3 ->   int c = a + 1;
    4      int d = b + 2;
    5      int e = c + d;
    6      return e;
""")

    def test_leave(self):
        assert_ends_with('b 0x00401120\nc\nl', """\
   12 ->   int c = weird_sum(a, b);
   13      return 0;
   14    }
""")

    def test_instruction_step(self):
        assert_ends_with('b 0x00401156\nc\ni\ni\ni\ni\ni', """\
    1    int weird_sum(int a,
    2                  int b) {
    3 ->   int c = a + 1;
    4      int d = b + 2;
    5      int e = c + d;
    6      return e;
""")

    def test_next(self):
        assert_ends_with('b 0x0040113d\nc\nn', """\
    2
    3    int add(int a, int b) {
    4      int c = a + b;
    5 ->   return c;
    6    }
    7
    8    int mul(int a, int b) {
""", NESTED_FUNCTIONS_BIN)

        assert_lit('n\nn\nn', """\
   17      int product = mul(9, 3);
   18      int sum = add(product, 6);
   19      printf("Product: %d; Sum: %d\\n", product, sum);
   20 ->   return 0;
   21    }
   22
""", NESTED_FUNCTIONS_BIN)


class TestRegisterCommands:
    def test_register_read(self):
        assert_lit('p %rip',
                   '     rip 00 00 00 00 00 40 11 4f')
        assert_lit('print %rip',
                   '     rip 00 00 00 00 00 40 11 4f')

    def test_register_command_errors(self):
        assert_lit('t %rax', 'Missing value to set the location to')
        assert_lit('t %rax ' + random_string(), 'Invalid value to set the location to')
        assert_lit('t %rax 0xc0ffee 0xbeef', 'Trailing characters in command')
        assert_lit('ste %rax 0x31', 'Unknown command')

    def test_register_write(self):
        assert_lit('t %rax 123',
                   '     rax 123 (read after write)')
        assert_lit('set %rbx 0xdeadbeef',
                   '     rbx 0xdeadbeef (read after write)')

    def test_register_name_conflict(self):
        assert_ends_with('p rax', """\
WARN: The variable name 'rax' is also the name of a register
HINT: All register names start with a '%'. Use '%rax' to access the 'rax' register instead
ERR: Failed to find a variable called rax
""")

class TestMemoryCommands:
    def test_memory_read(self):
        assert_lit('p 0x403020',
                   '         ff ff f0 40 00 00 00 48')
        assert_lit('print 0x403020',
                   '         ff ff f0 40 00 00 00 48')

    def test_memory_command_errors(self):
        assert_lit('t 0x123', 'Missing value to set the location to')
        assert_lit('t 0x123 ' + random_string(), 'Invalid value to set the location to')
        assert_lit('t 0x123 0xc0ffee 0xbeef', 'Trailing characters in command')
        assert_lit('ste 0xc0ffee 0x31', 'Unknown command')

    def test_memory_write(self):
        assert_lit('t 0x00007ffff7fe53b0 0xdecafbad',
                   '         0xdecafbad (read after write)')
        assert_lit('set 0x00007ffff7fe53a5 0xbadeaffe',
                   '         0xbadeaffe (read after write)')


class TestFilters:
    def test_filter_print(self):
        assert_ends_with("""t a 103
        p a | hex
        p a | bits
        p a | addr
        p a | dec
        p a | bytes""", """\
         103 (read after write) (tests/assets/simple.c:10)
         0x67 (tests/assets/simple.c:10)
         00000000 00000000 00000000 00000000 00000000 00000000 00000000 01100111 (tests/assets/simple.c:10)
         0x0000000000000067 (tests/assets/simple.c:10)
         103 (tests/assets/simple.c:10)
         00 00 00 00 00 00 00 67 (tests/assets/simple.c:10)
""")

    def test_filter_print_errors(self):
        assert_ends_with('t a 103\np a |bytes', 'Trailing characters in command')
        assert_ends_with('t a 103\np a |', 'Invalid filter')
        assert_ends_with('t a 103\np a | blah-invalid-filter', 'Invalid filter')

    def test_filter_set(self):
        assert_ends_with("""t a 103
        t a 0x600 | bytes
        t a 0x700 | hex
        t a 0x800""", """\
         00 00 00 00 00 00 06 00 (read after write) (tests/assets/simple.c:10)
         0x700 (read after write) (tests/assets/simple.c:10)
         0x800 (read after write) (tests/assets/simple.c:10)
""")

    def test_filter_set_errors(self):
        assert_ends_with('t a 0x10 |hex', 'Trailing characters in command')
        assert_ends_with('t a 0x10 |', 'Invalid filter')
        assert_ends_with('t a 0x10 | blah-invalid-filter', 'Invalid filter')


class TestTypedPrint():
    def test_print_typed_variables(self):
        assert_ends_with('b tests/assets/type_examples.c:24\nc\np a\np b\np c\n p d\np e\np f\np g\np h\np i\np j\np k\np l\np m\n', """
         1 (tests/assets/type_examples.c:1)
         2 (tests/assets/type_examples.c:2)
         0x3 (tests/assets/type_examples.c:3)
         0x4 (tests/assets/type_examples.c:4)
         0x5 (tests/assets/type_examples.c:5)
         0x6 (tests/assets/type_examples.c:6)
         0x7 (tests/assets/type_examples.c:7)
         'a' (tests/assets/type_examples.c:11)
         98 (tests/assets/type_examples.c:12)
         99 (tests/assets/type_examples.c:13)
         'd' (tests/assets/type_examples.c:14)
         101 (tests/assets/type_examples.c:15)
         102 (tests/assets/type_examples.c:16)
""", TYPE_EXAMPLES_BIN)

    def test_filter_typed_variables(self):
        assert_ends_with('b tests/assets/type_examples.c:24\nc\np i\np i | bytes\np i | hex\np n\np n | bytes\n', """
         98 (tests/assets/type_examples.c:12)
         00 00 00 00 00 00 00 62 (tests/assets/type_examples.c:12)
         0x62 (tests/assets/type_examples.c:12)
         9223372036854775808 (tests/assets/type_examples.c:17)
         80 00 00 00 00 00 00 00 (tests/assets/type_examples.c:17)
""", TYPE_EXAMPLES_BIN)

    def test_typedef_variables(self):
        assert_ends_with('b tests/assets/type_examples.c:24\nc\np o\np p\np p | bytes\n', """
         -123456789 (tests/assets/type_examples.c:20)
         255 (tests/assets/type_examples.c:22)
         00 00 00 00 00 00 00 ff (tests/assets/type_examples.c:22)
""", TYPE_EXAMPLES_BIN)


class TestBreakpointCommands:
    def test_breakpoint_set_and_delete(self):
        assert_lit('b 0x00401146\nd 0x00401146\nc', 'Child exited with code 0',
                   NESTED_FUNCTIONS_BIN)

    def test_breakpoint_on_function(self):
        assert_ends_with('b weird_sum\nc', """\
Hit breakpoint at address 0x000000000040111a in tests/assets/simple.c
    1    int weird_sum(int a,
    2                  int b) {
    3 ->   int c = a + 1;
    4      int d = b + 2;
    5      int e = c + d;
    6      return e;
""")
        # This checks that the function `add` has
        # precedence over the address `(0x)add`.
        assert_ends_with('b add\nc', """\
Hit breakpoint at address 0x000000000040113a in tests/assets/nested_functions.c
    1    #include <stdio.h>
    2
    3    int add(int a, int b) {
    4 ->   int c = a + b;
    5      return c;
    6    }
    7
""", NESTED_FUNCTIONS_BIN)

    def test_breakpoint_on_file_line(self):
        assert_ends_with('break tests/assets/file1.c:4\nc', """\
    1    #include "file2.h"
    2
    3    int file1_compute_something(int n) {
    4 ->   int i = 0;
    5      int acc = 0;
    6      while (i < n) {
    7        acc += i * i;
""", MULTI_FILE_BIN)

        # Only providing the filename works too,
        # even if the file doesn't exist in the
        # current directory.
        assert_ends_with('break file1.c:4\nc', """\
    1    #include "file2.h"
    2
    3    int file1_compute_something(int n) {
    4 ->   int i = 0;
    5      int acc = 0;
    6      while (i < n) {
    7        acc += i * i;
""", MULTI_FILE_BIN)

        # Breakpoints in different files that the
        # entrypoint work, too.
        assert_ends_with('break file2.c:7\nc', """\
in tests/assets/multi-file/file2.c
    4      if (n < 2) {
    5        return n;
    6      } else {
    7 ->     return   file2_compute_something(n - 1)
    8               + file2_compute_something(n - 2);
    9      }
   10    }
""", MULTI_FILE_BIN)

        # Breaking on an empty line falls through
        # to the next line with code on it.
        assert_ends_with('break file2.c:1\nc', """\
in tests/assets/multi-file/file2.c
    1    #include "file2.h"
    2
    3 -> int file2_compute_something(int n) {
    4      if (n < 2) {
    5        return n;
    6      } else {
""", MULTI_FILE_BIN)

    def test_breakpoints_delete(self):
        assert_ends_with('break file2.c:4\nc\ndelete file2.c:4\nc', """\
    1    #include "file2.h"
    2
    3    int file2_compute_something(int n) {
    4 ->   if (n < 2) {
    5        return n;
    6      } else {
    7        return   file2_compute_something(n - 1)
Child exited with code 0
""", MULTI_FILE_BIN)

        assert_ends_with('break add\nc\ndelete add\nc', """\
Hit breakpoint at address 0x000000000040113a in tests/assets/nested_functions.c
    1    #include <stdio.h>
    2
    3    int add(int a, int b) {
    4 ->   int c = a + b;
    5      return c;
    6    }
    7
Child exited with code 0
""", NESTED_FUNCTIONS_BIN)


class TestArumentPassing:
    def test_arguments_are_passed(self):
        assert_lit('c', """\
Command line arguments: tests/assets/print-args.bin Hello World
""", PRINT_ARGS_BIN, ["Hello", "World"])

USAGE_MSG = 'usage: ' + DEBUGGER

class TestCLI:
    def output(self, args: Optional[list[str]] = []):
        return run([DEBUGGER] + args, capture_output=True, text=True).stderr

    def test_help_if_no_args(self):
        assert USAGE_MSG in self.output()

    def test_help_if_invalid_flag(self):
        # `--colorize` is not a valid flag.
        assert USAGE_MSG in self.output(['--colorize'])
        assert USAGE_MSG in self.output(['--colorize', FRAME_POINTER_BIN])
        assert USAGE_MSG in self.output(['--colorize', PRINT_ARGS_BIN, 'hello', 'world'])

    def test_help_if_wrong_prefix(self):
        # `-c` is valid, `--c` is not!
        assert USAGE_MSG in self.output(['--c', PRINT_ARGS_BIN])

    def test_help_if_no_binary(self):
        # `--no-color` is a valid flag but we didn't specify a binary.
        assert USAGE_MSG in self.output(['--no-color'])


class TestBacktrace:
    def test_basic_backtrace(self):
        assert_ends_with('b add\nc\nbacktrace', """\
How did we even get here? (backtrace)
  0x0000000000401065 _start
  0x00007ffff7df4c4b <?>
  0x00007ffff7df4b8a <?>
  0x00000000004011be main:17
  0x0000000000401183 mul:11
  0x000000000040113a add:4
""", FRAME_POINTER_BIN)

        assert_ends_with('b add\nc\na', """\
WARN: it seems like this executable doesn't maintain a frame pointer.
      This results in incorrect or incomplete backtraces.
HINT: Try to compile again with `-fno-omit-frame-pointer`.

How did we even get here? (backtrace)
  0x0000000000401065 _start
  0x00007ffff7df4c4b <?>
  0x0000000000401138 add:4
""", NO_FRAME_POINTER_BIN)

class TestColors:
    def test_colored_comments(self):
        stdout = run_cmd('', COMMENTED_BIN, [], [])
        # `expect` contains all the escape codes that
        # will produce the right syntax highlighting.
        expect = """\
    4    \033[96m  I start outside the text that's printed.\033[0m
    5    \033[96m  and I span more than one line. \033[0m\033[96m*/\033[0m
    6    \033[32mint\033[0m \033[0mmain\033[0m(\033[0m\033[32mvoid\033[0m)\033[0m \033[0m{\033[0m  \033[0m\033[96m/*\033[0m\033[96m blah! \033[0m\033[96m*/\033[0m
    7 ->   \033[0mprintf\033[0m(\033[0m\033[31m"blah\\n"\033[0m)\033[0m;\033[0m  \033[0m\033[96m//\033[0m\033[96m This C++ style comment can contain this */ or that /*.\033[0m
    8      \033[0m\033[32mint\033[0m \033[0ma\033[0m \033[0m\033[33m=\033[0m \033[0m\033[34m7\033[0m;\033[0m
    9      \033[0m\033[96m/*\033[0m\033[96m This comment ends outside the printed text\033[0m
   10    \033[96m     and spans multiple lines, too.\033[0m
"""
        assert expect in stdout

    def test_colored_custom_types(self):
        stdout = run_cmd('break print_rat\nc\nn\nn', CUSTOM_TYPES_BIN, [], [])
        expect = """\
    6    }\033[0m;\033[0m
    7
    8    \033[32mvoid\033[0m \033[0mprint_rat\033[0m(\033[0m\033[35mstruct\033[0m \033[0m\033[32mRational\033[0m \033[0mrat\033[0m)\033[0m \033[0m{\033[0m
    9 ->   \033[0mprintf\033[0m(\033[0m\033[31m"%d / %d\\n"\033[0m,\033[0m \033[0mrat\033[0m\033[33m.\033[0mnumer\033[0m,\033[0m \033[0mrat\033[0m\033[33m.\033[0mdenom\033[0m)\033[0m;\033[0m
   10    }\033[0m
   11
   12    \033[96m/*\033[0m\033[96m `breakpoints` starts with the keyword `break`.\033[0m
    7
    8    \033[32mvoid\033[0m \033[0mprint_rat\033[0m(\033[0m\033[35mstruct\033[0m \033[0m\033[32mRational\033[0m \033[0mrat\033[0m)\033[0m \033[0m{\033[0m
    9      \033[0mprintf\033[0m(\033[0m\033[31m"%d / %d\\n"\033[0m,\033[0m \033[0mrat\033[0m\033[33m.\033[0mnumer\033[0m,\033[0m \033[0mrat\033[0m\033[33m.\033[0mdenom\033[0m)\033[0m;\033[0m
   10 -> }\033[0m
   11
   12    \033[96m/*\033[0m\033[96m `breakpoints` starts with the keyword `break`.\033[0m
   13    \033[96m   The syntax-highlighter must get confused by it. \033[0m\033[96m*/\033[0m
   20      \033[0mrat\033[0m\033[33m.\033[0mnumer\033[0m \033[0m\033[33m=\033[0m \033[0m\033[34m9\033[0m;\033[0m
   21      \033[0mprintf\033[0m(\033[0m\033[31m"The numerator is: %d\\n"\033[0m,\033[0m \033[0mrat\033[0m\033[33m.\033[0mnumer\033[0m)\033[0m;\033[0m
   22      \033[0mprint_rat\033[0m(\033[0mrat\033[0m)\033[0m;\033[0m
   23 ->   \033[0m\033[35mstruct\033[0m \033[0m\033[32mbreakpoints\033[0m \033[0mbp\033[0m \033[0m\033[33m=\033[0m \033[0m{\033[0m \033[0m\033[31m"hey!"\033[0m \033[0m}\033[0m;\033[0m
   24      \033[0m\033[35mreturn\033[0m \033[0m\033[34m0\033[0m;\033[0m
   25    }\033[0m
   26
"""
        assert expect in stdout

    def test_colored_custom_types_in_other_file(self):
        stdout = run_cmd('n\nn\ns\nbreak file1.c:19\nc', MULTI_FILE_BIN, [], [])
        expect = """\
   11    }\033[0m
   12
   13    \033[32mint\033[0m \033[0mmain\033[0m(\033[0m\033[32mvoid\033[0m)\033[0m \033[0m{\033[0m
   14 ->   \033[0m\033[32mint\033[0m \033[0mnum1\033[0m \033[0m\033[33m=\033[0m \033[0mfile1_compute_something\033[0m(\033[0m\033[34m3\033[0m)\033[0m;\033[0m
   15      \033[0m\033[32mint\033[0m \033[0mnum2\033[0m \033[0m\033[33m=\033[0m \033[0mfile2_compute_something\033[0m(\033[0mnum1\033[0m)\033[0m;\033[0m
   16      \033[0m(\033[0m\033[32mvoid\033[0m)\033[0m \033[0m(\033[0mnum1\033[0m \033[0m\033[33m+\033[0m \033[0mnum2\033[0m)\033[0m;\033[0m
   17      \033[0m\033[35mstruct\033[0m \033[0m\033[32mBlah\033[0m \033[0mblah\033[0m \033[0m\033[33m=\033[0m \033[0mfile2_init_blah\033[0m(\033[0m\033[34m4\033[0m)\033[0m;\033[0m
   12
   13    \033[32mint\033[0m \033[0mmain\033[0m(\033[0m\033[32mvoid\033[0m)\033[0m \033[0m{\033[0m
   14      \033[0m\033[32mint\033[0m \033[0mnum1\033[0m \033[0m\033[33m=\033[0m \033[0mfile1_compute_something\033[0m(\033[0m\033[34m3\033[0m)\033[0m;\033[0m
   15 ->   \033[0m\033[32mint\033[0m \033[0mnum2\033[0m \033[0m\033[33m=\033[0m \033[0mfile2_compute_something\033[0m(\033[0mnum1\033[0m)\033[0m;\033[0m
   16      \033[0m(\033[0m\033[32mvoid\033[0m)\033[0m \033[0m(\033[0mnum1\033[0m \033[0m\033[33m+\033[0m \033[0mnum2\033[0m)\033[0m;\033[0m
   17      \033[0m\033[35mstruct\033[0m \033[0m\033[32mBlah\033[0m \033[0mblah\033[0m \033[0m\033[33m=\033[0m \033[0mfile2_init_blah\033[0m(\033[0m\033[34m4\033[0m)\033[0m;\033[0m
   18      \033[0m(\033[0m\033[32mvoid\033[0m)\033[0m \033[0mblah\033[0m;\033[0m
   14      \033[0m\033[32mint\033[0m \033[0mnum1\033[0m \033[0m\033[33m=\033[0m \033[0mfile1_compute_something\033[0m(\033[0m\033[34m3\033[0m)\033[0m;\033[0m
   15      \033[0m\033[32mint\033[0m \033[0mnum2\033[0m \033[0m\033[33m=\033[0m \033[0mfile2_compute_something\033[0m(\033[0mnum1\033[0m)\033[0m;\033[0m
   16      \033[0m(\033[0m\033[32mvoid\033[0m)\033[0m \033[0m(\033[0mnum1\033[0m \033[0m\033[33m+\033[0m \033[0mnum2\033[0m)\033[0m;\033[0m
   17 ->   \033[0m\033[35mstruct\033[0m \033[0m\033[32mBlah\033[0m \033[0mblah\033[0m \033[0m\033[33m=\033[0m \033[0mfile2_init_blah\033[0m(\033[0m\033[34m4\033[0m)\033[0m;\033[0m
   18      \033[0m(\033[0m\033[32mvoid\033[0m)\033[0m \033[0mblah\033[0m;\033[0m
   19      \033[0m\033[35mreturn\033[0m \033[0m\033[34m0\033[0m;\033[0m
   20    }\033[0m
    9      \033[0m}\033[0m
   10    }\033[0m
   11
   12 -> \033[35mstruct\033[0m \033[0m\033[32mBlah\033[0m \033[0mfile2_init_blah\033[0m(\033[0m\033[32mint\033[0m \033[0mx\033[0m)\033[0m \033[0m{\033[0m
   13      \033[0m\033[35mreturn\033[0m \033[0m(\033[0m\033[35mstruct\033[0m \033[0m\033[32mBlah\033[0m)\033[0m \033[0m{\033[0m \033[0mx\033[0m \033[0m}\033[0m;\033[0m
   14    }\033[0m
Hit breakpoint at address 0x0000000000401194 in tests/assets/multi-file/file1.c
   16      \033[0m(\033[0m\033[32mvoid\033[0m)\033[0m \033[0m(\033[0mnum1\033[0m \033[0m\033[33m+\033[0m \033[0mnum2\033[0m)\033[0m;\033[0m
   17      \033[0m\033[35mstruct\033[0m \033[0m\033[32mBlah\033[0m \033[0mblah\033[0m \033[0m\033[33m=\033[0m \033[0mfile2_init_blah\033[0m(\033[0m\033[34m4\033[0m)\033[0m;\033[0m
   18      \033[0m(\033[0m\033[32mvoid\033[0m)\033[0m \033[0mblah\033[0m;\033[0m
   19 ->   \033[0m\033[35mreturn\033[0m \033[0m\033[34m0\033[0m;\033[0m
   20    }\033[0m
"""
        assert expect in stdout
