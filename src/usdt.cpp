#include <algorithm>
#include <bcc/bcc_elf.h>
#include <bcc/bcc_usdt.h>
#include <csignal>
#include <unordered_map>
#include <unordered_set>

#include "log.h"
#include "usdt.h"
#include "util/system.h"

namespace bpftrace {

static std::unordered_set<std::string> path_cache;
static std::unordered_set<int> pid_cache;

// Maps all traced paths and all their providers to vector of tracepoints
// on each provider
static std::unordered_map<std::string,
                          std::unordered_map<std::string, usdt_probe_list>>
    usdt_provider_cache;

// Maps a pid to a set of paths for its probes
static std::unordered_map<int, std::unordered_set<std::string>>
    usdt_pid_to_paths_cache;

// Used as a temporary buffer, during read_probes_for_pid to maintain
// current tracepoint paths for the current pid
static std::unordered_set<std::string> current_pid_paths;

static void usdt_probe_each(struct bcc_usdt *usdt_probe)
{
  usdt_provider_cache[usdt_probe->bin_path][usdt_probe->provider].emplace_back(
      usdt_probe_entry{
          .path = usdt_probe->bin_path,
          .provider = usdt_probe->provider,
          .name = usdt_probe->name,
          .semaphore_offset = usdt_probe->semaphore_offset,
          .num_locations = usdt_probe->num_locations,
      });
  current_pid_paths.emplace(usdt_probe->bin_path);
}

// Move the current pid paths onto the pid_to_paths_cache, and clear
// current_pid_paths.
static void cache_current_pid_paths(int pid)
{
  usdt_pid_to_paths_cache[pid].merge(current_pid_paths);
  current_pid_paths.clear();
}

std::optional<usdt_probe_entry> USDTHelper::find(std::optional<int> pid,
                                                 const std::string &target,
                                                 const std::string &provider,
                                                 const std::string &name)
{
  usdt_probe_list probes;
  if (pid.has_value()) {
    read_probes_for_pid(*pid);
    for (auto const &path : usdt_pid_to_paths_cache[*pid]) {
      probes.insert(probes.end(),
                    usdt_provider_cache[path][provider].begin(),
                    usdt_provider_cache[path][provider].end());
    }
  } else {
    read_probes_for_path(target);
    probes = usdt_provider_cache[target][provider];
  }

  auto it = std::ranges::find_if(probes,

                                 [&name](const usdt_probe_entry &e) {
                                   return e.name == name;
                                 });
  if (it != probes.end()) {
    return *it;
  } else {
    return std::nullopt;
  }
}

usdt_probe_list USDTHelper::probes_for_pid(int pid, bool print_error)
{
  read_probes_for_pid(pid, print_error);

  usdt_probe_list probes;
  for (auto const &path : usdt_pid_to_paths_cache[pid]) {
    for (auto const &usdt_probes : usdt_provider_cache[path]) {
      probes.insert(probes.end(),
                    usdt_probes.second.begin(),
                    usdt_probes.second.end());
    }
  }
  return probes;
}

usdt_probe_list USDTHelper::probes_for_all_pids()
{
  usdt_probe_list probes;
  for (int pid : util::get_all_running_pids()) {
    for (auto &probe : probes_for_pid(pid, false)) {
      probes.push_back(std::move(probe));
    }
  }
  return probes;
}

usdt_probe_list USDTHelper::probes_for_path(const std::string &path)
{
  read_probes_for_path(path);

  usdt_probe_list probes;
  for (auto const &usdt_probes : usdt_provider_cache[path]) {
    probes.insert(probes.end(),
                  usdt_probes.second.begin(),
                  usdt_probes.second.end());
  }
  return probes;
}

void USDTHelper::read_probes_for_pid(int pid, bool print_error)
{
  if (pid_cache.contains(pid))
    return;

  void *ctx = bcc_usdt_new_frompid(pid, nullptr);
  if (ctx == nullptr) {
    if (print_error) {
      LOG(ERROR) << "failed to initialize usdt context for pid: " << pid;

      if (kill(pid, 0) == -1 && errno == ESRCH)
        LOG(ERROR) << "hint: process not running";
    }

    return;
  }
  bcc_usdt_foreach(ctx, usdt_probe_each);
  bcc_usdt_close(ctx);
  cache_current_pid_paths(pid);

  pid_cache.emplace(pid);
}

void USDTHelper::read_probes_for_path(const std::string &path)
{
  if (path_cache.contains(path))
    return;

  void *ctx = bcc_usdt_new_frompath(path.c_str());
  if (ctx == nullptr) {
    LOG(ERROR) << "failed to initialize usdt context for path " << path;
    return;
  }
  bcc_usdt_foreach(ctx, usdt_probe_each);
  bcc_usdt_close(ctx);

  path_cache.emplace(path);
}

} // namespace bpftrace
