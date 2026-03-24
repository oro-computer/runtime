#include "tests.hh"

namespace oro::Tests {
  void preload (Harness& t) {
    t.assert(createPreload(WindowOptions {}), "createPreload() returns non-empty string");;
  }
}
