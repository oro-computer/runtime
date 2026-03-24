#include "tests.hh"

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
    t.test("oro::Env::get()", [](auto t) {
        const auto TEST_INJECTED_VARIABLE = oro::Env::get("TEST_INJECTED_VARIABLE");
        const auto HOME = oro::Env::get("HOME");
        t.equals(
          TEST_INJECTED_VARIABLE,
          "TEST_INJECTED_VARIABLE",
          "TEST_INJECTED_VARIABLE env var is set"
        );

        t.assert(HOME, "HOME env var is set");
    });


    t.test("oro::Env::set()", [](auto t) {
        const auto name = "ORO_TEST_ENV_ROUND_TRIP";
        const auto snapshot = snapshotEnv(name);

        oro::Env::set(name, "ok");
        t.equals(
          oro::Env::get(name),
          "ok",
          "returns value set via oro::Env::set"
        );

        restoreEnv(snapshot);
    });
  }
}
