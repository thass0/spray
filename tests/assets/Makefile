CC = clang
CFLAGS = -g

.PHONY = all clean

DEBUG_ME = debug_me.c
NESTED = mul_and_add.c
MULTI_FILE = file1.c file2.c
PRINT_ARGS = print_args.c
TARGETS = linux_x86_bin linux_32_bin nested_functions_bin multi_file_bin print_args_bin

all: $(TARGETS)

linux_x86_bin: $(DEBUG_ME)
	$(CC) $(CFLAGS) $< -o $@
linux_32_bin: $(DEBUG_ME)
	$(CC) $(CFLAGS) -m32 $< -o $@
nested_functions_bin: $(NESTED)
	$(CC) $(CFLAGS) $< -o $@
multi_file_bin: $(MULTI_FILE)
	$(CC) $(CFLAGS) $(MULTI_FILE) -o $@
print_args_bin: $(PRINT_ARGS)
	$(CC) $(CFLAGS) $< -o $@

clean:
	$(RM) $(TARGETS)

