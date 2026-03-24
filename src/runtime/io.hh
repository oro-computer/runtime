#ifndef ORO_RUNTIME_IO_H
#define ORO_RUNTIME_IO_H

#include <iostream>

#include "platform.hh"
#include "env.hh"

namespace oro::runtime::io {
  void write (const String& input, bool isErrorOutput = false);
}
#endif
