# Build the test binaries, using DWARF4 until
# I've found a library with good DWARF5 support.

CC = clang
CFLAGS = -gdwarf-4

.PHONY = all clean

SOURCE = debug_me.c
TARGETS = linux_x86_bin linux_32_bin

all: $(TARGETS)

linux_x86_bin: $(SOURCE)
	$(CC) $(CFLAGS) $< -o $@
linux_32_bin: $(SOURCE)
	$(CC) $(CFLAGS) -m32 $< -o $@

clean:
	$(RM) $(TARGETS)

