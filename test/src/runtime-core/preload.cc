#include "tests.hh"
#include "src/runtime/webview/preload.hh"

namespace oro::Tests {
  void preload (Harness& t) {
    const auto preload = runtime::webview::Preload::compile({});
    t.assert(!preload.str().empty(), "Preload::compile() returns a non-empty string");
  }
}
