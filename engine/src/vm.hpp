// vm.hpp — the stack machine that runs a compiled Program.
//
// All buffers (operand stack, local frame, store) are sized from the Program
// and allocated once in the constructor, then reused across every run(). A run
// touches no heap: it is the "per-tick" hot path.
//
// Two dispatch strategies are built from this same source (see vm.cpp):
//   * default: a `switch` over the opcode;
//   * -DENGINE_COMPUTED_GOTO: threaded / computed-goto dispatch.
// They are compiled into separate objects so the benchmark can compare the
// branch-misprediction cost of each.
#pragma once

#include <cstddef>
#include <vector>

#include "engine/bytecode.hpp"
#include "engine/value.hpp"

namespace engine {

class VM {
 public:
  explicit VM(const Program& p)
      : prog_(p),
        stack_(p.max_stack ? p.max_stack : 1),
        locals_(p.n_locals),
        store_(p.max_store) {}

  // Evaluate the program against `inputs` (indexed as Program::input_names).
  // Returns the single result value. Resets the store; never allocates.
  Value run(const Value* inputs);

  const Program& program() const { return prog_; }

 private:
  const Program& prog_;
  std::vector<Value> stack_;
  std::vector<Value> locals_;
  std::vector<Value> store_;
};

}  // namespace engine
