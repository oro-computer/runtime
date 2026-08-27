#include "tests.hh"
#include "src/runtime/env.hh"

#include <cstdlib>

namespace {
  struct EnvSnapshot {
    oro::runtime::String name;
    oro::runtime::String value;
    bool existed = false;
  };

  EnvSnapshot snapshotEnv (const char* name) {
    EnvSnapshot snapshot;
    snapshot.name = name;
    const auto current = std::getenv(name);
    snapshot.existed = current != nullptr;
    snapshot.value = current ? oro::runtime::String(current) : oro::runtime::String("");
    return snapshot;
  }

  void restoreEnv (const EnvSnapshot& snapshot) {
#if ORO_RUNTIME_PLATFORM_WINDOWS
    if (!snapshot.existed) {
      _putenv((snapshot.name + "=").c_str());
    } else {
      _putenv((snapshot.name + "=" + snapshot.value).c_str());
    }
#else
    if (!snapshot.existed) {
      unsetenv(snapshot.name.c_str());
    } else {
      setenv(snapshot.name.c_str(), snapshot.value.c_str(), 1);
    }
#endif
  }
}

namespace oro::Tests {
  void env (Harness& t) {
    t.test("oro::runtime::env::get()", [](auto t) {
        const auto TEST_INJECTED_VARIABLE = runtime::env::get("TEST_INJECTED_VARIABLE");
        const auto HOME = runtime::env::get("HOME");
        t.equals(
          TEST_INJECTED_VARIABLE,
          "TEST_INJECTED_VARIABLE",
          "TEST_INJECTED_VARIABLE env var is set"
        );

        t.assert(HOME, "HOME env var is set");
    });


    t.test("oro::runtime::env::set()", [](auto t) {
        const auto name = "ORO_TEST_ENV_ROUND_TRIP";
        const auto snapshot = snapshotEnv(name);

        runtime::env::set(name, "ok");
        t.equals(
          runtime::env::get(name),
          "ok",
          "returns value set via oro::runtime::env::set"
        );

        restoreEnv(snapshot);
    });
  }
}
