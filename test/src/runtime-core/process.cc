#include "src/runtime/process.hh"
#include "tests.hh"
#include <future>

namespace oro::Tests {
  void process (Harness& t) {
  #if ORO_RUNTIME_PLATFORM_LINUX || ORO_RUNTIME_PLATFORM_MACOS
    t.test("Process: drain output after the child closes its pipes", [](auto t) {
      const String expectedStdout(8192, 'x');
      const String expectedStderr(4096, 'y');
      String stdoutText;
      String stderrText;
      std::promise<void> exited;
      auto exitFuture = exited.get_future();

      runtime::process::ProcessConfig config;
      config.rawOutput = true;
      config.bufferSize = 32;
      runtime::Process child(
        "printf '%s' '" + expectedStdout + "'; printf '%s' '" + expectedStderr + "' >&2",
        "",
        "",
        [&](const String& output) { stdoutText += output; },
        [&](const String& output) { stderrText += output; },
        [&](const String&) { exited.set_value(); },
        false,
        config
      );

      const auto pid = child.open();
      t.assert(pid > 0, "child started");
      if (pid <= 0) return;

      const auto ready = exitFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
      t.assert(ready, "child output drained within five seconds");
      if (!ready) child.kill();
      t.assert(child.wait() == 0, "child exited successfully");
      t.equals(stdoutText, expectedStdout, "all stdout bytes survive hangup");
      t.equals(stderrText, expectedStderr, "all stderr bytes survive hangup");
    });
  #endif
  }
}
