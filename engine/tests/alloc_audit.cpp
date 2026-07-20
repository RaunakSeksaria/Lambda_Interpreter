// alloc_audit.cpp — proves the "run() never touches the heap" claim.
//
// vm.hpp promises that every buffer is sized from the Program and allocated
// once in the constructor, so a per-tick run() performs no allocation. Nothing
// in the type system enforces that: one stray push_back in a new opcode handler
// would silently reintroduce a malloc on the hot path, and difftest would still
// pass because the *results* stay correct. This is the gate for that property.
//
// Global operator new/delete are replaced with counting versions. For every
// corpus case we compile and construct the VM with the counters disarmed (setup
// allocation is expected and fine), then arm them and run the program N times.
// Any allocation observed in that window fails the build.
//
//   alloc_audit <corpus-path> [iters-per-case]
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "compiler.hpp"
#include "parser.hpp"
#include "vm.hpp"

namespace {

// Armed only around the measured region; see audit_case().
bool g_armed = false;
std::size_t g_allocs = 0;
std::size_t g_bytes = 0;
std::size_t g_frees = 0;

}  // namespace

// Replacing the global allocation functions is the one reliable way to observe
// every heap request made by this TU and by libstdc++ on our behalf.
void* operator new(std::size_t n) {
  if (g_armed) {
    ++g_allocs;
    g_bytes += n;
  }
  void* p = std::malloc(n != 0 ? n : 1);
  if (p == nullptr) throw std::bad_alloc();
  return p;
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  if (g_armed) {
    ++g_allocs;
    g_bytes += n;
  }
  return std::malloc(n != 0 ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept {
  return ::operator new(n, t);
}
void operator delete(void* p) noexcept {
  if (g_armed && p != nullptr) ++g_frees;
  std::free(p);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept {
  ::operator delete(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
  ::operator delete(p);
}

namespace {

// Corpus parsing mirrors difftest.cpp: `<inputs> <expr>` per line, `-` for no
// inputs, `#` comments and blank lines ignored.
struct Case {
  std::string inputs_tok;
  std::string expr;
};

std::string trim(const std::string& s) {
  std::size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  std::size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

engine::Value parse_input_value(const std::string& s) {
  if (s == "#t") return engine::Value::make_bool(true);
  if (s == "#f") return engine::Value::make_bool(false);
  return engine::Value::make_int(std::stoll(s));
}

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
    if (t.empty() || t[0] == '#') continue;
    std::size_t sp = t.find_first_of(" \t");
    if (sp == std::string::npos)
      throw std::runtime_error("corpus line has no expression: " + line);
    cases.push_back({t.substr(0, sp), trim(t.substr(sp))});
  }
  return cases;
}

struct Result {
  std::size_t allocs = 0;
  std::size_t bytes = 0;
  std::size_t frees = 0;
};

// Compile + construct outside the counted window, then run `iters` times inside
// it. Returns what the hot path allocated; all zeros is the passing outcome.
Result audit_case(const Case& c, int iters) {
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

  // Vary an input across ticks so the optimizer cannot hoist the whole loop,
  // and so data-dependent paths (branchy predicates, while trip counts) are
  // exercised rather than one fixed trace.
  const engine::Value seed = inputs.empty() ? engine::Value::make_int(0)
                                            : inputs[0];
  std::int64_t sink = 0;

  g_allocs = g_bytes = g_frees = 0;
  g_armed = true;
  for (int t = 0; t < iters; ++t) {
    if (!inputs.empty() && seed.is_int())
      inputs[0] = engine::Value::make_int(seed.bits + t % 5);
    sink += vm.run(inputs.data()).bits;
  }
  g_armed = false;

  Result r{g_allocs, g_bytes, g_frees};
  // Keep `sink` observable so the loop is not dead code at -O3.
  if (sink == 0x5EEDDEAD) std::cerr << "";
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: " << argv[0] << " <corpus-path> [iters-per-case]\n";
    return 2;
  }

  int iters = 1000;
  if (argc == 3) {
    try {
      iters = std::stoi(argv[2]);
    } catch (const std::exception&) {
      std::cerr << "bad iteration count: " << argv[2] << "\n";
      return 2;
    }
  }
  if (iters <= 0) {
    std::cerr << "iters must be positive\n";
    return 2;
  }

  try {
    std::vector<Case> cases = read_corpus(argv[1]);

    std::size_t checked = 0;
    std::size_t violations = 0;
    for (const Case& c : cases) {
      Result r;
      try {
        r = audit_case(c, iters);
      } catch (const std::exception&) {
        // Cases the engine rejects by design (unsupported forms, arity errors)
        // never reach run(); difftest is what pins their error behaviour.
        continue;
      }
      ++checked;
      if (r.allocs != 0 || r.frees != 0) {
        ++violations;
        std::cout << "ALLOC [" << c.expr << "]\n"
                  << "   " << r.allocs << " allocs, " << r.bytes << " bytes, "
                  << r.frees << " frees across " << iters << " runs\n";
      }
    }

    std::cout << (checked - violations) << "/" << checked
              << " cases allocate nothing across " << iters << " runs each\n";
    return violations == 0 ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  }
}
