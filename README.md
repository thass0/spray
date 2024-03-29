<p align="center">
	<h2 align="center">🐛🐛🐛 Spray 🐛🐛🐛</h3>
 <p align="center">
  <a href="https://github.com/thass0/spray/#%EF%B8%8F-installation">Get started</a> -
  <a href="https://github.com/thass0/spray/issues">Issues</a> -
  <a href="https://github.com/thass0/spray/issues/new">Bug report</a>
 </p>
</p>

[![Spray Demo](https://asciinema.org/a/620413.svg)](https://asciinema.org/a/620413)

> You can watch a tiny demo of using Spray to interact with a running program here: https://youtu.be/mjwIrfQkURc

Spray is a small debugger for C code that comes with minimal mental overhead. All functionality aims to be simple and easy to grasp.

In Spray you can easily control the execution of running programs, and inspect and modify their state.

I started work on Spray out of curiosity about the mysterious inner workings of debuggers. In addition, I want to explore ways in which debugging can be made more approachable.

## 🦾 Features

- [x] Breakpoints on functions, on lines in files and on addresses
- [x] Printing and setting variables, memory at addresses and registers
- [x] C syntax highlighting
- [x] Backtraces
- [x] Instruction, function and line level stepping
- [x] Filters to format command output

## 🚀 Roadmap

- [ ] Printing and modifying complex structures
- [ ] Syntax highlighting for complex structures
- [ ] Backtraces based on DWARF instead of frame pointers
- [ ] Inlined functions
- [ ] Loading external libraries
- [ ] Catching signals sent to the debugged program

## 💿️ Installation

Parts of the Spray frontend are written in Scheme and embedded into the application using [CHICKEN Scheme](https://www.call-cc.org/) which compiles Scheme to C. Currently, you need to have [CHICKEN installed](https://code.call-cc.org/#download) to build Spray. In the future it's possible that the generated C files are provided instead so that you only need a C compiler.

Spray depends on [libdwarf](https://github.com/davea42/libdwarf-code/releases)
so if you want to build Spray, you need to install libdwarf first. Then, to install Spray you clone this repository and run `make`. Note the you
have to [clone all the submodules](https://stackoverflow.com/a/4438292) too.

```sh
git clone --recurse-submodules https://github.com/thass0/spray.git
cd spray
make
```

The compiled binary is named `spray` and can be found in the `build` directory.

To use `spray` as a regular command you need to [add it to your `$PATH`](https://askubuntu.com/a/322773).

## 🏃‍♀️ Running Spray

> Ensure that the binary you want to debug has debug information enabled, i.e. it was compiled with the `-g` flag. Also, you should disable all compile-time optimizations to ensure the best output.

> Spray is only tested using Clang. The debug information generated by different compilers for the same piece of code varies. Thus, `clang` should be used to compile the programs you want to debug using Spray.

The first argument you pass to `spray` is the name of the binary that should be debugged (the debugee). All subsequent arguments are the arguments passed to the debugee.

For example

```sh
clang -g examples/free_uninit.c
spray a.out
```

starts a debugging session with the executable `a.out`.

## ⌨️ Commands

Spray's REPL offers the following commands to interact with a running program.

### Reading and writing values

<table>
    <tr>
        <td>Command</td>
        <td>Argument(s)</td>
        <td>Description</td>
    </tr>
    <tr>
        <td rowspan="3"><code>print</code>, <code>p</code></td>
        <td><code>&lt;variable&gt;</code></td>
        <td>Print the value of the runtime variable.</td>
    </tr>
    <tr>
        <td><code>&lt;register&gt;</code></td>
        <td>Print the value of the register.</td>
    </tr>
    <tr>
        <td><code>&lt;address&gt;</code></td>
        <td>Print the value of the program&#39;s memory at the address.</td>
    </tr>
    <tr>
        <td rowspan="3"><code>set</code>, <code>t</code></td>
        <td><code>&lt;variable&gt; &lt;value&gt;</code></td>
        <td>Set the value of the runtime variable.</td>
    </tr>
    <tr>
        <td><code>&lt;register&gt; &lt;value&gt;</code></td>
        <td>Set the value of the register.</td>
    </tr>
    <tr>
	<td><code>&lt;address&gt; &lt;value&gt;</code></td>
        <td>Set the value of the program&#39;s memory at the address.</td>
    </tr>
</table>

Register names are prefixed with a `%`, akin to the AT&T assembly syntax. This avoids name conflicts between register names and variable names. For example, to read the value of `rax`, use `print %rax`. You can find a table of all available register names in `src/registers.h`.

A `<value>` can be a hexadecimal or a decimal number. The default is base 10 and hexadecimal will only be chosen if the literal contains a character that's exclusive to base 16 (i.e. one of a - f). You can prefix the literal with `0x` to explicitly use hexadecimal in cases where decimal would work as well.

An `<address>` is always a hexadecimal number. The prefix `0x` is again optional.

### Breakpoints

<table>
    <tr>
        <td>Command</td>
        <td>Argument(s)</td>
        <td>Description</td>
    </tr>
    <tr>
        <td rowspan="3"><code>break</code>, <code>b</code></td>
        <td><code>&lt;function&gt;</code></td>
        <td>Set a breakpoint on the function.</td>
    </tr>
    <tr>
        <td><code>&lt;file&gt;:&lt;line&gt;</code></td>
        <td>Set a breakpoint on the line in the file.</td>
    </tr>
    <tr>
        <td><code>&lt;address&gt;</code></td>
        <td>Set a breakpoint on the address.</td>
    </tr>
    <tr>
        <td rowspan="3"><code>delete</code>, <code>d</code></td>
        <td><code>&lt;function&gt;</code></td>
        <td>Delete a breakpoint on the function.</td>
    </tr>
    <tr>
        <td><code>&lt;file&gt;:&lt;line&gt;</code></td>
        <td>Delete a breakpoint on the line in the file.</td>
    </tr>
    <tr>
        <td><code>&lt;address&gt;</code></td>
        <td>Delete a breakpoint on the address.</td>
    </tr>
    <tr>
        <td><code>continue</code>, <code>c</code></td>
        <td></td>
        <td>Continue execution until the next breakpoint.</td>
    </tr>
</table>

It's possible that the location passed to `break`, `delete`, `print`, or `set` is both a valid function name and a valid hexadecimal address. For example, `add` could refer to a function called `add` and the number `0xadd`. In such a case, the default is to interpret the location as a function name. Use the prefix `0x` to explicitly specify an address.

### Stepping

| Command          | Description                                         |
|------------------|-----------------------------------------------------|
| `next`, `n`      | Go to the next line. Don't step into functions.     |
| `step`, `s`      | Go to the next line. Step into functions.           |
| `leave`, `l`     | Step out of the current function.                   |
| `inst`, `i`      | Step to the next instruction.                       |
| `backtrace`, `a` | Print a backtrace starting at the current position. |

### Filters

The `print` and `set` commands can be followed by a filter, to change how output is displayed. For example, if you want to inspect the binary data in the rdx register, you can enter `print %rdx | bin`.

Filters are separated from the command by a pipe symbol: `<command> '|' <filter>`. Currently, only one filter can be used at a time.

The following table shows how different filters format the same 64-bit word with the value 103.


| Filter                | Output                                                                    |
|-----------------------|---------------------------------------------------------------------------|
| `dec` (*decimal*)     | `103`                                                                     |
| `hex` (*hexadecimal*) | `0x67`                                                                    |
| `addr` (*address*)    | `0x0000000000000067`                                                      |
| `bits`                | `00000000 00000000 00000000 00000000 00000000 00000000 00000000 01100111` |
| `bytes`               | `00 00 00 00 00 00 00 67`                                                 |
| `deref` `*`           | *Prints the value found at memory address `0x67`*                         |

Except for `deref`, all the above simply change the way the output is formatted. `deref`, abbreviated as `*`, interprets the value that would be printed as a memory address, and prints whatever is found it memory that address. Using `deref`, you can inspect that value that a pointer points to.

## 🛠️Contributing

All contributions are welcome. Before opening a pull request, please run
the test suite locally to verify that your changes don't break any other
features.

It's possible that some of the tests fail due to off-by-one errors when
making assertions about specific values found in the example binaries that
are used in the tests. Refer to [this issue](https://github.com/thass0/spray/issues/2)
for more details. You can ignore tests that fail for this reason only.

## 📖 References

- Sy Brand's blog series [Writing a Linux Debugger](https://blog.tartanllama.xyz/writing-a-linux-debugger-setup/) on writing a debugger in C++

- [The DWARF 5 standard](https://dwarfstd.org/dwarf5std.html)

- [libdwarf's documentation](https://www.prevanders.net/libdwarfdoc/index.html)

- Eli Bendersky's posts [How debuggers work](https://eli.thegreenplace.net/2011/01/23/how-debuggers-work-part-1)
