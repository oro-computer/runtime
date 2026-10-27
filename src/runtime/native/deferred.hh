#ifndef ORO_RUNTIME_NATIVE_DEFERRED_H
#define ORO_RUNTIME_NATIVE_DEFERRED_H

#include "execution.hh"
#include "../json.hh"
#include <set>

namespace oro::runtime::native {
  enum class DeferredOutcome { Completed, Retry, Conflict, Unauthorized, Unknown };
  struct DeferredExecutor {
    std::function<bool(const ExecutionTask&, const ExecutionCancellation&)> authorize;
    std::function<DeferredOutcome(const ExecutionTask&, const ExecutionCancellation&)> execute;
  };

  /** Durable scheduling while this native host runs; interrupted tasks require explicit resume. */
  class DeferredController {
  public:
    using Clock = std::function<int64_t()>;
    using Constraints = std::function<bool(const std::string& network, bool charging, bool batteryNotLow)>;
    DeferredController(RecordStore, std::shared_ptr<void> exclusiveLease, Constraints, Clock = nullptr);
    ~DeferredController();
    void registerRead (const std::string& kind, DeferredExecutor);
    void registerMaintenance (const std::string& kind, const std::set<std::string>& operations, bool retrySafe,
                              DeferredExecutor);
    JSON::Any request (const std::string& action, const JSON::Object&);
    void wake ();
    void shutdown ();

  private:
    struct State;
    std::shared_ptr<State> state;
  };
}  // namespace oro::runtime::native
#endif
