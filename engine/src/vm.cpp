#include "vm.hpp"

#include <cstddef>
#include <cstdint>

// Computed-goto dispatch uses GNU extensions (label addresses `&&L` and
// `goto *table[]`). They are intentional, so silence -Wpedantic for this build
// only; the switch build stays fully pedantic.
#ifdef ENGINE_COMPUTED_GOTO
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

// Dispatch macros. VM_CASE opens a handler, VM_NEXT advances to the next
// instruction, VM_DISPATCH re-dispatches after a jump has set `ip`. The same
// handler bodies are shared by both the switch and the computed-goto builds.
// NOTE: VM_NEXT / VM_DISPATCH must NOT be wrapped in a do/while — in the switch
// build the loop-control statement is `break`, and a `break` inside a do/while
// would break that loop instead of the switch, falling through to the next
// case. They are only ever used at statement position ending a handler, so the
// bare multi-statement form is safe here.
#ifdef ENGINE_COMPUTED_GOTO
#define VM_CASE(name) L_##name
#define VM_DISPATCH() goto* dispatch[static_cast<std::size_t>(ip->op)]
#define VM_NEXT() \
  ++ip;           \
  VM_DISPATCH()
#else
#define VM_CASE(name) case Op::name
#define VM_DISPATCH() break
#define VM_NEXT() \
  ++ip;           \
  break
#endif

namespace engine {

Value VM::run(const Value* inputs) {
  const Instr* code = prog_.code.data();
  const Value* consts = prog_.consts.data();
  Value* stack = stack_.data();
  Value* locals = locals_.data();
  Value* store = store_.data();

  Value* sp = stack;          // points one past the top
  const Instr* ip = code;
  std::int64_t store_top = 0;  // reset store every run -> no per-tick malloc

#ifdef ENGINE_COMPUTED_GOTO
  // Order MUST match enum Op exactly.
  static const void* const dispatch[] = {
      &&L_PushConst, &&L_LoadInput, &&L_LoadLocal, &&L_StoreLocal, &&L_Pop,
      &&L_Add,       &&L_Sub,       &&L_Mul,        &&L_Div,
      &&L_Eq,        &&L_Lt,        &&L_Gt,         &&L_Le,         &&L_Ge,
      &&L_Not,       &&L_And,       &&L_Or,
      &&L_Ref,       &&L_Deref,     &&L_Set,
      &&L_Jmp,       &&L_JmpIfFalse, &&L_Halt};
  VM_DISPATCH();
#else
  for (;;) {
    switch (ip->op) {
#endif

  VM_CASE(PushConst) : { *sp++ = consts[ip->arg]; } VM_NEXT();
  VM_CASE(LoadInput) : { *sp++ = inputs[ip->arg]; } VM_NEXT();
  VM_CASE(LoadLocal) : { *sp++ = locals[ip->arg]; } VM_NEXT();
  VM_CASE(StoreLocal) : { locals[ip->arg] = *--sp; } VM_NEXT();
  VM_CASE(Pop) : { --sp; } VM_NEXT();

  VM_CASE(Add) : { sp[-2] = Value::make_int(sp[-2].bits + sp[-1].bits); --sp; } VM_NEXT();
  VM_CASE(Sub) : { sp[-2] = Value::make_int(sp[-2].bits - sp[-1].bits); --sp; } VM_NEXT();
  VM_CASE(Mul) : { sp[-2] = Value::make_int(sp[-2].bits * sp[-1].bits); --sp; } VM_NEXT();
  // Integer division is intentionally unguarded on the hot path: a zero divisor
  // is treated as a config-time (compile-time) validation concern, not a
  // per-tick branch. Callers are expected to supply well-formed signals.
  // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
  VM_CASE(Div) : { sp[-2] = Value::make_int(sp[-2].bits / sp[-1].bits); --sp; } VM_NEXT();

  VM_CASE(Eq) : { sp[-2] = Value::make_bool(sp[-2].bits == sp[-1].bits); --sp; } VM_NEXT();
  VM_CASE(Lt) : { sp[-2] = Value::make_bool(sp[-2].bits <  sp[-1].bits); --sp; } VM_NEXT();
  VM_CASE(Gt) : { sp[-2] = Value::make_bool(sp[-2].bits >  sp[-1].bits); --sp; } VM_NEXT();
  VM_CASE(Le) : { sp[-2] = Value::make_bool(sp[-2].bits <= sp[-1].bits); --sp; } VM_NEXT();
  VM_CASE(Ge) : { sp[-2] = Value::make_bool(sp[-2].bits >= sp[-1].bits); --sp; } VM_NEXT();

  VM_CASE(Not) : { sp[-1] = Value::make_bool(!sp[-1].as_bool()); } VM_NEXT();
  VM_CASE(And) : { sp[-2] = Value::make_bool(sp[-2].as_bool() && sp[-1].as_bool()); --sp; } VM_NEXT();
  VM_CASE(Or)  : { sp[-2] = Value::make_bool(sp[-2].as_bool() || sp[-1].as_bool()); --sp; } VM_NEXT();

  VM_CASE(Ref) : {
    store[store_top] = sp[-1];
    sp[-1] = Value::make_loc(store_top);
    ++store_top;
  } VM_NEXT();
  VM_CASE(Deref) : { sp[-1] = store[sp[-1].bits]; } VM_NEXT();
  VM_CASE(Set) : {
    Value v = sp[-1];
    store[sp[-2].bits] = v;
    sp[-2] = v;  // `set` evaluates to the assigned value
    --sp;
  } VM_NEXT();

  VM_CASE(Jmp) : { ip = code + ip->arg; } VM_DISPATCH();
  VM_CASE(JmpIfFalse) : {
    bool b = (--sp)->as_bool();
    if (!b) {
      ip = code + ip->arg;
      VM_DISPATCH();
    }
  } VM_NEXT();

  VM_CASE(Halt) : { return sp[-1]; }

#ifndef ENGINE_COMPUTED_GOTO
    }  // switch
  }    // for
#endif
}

}  // namespace engine
