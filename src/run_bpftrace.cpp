#include <csignal>

#include "log.h"
#include "run_bpftrace.h"

using namespace bpftrace;

static const char *libbpf_print_level_string(enum libbpf_print_level level)
{
  switch (level) {
    case LIBBPF_WARN:
      return "WARN";
    case LIBBPF_INFO:
      return "INFO";
    default:
      return "DEBUG";
  }
}

int libbpf_print(enum libbpf_print_level level, const char *msg, va_list ap)
{
  if (!bt_debug.contains(DebugStage::Libbpf))
    return 0;

  printf("[%s] ", libbpf_print_level_string(level));
  return vprintf(msg, ap);
}

void check_is_root()
{
  if (geteuid() != 0) {
    LOG(ERROR) << "bpftrace currently only supports running as the root user.";
    exit(1);
  }
}

int run_bpftrace(BPFtrace &bpftrace,
                 Output &output,
                 BpfBytecode &bytecode,
                 std::vector<std::string> &&named_params)
{
  int err;

  auto named_param_vals = bpftrace.resources.global_vars.get_named_param_vals(
      named_params);
  if (!named_param_vals) {
    auto ok = handleErrors(std::move(named_param_vals),
                           [&](const globalvars::UnknownParamError &uo_err) {
                             LOG(ERROR) << uo_err.err();
                             auto hint = uo_err.hint();
                             if (!hint.empty()) {
                               LOG(HINT) << hint;
                             }
                           });
    if (!ok) {
      LOG(ERROR) << ok.takeError();
    }
    return 1;
  }

  bytecode.update_global_vars(bpftrace, std::move(*named_param_vals));

  // Signal handler that lets us know an exit signal was received.
  struct sigaction act = {};
  act.sa_handler = [](int) { BPFtrace::exitsig_recv = true; };
  sigaction(SIGINT, &act, nullptr);
  sigaction(SIGTERM, &act, nullptr);

  // Signal handler that prints all maps when SIGUSR1 was received.
  act.sa_handler = [](int) { BPFtrace::sigusr1_recv = true; };
  sigaction(SIGUSR1, &act, nullptr);

  err = bpftrace.run(output, std::move(bytecode));
  if (err)
    return err;

  // We are now post-processing. If we receive another SIGINT,
  // handle it normally (exit)
  act.sa_handler = SIG_DFL;
  sigaction(SIGINT, &act, nullptr);

  std::cout << "\n\n";

  if (bpftrace.run_benchmarks_) {
    bpftrace.print_benchmark_results(output);
  } else if (bpftrace.config_->print_maps_on_exit) {
    err = bpftrace.print_maps(output);
  }

  if (bpftrace.child_) {
    auto val = 0;
    if ((val = bpftrace.child_->term_signal()) > -1)
      LOG(V1) << "Child terminated by signal: " << val;
    if ((val = bpftrace.child_->exit_code()) > -1)
      LOG(V1) << "Child exited with code: " << val;
  }

  if (err)
    return err;

  return BPFtrace::exit_code;
}
