// bytecode.hpp — a compiled program, built once and then executed many times.
//
// Everything the hot loop needs is sized here at compile time:
//   - `code`       the flat instruction stream
//   - `consts`     literal pool (PushConst indexes into this)
//   - `input_names`/`n_inputs`  the market fields the program reads
//   - `n_locals`   frame size for let/let* bindings
//   - `max_stack`  operand-stack depth, so the VM preallocates exactly once
//   - `max_store`  number of `ref` allocations, so the store preallocates once
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/opcode.hpp"
#include "engine/value.hpp"

namespace engine {

struct Program {
  std::vector<Instr> code;
  std::vector<Value> consts;
  std::vector<std::string> input_names;  // index i -> name of input i
  std::uint32_t n_inputs = 0;
  std::uint32_t n_locals = 0;
  std::uint32_t max_stack = 0;
  std::uint32_t max_store = 0;
};

}  // namespace engine
