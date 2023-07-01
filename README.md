
# 🐛🐛🐛 Spray: a x86_64 linux debugger 🐛🐛🐛

Available commands are:

`(continue | c)`: continue execution until next breakpoint is hit. 

`(break | b) <address>`: set a breakpoint at <address>.

`(register | r) <name> (read | rd)`: read the value in register <name>.

`(register | r) <name> (write | wr) <value>`: write <value> to register <name>.

`(register | r) (print | dump)`: read the values of all registers.

`(memory | m) <address> (read | rd)`: read the value in memory at <address>.

`(memory | m) <address> (write | wr) <value>`: write <value> to memory at <address>.

`(istep | i)`: single step on instruction level. Doesn't trigger breakpoints we step on.

Where <address> and <value> are validated with `strtol(..., 16)`. 
All valid register names can be found in the `reg_descriptors`
table in `src/registers.h`.


