CC = clang
CFLAGS = -fsanitize=address -g -Werror -Wall -Wextra -pedantic-errors -std=gnu11
CPPFLAGS = -MMD -I$(SOURCE_DIR) -I$(DEP)/linenoise -I$(DEP)/hashmap.c
LDFLAGS = -ldwarf -lchicken

BUILD_DIR = build
SOURCE_DIR = src
DEP = dependencies
SOURCES = $(wildcard $(SOURCE_DIR)/*.c)
OBJECTS = $(patsubst $(SOURCE_DIR)/%.c, $(BUILD_DIR)/%.o, $(SOURCES))
OBJECTS += $(BUILD_DIR)/hashmap.o $(BUILD_DIR)/linenoise.o $(BUILD_DIR)/print-colored.o
BINARY = $(BUILD_DIR)/spray
DEPS = $(OBJECTS:%.o=%.d)

README = README.md

.PHONY = all bin clean run test unit integration assets

# === SPRAY ===

all: $(BINARY) assets
	@echo Build successful 👍️

run: all
	./$(BINARY) $(args)

$(BINARY): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) -o $(BINARY)

-include $(DEPS)

# Wow, seems like CHICKEN is quite strict ...
$(BUILD_DIR)/source_files.o: CFLAGS += -Wno-unused-parameter -Wno-strict-prototypes -Wno-pedantic -Wno-unused-but-set-variable -Wno-gnu-statement-expression-from-macro-expansion -Wno-unused-variable
$(BUILD_DIR)/source_files.o: CPPFLAGS += -I/usr/include/chicken
$(BUILD_DIR)/source_files.o: $(SOURCE_DIR)/source_files.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/print-colored.o: CPPFLAGS += -I/usr/include/chicken
$(BUILD_DIR)/print-colored.o: $(SOURCE_DIR)/print-colored.scm | $(BUILD_DIR)
	csc -c -embedded -output-file $(BUILD_DIR)/print-colored.o $<

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/hashmap.o: $(DEP)/hashmap.c/hashmap.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/linenoise.o: CFLAGS += -Wno-gnu-zero-variadic-macro-arguments
$(BUILD_DIR)/linenoise.o: $(DEP)/linenoise/linenoise.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@


$(BUILD_DIR):
	mkdir $(BUILD_DIR)

# Clang's JSON compilation database.
compile_commands.json:
ifeq (, $(shell which bear))
	$(error "Bear is required to generate `compile_commands.json`. You can get it here: https://github.com/rizsotto/Bear.git.")
else
	make clean
	bear -- make all
endif


# === README ===

$(README): src/spray.c
	@sed -n '/\/\*/,/\*\// { s/\/\*//; s/\*\///; s/^\s*//; p; }' $< > $@
	@echo Rebuilt $(README)


# === TESTS ===

TEST_SOURCE_DIR = tests
TEST_BUILD_DIR = tests/build
TEST_SOURCES = $(wildcard $(TEST_SOURCE_DIR)/*.c)
TEST_OBJECTS = $(filter-out $(BUILD_DIR)/spray.o, $(OBJECTS))
TEST_OBJECTS += $(patsubst $(TEST_SOURCE_DIR)/%.c, $(TEST_BUILD_DIR)/%.o, $(TEST_SOURCES))
TEST_OBJECTS += $(TEST_BUILD_DIR)/munit.o
TEST_DEPS = $(TEST_OBJECTS:%.o=%.d)
TEST_BINARY = $(TEST_BUILD_DIR)/test
TEST_SCHEME_SCRIPT = tests/colorize.scm

test: unit integration

unit: CPPFLAGS += -I$(TEST_SOURCE_DIR) -I$(DEP)/munit
unit: $(TEST_BINARY) $(BINARY) assets
	./$(TEST_BINARY) $(args)
	$(TEST_SCHEME_SCRIPT)

integration: $(BINARY) assets
	pytest

$(TEST_BINARY): $(TEST_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(TEST_OBJECTS) -o $(TEST_BINARY)

-include $(TEST_DEPS)

$(TEST_BUILD_DIR)/%.o: $(TEST_SOURCE_DIR)/%.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(TEST_BUILD_DIR)/munit.o: $(DEP)/munit/munit.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(TEST_BUILD_DIR):
	mkdir $(TEST_BUILD_DIR)

assets:
	$(MAKE) -C tests/assets all

clean:
	$(RM) -r $(BUILD_DIR) $(TEST_BUILD_DIR) compile_commands.json
	$(MAKE) -C tests/assets clean

