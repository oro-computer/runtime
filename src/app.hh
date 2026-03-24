#ifndef ORO_APP_H
#define ORO_APP_H

#include "runtime.hh"

namespace oro::app {
  class App : public runtime::app::App {
    public:
      using runtime::app::App::App;
  };
}
#endif
