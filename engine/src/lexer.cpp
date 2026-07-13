#include "lexer.hpp"

#include <cctype>
#include <stdexcept>

namespace engine {

namespace {

bool is_delim(char c) {
  return std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' ||
         c == '[' || c == ']';
}

// A symbol char is anything that is not whitespace, a paren/bracket, or a
// comment marker. This admits +, -, *, /, ==, <=, etc. as symbols.
bool is_symbol_char(char c) { return !is_delim(c) && c != ';'; }

}  // namespace

std::vector<Token> lex(const std::string& src) {
  std::vector<Token> out;
  std::size_t i = 0;
  const std::size_t n = src.size();

  while (i < n) {
    char c = src[i];

    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
      continue;
    }
    // Line comments: `; ...` to end of line (Racket style).
    if (c == ';') {
      while (i < n && src[i] != '\n') ++i;
      continue;
    }
    if (c == '(' || c == '[') {
      out.push_back({Tok::LParen});
      ++i;
      continue;
    }
    if (c == ')' || c == ']') {
      out.push_back({Tok::RParen});
      ++i;
      continue;
    }
    if (c == '#') {
      // Boolean literal: #t or #f.
      if (i + 1 < n && (src[i + 1] == 't' || src[i + 1] == 'f')) {
        Token t{Tok::Bool};
        t.bool_val = (src[i + 1] == 't');
        out.push_back(t);
        i += 2;
        continue;
      }
      throw std::runtime_error("lex: unexpected '#' (only #t/#f supported)");
    }

    // A run of symbol chars. Decide afterwards if it is an integer.
    std::size_t start = i;
    while (i < n && is_symbol_char(src[i])) ++i;
    std::string word = src.substr(start, i - start);

    // Integer if it is [-+]?digits and not a bare sign (which is a symbol).
    bool numeric = false;
    {
      std::size_t k = 0;
      if (word.size() > 1 && (word[0] == '-' || word[0] == '+')) k = 1;
      if (k < word.size()) {
        numeric = true;
        for (std::size_t j = k; j < word.size(); ++j) {
          if (!std::isdigit(static_cast<unsigned char>(word[j]))) {
            numeric = false;
            break;
          }
        }
      }
    }

    if (numeric) {
      Token t{Tok::Int};
      t.int_val = std::stoll(word);
      out.push_back(t);
    } else {
      Token t{Tok::Symbol};
      t.text = word;
      out.push_back(t);
    }
  }

  out.push_back({Tok::End});
  return out;
}

}  // namespace engine
