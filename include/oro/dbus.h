#ifndef ORO_RUNTIME_DBUS_H
#define ORO_RUNTIME_DBUS_H

#include "platform.h"

#ifndef ORO_RUNTIME_HAVE_DBUS
// Define ORO_RUNTIME_HAVE_DBUS=1 when building with libdbus headers/libraries available.
#  if defined(__has_include)
#    if __has_include(<dbus/dbus.h>)
#      define ORO_RUNTIME_HAVE_DBUS 1
#    else
#      define ORO_RUNTIME_HAVE_DBUS 0
#    endif
#  else
#    define ORO_RUNTIME_HAVE_DBUS 0
#  endif
#endif

#endif
