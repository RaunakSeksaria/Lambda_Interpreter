// main.cpp — command-line front-end: compile one expression and evaluate it
// once against a set of named inputs. Useful for manual spot-checks against the
// Racket REPL.
//
//   lambda_eval "<expr>" name=val name=val ...
//
// Example:
//   lambda_eval "(if (@ < bid ask) (@ - ask bid) 0)" bid=100 ask=101   =>  1
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "compiler.hpp"
#include "parser.hpp"
#include "vm.hpp"

namespace {

engine::Value parse_input_value(const std::string& s) {
  if (s == "#t") return engine::Value::make_bool(true);
  if (s == "#f") return engine::Value::make_bool(false);
  return engine::Value::make_int(std::stoll(s));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0]
              << " \"<expr>\" [name=val ...]\n";
    return 2;
  }

  try {
    engine::Program prog = engine::compile(*engine::parse(argv[1]));

    std::unordered_map<std::string, engine::Value> provided;
    for (int i = 2; i < argc; ++i) {
      std::string arg = argv[i];
      auto eq = arg.find('=');
      if (eq == std::string::npos)
        throw std::runtime_error("expected name=val, got '" + arg + "'");
      provided.emplace(arg.substr(0, eq), parse_input_value(arg.substr(eq + 1)));
    }

    std::vector<engine::Value> inputs(prog.n_inputs);
    for (std::size_t i = 0; i < prog.input_names.size(); ++i) {
      auto it = provided.find(prog.input_names[i]);
      if (it == provided.end())
        throw std::runtime_error("missing input '" + prog.input_names[i] + "'");
      inputs[i] = it->second;
    }

    engine::VM vm(prog);
    engine::Value result = vm.run(inputs.data());
    std::cout << engine::to_string(result) << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
