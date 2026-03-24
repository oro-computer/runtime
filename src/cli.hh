#ifndef ORO_CLI_H
#define ORO_CLI_H

#include "runtime.hh"

#include <signal.h>

namespace oro::cli {
  inline void notify (int signal) {
  #if !defined(_WIN32)
    static auto ppid = runtime::env::get("ORO_CLI_PID");
    static auto pid = ppid.size() > 0 ? std::stoi(ppid) : 0;
    if (pid > 0) {
      kill(pid, signal);
    }
  #endif
  }

  inline void notify () {
  #if !defined(_WIN32)
    return notify(SIGUSR1);
  #endif
  }
}
#endif
