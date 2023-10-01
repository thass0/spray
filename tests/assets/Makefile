CC = clang
CFLAGS = -g

.PHONY = all clean

SIMPLE = simple.c
NESTED = nested_functions.c
MULTI_FILE = multi-file/file1.c multi-file/file2.c
EXTERN_VARIABLES = extern-variables/main.c extern-variables/second_file.c extern-variables/first_file.c extern-variables/third_file.c
INCLUDE_VARIABLE = include-variable/main.c
PRINT_ARGS = print_args.c
COMMENTED = commented.c
CUSTOM_TYPES = custom_types.c
VARIABLES = recurring_variables.c
POINTERS = pointers.c
TYPE_EXAMPLES = type_examples.c
TARGETS = 64bit-linux-simple.bin 32bit-linux-simple.bin nested-functions.bin multi-file.bin print-args.bin frame-pointer-nested-functions.bin no-frame-pointer-nested-functions.bin commented.bin custom-types.bin recurring-variables.bin pointers.bin extern-variables.bin include-variable.bin wrong-compiler.bin type-examples.o

all: $(TARGETS)

64bit-linux-simple.bin: $(SIMPLE)
	$(CC) $(CFLAGS) $< -o $@
32bit-linux-simple.bin: $(SIMPLE)
	$(CC) $(CFLAGS) -m32 $< -o $@
nested-functions.bin: $(NESTED)
	$(CC) $(CFLAGS) $< -o $@
recurring-variables.bin: $(VARIABLES)
	$(CC) $(CFLAGS) $< -o $@
frame-pointer-nested-functions.bin: CFLAGS += -fno-omit-frame-pointer
frame-pointer-nested-functions.bin: $(NESTED)
	$(CC) $(CFLAGS) $< -o $@
no-frame-pointer-nested-functions.bin: CFLAGS += -fomit-frame-pointer
no-frame-pointer-nested-functions.bin: $(NESTED)
	$(CC) $(CFLAGS) $< -o $@
multi-file.bin: $(MULTI_FILE)
	$(CC) $(CFLAGS) $(MULTI_FILE) -o $@
print-args.bin: $(PRINT_ARGS)
	$(CC) $(CFLAGS) $< -o $@
commented.bin: $(COMMENTED)
	$(CC) $(CFLAGS) $< -o $@
custom-types.bin: $(CUSTOM_TYPES)
	$(CC) $(CFLAGS) $< -o $@
pointers.bin: $(POINTERS)
	$(CC) $(CFLAGS) $< -o $@
extern-variables.bin: $(EXTERN_VARIABLES)
	$(CC) $(CFLAGS) $(EXTERN_VARIABLES) -o $@
include-variable.bin: $(INCLUDE_VARIABLE)
	$(CC) $(CFLAGS) $< -o $@
wrong-compiler.bin: $(SIMPLE)
	gcc $(CFLAGS) $< -o $@
type-examples.o: $(TYPE_EXAMPLES)
	$(CC) $(CFLAGS) $< -c -o $@


clean:
	$(RM) $(TARGETS)

