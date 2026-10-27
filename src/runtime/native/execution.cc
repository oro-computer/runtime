#include "execution.hh"

#include <sodium.h>

#include <condition_variable>
#include <mutex>
#include <thread>

namespace oro::runtime::native {
  namespace {
    using Fields = std::map<std::string, std::string>;

    int64_t now () {
      return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
    }

    bool identifier (const std::string& value, bool kind = false) {
      if (value.empty() || value.size() > (kind ? 64 : 128))
        return false;
      if (kind && (value.front() < 'a' || value.front() > 'z'))
        return false;
      for (const unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
            (!kind && ((c >= 'A' && c <= 'Z') || c == '_')) || (kind && c == '.'))
          continue;
        return false;
      }
      return true;
    }

    std::string generation () {
      std::array<unsigned char, 16> bytes;
      randombytes_buf(bytes.data(), bytes.size());
      bytes[6] = (bytes[6] & 0x0f) | 0x40;
      bytes[8] = (bytes[8] & 0x3f) | 0x80;
      constexpr char hex[] = "0123456789abcdef";
      std::string result;
      for (size_t i = 0; i < bytes.size(); i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
          result += '-';
        result += hex[bytes[i] >> 4];
        result += hex[bytes[i] & 15];
      }
      return result;
    }

    void field (RecordBytes& output, const std::string& value) {
      if (value.size() > 4096)
        throw StateError("E2BIG");
      const auto size = static_cast<uint32_t>(value.size());
      for (int i = 0; i < 4; i++)
        output.push_back((size >> (i * 8)) & 255);
      output.insert(output.end(), value.begin(), value.end());
    }

    std::string field (const RecordBytes& input, size_t& offset) {
      if (offset + 4 > input.size())
        throw StateError("EBADMSG");
      uint32_t size = 0;
      for (int i = 0; i < 4; i++)
        size |= static_cast<uint32_t>(input[offset++]) << (i * 8);
      if (size > 4096 || size > input.size() - offset)
        throw StateError("EBADMSG");
      const std::string value(input.begin() + offset, input.begin() + offset + size);
      offset += size;
      return value;
    }

    RecordBytes encode (const ExecutionStatus& value, const std::string& process) {
      RecordBytes output;
      for (const auto& part :
           {std::string("oro.execution.v1"), value.task.id, value.task.kind, value.task.authorityRef,
            value.task.accountRef, value.task.targetRef, value.task.operationRef, value.generation, value.phase,
            value.outcome, std::to_string(value.updatedAt), std::to_string(value.durationMs), process})
        field(output, part);
      return output;
    }

    struct Entry {
      ExecutionStatus status;
      std::string process;
    };

    Entry decode (const RecordBytes& value) {
      size_t offset = 0;
      if (field(value, offset) != "oro.execution.v1")
        throw StateError("EBADMSG");
      Entry entry;
      auto& status = entry.status;
      status.task = {field(value, offset), field(value, offset), field(value, offset),
                     field(value, offset), field(value, offset), field(value, offset)};
      status.generation = field(value, offset);
      status.phase = field(value, offset);
      status.outcome = field(value, offset);
      try {
        size_t consumed = 0;
        const auto time = field(value, offset);
        status.updatedAt = std::stoll(time, &consumed);
        if (consumed != time.size() || status.updatedAt < 0)
          throw StateError("EBADMSG");
        const auto duration = field(value, offset);
        status.durationMs = std::stoll(duration, &consumed);
        if (consumed != duration.size() || status.durationMs < 0 || status.durationMs > 1800000)
          throw StateError("EBADMSG");
      } catch (...) {
        throw StateError("EBADMSG");
      }
      entry.process = field(value, offset);
      if (offset != value.size() || status.generation.size() != 36 || entry.process.size() != 36 ||
          !identifier(status.task.id) || !identifier(status.task.kind, true) || !identifier(status.task.authorityRef) ||
          !identifier(status.task.accountRef) || !identifier(status.task.targetRef) ||
          !identifier(status.task.operationRef) ||
          (status.phase != "starting" && status.phase != "running" && status.phase != "completed" &&
           status.phase != "unauthorized" && status.phase != "unknown" && status.phase != "rejected") ||
          (status.outcome != "unknown" && status.outcome != "completed" && status.outcome != "unauthorized" &&
           status.outcome != "not-started"))
        throw StateError("EBADMSG");
      return entry;
    }

    bool terminal (const ExecutionStatus& value) {
      return value.phase != "starting" && value.phase != "running";
    }
  }  // namespace

  struct ExecutionController::State {
    RecordStore store;
    bool continuous;
    std::shared_ptr<void> lease;
    Admission admission;
    Finished finished;
    std::string process = generation();
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::map<std::string, Executor> executors;
    struct Running {
      ExecutionStatus status;
      std::string interruptGeneration;
      std::thread::id worker;
      ExecutionCancellation cancellation;
      bool admissionPending = true;
      std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    };
    std::shared_ptr<Running> active;
    std::shared_ptr<Running> interruptible;
    mutable std::mutex interruptionMutex;
    size_t workers = 0;
    std::thread::id workerThread;
    bool closing = false;

    State(RecordStore store, bool continuous, std::shared_ptr<void> lease, Admission admission, Finished finished)
        : store(std::move(store)),
          continuous(continuous),
          lease(std::move(lease)),
          admission(std::move(admission)),
          finished(std::move(finished)) {
    }

    std::shared_ptr<Running> interruptionTarget () const {
      std::lock_guard lock(this->interruptionMutex);
      return this->interruptible;
    }

    RecordSnapshot refreshed () {
      auto snapshot = this->store.read();
      RecordChanges changes;
      for (const auto& [key, bytes] : snapshot.records) {
        auto entry = decode(bytes);
        if (entry.status.task.id != key)
          throw StateError("EBADMSG");
        if (!terminal(entry.status) && (entry.process != this->process || !this->active ||
                                        this->active->status.generation != entry.status.generation)) {
          entry.status.phase = "unknown";
          entry.status.outcome = "unknown";
          entry.status.updatedAt = now();
          changes[key] = encode(entry.status, this->process);
        }
      }
      if (!changes.empty())
        snapshot = this->store.apply(snapshot.revision, changes);
      return snapshot;
    }

    void persist (const ExecutionStatus& status) {
      auto snapshot = this->store.read();
      const auto found = snapshot.records.find(status.task.id);
      if (found == snapshot.records.end() || decode(found->second).status.generation != status.generation)
        throw StateError("ESTALE");
      this->store.apply(snapshot.revision, {{status.task.id, encode(status, this->process)}});
    }

    void notify (const ExecutionStatus& value) {
      if (this->finished) {
        try {
          this->finished(value);
        } catch (...) {
        }
      }
    }
  };

  ExecutionController::ExecutionController(RecordStore store, bool continuous, std::shared_ptr<void> lease,
                                           Admission admission, Finished finished) {
    if (!lease || !admission || sodium_init() < 0)
      throw StateError("EINVAL");
    this->state = std::make_shared<State>(std::move(store), continuous, std::move(lease), std::move(admission),
                                          std::move(finished));
  }

  ExecutionController::~ExecutionController() {
    try {
      this->shutdown();
    } catch (...) {
    }
  }

  void ExecutionController::registerExecutor(const std::string& kind, Executor executor) {
    std::lock_guard lock(this->state->mutex);
    if (!identifier(kind, true) || !executor)
      throw StateError("EINVAL");
    if (this->state->closing)
      throw StateError("ESHUTDOWN");
    if (this->state->executors.contains(kind))
      throw StateError("EEXIST");
    if (this->state->executors.size() >= 16)
      throw StateError("ENOSPC");
    this->state->executors[kind] = std::move(executor);
  }

  std::vector<std::string> ExecutionController::registeredKinds() const {
    std::lock_guard lock(this->state->mutex);
    std::vector<std::string> result;
    for (const auto& [kind, executor] : this->state->executors)
      result.push_back(kind);
    return result;
  }

  ExecutionStatus ExecutionController::start(const ExecutionTask& task, bool userInitiated, int64_t duration) {
    auto state = this->state;
    if (!userInitiated || !identifier(task.id) || !identifier(task.kind, true) || !identifier(task.authorityRef) ||
        !identifier(task.accountRef) || !identifier(task.targetRef) || !identifier(task.operationRef) ||
        (state->continuous ? duration != 0 : duration < 1000 || duration > 1800000))
      throw StateError("EINVAL");
    Executor executor;
    ExecutionStatus admittedStatus;
    bool rejected = false;
    auto running = std::make_shared<State::Running>();
    {
      std::lock_guard lock(state->mutex);
      if (state->closing)
        throw StateError("ESHUTDOWN");
      const auto snapshot = state->refreshed();
      if (const auto found = snapshot.records.find(task.id); found != snapshot.records.end()) {
        const auto current = decode(found->second).status;
        if (current.task != task || current.durationMs != duration)
          throw StateError("EEXIST");
        return current;
      }
      if (state->active || state->workers)
        throw StateError("EBUSY");
      if (snapshot.records.size() >= 16)
        throw StateError("ENOSPC");
      const auto found = state->executors.find(task.kind);
      if (found == state->executors.end())
        throw StateError("ENOSYS");
      executor = found->second;
      running->status = {task, generation(), "starting", "unknown", now(), duration};
      running->interruptGeneration = running->status.generation;
      running->cancellation.expireAfter(duration);
      state->store.apply(snapshot.revision, {{task.id, encode(running->status, state->process)}});
      state->active = running;
      {
        std::lock_guard interrupt(state->interruptionMutex);
        state->interruptible = running;
      }
      admittedStatus = running->status;
    }
    struct AdmissionCleanup {
      std::function<void()> callback;
      ~AdmissionCleanup () { if (callback) callback(); }
    } cleanup{[state, admittedStatus] {
      auto unknown = admittedStatus;
      unknown.phase = "unknown";
      unknown.outcome = "unknown";
      unknown.updatedAt = now();
      state->notify(unknown);
    }};
    bool admitted = false;
    try {
      admitted = state->admission(admittedStatus);
    } catch (...) {
    }
    {
      std::lock_guard lock(state->mutex);
      running->admissionPending = false;
      if (!admitted || state->closing || running->cancellation.cancelled()) {
        running->status.phase = "rejected";
        running->status.outcome = "not-started";
        running->status.updatedAt = now();
        try {
          state->persist(running->status);
        } catch (...) {
          state->active.reset();
          throw;
        }
        state->active.reset();
        state->condition.notify_all();
        rejected = true;
      } else {
        running->status.phase = "running";
        running->status.updatedAt = now();
        try {
          state->persist(running->status);
        } catch (...) {
          state->active.reset();
          throw;
        }
      }
      admittedStatus = running->status;
    }
    if (rejected) {
      cleanup.callback = nullptr;
      state->notify(admittedStatus);
      return admittedStatus;
    }
    bool workerLaunched = false;
    try {
      {
        std::lock_guard lock(state->mutex);
        state->workers = 1;
      }
      std::thread([state, running, executor, task] {
        {
          std::lock_guard lock(state->mutex);
          running->worker = std::this_thread::get_id();
          state->workerThread = running->worker;
        }
        ExecutionOutcome outcome = ExecutionOutcome::Unknown;
        try {
          if (!running->cancellation.cancelled())
            outcome = executor(task, running->cancellation);
        } catch (...) {
        }
        ExecutionStatus result;
        bool deliver = false;
        {
          std::lock_guard lock(state->mutex);
          result = running->status;
          if (!state->closing && !running->cancellation.cancelled()) {
            result.phase = outcome == ExecutionOutcome::Completed      ? "completed"
                           : outcome == ExecutionOutcome::Unauthorized ? "unauthorized"
                                                                       : "unknown";
            result.outcome = result.phase;
          } else {
            result.phase = "unknown";
            result.outcome = "unknown";
          }
          result.updatedAt = now();
          if (!state->closing) {
            try {
              state->persist(result);
            } catch (...) {
              result.phase = "unknown";
              result.outcome = "unknown";
            }
          }
          if (state->active == running)
            state->active.reset();
          deliver = !state->closing;
          state->condition.notify_all();
        }
        if (deliver)
          state->notify(result);
        {
          std::lock_guard lock(state->mutex);
          state->workers = 0;
          state->condition.notify_all();
        }
      }).detach();
      workerLaunched = true;
      if (!state->continuous) {
        std::thread([state, running, duration] {
          std::unique_lock lock(state->mutex);
          if (state->condition.wait_until(lock, running->started + std::chrono::milliseconds(duration), [&] {
                return state->active != running || state->closing || running->status.phase != "running";
              }))
            return;
          running->cancellation.cancel();
          running->status.phase = "unknown";
          running->status.outcome = "unknown";
          running->status.updatedAt = now();
          try {
            state->persist(running->status);
          } catch (...) {
          }
          const auto status = running->status;
          lock.unlock();
          state->notify(status);
        }).detach();
      }
    } catch (...) {
      running->cancellation.cancel();
      // Thread admission can fail after the executor starts. Keep its slot
      // quarantined until that executor exits and report durable uncertainty.
      std::lock_guard lock(state->mutex);
      running->status.phase = workerLaunched ? "unknown" : "rejected";
      running->status.outcome = workerLaunched ? "unknown" : "not-started";
      running->status.updatedAt = now();
      if (!workerLaunched) {
        if (state->active == running)
          state->active.reset();
        state->workers = 0;
      }
      state->condition.notify_all();
      state->persist(running->status);
      throw StateError("EUNKNOWN");
    }
    cleanup.callback = nullptr;
    return admittedStatus;
  }

  std::vector<ExecutionStatus> ExecutionController::list() {
    std::lock_guard lock(this->state->mutex);
    const auto snapshot = this->state->refreshed();
    std::vector<ExecutionStatus> result;
    for (const auto& [key, bytes] : snapshot.records)
      result.push_back(decode(bytes).status);
    return result;
  }

  ExecutionStatus ExecutionController::status(const std::string& id) {
    if (!identifier(id))
      throw StateError("EINVAL");
    for (const auto& value : this->list())
      if (value.task.id == id)
        return value;
    throw StateError("ENOENT");
  }

  ExecutionStatus ExecutionController::stop(const std::string& id, const std::string& generation) {
    auto state = this->state;
    ExecutionStatus result;
    {
      std::lock_guard lock(state->mutex);
      const auto snapshot = state->refreshed();
      const auto found = snapshot.records.find(id);
      if (found == snapshot.records.end())
        throw StateError("ENOENT");
      result = decode(found->second).status;
      if (result.generation != generation)
        throw StateError("ESTALE");
      if (state->active && state->active->status.generation == generation) {
        state->active->cancellation.cancel();
        result.phase = state->active->admissionPending ? "rejected" : "unknown";
        result.outcome = state->active->admissionPending ? "not-started" : "unknown";
        result.updatedAt = now();
        state->active->status = result;
        state->persist(result);
        state->condition.notify_all();
      }
    }
    state->notify(result);
    return result;
  }

  void ExecutionController::release(const std::string& id, const std::string& generation) {
    std::lock_guard lock(this->state->mutex);
    const auto snapshot = this->state->refreshed();
    const auto found = snapshot.records.find(id);
    if (found == snapshot.records.end())
      throw StateError("ENOENT");
    const auto value = decode(found->second).status;
    if (value.generation != generation)
      throw StateError("ESTALE");
    if (!terminal(value) || (this->state->active && this->state->active->status.generation == generation) ||
        (this->state->workers && this->state->interruptionTarget()->interruptGeneration == generation))
      throw StateError("EBUSY");
    this->state->store.apply(snapshot.revision, {{id, std::nullopt}});
  }

  void ExecutionController::shutdown() {
    auto state = this->state;
    std::lock_guard lock(state->mutex);
    if (state->closing)
      return;
    state->closing = true;
    state->condition.notify_all();
    if (state->active) {
      state->active->cancellation.cancel();
      auto value = state->active->status;
      value.phase = "unknown";
      value.outcome = "unknown";
      value.updatedAt = now();
      state->persist(value);
    }
    state->condition.notify_all();
  }

  bool ExecutionController::busy() const {
    std::lock_guard lock(this->state->mutex);
    return this->state->active != nullptr || this->state->workers != 0;
  }

  void ExecutionController::drain() {
    std::unique_lock lock(this->state->mutex);
    if (this->state->workers && this->state->workerThread == std::this_thread::get_id())
      throw StateError("EDEADLK");
    this->state->condition.wait(lock, [&] {
      return !this->state->active && !this->state->workers;
    });
  }

  std::function<void(const std::string&)> ExecutionController::cancellationHandler() const {
    const auto weak = std::weak_ptr<State>(this->state);
    return [weak] (const std::string& generation) {
      if (const auto state = weak.lock()) {
        if (const auto running = state->interruptionTarget(); running && running->interruptGeneration == generation) {
          running->cancellation.cancel();
        }
      }
    };
  }
}  // namespace oro::runtime::native
