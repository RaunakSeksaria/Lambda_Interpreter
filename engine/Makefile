# Makefile for the C++ expression-evaluation engine.
#
# No external dependencies (no cmake, no fetched libraries): `make` builds
# everything with g++/clang++. Two dispatch variants of the VM are built into
# separate binaries so the benchmark can compare switch vs computed-goto.
#
#   make            # build lambda_eval, difftest, bench (switch + goto)
#   make test       # regenerate golden from the Racket oracle, run difftest
#   make alloc-audit # assert run() performs no heap allocation
#   make check      # all correctness gates: test + alloc-audit + lint
#   make bench      # run both benchmark binaries
#   make clean

CXX      ?= g++
CXXSTD   ?= -std=c++20
OPT      ?= -O3 -march=native -DNDEBUG
# -Wnull-dereference is intentionally omitted: it is optimization-level
# dependent and false-positives inside libstdc++ headers at -O1 (the sanitize
# build). Real null derefs are caught by the ASan/UBSan `sanitize` target.
WARN     ?= -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
            -Wold-style-cast -Wcast-qual -Wdouble-promotion
INCLUDES := -Iinclude -Isrc

# Warnings are errors by default (STRICT=1). Override with `make STRICT=0`.
STRICT   ?= 1
ifeq ($(STRICT),1)
WARN     += -Werror
endif

CXXFLAGS := $(CXXSTD) $(OPT) $(WARN) $(INCLUDES)

BUILD := build

# Frontend TUs shared by every binary (parse + compile).
FRONT_SRCS := src/lexer.cpp src/parser.cpp src/compiler.cpp
FRONT_OBJS := $(BUILD)/lexer.o $(BUILD)/parser.o $(BUILD)/compiler.o

.PHONY: all test alloc-audit check bench sanitize valgrind lint format clean
# Default build: the usable binaries + the differential-test driver. Benchmarks
# are built on demand via `make bench` (perf-sensitive; not part of CI).
all: $(BUILD)/lambda_eval $(BUILD)/difftest

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

$(BUILD)/alloc_audit: tests/alloc_audit.cpp $(FRONT_OBJS) $(BUILD)/vm_switch.o | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/bench_switch: bench/bench_vm.cpp $(FRONT_OBJS) $(BUILD)/vm_switch.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -DENGINE_DISPATCH_NAME='"switch"' $^ -o $@

$(BUILD)/bench_goto: bench/bench_vm.cpp $(FRONT_OBJS) $(BUILD)/vm_goto.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -DENGINE_DISPATCH_NAME='"computed-goto"' $^ -o $@

test: $(BUILD)/difftest
	racket tests/gen_golden.rkt tests/corpus.txt > tests/golden.txt
	$(BUILD)/difftest tests/corpus.txt tests/golden.txt

# Interposes global operator new/delete and asserts run() allocates nothing on
# every corpus case — the gate for vm.hpp's zero-allocation claim, which no
# compiler check enforces and which difftest cannot see.
alloc-audit: $(BUILD)/alloc_audit
	$(BUILD)/alloc_audit tests/corpus.txt

# Every correctness gate, in increasing order of cost. Kept separate from
# `test` because `test` shells out to racket for the golden file, while
# alloc-audit needs only a compiler — a racket-less checkout can still run it.
check: test alloc-audit lint

bench: $(BUILD)/bench_switch $(BUILD)/bench_goto
	@$(BUILD)/bench_switch
	@echo ""; $(BUILD)/bench_goto
	@echo ""; echo "== racket oracle (reference tree-walker) =="; \
	    racket bench/bench_oracle.rkt

# --- quality gates ---

# Address + UndefinedBehavior sanitizers over the differential test. On a box
# whose UBSan runtime is missing, run `make sanitize SANITIZERS=address`.
SANITIZERS ?= address,undefined
sanitize: | $(BUILD)
	$(CXX) $(CXXSTD) -O1 -g -fsanitize=$(SANITIZERS) -fno-omit-frame-pointer \
	    $(WARN) $(INCLUDES) tests/difftest.cpp $(FRONT_SRCS) src/vm.cpp \
	    -o $(BUILD)/difftest_san
	racket tests/gen_golden.rkt tests/corpus.txt > tests/golden.txt
	$(BUILD)/difftest_san tests/corpus.txt tests/golden.txt

# valgrind memcheck over the differential test (no sanitizers).
valgrind: $(BUILD)/difftest
	racket tests/gen_golden.rkt tests/corpus.txt > tests/golden.txt
	valgrind -q --leak-check=full --error-exitcode=1 \
	    $(BUILD)/difftest tests/corpus.txt tests/golden.txt

# Static analysis: runs clang-tidy / cppcheck when installed, else skips.
# clang-tidy is invoked per file (one TU at a time) to bound peak memory.
lint:
	@if command -v clang-tidy >/dev/null 2>&1; then \
	    echo "== clang-tidy =="; \
	    for f in src/*.cpp tests/difftest.cpp tests/alloc_audit.cpp; do \
	        echo "  tidy $$f"; \
	        clang-tidy "$$f" -- $(CXXSTD) $(INCLUDES) || exit 1; \
	    done; \
	 else echo "clang-tidy not installed; skipping"; fi
# cppcheck cannot parse the computed-goto extension (&&label / goto *tbl), so
# -UENGINE_COMPUTED_GOTO points it at the switch config, whose handler bodies
# are identical — real logic is still analyzed.
	@if command -v cppcheck >/dev/null 2>&1; then \
	    echo "== cppcheck =="; \
	    cppcheck --std=c++20 --enable=warning,performance,portability \
	        --inline-suppr --error-exitcode=1 -q -UENGINE_COMPUTED_GOTO \
	        $(INCLUDES) src tests/difftest.cpp tests/alloc_audit.cpp; \
	 else echo "cppcheck not installed; skipping"; fi

# Apply the repo style in place (clang-format), when installed.
format:
	@if command -v clang-format >/dev/null 2>&1; then \
	    clang-format -i include/engine/*.hpp src/*.hpp src/*.cpp \
	        tests/difftest.cpp tests/alloc_audit.cpp bench/*.cpp 2>/dev/null; echo "formatted"; \
	 else echo "clang-format not installed; skipping"; fi

clean:
	rm -rf $(BUILD)
