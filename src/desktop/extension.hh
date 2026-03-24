#ifndef ORO_RUNTIME_DESKTOP_EXTENSION_H
#define ORO_RUNTIME_DESKTOP_EXTENSION_H

#include "../runtime/platform.hh"

namespace oro::desktop {
  struct WebExtensionContext {
    struct ConfigData {
      char* bytes = nullptr;
      size_t size = 0;
      int format = 0;
    };

    ConfigData config;
  };
}

#endif
