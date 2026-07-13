// lexer.hpp — tokenizer for the S-expression surface syntax.
//
// The engine keeps the exact same source syntax as interpreter.rkt so the same
// text can be fed to both the C++ engine and the Racket oracle. Tokens are the
// minimal set for that grammar: parentheses/brackets, integers, booleans
// (#t/#f) and symbols (identifiers and operators like + == <=).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

enum class Tok : std::uint8_t { LParen, RParen, Int, Bool, Symbol, End };

struct Token {
  Tok kind;
  std::int64_t int_val = 0;   // Int
  bool bool_val = false;      // Bool
  std::string text;           // Symbol
};

// Tokenize `src`. Throws std::runtime_error on an illegal character.
// `(` and `[` both open; `)` and `]` both close (interpreter.rkt uses [] in
// let/letrec binding lists).
std::vector<Token> lex(const std::string& src);

}  // namespace engine
