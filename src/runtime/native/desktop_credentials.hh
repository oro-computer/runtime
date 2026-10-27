#ifndef ORO_RUNTIME_NATIVE_DESKTOP_CREDENTIALS_H
#define ORO_RUNTIME_NATIVE_DESKTOP_CREDENTIALS_H

#include <oro/platform.h>
#if ORO_RUNTIME_PLATFORM_LINUX
#include "execution.hh"
#include <gtk/gtk.h>

namespace oro::runtime::native {
  struct DesktopPassword {
    std::string username;
    std::string password;
  };
  /** Explicit selection from Secret Service items associated with this application. */
  class DesktopCredentials {
  public:
    static bool available ();
    /** Called on a worker; the parent must remain referenced until this call returns. */
    static DesktopPassword select (const std::string& application, const std::string& username, GtkWindow* parent,
                                   const std::shared_ptr<ExecutionCancellation>&);
    /** UI-thread probe used to distinguish our modal from an external focus change. */
    static bool ownsPrompt (GtkWidget* parent);
  };
}  // namespace oro::runtime::native
#endif
#endif
