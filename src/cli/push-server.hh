#ifndef ORO_CLI_PUSH_SERVER_H
#define ORO_CLI_PUSH_SERVER_H

#include <uv.h>
#include <array>
#include <csignal>
#include <stdexcept>
#include <string>
#include <vector>

namespace oro::cli {
  // Inherit stdio without a shell and forward signals even when only the CLI
  // PID receives them. The helper owns the server's graceful shutdown.
  inline int runPushServer (const std::vector<std::string>& arguments) {
    struct State {
      uv_loop_t loop {};
      uv_process_t child {};
      std::array<uv_signal_t, 2> signals {};
      int status = 1;
#if defined(_WIN32)
      HANDLE job = nullptr;
      ~State () {
        if (job != nullptr) {
          CloseHandle(job);
        }
      }
#endif
    } state;

    int result = uv_loop_init(&state.loop);
    if (result != 0) {
      throw std::runtime_error(uv_strerror(result));
    }

    std::vector<char*> argv;
    for (const auto& argument : arguments) {
      argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    std::array<uv_stdio_container_t, 3> stdio {};
    for (int index = 0; index < 3; index++) {
      stdio[index].flags = UV_INHERIT_FD;
      stdio[index].data.fd = index;
    }

    uv_process_options_t options {};
    options.file = argv.front();
    options.args = argv.data();
    options.stdio_count = stdio.size();
    options.stdio = stdio.data();
    options.exit_cb = [](uv_process_t* process, int64_t status, int signal) {
      auto* state = static_cast<State*>(process->data);
      state->status = signal ? 128 + signal : static_cast<int>(status);
      uv_close(reinterpret_cast<uv_handle_t*>(process), nullptr);
      for (auto& watcher : state->signals) {
        uv_signal_stop(&watcher);
        uv_close(reinterpret_cast<uv_handle_t*>(&watcher), nullptr);
      }
    };
    state.child.data = &state;
    result = uv_spawn(&state.loop, &state.child, &options);
    if (result != 0) {
      uv_close(reinterpret_cast<uv_handle_t*>(&state.child), nullptr);
      uv_run(&state.loop, UV_RUN_DEFAULT);
      uv_loop_close(&state.loop);
      throw std::runtime_error("cannot start Node.js: " + std::string(uv_strerror(result)));
    }

#if defined(_WIN32)
    // Windows process signals terminate Node without running JS handlers. Keep
    // its descendants in a job so termination also closes the server listener.
    state.job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (state.job == nullptr ||
        !SetInformationJobObject(state.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(state.job, state.child.process_handle)) {
      // Initialize the watchers before running the process-exit callback.
      for (auto& watcher : state.signals) {
        uv_signal_init(&state.loop, &watcher);
      }
      uv_process_kill(&state.child, SIGKILL);
      uv_run(&state.loop, UV_RUN_DEFAULT);
      uv_loop_close(&state.loop);
      throw std::runtime_error("cannot contain push server processes in a Windows job");
    }
#endif

    const std::array<int, 2> signalNumbers { SIGINT, SIGTERM };
    for (size_t index = 0; index < state.signals.size(); index++) {
      auto& watcher = state.signals[index];
      uv_signal_init(&state.loop, &watcher);
      watcher.data = &state.child;
      uv_signal_start(&watcher, [](uv_signal_t* watcher, int signal) {
        uv_process_kill(static_cast<uv_process_t*>(watcher->data), signal);
      }, signalNumbers[index]);
    }
    uv_run(&state.loop, UV_RUN_DEFAULT);
    uv_loop_close(&state.loop);
    return state.status;
  }
}

#endif
