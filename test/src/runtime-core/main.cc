#include <oro/extension.h>
#include "tests.hh"

static bool initialize (oapi_context_t* context, const void *data) {
  oro::Tests::Harness harness;
  return harness.run("runtime-core-tests", [](auto t) {
    t.run(oro::Tests::codec);
    t.run(oro::Tests::config);
    t.run(oro::Tests::env);
    t.run(oro::Tests::ini);
    t.run(oro::Tests::json);
    t.run(oro::Tests::mcp);
    t.run(oro::Tests::toml);
    t.run(oro::Tests::platform);
    t.run(oro::Tests::preload);
    t.run(oro::Tests::process);
    t.run(oro::Tests::state_manager);
    t.run(oro::Tests::string);
    t.run(oro::Tests::version);
    t.run(oro::Tests::restart);
    t.run(oro::Tests::tar);
    t.run(oro::Tests::webview_tls_pins);
  });
}

ORO_RUNTIME_REGISTER_EXTENSION("runtime-core-tests", initialize);
