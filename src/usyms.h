#pragma once

#include <bcc/bcc_elf.h>
#include <bcc/bcc_syms.h>
#include <cstdint>
#include <map>
#include <string>

#include "util/symbols.h"

namespace bpftrace {

using util::elf_symbol;

class Config;

class Usyms {
public:
  Usyms(const Config& config);
  ~Usyms();

  Usyms(Usyms&) = delete;
  Usyms& operator=(const Usyms&) = delete;

  void cache(const std::string& elf_file);
  std::string resolve(uint64_t addr,
                      int32_t pid,
                      const std::string& pid_exe,
                      bool show_offset,
                      bool show_module);

private:
  const Config& config_;
  // note: exe_sym_ is used when layout is same for all instances of program
  std::map<std::string, std::pair<int, void*>> exe_sym_; // exe -> (pid, cache)
  std::map<int, void*> pid_sym_;                         // pid -> cache
  std::map<std::string, std::map<uintptr_t, elf_symbol, std::greater<>>>
      symbol_table_cache_;

  void cache_bcc(const std::string& elf_file);
  std::string resolve_bcc(uint64_t addr,
                          int32_t pid,
                          const std::string& pid_exe,
                          bool show_offset,
                          bool show_module);
  struct bcc_symbol_option& get_symbol_opts();
};

} // namespace bpftrace
