#ifndef ORO_RUNTIME_CORE_SERVICES_DNS_H
#define ORO_RUNTIME_CORE_SERVICES_DNS_H

#include "../../core.hh"

namespace oro::runtime::core::services {
  class DNS : public core::Service {
    public:
      struct LookupOptions {
        String hostname;
        int family = 0;
        bool all = false;
        bool verbatim = false;
        int hints = 0;
      };

      DNS (const Options& options)
        : core::Service(options)
      {}

      void lookup (const String&, const LookupOptions&, const Callback) const;
  };
}
#endif
