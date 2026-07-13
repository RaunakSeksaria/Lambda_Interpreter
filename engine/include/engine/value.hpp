// value.hpp — the runtime value of the expression engine.
//
// A Value is a small tagged union, deliberately kept to 16 bytes so it is
// trivially copyable, register/cache friendly, and never touches the heap.
// The engine is first-order (no closures), so every value is one of:
//   Int   — a 64-bit signed integer
//   Bool  — a boolean (stored in `bits` as 0/1)
//   Loc   — a store location (index into the VM store vector), from `ref`
//   Undef — the "bottom" result of a `while` whose condition was never true
//
// This mirrors the value domain of interpreter.rkt (numbers, booleans, loc),
// minus closures/primitives which are compiled away into bytecode.
#pragma once

#include <cstdint>
#include <string>

namespace engine {

enum class Tag : std::uint8_t { Int, Bool, Loc, Undef };

struct Value {
  Tag tag;
  std::int64_t bits;  // Int payload | Bool 0/1 | Loc index

  static Value make_int(std::int64_t v) { return Value{Tag::Int, v}; }
  static Value make_bool(bool b) { return Value{Tag::Bool, b ? 1 : 0}; }
  static Value make_loc(std::int64_t l) { return Value{Tag::Loc, l}; }
  static Value make_undef() { return Value{Tag::Undef, 0}; }

  bool is_int() const { return tag == Tag::Int; }
  bool is_bool() const { return tag == Tag::Bool; }
  bool is_loc() const { return tag == Tag::Loc; }
  bool is_undef() const { return tag == Tag::Undef; }

  bool as_bool() const { return bits != 0; }
  std::int64_t as_int() const { return bits; }

  bool operator==(const Value& o) const { return tag == o.tag && bits == o.bits; }
};

// Text form matching interpreter.rkt's pretty-printer, so differential tests
// can compare the engine's output against the Racket oracle byte-for-byte.
inline std::string to_string(const Value& v) {
  switch (v.tag) {
    case Tag::Int:   return std::to_string(v.bits);
    case Tag::Bool:  return v.bits ? "#t" : "#f";
    case Tag::Loc:   return "<loc:" + std::to_string(v.bits) + ">";
    case Tag::Undef: return "undefined";
  }
  return "undefined";
}

}  // namespace engine
