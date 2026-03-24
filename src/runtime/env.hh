#ifndef ORO_RUNTIME_ENV_H
#define ORO_RUNTIME_ENV_H

#include "platform.hh"

namespace oro::runtime::env {
  bool has (const char* name);
  bool has (const String& name);

  String get (const char* name);
  String get (const String& name);
  String get (const String& name, const String& fallback);

  void set (const String& name, const String& value);
  void set (const char* name);
}
#endif
