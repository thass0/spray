
# 🐛🐛🐛 Spray: a x86_64 linux debugger 🐛🐛🐛

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


