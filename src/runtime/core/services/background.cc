#include "background.hh"
#include "../../debug.hh"

namespace oro::runtime::core::services {
  Background::Background (
    const Options& options,
    const background::Options& config
  )
    : core::Service(options),
      config(config) {
    if (!config.enabled) {
      this->enabled.store(false, std::memory_order_relaxed);
    }
  }

  bool Background::start () {
    if (!this->enabled.load(std::memory_order_relaxed)) {
      return true;
    }

  #if defined(DEBUG)
    debug("BackgroundService::start(): services=%zu", this->config.services.size());
  #endif

    return true;
  }

  bool Background::stop () {
    if (!this->enabled.load(std::memory_order_relaxed)) {
      return true;
    }

    return true;
  }
}
