#ifndef ORO_RUNTIME_NATIVE_EXECUTION_H
#define ORO_RUNTIME_NATIVE_EXECUTION_H

#include "records.hh"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

namespace oro::runtime::native {
  struct ExecutionTask {
    std::string id;
    std::string kind;
    std::string authorityRef;
    std::string accountRef;
    std::string targetRef;
    std::string operationRef;
    bool operator==(const ExecutionTask&) const = default;
  };

  struct ExecutionStatus {
    ExecutionTask task;
    std::string generation;
    std::string phase;
    std::string outcome;
    int64_t updatedAt = 0;
    int64_t durationMs = 0;
  };

  enum class ExecutionOutcome { Completed, Unauthorized, Unknown };

  /** An event registration; destruction waits for an executing listener to return. */
  class CancellationSubscription {
   public:
    CancellationSubscription ();
    ~CancellationSubscription ();
    CancellationSubscription (CancellationSubscription&&) noexcept;
    CancellationSubscription& operator= (CancellationSubscription&&) noexcept;
    CancellationSubscription (const CancellationSubscription&) = delete;
    CancellationSubscription& operator= (const CancellationSubscription&) = delete;
    void reset ();

   private:
    friend class ExecutionCancellation;
    struct State;
    std::unique_ptr<State> state;
  };

  /** Cooperative stop/deadline signal. Copies share state; cancellation never frees a worker slot. */
  class ExecutionCancellation {
   public:
    explicit ExecutionCancellation (int64_t durationMs = 0);
    bool cancelled () const;
    void cancel ();
    void expireAfter (int64_t durationMs);
    /** At most 32 listeners/signal and 256 observed signals/process. Delivery uses shared SDK workers. */
    CancellationSubscription onCancel (std::function<void ()>) const;

   private:
    struct State;
    std::shared_ptr<State> state;
  };

  /**
   * Bounded, native-only controller. Callers provide an exclusive process lease
   * for the journal and an OS admission callback before using start(). Native
   * executors own authorization and remote-outcome verification at each run.
   */
  class ExecutionController {
  public:
    using Executor = std::function<ExecutionOutcome(const ExecutionTask&, const ExecutionCancellation&)>;
    using Admission = std::function<bool(const ExecutionStatus&)>;
    using Finished = std::function<void(const ExecutionStatus&)>;

    ExecutionController(RecordStore, bool continuous, std::shared_ptr<void> exclusiveLease, Admission,
                        Finished = nullptr);
    ~ExecutionController();
    ExecutionController(const ExecutionController&) = delete;
    ExecutionController& operator=(const ExecutionController&) = delete;

    void registerExecutor (const std::string& kind, Executor);
    std::vector<std::string> registeredKinds () const;
    ExecutionStatus start (const ExecutionTask&, bool userInitiated, int64_t durationMs = 900000);
    ExecutionStatus status (const std::string& id);
    std::vector<ExecutionStatus> list ();
    ExecutionStatus stop (const std::string& id, const std::string& generation);
    void release (const std::string& id, const std::string& generation);
    void shutdown ();
    /** Wait for cooperative cleanup before destroying resources captured by executors. */
    void drain ();
    bool busy () const;
    /** Signal a generation without waiting for storage or a native request queue. */
    std::function<void(const std::string&)> cancellationHandler () const;

  private:
    struct State;
    std::shared_ptr<State> state;
  };
}  // namespace oro::runtime::native
#endif
