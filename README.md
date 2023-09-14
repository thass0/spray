<p align="center">
	<h2 align="center">🐛🐛🐛 Spray 🐛🐛🐛</h3>
	<p align="center">Debugging as fun.</p>
 <p align="center">
  <a href="https://github.com/d4ckard/spray/#%EF%B8%8F-installation">Get started</a> -
  <a href="https://github.com/d4ckard/spray/issues">Issues</a> -
  <a href="https://github.com/d4ckard/spray/issues/new">Bug report</a>
 </p>
</p>

![Spray debugging itself](.assets/using_spray.png) 
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; *Spray debugging itself*

Spray is a debugger targeted at C code. It's aimed at a core set of debugging tasks, trying to make them as easy as possible. In general, all of Spray should comfortably fit into your head. This reduces the mental overhead of jumping right into your code, all the time.

It also means that Spray doesn't try to fulfill *all* needs. It's inspired by the typical
local debugging workflow that I find myself doing the most of.

## 🦾 Features

- [x] Breakpoints on functions, on lines in files and on addresses
- [x] Reading an writing memory
- [x] C syntax highlighting
- [x] Backtraces
- [x] Instruction, function and line level stepping

## 🚀 Roadmap 

- [ ] [redis-cli](https://redis.io/docs/ui/cli/)-like command auto-completion to improve command discoverability
- [ ] Interacting with variables (printing, setting, etc.)
- [ ] Backtraces based on DWARF info
- [ ] Command modularity
- [ ] Improved separation of the debugger and the debuggee in the command line.

## 💿️ Installation

Parts of the Spray frontend are written in Scheme and embedded into the application
using [CHICKEN Scheme](https://www.call-cc.org/) which compiles Scheme to C. Currently,
you need to have [CHICKEN installed](https://code.call-cc.org/#download) to build Spray.
In the future it's possible that the generated C files are provided instead so that you
only need a C compiler.

Spray depends on [libdwarf](https://github.com/davea42/libdwarf-code/releases)
so if you want to build Spray, you need to install libdwarf first.
Then, to install Spray you clone this repository and run `make`. Note the you
have to [clone all the submodules](https://stackoverflow.com/a/4438292) too.

```sh
git clone --recurse-submodules https://github.com/d4ckard/spray.git
cd spray
make
```

The compiled binary is named `spray` and can be found in the `build` directory.

To use `spray` as a regular command you need to [add it to your `$PATH`](https://askubuntu.com/a/322773).

## 🏃‍♀️ Running Spray

Ensure that the binary you want to debug has debug information enabled, i.e. it was compiled with the `-g` flag. Also, you should disable all compile-time optimizations to ensure the best output.

The first argument you pass to `spray` is the name of the binary that should be debugged (the debugee). All subsequent arguments are the arguments passed to the debugee.

For example

```sh
spray tests/assets/print-args.bin Hello World
```

starts a debugging session with the executable `print-args.bin`
this executable the additional arguments `Hello` and `World`
(note that you need to run `make` in `tests/assets` to build
`print-args.bin`).

## ⌨️ Commands

Available commands are:

- `(print | p) <name>`: print the current value of the variable called `<name>`.

- `(continue | c)`: continue execution until next breakpoint is hit. 

- `(break | b) (<function> | <address> | <file>:<line>)`: set a breakpoint.

- `(delete | d) (<function> | <address> | <file>:<line>)`: delete a breakpoint.

- `(register | r) <name> (read | rd)`: read the value in register `<name>`.

- `(register | r) <name> (write | wr) <value>`: write `<value>` to register `<name>`.

- `(register | r) (print | dump)`: read the values of all registers.

- `(memory | m) <address> (read | rd)`: read the value in memory at `<address>`.

- `(memory | m) <address> (write | wr) <value>`: write `<value>` to memory at `<address>`.

- `(inst | i)`: Single step to the next instruction.

- `(leave | l)`: Step out of the current function.

- `(step | s)`: Single step to the next line. Steps into functions.

- `(next | n)`: Go to the next line. Doesn't step into functions.

Where `<address>` and `<value>` are validated with `strtol(..., 16)`. 
All valid register names can be found in the `reg_descriptors`
table in `src/registers.h`.

Also note that if the location provided to `break` or `delete` is
both a valid function name and a valid hexadecimal address (e.g. `add`
can be read as the function `add` or the number `0xadd`), this location
is always interpreted as a function name. Use the prefix `0x` to specify
addresses explicitly.

Run `spray --help` to see all parameters that are available on the command line.

## 🛠️Contributing

All contributions are welcome. Before opening a pull request, please run
the test suite locally to verify that your changes don't break any other
features.

If any of the tests fail because they are in conflict with the
change you made, make sure to either improve your changes so that they
pass the tests or to change the test in a way that again verifies correct
behavior.

It's possible that some of the tests fail due to off-by-one errors when
making assertions about specific values found in the example binaries that
are used in the tests. Refer to [this issue](https://github.com/d4ckard/spray/issues/2)
for more details. You can ignore tests that fail for this reason only.

