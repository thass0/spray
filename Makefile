CC = clang
CFLAGS = -fsanitize=address -g -Werror -Wall -Wextra -pedantic-errors -Wno-gnu-designator -std=gnu11
CPPFLAGS = -MMD -I$(SOURCE_DIR) -I$(DEP)/linenoise -I$(DEP)/hashmap.c
LDFLAGS = -ldwarf -lchicken -lzstd -lz

BUILD_DIR = build
SOURCE_DIR = src
DEP = dependencies
SOURCES = $(wildcard $(SOURCE_DIR)/*.c)
OBJECTS = $(patsubst $(SOURCE_DIR)/%.c, $(BUILD_DIR)/%.o, $(SOURCES))
OBJECTS += $(BUILD_DIR)/hashmap.o $(BUILD_DIR)/linenoise.o $(BUILD_DIR)/print-source.o $(BUILD_DIR)/tokenize.o $(BUILD_DIR)/c-syntax.o
BINARY = $(BUILD_DIR)/spray
DEPS = $(OBJECTS:%.o=%.d)

.PHONY = all bin clean run test unit integration assets install docker

# === SPRAY ===

all: $(BINARY) assets
	@echo Build successful 👍️

run: all
	./$(BINARY) $(args)

install: $(BINARY)
	cp $(BINARY) $$HOME/.local/bin/

docker: $(BINARY)
	docker create -i ubuntu
	docker cp $(BINARY) `docker ps -q -l`:/opt/spray
	docker start `docker ps -q -l`
	docker exec -i `docker ps -q -l` bash

$(BINARY): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(BINARY) $(LDFLAGS)

-include $(DEPS)

# Wow, seems like CHICKEN is quite strict ...
$(BUILD_DIR)/print_source.o: CFLAGS += -Wno-unused-parameter -Wno-strict-prototypes -Wno-pedantic -Wno-unused-but-set-variable -Wno-unused-variable
$(BUILD_DIR)/print_source.o: CPPFLAGS += -I/usr/include/chicken
$(BUILD_DIR)/print_source.o: $(SOURCE_DIR)/print_source.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/print-source.o: $(SOURCE_DIR)/print-source.scm $(BUILD_DIR)/tokenize.o | $(BUILD_DIR)
	csc -uses tokenizer -c -embedded $(SOURCE_DIR)/print-source.scm -o $@

$(BUILD_DIR)/tokenize.o: $(SOURCE_DIR)/tokenize.scm $(BUILD_DIR)/c-syntax.o | $(BUILD_DIR)
	csc -uses c-syntax -unit tokenizer -c -J $(SOURCE_DIR)/tokenize.scm  -o $@

$(BUILD_DIR)/c-syntax.o: $(SOURCE_DIR)/c-syntax.scm | $(BUILD_DIR)
	csc -unit c-syntax -c -J $(SOURCE_DIR)/c-syntax.scm  -o $@

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


# === TESTS ===

TEST_SOURCE_DIR = tests
TEST_BUILD_DIR = tests/build
TEST_SOURCES = $(wildcard $(TEST_SOURCE_DIR)/*.c)
TEST_OBJECTS = $(filter-out $(BUILD_DIR)/spray.o, $(OBJECTS))
TEST_OBJECTS += $(patsubst $(TEST_SOURCE_DIR)/%.c, $(TEST_BUILD_DIR)/%.o, $(TEST_SOURCES))
TEST_OBJECTS += $(TEST_BUILD_DIR)/munit.o
TEST_DEPS = $(TEST_OBJECTS:%.o=%.d)
TEST_BINARY = $(TEST_BUILD_DIR)/test

# Run all tests.
test: unit integration

# Run C and Scheme unit tests.
unit: cunit schemeunit

cunit: CPPFLAGS += -I$(TEST_SOURCE_DIR) -I$(DEP)/munit
cunit: $(TEST_BINARY) assets
	./$(TEST_BINARY) $(args)

schemeunit: assets
	csi -s tests/tokenize.scm
	csi -s tests/c-types.scm

# Run integration tests.
integration: $(BINARY) assets
	python -m pytest

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
	$(RM) *.import.scm
	$(RM) -r $(BUILD_DIR) $(TEST_BUILD_DIR) compile_commands.json
	$(MAKE) -C tests/assets clean

