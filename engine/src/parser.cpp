#include "parser.hpp"

#include <stdexcept>
#include <unordered_set>

#include "lexer.hpp"

namespace engine {

namespace {

const std::unordered_set<std::string>& primitives() {
  static const std::unordered_set<std::string> s = {
      "+", "-", "*", "/", "==", "<", ">", "<=", ">=", "not", "and", "or"};
  return s;
}

// Recursive-descent over the token stream.
class Parser {
 public:
  explicit Parser(std::vector<Token> toks) : toks_(std::move(toks)) {}

  NodePtr parse_top() {
    NodePtr e = parse_expr();
    expect(Tok::End, "trailing tokens after expression");
    return e;
  }

 private:
  std::vector<Token> toks_;
  std::size_t pos_ = 0;

  [[nodiscard]] const Token& peek() const { return toks_[pos_]; }
  const Token& advance() { return toks_[pos_++]; }

  [[noreturn]] void fail(const std::string& msg) {
    throw std::runtime_error("parse: " + msg);
  }

  void expect(Tok k, const std::string& what) {
    if (peek().kind != k) fail("expected " + what);
    advance();
  }

  static NodePtr mk(NodeKind k) {
    auto n = std::make_unique<Node>();
    n->kind = k;
    return n;
  }

  NodePtr parse_expr() {
    const Token& t = peek();
    switch (t.kind) {
      case Tok::Int: {
        auto n = mk(NodeKind::Int);
        n->int_val = advance().int_val;
        return n;
      }
      case Tok::Bool: {
        auto n = mk(NodeKind::Bool);
        n->bool_val = advance().bool_val;
        return n;
      }
      case Tok::Symbol: {
        auto n = mk(NodeKind::Var);
        n->sym = advance().text;
        return n;
      }
      case Tok::LParen:
        return parse_list();
      default:
        fail("unexpected token");
    }
  }

  // Read a `(head ...)` form and dispatch on the head symbol.
  NodePtr parse_list() {
    expect(Tok::LParen, "'('");
    if (peek().kind != Tok::Symbol) fail("expected a form head symbol");
    std::string head = advance().text;

    if (head == "if") return parse_if();
    if (head == "let") return parse_let();
    if (head == "let*") return parse_let_star();
    if (head == "@") return parse_app();
    if (head == "ref") return parse_unary(NodeKind::Ref);
    if (head == "deref") return parse_unary(NodeKind::Deref);
    if (head == "set") return parse_binary(NodeKind::Set);
    if (head == "seq") return parse_binary(NodeKind::Seq);
    if (head == "while") return parse_binary(NodeKind::While);

    // Forms deliberately outside the hot-path subset.
    if (head == "lambda" || head == "letrec" || head == "let*2" ||
        head == "set!" || head == "set!-pure") {
      fail("unsupported form '" + head +
           "' (engine is first-order; see plan scope)");
    }
    fail("unknown form '" + head + "'");
  }

  NodePtr parse_if() {
    auto n = mk(NodeKind::If);
    n->kids.push_back(parse_expr());  // cond
    n->kids.push_back(parse_expr());  // then
    n->kids.push_back(parse_expr());  // else
    expect(Tok::RParen, "')' to close if");
    return n;
  }

  // (let ([x e]) body) — single binding, matching interpreter.rkt:206.
  NodePtr parse_let() {
    auto n = mk(NodeKind::Let);
    expect(Tok::LParen, "'(' opening let bindings");
    expect(Tok::LParen, "'(' opening binding");
    if (peek().kind != Tok::Symbol) fail("let binding needs a variable name");
    n->sym = advance().text;
    n->kids.push_back(parse_expr());  // value
    expect(Tok::RParen, "')' closing binding");
    expect(Tok::RParen, "')' closing let bindings");
    n->kids.push_back(parse_expr());  // body
    expect(Tok::RParen, "')' closing let");
    return n;
  }

  // (let* ([x1 e1] [x2 e2] ...) body) — sequential, interpreter.rkt:354.
  NodePtr parse_let_star() {
    auto n = mk(NodeKind::LetStar);
    expect(Tok::LParen, "'(' opening let* bindings");
    while (peek().kind == Tok::LParen) {
      advance();  // '('
      if (peek().kind != Tok::Symbol) fail("let* binding needs a variable name");
      std::string name = advance().text;
      NodePtr val = parse_expr();
      expect(Tok::RParen, "')' closing let* binding");
      n->bindings.emplace_back(std::move(name), std::move(val));
    }
    expect(Tok::RParen, "')' closing let* bindings");
    n->kids.push_back(parse_expr());  // body
    expect(Tok::RParen, "')' closing let*");
    return n;
  }

  // (@ op arg ...) — only primitive heads are legal (no closures).
  NodePtr parse_app() {
    if (peek().kind != Tok::Symbol) fail("@ head must be a primitive symbol");
    std::string op = advance().text;
    if (!primitives().contains(op)) {
      fail("@ head '" + op +
           "' is not a primitive (engine has no user-defined functions)");
    }
    auto n = mk(NodeKind::Prim);
    n->sym = op;
    while (peek().kind != Tok::RParen) n->kids.push_back(parse_expr());
    expect(Tok::RParen, "')' closing application");
    return n;
  }

  NodePtr parse_unary(NodeKind k) {
    auto n = mk(k);
    n->kids.push_back(parse_expr());
    expect(Tok::RParen, "')' closing form");
    return n;
  }

  NodePtr parse_binary(NodeKind k) {
    auto n = mk(k);
    n->kids.push_back(parse_expr());
    n->kids.push_back(parse_expr());
    expect(Tok::RParen, "')' closing form");
    return n;
  }
};

}  // namespace

NodePtr parse(const std::string& src) {
  Parser p(lex(src));
  return p.parse_top();
}

}  // namespace engine
