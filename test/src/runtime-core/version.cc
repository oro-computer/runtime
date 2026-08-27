#include "tests.hh"
#include "src/runtime/version.hh"

namespace oro::Tests {
  void version (Harness& t) {
    t.test("oro::{VERSION_FULL_STRING,VERSION_HASH_STRING,VERSION_STRING}", [](auto t) {
      t.assert(runtime::version::VERSION_FULL_STRING, "VERSION_FULL_STRING is defined");
      t.assert(runtime::version::VERSION_HASH_STRING, "VERSION_HASH_STRING is defined");
      t.assert(runtime::version::VERSION_STRING, "VERSION_STRING is defined");
    });
  }
}
