# Makefile for the C++ expression-evaluation engine.
#
# No external dependencies (no cmake, no fetched libraries): `make` builds
# everything with g++/clang++. Two dispatch variants of the VM are built into
# separate binaries so the benchmark can compare switch vs computed-goto.
#
#   make            # build lambda_eval, difftest, bench (switch + goto)
#   make test       # regenerate golden from the Racket oracle, run difftest
#   make bench      # run both benchmark binaries
#   make clean

CXX      ?= g++
CXXSTD   ?= -std=c++20
OPT      ?= -O3 -march=native -DNDEBUG
WARN     ?= -Wall -Wextra
INCLUDES := -Iinclude -Isrc
CXXFLAGS := $(CXXSTD) $(OPT) $(WARN) $(INCLUDES)

BUILD := build

# Frontend TUs shared by every binary (parse + compile).
FRONT_SRcs := src/lexer.cpp src/parser.cpp src/compiler.cpp
FRONT_OBJS := $(BUILD)/lexer.o $(BUILD)/parser.o $(BUILD)/compiler.o

.PHONY: all test bench clean
all: $(BUILD)/lambda_eval $(BUILD)/difftest $(BUILD)/bench_switch $(BUILD)/bench_goto

$(BUILD):
	mkdir -p $(BUILD)

# --- frontend objects ---
$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- VM objects: two dispatch strategies from the same source ---
$(BUILD)/vm_switch.o: src/vm.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/vm_goto.o: src/vm.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -DENGINE_COMPUTED_GOTO -c $< -o $@

# --- binaries ---
$(BUILD)/lambda_eval: src/main.cpp $(FRONT_OBJS) $(BUILD)/vm_goto.o | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/difftest: tests/difftest.cpp $(FRONT_OBJS) $(BUILD)/vm_switch.o | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/bench_switch: bench/bench_vm.cpp $(FRONT_OBJS) $(BUILD)/vm_switch.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -DENGINE_DISPATCH_NAME='"switch"' $^ -o $@

$(BUILD)/bench_goto: bench/bench_vm.cpp $(FRONT_OBJS) $(BUILD)/vm_goto.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -DENGINE_DISPATCH_NAME='"computed-goto"' $^ -o $@

test: $(BUILD)/difftest
	racket tests/gen_golden.rkt tests/corpus.txt > tests/golden.txt
	$(BUILD)/difftest tests/corpus.txt tests/golden.txt

bench: $(BUILD)/bench_switch $(BUILD)/bench_goto
	@echo "== switch dispatch =="; $(BUILD)/bench_switch
	@echo "== computed-goto dispatch =="; $(BUILD)/bench_goto

clean:
	rm -rf $(BUILD)
