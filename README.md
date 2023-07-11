
# 🐛🐛🐛 Spray: a x86_64 linux debugger 🐛🐛🐛

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
i.e. was compiled with the `-g` flag.

The first argument you pass to `spray` is the name of the binary that
should be debugged (the debugee). All subsequent arguments are the
arguments passed to the debugee.

For example

```sh
spray tests/assets/print_args_bin Hello World
```

will debug the binary `print_args_bin` and pass this binary the
additional arguments `Hello` and `World`.


