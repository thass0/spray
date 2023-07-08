CC = clang
CFLAGS = -g

.PHONY = all clean

DEBUG_ME = debug_me.c
NESTED = mul_and_add.c
TARGETS = linux_x86_bin linux_32_bin nested_functions_bin

all: $(TARGETS)

linux_x86_bin: $(DEBUG_ME)
	$(CC) $(CFLAGS) $< -o $@
linux_32_bin: $(DEBUG_ME)
	$(CC) $(CFLAGS) -m32 $< -o $@
nested_functions_bin: $(NESTED)
	$(CC) $(CFLAGS) $< -o $@

clean:
	$(RM) $(TARGETS)

