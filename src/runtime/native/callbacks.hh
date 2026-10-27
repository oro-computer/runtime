#ifndef ORO_RUNTIME_NATIVE_CALLBACKS_H
#define ORO_RUNTIME_NATIVE_CALLBACKS_H

#include "execution.hh"
#include "execution_children.hh"
#include <set>
#include <span>

namespace oro::runtime::native {
  enum class CallbackType {
    Foreground = 0,
    Continuous = 1,
    DeferredRead = 2,
    Maintenance = 3,
    Push = 4,
    Extension = 5
  };
  enum class CallbackOutcome { Completed, Retry, Conflict, Unauthorized, Unknown };
  /** An authorized native resource. Its destructor releases credentials/connections on its owned worker. */
  class CallbackExecution {
   public:
    virtual ~CallbackExecution () = default;
    virtual CallbackOutcome run (const ExecutionCancellation&) = 0;
  };
  struct CallbackDescriptor {
    CallbackType type;
    std::string kind;
    std::set<std::string> operations;
  };
  struct CallbackPushHandler {
    std::function<bool (const ExecutionTask&, const ExecutionCancellation&)> authorize;
    std::function<bool (const ExecutionTask&, std::span<const unsigned char>, const ExecutionCancellation&)> message;
    /** Enrollment JSON is native-only sensitive material, never a renderer diagnostic. */
    std::function<bool (const ExecutionTask&, const std::string&, const ExecutionCancellation&)> enrollment;
  };
  /** Compiled registrations and bounded opaque invocation handles; no dynamic module or executable lookup. */
  class CallbackRegistry {
   public:
    using Authorizer =
        std::function<std::unique_ptr<CallbackExecution> (const ExecutionTask&, const ExecutionCancellation&)>;
    /** Extension payload/result are JSON text; the platform adapter validates their shape/size. */
    using Extension = std::function<std::string (const std::string&, const std::string&, const ExecutionCancellation&)>;
    CallbackRegistry ();
    virtual ~CallbackRegistry ();
    void registerExecution (CallbackType, const std::string& kind, Authorizer,
                            const std::set<std::string>& operations = {}, bool retrySafe = false);
    void registerPush (const std::string& kind, CallbackPushHandler);
    void registerExtension (const std::string& name, Extension);
    /** Freezes registrations at Application startup, before any receiver/job can dispatch. */
    std::vector<CallbackDescriptor> seal ();
    uint64_t begin (CallbackType, const ExecutionTask&);
    bool authorize (uint64_t);
    CallbackOutcome run (uint64_t);
    bool push (uint64_t, const std::string& stage, std::span<const unsigned char> bytes = {},
               const std::string& enrollment = "");
    std::string extension (uint64_t, const std::string& action, const std::string& payload);
    void cancel (uint64_t);
    /** Throws EBUSY while a worker is executing; cancellation never destroys a running callback. */
    void close (uint64_t);
    virtual void shutdown ();
    virtual void drain ();
    /** True only while this thread owns an active callback or its cleanup. */
    bool ownsInvocationThread () const;
    /** Create only inside this invocation's top-level callback; children share eight SDK slots. */
    std::shared_ptr<ExecutionGroup> children (const ExecutionCancellation&, size_t limit = 4);

   protected:
    /** Scoped platform bindings may retain this guard; it grants no ownership to unrelated threads. */
    std::function<bool ()> invocationGuard () const;
    ExecutionCancellation invocationSignal () const;

   private:
    struct State;
    std::shared_ptr<State> state;
  };
}  // namespace oro::runtime::native
#endif
