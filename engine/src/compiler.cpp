#include "compiler.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace engine {

namespace {

[[noreturn]] void fail(const std::string& msg) {
  throw std::runtime_error("compile: " + msg);
}

Op prim_op(const std::string& s, std::size_t argc) {
  auto need = [&](std::size_t k) {
    if (argc != k)
      fail("primitive '" + s + "' expects " + std::to_string(k) +
           " args, got " + std::to_string(argc));
  };
  if (s == "+") { need(2); return Op::Add; }
  if (s == "-") { need(2); return Op::Sub; }
  if (s == "*") { need(2); return Op::Mul; }
  if (s == "/") { need(2); return Op::Div; }
  if (s == "==") { need(2); return Op::Eq; }
  if (s == "<") { need(2); return Op::Lt; }
  if (s == ">") { need(2); return Op::Gt; }
  if (s == "<=") { need(2); return Op::Le; }
  if (s == ">=") { need(2); return Op::Ge; }
  if (s == "not") { need(1); return Op::Not; }
  if (s == "and") { need(2); return Op::And; }
  if (s == "or") { need(2); return Op::Or; }
  fail("unknown primitive '" + s + "'");
}

class Compiler {
 public:
  Program run(const Node& root) {
    prog_.max_stack = stack_need(root);
    compile(root);
    emit(Op::Halt, 0);
    prog_.n_inputs = static_cast<std::uint32_t>(prog_.input_names.size());
    prog_.n_locals = n_locals_;
    prog_.max_store = n_refs_;
    return std::move(prog_);
  }

 private:
  Program prog_;
  // Local scopes: a stack of frames, each mapping name -> frame slot index.
  std::vector<std::vector<std::pair<std::string, std::uint32_t>>> scopes_;
  std::unordered_map<std::string, std::uint32_t> input_index_;
  std::uint32_t slot_top_ = 0;    // next free local slot
  std::uint32_t n_locals_ = 0;    // high-water mark of slot_top_
  std::uint32_t n_refs_ = 0;      // count of `ref` sites

  // ---- emission helpers ----
  std::uint32_t emit(Op op, std::uint32_t arg) {
    prog_.code.push_back(Instr{op, arg});
    return static_cast<std::uint32_t>(prog_.code.size() - 1);
  }
  std::uint32_t here() const {
    return static_cast<std::uint32_t>(prog_.code.size());
  }
  void patch(std::uint32_t at, std::uint32_t target) {
    prog_.code[at].arg = target;
  }

  std::uint32_t const_index(Value v) {
    for (std::uint32_t i = 0; i < prog_.consts.size(); ++i)
      if (prog_.consts[i] == v) return i;
    prog_.consts.push_back(v);
    return static_cast<std::uint32_t>(prog_.consts.size() - 1);
  }

  // Resolve a name: innermost local slot, else an input index (assigning one
  // on first sight). This is the compile-time analogue of interpreter.rkt's
  // runtime `lookup-env`, done once instead of on every evaluation.
  void compile_var(const std::string& name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      for (auto jt = it->rbegin(); jt != it->rend(); ++jt) {
        if (jt->first == name) {
          emit(Op::LoadLocal, jt->second);
          return;
        }
      }
    }
    auto found = input_index_.find(name);
    std::uint32_t idx;
    if (found == input_index_.end()) {
      idx = static_cast<std::uint32_t>(prog_.input_names.size());
      prog_.input_names.push_back(name);
      input_index_.emplace(name, idx);
    } else {
      idx = found->second;
    }
    emit(Op::LoadInput, idx);
  }

  // ---- code generation (each node nets +1 on the operand stack) ----
  void compile(const Node& n) {
    switch (n.kind) {
      case NodeKind::Int:
        emit(Op::PushConst, const_index(Value::make_int(n.int_val)));
        return;
      case NodeKind::Bool:
        emit(Op::PushConst, const_index(Value::make_bool(n.bool_val)));
        return;
      case NodeKind::Var:
        compile_var(n.sym);
        return;
      case NodeKind::Prim: {
        for (const auto& k : n.kids) compile(*k);
        emit(prim_op(n.sym, n.kids.size()), 0);
        return;
      }
      case NodeKind::If: {
        compile(*n.kids[0]);                        // cond
        std::uint32_t jf = emit(Op::JmpIfFalse, 0);
        compile(*n.kids[1]);                         // then
        std::uint32_t je = emit(Op::Jmp, 0);
        patch(jf, here());
        compile(*n.kids[2]);                         // else
        patch(je, here());
        return;
      }
      case NodeKind::Let: {
        compile(*n.kids[0]);                         // value
        std::uint32_t slot = slot_top_++;
        n_locals_ = std::max(n_locals_, slot_top_);
        emit(Op::StoreLocal, slot);
        scopes_.push_back({{n.sym, slot}});
        compile(*n.kids[1]);                         // body
        scopes_.pop_back();
        --slot_top_;
        return;
      }
      case NodeKind::LetStar: {
        scopes_.emplace_back();
        std::uint32_t base = slot_top_;
        for (auto& b : n.bindings) {
          compile(*b.second);                        // value (sees prior binds)
          std::uint32_t slot = slot_top_++;
          n_locals_ = std::max(n_locals_, slot_top_);
          emit(Op::StoreLocal, slot);
          scopes_.back().push_back({b.first, slot});
        }
        compile(*n.kids[0]);                          // body
        scopes_.pop_back();
        slot_top_ = base;
        return;
      }
      case NodeKind::Ref:
        compile(*n.kids[0]);
        ++n_refs_;
        emit(Op::Ref, 0);
        return;
      case NodeKind::Deref:
        compile(*n.kids[0]);
        emit(Op::Deref, 0);
        return;
      case NodeKind::Set:
        compile(*n.kids[0]);                          // loc
        compile(*n.kids[1]);                          // value
        emit(Op::Set, 0);
        return;
      case NodeKind::Seq:
        compile(*n.kids[0]);
        emit(Op::Pop, 0);                             // discard e1's result
        compile(*n.kids[1]);
        return;
      case NodeKind::While: {
        std::uint32_t start = here();
        compile(*n.kids[0]);                          // cond
        std::uint32_t jf = emit(Op::JmpIfFalse, 0);
        compile(*n.kids[1]);                          // body
        emit(Op::Pop, 0);                             // discard body result
        emit(Op::Jmp, start);
        patch(jf, here());
        emit(Op::PushConst, const_index(Value::make_undef()));  // result
        return;
      }
    }
    fail("unhandled node kind");
  }

  // ---- static operand-stack sizing (peak depth to evaluate n, base 0) ----
  static std::uint32_t stack_need(const Node& n) {
    switch (n.kind) {
      case NodeKind::Int:
      case NodeKind::Bool:
      case NodeKind::Var:
        return 1;
      case NodeKind::Prim: {
        std::uint32_t peak = 1;  // op needs its operands present
        for (std::size_t i = 0; i < n.kids.size(); ++i)
          peak = std::max<std::uint32_t>(
              peak, static_cast<std::uint32_t>(i) + stack_need(*n.kids[i]));
        return peak;
      }
      case NodeKind::If:
        return std::max({stack_need(*n.kids[0]), stack_need(*n.kids[1]),
                         stack_need(*n.kids[2])});
      case NodeKind::Let:
        return std::max(stack_need(*n.kids[0]), stack_need(*n.kids[1]));
      case NodeKind::LetStar: {
        std::uint32_t peak = stack_need(*n.kids[0]);  // body
        for (auto& b : n.bindings)
          peak = std::max(peak, stack_need(*b.second));
        return peak;
      }
      case NodeKind::Ref:
      case NodeKind::Deref:
        return stack_need(*n.kids[0]);
      case NodeKind::Set:
        return std::max(stack_need(*n.kids[0]), 1 + stack_need(*n.kids[1]));
      case NodeKind::Seq:
        return std::max(stack_need(*n.kids[0]), stack_need(*n.kids[1]));
      case NodeKind::While:
        return std::max({stack_need(*n.kids[0]), stack_need(*n.kids[1]),
                         std::uint32_t{1}});
    }
    return 1;
  }
};

}  // namespace

Program compile(const Node& root) { return Compiler{}.run(root); }

}  // namespace engine
