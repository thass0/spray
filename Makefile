CC = clang
CFLAGS = -fsanitize=address -g -Werror -Wall -Wextra -pedantic-errors -std=gnu11
LDFLAGS = $$(pkg-config --cflags --libs libelf++ libdwarf++) -g -lstdc++

CXX = clang++
# Turning off `-Wval-extension` and `-Wunused-parameter`
# is necessary because libelfin raises them.
CXXFLAGS = -fsanitize=address -g -Wall -Wextra -pedantic-errors -std=c++0x -Wno-vla-extension -Wno-unused-parameter

export PKG_CONFIG_PATH = libelfin/elf:libelfin/dwarf
CPPFLAGS =

BUILD_DIR = build
SOURCE_DIR = src
SOURCES = $(wildcard $(SOURCE_DIR)/*.c)
SOURCES += $(wildcard $(SOURCE_DIR)/*.cc)
OBJECTS := $(patsubst $(SOURCE_DIR)/%.cc, $(BUILD_DIR)/%.o, $(SOURCES))
OBJECTS := $(patsubst $(SOURCE_DIR)/%.c, $(BUILD_DIR)/%.o, $(OBJECTS))
BINARY = $(BUILD_DIR)/spray
DEPS = $(OBJECTS:%.o=%.d)

README = README.md

.PHONY = all clean run test

# === SPRAY ===

all: $(BINARY) $(README)
	@echo Build successful 👍️

run: all
	./$(BINARY) $(args)

$(BINARY): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) -o $(BINARY)

-include $(DEPS)

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -I$(SOURCE_DIR) -c $< -o $@

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.cc | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -I$(SOURCE_DIR) -c $< -o $@

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
TEST_OBJECTS = $(patsubst $(TEST_SOURCE_DIR)/%.c, $(TEST_BUILD_DIR)/%.o, $(TEST_SOURCES))
TEST_OBJECTS += $(filter-out $(BUILD_DIR)/spray.o, $(OBJECTS))
TEST_DEPS = $(TEST_OBJECTS:%.o=%.d)
TEST_BINARY = $(TEST_BUILD_DIR)/test

test: $(TEST_BINARY)
	./$(TEST_BINARY) $(args)

$(TEST_BINARY): $(TEST_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -I$(SOURCE_DIR) -I$(TEST_SOURCE_DIR) $(TEST_OBJECTS) -o $(TEST_BINARY)

-include $(TEST_DEPS)

# Only run this until freeing libelfin internal
# allocation using the `new` operator are fixed.
export ASAN_OPTIONS=detect_leaks=0

$(TEST_BUILD_DIR)/%.o: $(TEST_SOURCE_DIR)/%.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -I$(TEST_SOURCE_DIR) -c $< -o $@

$(TEST_BUILD_DIR):
	mkdir $(TEST_BUILD_DIR)

clean:
	$(RM) -r $(BUILD_DIR) $(DEPS) $(TEST_BUILD_DIR)

