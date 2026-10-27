#include <oro/extension.h>
#include "runtime/native/linux_platform.hh"

static bool initialize (oapi_context_t* context, const void*) {
  try {
    const auto backend = oro::runtime::native::LinuxPlatform::fromExtension(context);
    backend->registerExtension("public.metadata", [](const auto& action, const auto&, const auto& cancellation) -> oro::runtime::JSON::Any {
      if (action != "read") throw oro::runtime::native::StateError("EINVAL");
      if (cancellation.cancelled()) throw oro::runtime::native::StateError("ABORT_ERR");
      return oro::runtime::JSON::Object::Entries {{"public", true}};
    });
    return true;
  } catch (...) { return false; }
}

ORO_RUNTIME_REGISTER_EXTENSION("probe", initialize, nullptr, "1.0.0", "Public compiled native registration")
