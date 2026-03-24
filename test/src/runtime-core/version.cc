#include "tests.hh"

namespace oro::Tests {
  void version (Harness& t) {
    t.test("oro::{VERSION_FULL_STRING,VERSION_HASH_STRING,VERSION_STRING}", [](auto t) {
      t.assert(VERSION_FULL_STRING, "VERSION_FULL_STRING is defined");
      t.assert(VERSION_HASH_STRING, "VERSION_HASH_STRING is defined");
      t.assert(VERSION_STRING, "VERSION_STRING is defined");
    });
  }
}
