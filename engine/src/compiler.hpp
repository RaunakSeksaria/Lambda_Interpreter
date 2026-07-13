// compiler.hpp — AST -> Program.
//
// The compiler does the work that makes the hot loop fast:
//   * every variable is resolved to an integer index at compile time —
//     a local frame slot (let/let*) or an input index — so the VM never does a
//     string lookup (contrast interpreter.rkt's assoc-list `lookup-env`);
//   * control flow (if/while) is lowered to jumps;
//   * the operand-stack high-water mark (`max_stack`), local frame size
//     (`n_locals`) and store size (`max_store`) are computed so the VM can
//     preallocate every buffer exactly once and never touch the heap per tick.
//
// Free variables become inputs, assigned indices in first-seen order; their
// names are recorded in Program::input_names for the caller to bind.
#pragma once

#include "engine/bytecode.hpp"
#include "parser.hpp"

namespace engine {

// Throws std::runtime_error on arity errors or unsupported constructs.
Program compile(const Node& root);

}  // namespace engine
