// bench_vm.cpp — latency benchmark for the VM hot loop.
//
// Measures the per-tick cost of evaluating one compiled "signal" program over a
// preallocated stream of input vectors (simulating a market-data feed). Built
// twice by the Makefile — once per dispatch strategy (switch vs computed-goto,
// via ENGINE_DISPATCH_NAME) — so the two can be compared directly.
//
// What it reports:
//   * per-eval latency distribution (min / p50 / p90 / p99 / p999 / mean) in
//     both TSC cycles and nanoseconds (TSC calibrated at startup);
//   * hardware counters via perf_event_open (instructions, IPC, branches, and
//     the branch-miss rate — the number that actually separates the two
//     dispatch strategies). Degrades gracefully if perf is not permitted.
//
// Methodology notes: the benchmarking thread is pinned to one CPU; the program
// and the entire input stream are allocated once (no per-tick allocation); the
// result of every eval is fed to an inline-asm sink so the optimizer cannot
// elide the work.
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

// A representative stateful signal: accumulate a fixed-length weighted sum in
// the store, then combine with an input. Fixed loop length keeps per-eval work
// constant, so the latency spread reflects system/dispatch jitter rather than
// input-dependent work. Exercises arithmetic, comparison, branch, and the
// ref/deref/set store path — the full opcode mix a real rule would hit.
constexpr const char* kSignal =
    "(let* ([acc (ref 0)] [i (ref 32)])"
    "  (seq (while (@ > (deref i) 0)"
    "         (seq (set acc (@ + (deref acc) (@ * (deref i) px)))"
    "              (set i (@ - (deref i) 1))))"
    "       (@ + (deref acc) qty)))";

constexpr std::size_t kTicks = 4096;    // distinct input vectors (the stream)
constexpr std::size_t kRepeats = 120;   // passes over the stream
constexpr std::size_t kEvals = kTicks * kRepeats;

// Pin the calling thread to one CPU so the TSC and counters are stable.
void pin_to_cpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  // glibc's CPU_SET macro does an int->size_t conversion internally.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
  CPU_SET(cpu, &set);
#pragma GCC diagnostic pop
  if (sched_setaffinity(0, sizeof(set), &set) != 0) {
    std::fprintf(stderr, "warning: could not pin to CPU %d\n", cpu);
  }
}

double now_seconds() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

// Calibrate the TSC tick rate against CLOCK_MONOTONIC (~200 ms busy wait).
double measure_tsc_hz() {
  const double t0 = now_seconds();
  const std::uint64_t c0 = __rdtsc();
  while (now_seconds() - t0 < 0.2) { /* spin */
  }
  const std::uint64_t c1 = __rdtsc();
  const double t1 = now_seconds();
  return static_cast<double>(c1 - c0) / (t1 - t0);
}

// A small group of hardware counters read around the measured region.
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
  int leader_ = -1;
  int ins_ = -1;
  int br_ = -1;
  int brm_ = -1;
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

}  // namespace

int main() {
  const int cpu = []() {
    const char* e = std::getenv("BENCH_CPU");
    return e ? std::atoi(e) : 0;
  }();
  pin_to_cpu(cpu);

  // Compile the signal once and locate its inputs.
  engine::Program prog = engine::compile(*engine::parse(kSignal));

  // Build the input stream once (fixed seed -> reproducible), no per-tick alloc.
  std::mt19937_64 rng(0xC0FFEE);
  std::uniform_int_distribution<std::int64_t> px(1, 1000);
  std::uniform_int_distribution<std::int64_t> qty(1, 100);
  std::vector<engine::Value> stream(kTicks * prog.n_inputs);
  for (std::size_t t = 0; t < kTicks; ++t) {
    for (std::size_t i = 0; i < prog.n_inputs; ++i) {
      const std::string& name = prog.input_names[i];
      std::int64_t v = (name == "px") ? px(rng) : qty(rng);
      stream[t * prog.n_inputs + i] = engine::Value::make_int(v);
    }
  }

  engine::VM vm(prog);
  volatile std::uint64_t sink = 0;

  // Warm up caches / branch predictor.
  for (std::size_t r = 0; r < 8; ++r) {
    for (std::size_t t = 0; t < kTicks; ++t) {
      engine::Value v = vm.run(&stream[t * prog.n_inputs]);
      sink += static_cast<std::uint64_t>(v.bits);
    }
  }

  const double tsc_hz = measure_tsc_hz();

  // Measured region: per-eval cycle samples + aggregate hardware counters.
  std::vector<std::uint32_t> samples;
  samples.reserve(kEvals);

  PerfCounters perf;
  const bool perf_ok = perf.open_all();
  if (perf_ok) perf.start();

  for (std::size_t r = 0; r < kRepeats; ++r) {
    for (std::size_t t = 0; t < kTicks; ++t) {
      const engine::Value* in = &stream[t * prog.n_inputs];
      _mm_lfence();
      const std::uint64_t c0 = __rdtsc();
      _mm_lfence();
      engine::Value v = vm.run(in);
      _mm_lfence();
      const std::uint64_t c1 = __rdtsc();
      // Sink the result so the eval cannot be optimized away.
      asm volatile("" : : "r"(v.bits) : "memory");
      samples.push_back(static_cast<std::uint32_t>(c1 - c0));
    }
  }

  if (perf_ok) perf.stop();

  std::sort(samples.begin(), samples.end());
  const double mean_cyc = [&]() {
    unsigned long long s = 0;
    for (std::uint32_t c : samples) s += c;
    return static_cast<double>(s) / static_cast<double>(samples.size());
  }();
  const double cyc_to_ns = 1e9 / tsc_hz;

  std::printf("=== dispatch: %-13s  signal loop=32, ticks=%zu x %zu = %zu evals ===\n",
              ENGINE_DISPATCH_NAME, kTicks, kRepeats, kEvals);
  std::printf("  TSC: %.3f GHz\n", tsc_hz / 1e9);
  std::printf("  cycles/eval : min=%.0f  p50=%.0f  p90=%.0f  p99=%.0f  p999=%.0f  mean=%.1f\n",
              static_cast<double>(samples.front()), percentile(samples, 0.50),
              percentile(samples, 0.90), percentile(samples, 0.99),
              percentile(samples, 0.999), mean_cyc);
  std::printf("  ns/eval     : p50=%.1f  p99=%.1f  mean=%.1f\n",
              percentile(samples, 0.50) * cyc_to_ns,
              percentile(samples, 0.99) * cyc_to_ns, mean_cyc * cyc_to_ns);

  if (perf_ok) {
    const double n = static_cast<double>(kEvals);
    const double cyc = static_cast<double>(perf.cycles());
    const double ins = static_cast<double>(perf.instructions());
    const double br = static_cast<double>(perf.branches());
    const double brm = static_cast<double>(perf.branch_misses());
    std::printf("  perf/eval   : insns=%.1f  branches=%.1f  branch-misses=%.2f\n",
                ins / n, br / n, brm / n);
    std::printf("  perf ratios : IPC=%.2f  branch-miss-rate=%.2f%%\n",
                ins / cyc, 100.0 * brm / br);
  } else {
    std::printf("  perf        : counters unavailable "
                "(set /proc/sys/kernel/perf_event_paranoid <= 2 or grant CAP_PERFMON)\n");
  }
  std::fflush(stdout);
  return 0;
}
