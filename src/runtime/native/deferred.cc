#include "deferred.hh"

#include <condition_variable>
#include <thread>
#include <cmath>
#include <regex>

namespace oro::runtime::native {
  namespace {
    int64_t now () {
      return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
    }
    std::string generation () {
      auto id = crypto::rand64();
      auto other = crypto::rand64();
      char value[40];
      std::snprintf(value, sizeof(value), "%08x-%04x-4%03x-8%03x-%012llx", static_cast<unsigned>(id >> 32),
                    static_cast<unsigned>((id >> 16) & 65535), static_cast<unsigned>(id & 4095),
                    static_cast<unsigned>((other >> 48) & 4095),
                    static_cast<unsigned long long>(other & 0xffffffffffff));
      return value;
    }
    std::string text (const JSON::Object& object, const std::string& key, const std::string& fallback = "") {
      if (!object.has(key))
        return fallback;
      if (object.get(key).type != JSON::Type::String)
        throw StateError("EINVAL");
      return object.get(key).as<JSON::String>().value();
    }
    int64_t number (const JSON::Object& object, const std::string& key, int64_t fallback = 0) {
      if (!object.has(key))
        return fallback;
      if (object.get(key).type != JSON::Type::Number)
        throw StateError("EINVAL");
      const auto value = object.get(key).as<JSON::Number>().value();
      if (!std::isfinite(value) || value < 0 || value > 9007199254740991.0 || std::floor(value) != value)
        throw StateError("EINVAL");
      return static_cast<int64_t>(value);
    }
    bool flag (const JSON::Object& object, const std::string& key, bool fallback = false) {
      if (!object.has(key))
        return fallback;
      if (object.get(key).type != JSON::Type::Boolean)
        throw StateError("EINVAL");
      return object.get(key).as<JSON::Boolean>().value();
    }
    bool identifier (const std::string& value, bool kind = false) {
      return std::regex_match(value, std::regex(kind ? "[a-z][a-z0-9.-]{0,63}" : "[a-zA-Z0-9_-]{1,128}"));
    }
    RecordBytes encode (const JSON::Object& value) {
      const auto text = value.str();
      return {text.begin(), text.end()};
    }
    JSON::Object decode (const RecordBytes& value) {
      try {
        return JSON::parse(std::string(value.begin(), value.end())).as<JSON::Object>();
      } catch (...) {
        throw StateError("EBADMSG");
      }
    }
    bool terminal (const std::string& phase) {
      return phase == "completed" || phase == "cancelled" || phase == "failed" || phase == "unauthorized" ||
             phase == "conflict" || phase == "unknown";
    }
    ExecutionTask task (const JSON::Object& value) {
      return {text(value, "id"),         text(value, "kind"),      text(value, "authorityRef"),
              text(value, "accountRef"), text(value, "targetRef"), text(value, "operationRef")};
    }
    JSON::Object publicState (JSON::Object value) {
      value.data.erase("owner");
      return value;
    }
  }  // namespace

  struct DeferredController::State {
    RecordStore store;
    std::shared_ptr<void> lease;
    Constraints constraints;
    Clock clock;
    std::string process = generation();
    std::mutex mutex;
    std::condition_variable changed;
    struct Registration {
      std::set<std::string> operations;
      DeferredExecutor executor;
    };
    std::map<std::string, DeferredExecutor> reads;
    std::map<std::string, Registration> maintenance;
    std::thread worker;
    std::string activeId;
    std::shared_ptr<ExecutionCancellation> active;
    bool closing = false;
    bool started = false;

    State(RecordStore store, std::shared_ptr<void> lease, Constraints constraints, Clock clock)
        : store(std::move(store)),
          lease(std::move(lease)),
          constraints(std::move(constraints)),
          clock(std::move(clock)) {
    }

    void save (const std::string& id, const JSON::Object& value) {
      const auto snapshot = this->store.read();
      this->store.apply(snapshot.revision, {{id, encode(value)}});
    }

    RecordSnapshot refresh () {
      const auto snapshot = this->store.read();
      RecordChanges changes;
      for (const auto& [id, bytes] : snapshot.records) {
        auto record = decode(bytes);
        const auto phase = text(record, "phase");
        if (id != text(record, "id"))
          throw StateError("EBADMSG");
        if (!terminal(phase) && phase != "interrupted" &&
            (text(record, "owner") != this->process ||
             (phase == "running" && (this->activeId != id || !this->active || this->active->cancelled())))) {
          record["phase"] = "interrupted";
          record["lastOutcome"] = phase == "running" ? "unknown" : "not-started";
          record["updatedAt"] = this->clock();
          changes[id] = encode(record);
        }
      }
      return changes.empty() ? snapshot : this->store.apply(snapshot.revision, changes);
    }

    DeferredExecutor executor (const JSON::Object& value) {
      const auto kind = text(value, "kind");
      if (text(value, "mode", "read") == "maintenance") {
        const auto found = this->maintenance.find(kind);
        if (found == this->maintenance.end() || !found->second.operations.contains(text(value, "operationRef")))
          throw StateError("ENOSYS");
        return found->second.executor;
      }
      const auto found = this->reads.find(kind);
      if (found == this->reads.end())
        throw StateError("ENOSYS");
      return found->second;
    }

    void run () {
      std::unique_lock lock(this->mutex);
      while (!this->closing) {
        JSON::Object record;
        DeferredExecutor executor;
        int64_t wait = 30000;
        try {
          for (const auto& [id, bytes] : this->refresh().records) {
            auto candidate = decode(bytes);
            const auto phase = text(candidate, "phase");
            if ((phase != "scheduled" && phase != "retry") || number(candidate, "attempts") >= 5)
              continue;
            const auto remaining = number(candidate, "notBefore") - this->clock();
            if (remaining > 0) {
              wait = std::min(wait, remaining);
              continue;
            }
            try {
              executor = this->executor(candidate);
            } catch (const StateError&) {
              continue;
            }
            if (!this->constraints(text(candidate, "network"), flag(candidate, "requiresCharging"),
                                   flag(candidate, "requiresBatteryNotLow", true)))
              continue;
            record = candidate;
            break;
          }
          if (record.size() == 0) {
            this->changed.wait_for(lock, std::chrono::milliseconds(std::max<int64_t>(1, wait)));
            continue;
          }
          const auto id = text(record, "id");
          const auto token = text(record, "generation");
          record["phase"] = "running";
          record["attempts"] = number(record, "attempts") + 1;
          record["updatedAt"] = this->clock();
          this->save(id, record);
          auto signal = std::make_shared<ExecutionCancellation>(60000);
          this->activeId = id;
          this->active = signal;
          lock.unlock();
          DeferredOutcome outcome = DeferredOutcome::Unknown;
          try {
            const auto reference = native::task(record);
            outcome = executor.authorize(reference, *signal) ? executor.execute(reference, *signal)
                                                             : DeferredOutcome::Unauthorized;
          } catch (...) {
          }
          lock.lock();
          this->active.reset();
          this->activeId.clear();
          const auto snapshot = this->store.read();
          const auto found = snapshot.records.find(id);
          if (found == snapshot.records.end())
            continue;
          auto current = decode(found->second);
          if (this->closing || text(current, "generation") != token || text(current, "phase") != "running")
            continue;
          const auto phase = signal->cancelled()                        ? "unknown"
                             : outcome == DeferredOutcome::Completed    ? "completed"
                             : outcome == DeferredOutcome::Retry        ? "retry"
                             : outcome == DeferredOutcome::Unauthorized ? "unauthorized"
                             : outcome == DeferredOutcome::Conflict     ? "conflict"
                                                                        : "unknown";
          current["lastOutcome"] = phase;
          current["phase"] = phase;
          current["updatedAt"] = this->clock();
          if (outcome == DeferredOutcome::Retry && !signal->cancelled()) {
            if (number(current, "attempts") >= 5)
              current["phase"] = "failed";
            else
              current["notBefore"] = this->clock() + (30000LL << (number(current, "attempts") - 1));
          } else if (outcome == DeferredOutcome::Completed && !signal->cancelled()) {
            current["completedRuns"] = number(current, "completedRuns") + 1;
            if (number(current, "intervalMs") > 0) {
              current["phase"] = "scheduled";
              current["attempts"] = 0;
              current["notBefore"] = this->clock() + number(current, "intervalMs");
            }
          }
          this->save(id, current);
        } catch (...) {
          if (!lock.owns_lock())
            lock.lock();
          this->active.reset();
          this->activeId.clear();
          this->changed.wait_for(lock, std::chrono::seconds(1));
        }
      }
    }
  };

  DeferredController::DeferredController(RecordStore store, std::shared_ptr<void> lease, Constraints constraints,
                                         Clock clock)
      : state(std::make_shared<State>(std::move(store), std::move(lease), std::move(constraints),
                                      clock ? std::move(clock) : Clock(now))) {
    if (!this->state->lease || !this->state->constraints)
      throw StateError("EINVAL");
  }

  DeferredController::~DeferredController() {
    try {
      this->shutdown();
    } catch (...) {
    }
  }

  void DeferredController::registerRead(const std::string& name, DeferredExecutor executor) {
    std::lock_guard lock(this->state->mutex);
    if (this->state->closing)
      throw StateError("ESHUTDOWN");
    if (!identifier(name, true) || !executor.authorize || !executor.execute)
      throw StateError("EINVAL");
    if (this->state->reads.contains(name))
      throw StateError("EEXIST");
    if (this->state->reads.size() >= 16)
      throw StateError("ENOSPC");
    this->state->reads[name] = std::move(executor);
    this->state->changed.notify_all();
  }

  void DeferredController::registerMaintenance(const std::string& name, const std::set<std::string>& operations,
                                               bool retrySafe, DeferredExecutor executor) {
    std::lock_guard lock(this->state->mutex);
    if (this->state->closing)
      throw StateError("ESHUTDOWN");
    if (!identifier(name, true) || !retrySafe || operations.empty() || !executor.authorize || !executor.execute)
      throw StateError("EINVAL");
    for (const auto& operation : operations)
      if (operation != "cleanup" && operation != "revoke")
        throw StateError("EINVAL");
    if (this->state->maintenance.contains(name))
      throw StateError("EEXIST");
    if (this->state->maintenance.size() >= 16)
      throw StateError("ENOSPC");
    this->state->maintenance[name] = {operations, std::move(executor)};
    this->state->changed.notify_all();
  }

  JSON::Any DeferredController::request(const std::string& action, const JSON::Object& options) {
    auto state = this->state;
    std::lock_guard lock(state->mutex);
    if (state->closing)
      throw StateError("ESHUTDOWN");
    if (action == "support") {
      if (options.size())
        throw StateError("EINVAL");
      JSON::Array reads;
      JSON::Array maintenance;
      JSON::Object operations;
      for (const auto& [kind, executor] : state->reads)
        reads.push(kind);
      for (const auto& [kind, registered] : state->maintenance) {
        maintenance.push(kind);
        JSON::Array allowed;
        for (const auto& operation : registered.operations)
          allowed.push(operation);
        operations[kind] = allowed;
      }
      return JSON::Object::Entries{{"available", true},
                                   {"registeredKinds", reads},
                                   {"registeredMaintenanceKinds", maintenance},
                                   {"maintenanceOperations", operations},
                                   {"maximumTasks", 32},
                                   {"maximumAttempts", 5},
                                   {"readOnly", true},
                                   {"queuedWrites", false},
                                   {"backgroundJavaScript", false},
                                   {"survivesForceStop", false},
                                   {"periodicReads", true},
                                   {"minimumIntervalMs", 900000},
                                   {"minimumFlexMs", 300000},
                                   {"platform", "linux"},
                                   {"executionScope", "native-host-process"},
                                   {"survivesProcessExit", false},
                                   {"automaticResume", false}};
    }
    const auto snapshot = state->refresh();
    if (action == "list") {
      if (options.size())
        throw StateError("EINVAL");
      JSON::Array values;
      for (const auto& [id, bytes] : snapshot.records)
        values.push(publicState(decode(bytes)));
      return values;
    }
    const auto id = text(options, "id");
    if (!identifier(id))
      throw StateError("EINVAL");
    const auto found = snapshot.records.find(id);
    if (action == "enqueue" || action == "enqueueMaintenance") {
      const bool maintenance = action == "enqueueMaintenance";
      for (const auto& [key, value] : options) {
        if (key != "id" && key != "kind" && key != "authorityRef" && key != "accountRef" && key != "targetRef" &&
            key != "operationRef" && key != "network" && key != "requiresCharging" && key != "requiresBatteryNotLow" &&
            key != "delayMs" && (maintenance || (key != "intervalMs" && key != "flexMs")))
          throw StateError("EINVAL");
      }
      const auto reference = task(options);
      if (!identifier(reference.kind, true) || !identifier(reference.authorityRef) ||
          !identifier(reference.accountRef) || !identifier(reference.targetRef) || !identifier(reference.operationRef))
        throw StateError("EINVAL");
      const auto network = text(options, "network", "unmetered");
      const auto delay = number(options, "delayMs");
      const auto interval = number(options, "intervalMs");
      const auto flex = number(options, "flexMs", interval ? std::max<int64_t>(300000, interval / 20) : 0);
      if ((network != "any" && network != "unmetered") || delay > 604800000 ||
          (interval && (interval < 900000 || interval > 604800000 || delay || flex < 300000 || flex < interval / 20 ||
                        flex > interval)) ||
          (!interval && options.has("flexMs")))
        throw StateError("EINVAL");
      JSON::Object record(JSON::Object::Entries{{"id", id},
                                                {"kind", reference.kind},
                                                {"authorityRef", reference.authorityRef},
                                                {"accountRef", reference.accountRef},
                                                {"targetRef", reference.targetRef},
                                                {"operationRef", reference.operationRef},
                                                {"network", network},
                                                {"requiresCharging", flag(options, "requiresCharging")},
                                                {"requiresBatteryNotLow", flag(options, "requiresBatteryNotLow", true)},
                                                {"delayMs", delay},
                                                {"intervalMs", interval},
                                                {"flexMs", flex},
                                                {"mode", maintenance ? "maintenance" : "read"}});
      state->executor(record);
      if (found != snapshot.records.end()) {
        const auto current = decode(found->second);
        for (const auto& [key, value] : record)
          if (current.get(key).str() != value.str())
            throw StateError("EEXIST");
        return publicState(current);
      }
      if (snapshot.records.size() >= 32)
        throw StateError("ENOSPC");
      record["schema"] = 1;
      record["generation"] = generation();
      record["owner"] = state->process;
      record["jobId"] = 0;
      record["phase"] = "scheduled";
      record["attempts"] = 0;
      record["updatedAt"] = state->clock();
      record["notBefore"] = state->clock() + (interval ? interval - flex : delay);
      record["completedRuns"] = 0;
      state->save(id, record);
      if (!state->started) {
        state->started = true;
        try {
          state->worker = std::thread([state] {
            state->run();
          });
        } catch (...) {
          state->started = false;
          record["phase"] = "interrupted";
          state->save(id, record);
          throw StateError("EAGAIN");
        }
      }
      state->changed.notify_all();
      return publicState(record);
    }
    if (options.size() != 1)
      throw StateError("EINVAL");
    if (found == snapshot.records.end())
      throw StateError("ENOENT");
    auto current = decode(found->second);
    if (action == "status")
      return publicState(current);
    if (action == "cancel" || action == "release") {
      if (state->activeId == id && state->active)
        state->active->cancel();
      if (action == "release") {
        state->store.apply(snapshot.revision, {{id, std::nullopt}});
        return nullptr;
      }
      current["phase"] = "cancelled";
      current["updatedAt"] = state->clock();
      state->save(id, current);
      state->changed.notify_all();
      return publicState(current);
    }
    if (action == "resume") {
      if ((text(current, "phase") != "interrupted" && text(current, "phase") != "unavailable") ||
          number(current, "attempts") >= 5)
        throw StateError("EINVAL");
      state->executor(current);
      current["generation"] = generation();
      current["owner"] = state->process;
      current["phase"] = "scheduled";
      current["updatedAt"] = state->clock();
      current["notBefore"] = state->clock();
      state->save(id, current);
      if (!state->started) {
        state->started = true;
        try {
          state->worker = std::thread([state] {
            state->run();
          });
        } catch (...) {
          state->started = false;
          current["phase"] = "interrupted";
          state->save(id, current);
          throw StateError("EAGAIN");
        }
      }
      state->changed.notify_all();
      return publicState(current);
    }
    throw StateError("EINVAL");
  }

  void DeferredController::wake() {
    this->state->changed.notify_all();
  }

  void DeferredController::shutdown() {
    auto state = this->state;
    std::exception_ptr failure;
    {
      std::lock_guard lock(state->mutex);
      if (!state->closing) {
        state->closing = true;
        if (state->active)
          state->active->cancel();
        state->changed.notify_all();
        try {
          const auto snapshot = state->store.read();
          RecordChanges changes;
          for (const auto& [id, bytes] : snapshot.records) {
            auto record = decode(bytes);
            if (!terminal(text(record, "phase"))) {
              record["lastOutcome"] = text(record, "phase") == "running" ? "unknown" : "not-started";
              record["phase"] = "interrupted";
              record["updatedAt"] = state->clock();
              changes[id] = encode(record);
            }
          }
          if (!changes.empty())
            state->store.apply(snapshot.revision, changes);
        } catch (...) {
          failure = std::current_exception();
        }
      }
    }
    if (state->worker.joinable()) {
      if (state->worker.get_id() == std::this_thread::get_id())
        state->worker.detach();
      else
        state->worker.join();
    }
    if (failure)
      std::rethrow_exception(failure);
  }
}  // namespace oro::runtime::native
