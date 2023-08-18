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
                   '💢 Failed to find another line to step to')

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
        assert_lit('r rip rd', '     rip 0x000000000040114f')
        assert_lit('register rip read', '     rip 0x000000000040114f')

    def test_register_command_errors(self):
        assert_lit('r rax', 'Missing register operation')
        assert_lit('r rax ' + random_string(), 'Invalid register operation')
        assert_lit('r rax rd 0xdeadbeef', 'Trailing characters in command')
        assert_lit('registre rax rd', 'I don\'t know that')

    def test_register_write(self):
        assert_lit('r rax wr 0x123',
                   '     rax 0x0000000000000123 (read after write)')
        assert_lit('register rbx write 0xdeadbeef',
                   '     rbx 0x00000000deadbeef (read after write)')

    def test_register_dump(self):
        dump_end = 'gs 0x00000000000000'
        assert_lit('register dump', dump_end)
        assert_lit('r dump', dump_end)
        assert_lit('r print', dump_end)


class TestMemoryCommands:
    def test_memory_read(self):
        assert_lit('m 0x00007ffff7fe53b0 rd', '         0x00000c98e8e78948')
        assert_lit('memory 0x00007ffff7fe53ba read',
                   '         0xd6894824148b48c4')

    def test_memory_command_errors(self):
        assert_lit('m 0x123', 'Missing memory operation')
        assert_lit('m 0x123 ' + random_string(), 'Invalid memory operation')
        assert_lit('m 0x123 read 0xdeadbeef', 'Trailing characters in command')
        assert_lit('memroy 0x12 wr 0x34', 'I don\'t know that')

    def test_memory_write(self):
        assert_lit('m 0x00007ffff7fe53b0 wr 0xdecafbad',
                   '         0x00000000decafbad (read after write)')
        assert_lit('memory 0x00007ffff7fe53a5 write 0xbadeaffe',
                   '         0x00000000badeaffe (read after write)')


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
Hit breakpoint at address 0x00000000004011b0 in tests/assets/multi-file/file2.c
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
Hit breakpoint at address 0x0000000000401190 in tests/assets/multi-file/file2.c
    1    #include "file2.h"
    2
    3 -> int file2_compute_something(int n) {
    4      if (n < 2) {
    5        return n;
    6      } else {
""", MULTI_FILE_BIN)

    def test_breakpoints_delete(self):
        assert_ends_with('break file2.c:4\nc\ndelete file2.c:4\nc', """\
Hit breakpoint at address 0x000000000040119b in tests/assets/multi-file/file2.c
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
        assert_ends_with('b add\nc\nbt', """\
How did we even get here? (backtrace)
  0x0000000000401065 _start
  0x00007ffff7df6c0b <?>
  0x00007ffff7df6b4a <?>
  0x00000000004011be main:17
  0x0000000000401183 mul:11
  0x000000000040113a add:4
""", FRAME_POINTER_BIN)

        assert_ends_with('b add\nc\nbt', """\
WARN: it seems like this executable doesn't maintain a frame pointer.
      This results in incorrect or incomplete backtraces.
HINT: Try to compile again with `-fno-omit-frame-pointer`.

How did we even get here? (backtrace)
  0x0000000000401065 _start
  0x00007ffff7df6c0b <?>
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
   12    \033[32mint\033[0m \033[0mmain\033[0m(\033[0m\033[32mvoid\033[0m)\033[0m \033[0m{\033[0m
    7
    8    \033[32mvoid\033[0m \033[0mprint_rat\033[0m(\033[0m\033[35mstruct\033[0m \033[0m\033[32mRational\033[0m \033[0mrat\033[0m)\033[0m \033[0m{\033[0m
    9      \033[0mprintf\033[0m(\033[0m\033[31m"%d / %d\\n"\033[0m,\033[0m \033[0mrat\033[0m\033[33m.\033[0mnumer\033[0m,\033[0m \033[0mrat\033[0m\033[33m.\033[0mdenom\033[0m)\033[0m;\033[0m
   10 -> }\033[0m
   11
   12    \033[32mint\033[0m \033[0mmain\033[0m(\033[0m\033[32mvoid\033[0m)\033[0m \033[0m{\033[0m
   13      \033[0m\033[35mstruct\033[0m \033[0m\033[32mRational\033[0m \033[0mrat\033[0m \033[0m\033[33m=\033[0m \033[0m(\033[0m\033[35mstruct\033[0m \033[0m\033[32mRational\033[0m)\033[0m \033[0m{\033[0m \033[0m\033[34m5\033[0m,\033[0m \033[0m\033[34m3\033[0m \033[0m}\033[0m;\033[0m
   14      \033[0mrat\033[0m\033[33m.\033[0mnumer\033[0m \033[0m\033[33m=\033[0m \033[0m\033[34m9\033[0m;\033[0m
   15      \033[0mprintf\033[0m(\033[0m\033[31m"The numerator is: %d\\n"\033[0m,\033[0m \033[0mrat\033[0m\033[33m.\033[0mnumer\033[0m)\033[0m;\033[0m
   16      \033[0mprint_rat\033[0m(\033[0mrat\033[0m)\033[0m;\033[0m
   17 ->   \033[0m\033[35mreturn\033[0m \033[0m\033[34m0\033[0m;\033[0m
   18    }\033[0m
   19
"""
        assert expect in stdout
