CXX ?= clang++
CXXFLAGS ?= -std=c++23 -Wall -Wextra -g
CPPFLAGS ?= -Iinclude

SRC := src/main.cpp \
	src/core/errors.cpp \
	src/core/pipe.cpp \
	src/raw/command.cpp \
	src/raw/runner.cpp \
	src/config/parser.cpp \
	src/config/cli.cpp \
	src/proc/spawn.cpp \
	src/proc/signals.cpp \
	src/proc/orchestrator.cpp

OBJ := $(SRC:.cpp=.o)
DEP := $(OBJ:.o=.d)

BIN := ronq

.PHONY: all build clean

all: build

build: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f $(BIN) $(OBJ) $(DEP)

-include $(DEP)
