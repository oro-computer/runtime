#ifndef ORO_RUNTIME_NATIVE_PROTECTED_KEYS_H
#define ORO_RUNTIME_NATIVE_PROTECTED_KEYS_H

#include <oro/platform.h>
#if ORO_RUNTIME_PLATFORM_LINUX
#include "execution.hh"
#include "pkcs11_backend.hh"
#include "../json.hh"
#include <gtk/gtk.h>

namespace oro::runtime::native {
  /** Private-key authentication backed by a trusted TPM2 or hardware PKCS #11 module. */
  class ProtectedKeys {
  public:
    using PinPrompt = std::function<RecordBytes(const Pkcs11Token&, const std::shared_ptr<ExecutionCancellation>&)>;
    ProtectedKeys(RecordStore, Pkcs11Options, std::string application, std::shared_ptr<void> exclusiveLease,
                  PinPrompt = nullptr);
    ~ProtectedKeys();
    static JSON::Object status (const Pkcs11Options&, const std::string& policy, bool enabled, bool busy = false);
    JSON::Any request (const std::string& action, const JSON::Object&, GtkWindow*,
                       std::shared_ptr<ExecutionCancellation>);
    bool busy () const;
    void shutdown ();
    void drain ();

  private:
    struct State;
    std::shared_ptr<State> state;
  };
}  // namespace oro::runtime::native
#endif
#endif
