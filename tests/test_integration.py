from subprocess import Popen, PIPE
import re
import string
import random

COMMAND = 'build/spray'
DEBUGEE = 'tests/assets/linux_x86_bin'


def random_string() -> str:
    letters = string.ascii_letters
    return ''.join(random.choice(letters) for i in range(8))


def assert_lit(cmd: str, out: str):
    stdout = run_cmd(cmd)
    assert out in stdout


class CmdOutput(str):
    def ends_with(self, end: str) -> bool:
        pattern = f'{re.escape(end)}$'
        match = re.search(pattern, self, re.MULTILINE)
        if match:
            return True
        else:
            return False


def run_cmd(commands: str) -> CmdOutput:
    p = Popen([COMMAND, DEBUGEE], stdout=PIPE, stdin=PIPE, stderr=PIPE)
    output = p.communicate(commands.encode('UTF-8'))
    return CmdOutput(output[0].decode('UTF-8'))


class TestStepCommands:
    def test_single_step(self):
        assert_lit('b 0x0040116b\nc\ns', '💢 Failed to find another line')

        stdout = run_cmd('b 0x0040115d\nc\ns\ns\ns\ns\ns\ns')
        assert stdout.ends_with("""\
->   int c = weird_sum(a, b);
      return 0;
    }
""")
        stdout = run_cmd('b 0x0040115d\nc\ns\ns')
        assert stdout.ends_with("""\
    int weird_sum(int a, int b) {
 ->   int c = a + 1;
      int d = b + 2;
      int e = c + d;
      return e;
    }
""")

    def test_leave(self):
        stdout = run_cmd('b 0x00401120\nc\nl')
        assert stdout.ends_with("""\
 ->   int c = weird_sum(a, b);
      return 0;
    }
""")

    def test_instruction_step(self):
        assert_lit('b 0x00401156\nc\ni\ni\ni\ni\ni',
                   '<No source info for PC 0x0000000000401111>\n')


class TestRegisterCommands:
    def test_register_read(self):
        assert_lit('r rip rd', '     rip 0x00007ffff7fe53b0')
        assert_lit('register rip read', '     rip 0x00007ffff7fe53b0')

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
