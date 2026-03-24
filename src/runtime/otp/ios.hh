#ifndef ORO_RUNTIME_OTP_IOS_H
#define ORO_RUNTIME_OTP_IOS_H

#include "../platform/types.hh"
#include <cstdint>

#if ORO_RUNTIME_PLATFORM_IOS
namespace oro::runtime::core::services {
  class OTP;
  bool startIOSOTPRequest(uint64_t id, OTP& service, uint64_t timeoutMs, const oro::runtime::types::String& host);
  void stopIOSOTPRequest(uint64_t id);
}
#endif

#endif
