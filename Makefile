CC = clang
CFLAGS = -fsanitize=address -g -Werror -Wall -Wextra -pedantic-errors -std=gnu11 -I$(SOURCE_DIR) -I$(DEP)/linenoise -I$(DEP)/munit
CPPFLAGS = -MMD
LDFLAGS = -ldwarf

BUILD_DIR = build
SOURCE_DIR = src
DEP = dependencies
SOURCES = $(wildcard $(SOURCE_DIR)/*.c)
OBJECTS = $(patsubst $(SOURCE_DIR)/%.c, $(BUILD_DIR)/%.o, $(SOURCES))
OBJECTS += $(BUILD_DIR)/hashmap.o $(BUILD_DIR)/linenoise.o
BINARY = $(BUILD_DIR)/spray
DEPS = $(OBJECTS:%.o=%.d)

README = README.md

.PHONY = all clean run test debugees

# === SPRAY ===

all: $(BINARY) $(README)
	@echo Build successful 👍️

run: all
	./$(BINARY) $(args)

$(BINARY): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) -o $(BINARY)

-include $(DEPS)

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/hashmap.o: $(DEP)/hashmap.c/hashmap.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/linenoise.o: $(DEP)/linenoise/linenoise.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@


$(BUILD_DIR):
	mkdir $(BUILD_DIR)

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

test: CFLAGS += -I$(TEST_SOURCE_DIR)
test: $(TEST_BINARY) $(BINARY) | debugees
	./$(TEST_BINARY) $(args)
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

debugees:
	$(MAKE) -C tests/assets all

clean:
	$(RM) -r $(BUILD_DIR) $(TEST_BUILD_DIR)

