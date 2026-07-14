// parser.hpp — S-expression tokens -> AST for the first-order subset.
//
// The AST mirrors the grammar interpreter.rkt accepts, restricted to the
// hot-path subset (no lambda/@-closures, no letrec/let*2/set!). Primitive
// applications keep the source's `(@ op arg ...)` shape and become Prim nodes;
// any `@` whose head is not a known primitive is rejected (the engine is
// first-order and has no closures to apply).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace engine {

enum class NodeKind : std::uint8_t {
  Int, Bool, Var,
  If, Let, LetStar,
  Prim,               // sym = operator; kids = args
  Ref, Deref, Set,    // Set: kids[0]=loc expr, kids[1]=value expr
  Seq, While,
};

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
  NodeKind kind = NodeKind::Int;
  std::int64_t int_val = 0;                                 // Int
  bool bool_val = false;                                    // Bool
  std::string sym;                                          // Var / Prim op / Let var
  std::vector<std::pair<std::string, NodePtr>> bindings;    // Let* bindings
  std::vector<NodePtr> kids;                                // children
};

// Parse a single top-level expression. Throws std::runtime_error with a message
// that names the offending form (e.g. unsupported `letrec`).
NodePtr parse(const std::string& src);

}  // namespace engine
