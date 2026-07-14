// bench_vm.cpp — latency benchmark for the VM hot loop.
//
// Sweeps three "signals" (compiled programs) that a hot-path rule engine would
// realistically run, and for each reports:
//   * batch throughput (ns/eval, cycles/eval) — the headline, measured over one
//     timing window with no per-eval fence, so a tiny signal's true fast path
//     is visible instead of being drowned by rdtsc/lfence overhead;
//   * per-eval fenced percentiles (p50/p99/p999) for the tail;
//   * perf_event_open counters (insns/eval, IPC, branches/eval, branch-miss
//     rate) over the batch window — degrades gracefully if perf is denied;
//   * the interpreter overhead vs a hand-written native C++ equivalent.
//
// Built twice by the Makefile, once per dispatch strategy (ENGINE_DISPATCH_NAME
// = switch | computed-goto). The Racket reference-interpreter baseline is a
// separate script, bench_oracle.rkt. "op"/"eval" = one signal evaluation.
//
// Methodology: thread pinned to one CPU; program + entire input stream
// allocated once (no per-tick allocation); every result fed to an inline-asm
// sink so the optimizer cannot elide the work. A native/engine agreement check
// runs first to guarantee the two compute the same thing.
#include <linux/perf_event.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <x86intrin.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>
#include <string>
#include <vector>

#include "compiler.hpp"
#include "parser.hpp"
#include "vm.hpp"

#ifndef ENGINE_DISPATCH_NAME
#define ENGINE_DISPATCH_NAME "unknown"
#endif

namespace {

constexpr std::size_t kTicks = 4096;         // distinct input vectors (the stream)
constexpr std::size_t kBatchPasses = 256;    // passes for throughput (~1M evals)
constexpr std::size_t kSamplePasses = 48;    // passes for fenced percentiles

// ---- native C++ equivalents (same math the signals compute) ----
std::int64_t native_light(const std::int64_t* in) {
  const std::int64_t bid = in[0], ask = in[1];
  const std::int64_t mid = (bid + ask) / 2;
  const std::int64_t spread = ask - bid;
  return (spread > 0 && spread < 10) ? mid : 0;
}
std::int64_t native_branchy(const std::int64_t* in) {
  const std::int64_t a = in[0], b = in[1], c = in[2], d = in[3];
  if (a > b) return (c < d) ? a + c : a - c;
  return (c > d) ? b * 2 : b + d;
}
std::int64_t native_heavy(const std::int64_t* in) {
  const std::int64_t px = in[0], qty = in[1];
  std::int64_t acc = 0;
  for (std::int64_t i = 32; i > 0; --i) acc += i * px;
  return acc + qty;
}

struct Signal {
  const char* name;
  const char* desc;
  const char* source;
  std::vector<std::string> inputs;                    // canonical order
  std::vector<std::pair<std::int64_t, std::int64_t>> ranges;  // gen range per input
  std::int64_t (*native)(const std::int64_t*);
};

std::vector<Signal> make_signals() {
  return {
      {"light", "straight-line mid/spread predicate",
       "(let* ([mid (@ / (@ + bid ask) 2)] [spread (@ - ask bid)])"
       "  (if (@ and (@ > spread 0) (@ < spread 10)) mid 0))",
       {"bid", "ask"}, {{100, 1000}, {100, 1000}}, native_light},
      {"branchy", "data-dependent nested branches (unpredictable)",
       "(if (@ > a b) (if (@ < c d) (@ + a c) (@ - a c))"
       "              (if (@ > c d) (@ * b 2) (@ + b d)))",
       {"a", "b", "c", "d"}, {{0, 1000}, {0, 1000}, {0, 1000}, {0, 1000}},
       native_branchy},
      {"heavy", "32-iteration store/loop accumulation",
       "(let* ([acc (ref 0)] [i (ref 32)])"
       "  (seq (while (@ > (deref i) 0)"
       "         (seq (set acc (@ + (deref acc) (@ * (deref i) px)))"
       "              (set i (@ - (deref i) 1))))"
       "       (@ + (deref acc) qty)))",
       {"px", "qty"}, {{1, 1000}, {1, 100}}, native_heavy},
  };
}

// ---- environment / timing helpers ----
void pin_to_cpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  // glibc's CPU_SET macro does an int->size_t conversion internally.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
  CPU_SET(cpu, &set);
#pragma GCC diagnostic pop
  if (sched_setaffinity(0, sizeof(set), &set) != 0)
    std::fprintf(stderr, "warning: could not pin to CPU %d\n", cpu);
}

double now_seconds() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

// Group of hardware counters read around the measured region.
class PerfCounters {
 public:
  bool open_all() {
    leader_ = open_one(PERF_COUNT_HW_CPU_CYCLES, -1);
    if (leader_ < 0) return false;
    ins_ = open_one(PERF_COUNT_HW_INSTRUCTIONS, leader_);
    br_ = open_one(PERF_COUNT_HW_BRANCH_INSTRUCTIONS, leader_);
    brm_ = open_one(PERF_COUNT_HW_BRANCH_MISSES, leader_);
    ok_ = (ins_ >= 0 && br_ >= 0 && brm_ >= 0);
    return ok_;
  }
  void start() const {
    ioctl(leader_, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
    ioctl(leader_, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
  }
  void stop() const { ioctl(leader_, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP); }
  [[nodiscard]] bool ok() const { return ok_; }
  [[nodiscard]] std::uint64_t cycles() const { return read_one(leader_); }
  [[nodiscard]] std::uint64_t instructions() const { return read_one(ins_); }
  [[nodiscard]] std::uint64_t branches() const { return read_one(br_); }
  [[nodiscard]] std::uint64_t branch_misses() const { return read_one(brm_); }

 private:
  int leader_ = -1, ins_ = -1, br_ = -1, brm_ = -1;
  bool ok_ = false;

  static int open_one(std::uint32_t config, int group_fd) {
    perf_event_attr attr{};
    attr.type = PERF_TYPE_HARDWARE;
    attr.size = sizeof(attr);
    attr.config = config;
    attr.disabled = (group_fd == -1) ? 1U : 0U;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    const long fd = syscall(SYS_perf_event_open, &attr, 0, -1, group_fd, 0UL);
    return static_cast<int>(fd);
  }
  static std::uint64_t read_one(int fd) {
    std::uint64_t v = 0;
    if (::read(fd, &v, sizeof(v)) != static_cast<ssize_t>(sizeof(v))) return 0;
    return v;
  }
};

double percentile(const std::vector<std::uint32_t>& sorted, double q) {
  const std::size_t idx =
      static_cast<std::size_t>(q * static_cast<double>(sorted.size() - 1));
  return static_cast<double>(sorted[idx]);
}

// One signal's benchmark: build inputs, verify vs native, measure, print.
void run_signal(const Signal& sig, int /*unused*/) {
  engine::Program prog = engine::compile(*engine::parse(sig.source));
  const std::size_t nc = sig.inputs.size();
  const std::size_t ni = prog.n_inputs;
  if (ni != nc) {
    std::fprintf(stderr, "signal %s: input count mismatch (%zu vs %zu)\n",
                 sig.name, ni, nc);
    return;
  }

  // Generate the raw input stream (canonical order), reproducible.
  std::mt19937_64 rng(0xC0FFEE);
  std::vector<std::int64_t> raw(kTicks * nc);
  for (std::size_t t = 0; t < kTicks; ++t)
    for (std::size_t i = 0; i < nc; ++i) {
      std::uniform_int_distribution<std::int64_t> d(sig.ranges[i].first,
                                                    sig.ranges[i].second);
      raw[t * nc + i] = d(rng);
    }

  // Map program input order -> canonical order, and build the engine stream.
  std::vector<std::size_t> map(ni);
  for (std::size_t j = 0; j < ni; ++j) {
    map[j] = 0;
    for (std::size_t k = 0; k < nc; ++k)
      if (prog.input_names[j] == sig.inputs[k]) map[j] = k;
  }
  std::vector<engine::Value> eng(kTicks * ni);
  for (std::size_t t = 0; t < kTicks; ++t)
    for (std::size_t j = 0; j < ni; ++j)
      eng[t * ni + j] = engine::Value::make_int(raw[t * nc + map[j]]);

  engine::VM vm(prog);

  // Correctness gate: engine must agree with native on every tick.
  for (std::size_t t = 0; t < kTicks; ++t) {
    const std::int64_t got = vm.run(&eng[t * ni]).bits;
    const std::int64_t want = sig.native(&raw[t * nc]);
    if (got != want) {
      std::fprintf(stderr, "signal %s: engine/native disagree at tick %zu (%lld vs %lld)\n",
                   sig.name, t, static_cast<long long>(got),
                   static_cast<long long>(want));
      return;
    }
  }

  volatile std::int64_t sink = 0;
  // Warm up caches / predictors.
  for (std::size_t r = 0; r < 8; ++r)
    for (std::size_t t = 0; t < kTicks; ++t)
      sink += vm.run(&eng[t * ni]).bits;

  // Batch throughput + perf counters (engine).
  PerfCounters perf;
  const bool perf_ok = perf.open_all();
  const double t0 = now_seconds();
  const std::uint64_t c0 = __rdtsc();
  if (perf_ok) perf.start();
  for (std::size_t r = 0; r < kBatchPasses; ++r)
    for (std::size_t t = 0; t < kTicks; ++t) {
      const std::int64_t x = vm.run(&eng[t * ni]).bits;
      asm volatile("" : : "r"(x) : "memory");
      sink += x;
    }
  if (perf_ok) perf.stop();
  const std::uint64_t c1 = __rdtsc();
  const double t1 = now_seconds();
  const double batch_n = static_cast<double>(kBatchPasses * kTicks);
  const double eng_ns = (t1 - t0) * 1e9 / batch_n;
  const double eng_cyc = static_cast<double>(c1 - c0) / batch_n;

  // Fenced per-eval percentiles.
  std::vector<std::uint32_t> samples;
  samples.reserve(kSamplePasses * kTicks);
  for (std::size_t r = 0; r < kSamplePasses; ++r)
    for (std::size_t t = 0; t < kTicks; ++t) {
      const engine::Value* in = &eng[t * ni];
      _mm_lfence();
      const std::uint64_t s0 = __rdtsc();
      _mm_lfence();
      const std::int64_t x = vm.run(in).bits;
      _mm_lfence();
      const std::uint64_t s1 = __rdtsc();
      asm volatile("" : : "r"(x) : "memory");
      samples.push_back(static_cast<std::uint32_t>(s1 - s0));
    }
  std::sort(samples.begin(), samples.end());

  // Native baseline throughput.
  const double n0 = now_seconds();
  for (std::size_t r = 0; r < kBatchPasses; ++r)
    for (std::size_t t = 0; t < kTicks; ++t) {
      const std::int64_t x = sig.native(&raw[t * nc]);
      asm volatile("" : : "r"(x) : "memory");
      sink += x;
    }
  const double nat_ns = (now_seconds() - n0) * 1e9 / batch_n;
  (void)sink;

  // ---- report ----
  std::printf("--- %s: %s ---\n", sig.name, sig.desc);
  std::printf("  [%s] throughput: %7.1f ns/eval  %8.1f cyc/eval\n",
              ENGINE_DISPATCH_NAME, eng_ns, eng_cyc);
  std::printf("       tail (fenced): p50=%.0f  p99=%.0f  p999=%.0f cyc\n",
              percentile(samples, 0.50), percentile(samples, 0.99),
              percentile(samples, 0.999));
  if (perf_ok) {
    const double cyc = static_cast<double>(perf.cycles());
    const double ins = static_cast<double>(perf.instructions());
    const double br = static_cast<double>(perf.branches());
    const double brm = static_cast<double>(perf.branch_misses());
    std::printf("       perf: insns/eval=%.0f  branches/eval=%.0f  IPC=%.2f  branch-miss=%.2f%%\n",
                ins / batch_n, br / batch_n, ins / cyc, 100.0 * brm / br);
  } else {
    std::printf("       perf: unavailable (perf_event_paranoid > 2?)\n");
  }
  std::printf("       vs native: native=%.1f ns/eval  ->  engine %.1fx native\n",
              nat_ns, eng_ns / nat_ns);
  std::fflush(stdout);
}

}  // namespace

int main() {
  const int cpu = []() {
    const char* e = std::getenv("BENCH_CPU");
    return e ? std::atoi(e) : 0;
  }();
  pin_to_cpu(cpu);

  std::printf("=== dispatch: %s  (ticks=%zu, batch=%zux -> %zu evals) ===\n",
              ENGINE_DISPATCH_NAME, kTicks, kBatchPasses, kBatchPasses * kTicks);
  for (const Signal& sig : make_signals()) run_signal(sig, 0);
  return 0;
}
