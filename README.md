
# 🐛🐛🐛 Spray: a x86_64 linux debugger 🐛🐛🐛

## Installation

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

## Commands

Available commands are:

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

## Running the debugger

Ensure that the binary you want to debug has debug information enabled,
i.e. it was compiled with the `-g` flag.

The first argument you pass to `spray` is the name of the binary that
should be debugged (the debugee). All subsequent arguments are the
arguments passed to the debugee.

For example

```sh
spray tests/assets/print-args.bin Hello World
```

starts a debugging session with the executable `print-args.bin`
this executable the additional arguments `Hello` and `World`
(note that you need to run `make` in `tests/assets` to build
`print-args.bin`).


