#ifndef ORO_RUNTIME_NATIVE_DESKTOP_LAUNCHER_H
#define ORO_RUNTIME_NATIVE_DESKTOP_LAUNCHER_H

#include <oro/platform.h>
#if ORO_RUNTIME_PLATFORM_LINUX
#include "records.hh"
#include "../json.hh"

namespace oro::runtime::native {
  /** Desktop actions contain opaque routing IDs; authorization is always application-owned. */
  class DesktopLauncher {
  public:
    DesktopLauncher(RecordStore, std::string bundle, std::string title, std::string protocol);
    JSON::Object replace (const JSON::Array&);
    JSON::Array responses ();
    void acknowledge (const std::string& receiptId);
    bool receive (const std::string& uri);

  private:
    RecordStore store;
    std::string bundle;
    std::string title;
    std::string protocol;
    std::string executable;
    void publish (const JSON::Object&);
  };
}  // namespace oro::runtime::native
#endif
#endif
