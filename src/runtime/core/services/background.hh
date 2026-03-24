#ifndef ORO_RUNTIME_CORE_SERVICES_BACKGROUND_H
#define ORO_RUNTIME_CORE_SERVICES_BACKGROUND_H

#include "../../core.hh"
#include "../../background/options.hh"

namespace oro::runtime::core::services {
  class Background : public core::Service {
    public:
      Background (const Options&, const background::Options&);

      bool start () override;
      bool stop () override;

    private:
      const background::Options& config;
  };
}
#endif
