#ifndef ORO_RUNTIME_NATIVE_EXECUTION_CHILDREN_H
#define ORO_RUNTIME_NATIVE_EXECUTION_CHILDREN_H

#include "execution.hh"

namespace oro::runtime::native {
  class CallbackRegistry;
  struct ChildExecutionStatus {
    uint64_t handle = 0;
    bool finished = false;
    std::string error;
  };

  /** Invocation-owned children. Handles are never reused; stop does not release capacity before join. */
  class ExecutionGroup {
   public:
    using Work = std::function<void (const ExecutionCancellation&)>;
    ~ExecutionGroup ();
    uint64_t start (Work);
    ChildExecutionStatus status (uint64_t) const;
    void cancel (uint64_t);
    void cancel ();
    ChildExecutionStatus join (uint64_t);
    /** Cancels and joins every remaining child. An uncooperative child keeps this call waiting. */
    void close ();

   private:
    friend class CallbackRegistry;
    struct Capacity;
    struct State;
    static std::shared_ptr<Capacity> capacity ();
    ExecutionGroup (const ExecutionCancellation&, std::shared_ptr<Capacity>, size_t limit,
                    std::function<void (const ExecutionCancellation&)> enter, std::function<void ()> leave);
    std::shared_ptr<State> state;
    CancellationSubscription parent;
  };
}  // namespace oro::runtime::native
#endif
