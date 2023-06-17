CC = gcc
CFLAGS = -g -Werror -Wall -Wextra -pedantic-errors -std=gnu11
LDFLAGS =
CPPFLAGS =

BUILD_DIR = build
SOURCE_DIR = src
SOURCES = $(wildcard $(SOURCE_DIR)/*.c)
OBJECTS = $(patsubst $(SOURCE_DIR)/%.c, $(BUILD_DIR)/%.o, $(SOURCES))
BINARY = $(BUILD_DIR)/spray
DEPS = $(OBJECTS:%.o=%.d)

.PHONY = all clean run

all: $(BINARY)
	@echo --- Build done ---

run: all
	./$(BINARY) $(args)

$(BINARY): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) -o $(BINARY)

-include $(DEPS)

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -I$(SOURCE_DIR) -c $< -o $@

$(BUILD_DIR):
	mkdir $(BUILD_DIR)

clean:
	$(RM) -r $(BUILD_DIR) $(DEPS) $(TEST_BUILD_DIR)

