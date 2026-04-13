#ifndef ORO_RUNTIME_cWD_H
#define ORO_RUNTIME_cWD_H

#include "platform.hh"

namespace oro::runtime {
  using types::String;

  void setcwd (const String& value);
  const String getcwd_state_value ();
  const String getcwd ();
}
#endif
