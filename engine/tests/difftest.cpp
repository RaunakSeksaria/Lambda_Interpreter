// difftest.cpp — differential-test DRIVER.
//
// Evaluates every corpus case with the C++ engine and compares the result
// against the golden output produced by the Racket oracle (gen_golden.rkt).
// Any divergence means the engine's semantics drifted from the reference
// interpreter — the gate every optimization must keep passing.
//
//   difftest <corpus-path> <golden-path>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "compiler.hpp"
#include "parser.hpp"
#include "vm.hpp"

namespace {

struct Case {
  std::string inputs_tok;  // "-" or "name=val,name=val"
  std::string expr;        // remainder of the line
  std::string source;      // original line, for error messages
};

std::string trim(const std::string& s) {
  std::size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  std::size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

bool live_line(const std::string& t) { return !t.empty() && t[0] != '#'; }

engine::Value parse_input_value(const std::string& s) {
  if (s == "#t") return engine::Value::make_bool(true);
  if (s == "#f") return engine::Value::make_bool(false);
  return engine::Value::make_int(std::stoll(s));
}

// "bid=100,ask=101" -> lookups by name; "-" -> empty.
std::vector<std::pair<std::string, engine::Value>> parse_inputs(
    const std::string& tok) {
  std::vector<std::pair<std::string, engine::Value>> out;
  if (tok == "-") return out;
  std::stringstream ss(tok);
  std::string pair;
  while (std::getline(ss, pair, ',')) {
    auto eq = pair.find('=');
    if (eq == std::string::npos)
      throw std::runtime_error("bad input '" + pair + "'");
    out.emplace_back(pair.substr(0, eq), parse_input_value(pair.substr(eq + 1)));
  }
  return out;
}

std::vector<Case> read_corpus(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open corpus: " + path);
  std::vector<Case> cases;
  std::string line;
  while (std::getline(in, line)) {
    std::string t = trim(line);
    if (!live_line(t)) continue;
    std::size_t sp = t.find_first_of(" \t");
    if (sp == std::string::npos)
      throw std::runtime_error("corpus line has no expression: " + line);
    cases.push_back({t.substr(0, sp), trim(t.substr(sp)), line});
  }
  return cases;
}

std::vector<std::string> read_lines(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open golden: " + path);
  std::vector<std::string> out;
  std::string line;
  while (std::getline(in, line)) out.push_back(trim(line));
  return out;
}

// Compile + run one case, returning the engine's stringified result.
std::string eval_case(const Case& c) {
  engine::Program prog = engine::compile(*engine::parse(c.expr));
  auto provided = parse_inputs(c.inputs_tok);

  std::vector<engine::Value> inputs(prog.n_inputs);
  for (std::size_t i = 0; i < prog.input_names.size(); ++i) {
    const std::string& name = prog.input_names[i];
    bool found = false;
    for (const auto& kv : provided) {
      if (kv.first == name) {
        inputs[i] = kv.second;
        found = true;
        break;
      }
    }
    if (!found) throw std::runtime_error("missing input '" + name + "'");
  }

  engine::VM vm(prog);
  return engine::to_string(vm.run(inputs.data()));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <corpus-path> <golden-path>\n";
    return 2;
  }

  try {
    std::vector<Case> cases = read_corpus(argv[1]);
    std::vector<std::string> golden = read_lines(argv[2]);
    if (cases.size() != golden.size()) {
      std::cerr << "count mismatch: " << cases.size() << " cases vs "
                << golden.size() << " golden lines "
                << "(regenerate with `make test`)\n";
      return 2;
    }

    std::size_t failures = 0;
    for (std::size_t i = 0; i < cases.size(); ++i) {
      std::string got;
      try {
        got = eval_case(cases[i]);
      } catch (const std::exception& e) {
        got = std::string("<error: ") + e.what() + ">";
      }
      if (got != golden[i]) {
        ++failures;
        std::cout << "FAIL [" << i << "] " << cases[i].expr << "\n"
                  << "   expected: " << golden[i] << "\n"
                  << "   got:      " << got << "\n";
      }
    }

    std::cout << (cases.size() - failures) << "/" << cases.size()
              << " cases match the Racket oracle\n";
    return failures == 0 ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  }
}
