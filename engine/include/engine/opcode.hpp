// opcode.hpp — the instruction set of the stack VM.
//
// The compiler lowers the AST to a flat array of Instr. All variable names are
// resolved at compile time to integer indices (input slot or local slot), so
// the hot loop never does a string compare — the single biggest win over the
// assoc-list environment in interpreter.rkt.
#pragma once

#include <cstdint>

#include "engine/value.hpp"

namespace engine {

enum class Op : std::uint8_t {
  // --- stack / variables ---
  PushConst,   // arg = const index -> push consts[arg]
  LoadInput,   // arg = input index -> push inputs[arg]
  LoadLocal,   // arg = local slot  -> push locals[arg]
  StoreLocal,  // arg = local slot  -> locals[arg] = pop() (no push)
  Pop,         // discard top of stack (used by `seq`)

  // --- arithmetic (Int, Int) -> Int ---
  Add, Sub, Mul, Div,   // Div = integer quotient, matching interpreter.rkt

  // --- comparison (Int, Int) -> Bool ---
  Eq, Lt, Gt, Le, Ge,   // Eq is numeric `=`

  // --- boolean ---
  Not,         // (Bool) -> Bool, unary
  And, Or,     // (Bool, Bool) -> Bool  (strict, matches primitives table)

  // --- mutable store (ref/deref/set) ---
  Ref,         // pop v -> alloc loc l, store[l]=v, push loc l
  Deref,       // pop loc l -> push store[l]
  Set,         // pop v, pop loc l -> store[l]=v, push v  (set returns the value)

  // --- control flow ---
  Jmp,         // arg = absolute code index -> ip = arg
  JmpIfFalse,  // arg = absolute code index -> pop b; if !b then ip = arg
  Halt,        // stop; result is top of stack
};

// One instruction: opcode + a 32-bit argument (const/slot/jump target).
// 8 bytes, so the code stream is dense and cache friendly.
struct Instr {
  Op op;
  std::uint32_t arg;
};

}  // namespace engine
