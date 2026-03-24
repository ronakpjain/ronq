CXX ?= clang++
CXXFLAGS ?= -std=c++23 -Wall -Wextra -g
CPPFLAGS ?= -Iinclude

# Detect clang and add libc++ flag for compatibility on macOS
UNAME_CC := $(shell $(CXX) --version 2>/dev/null | tr '[:upper:]' '[:lower:]')
ifeq ($(findstring clang,$(UNAME_CC)),clang)
	CXXFLAGS += -stdlib=libc++
endif

SRC := main.cpp \
	src/core/errors.cpp \
	src/core/pipe.cpp \
	src/raw/command.cpp \
	src/raw/runner.cpp \
	src/config/parser.cpp \
	src/config/cli.cpp
BIN := ronq

.PHONY: all build clean run

all: build

build: $(BIN)

$(BIN): $(SRC)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(BIN) *.o

# Usage: make run ARGS="<cmd1>" "<cmd2>"
run: $(BIN)
	@echo "Running: ./$(BIN) $$ARGS"
	./$(BIN) $(ARGS)
