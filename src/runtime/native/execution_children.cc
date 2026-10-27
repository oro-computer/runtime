#include "execution_children.hh"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace oro::runtime::native {
  struct ExecutionGroup::Capacity {
    std::mutex mutex;
    size_t reserved = 0;
  };
  struct ExecutionGroup::State {
    struct Child {
      ExecutionCancellation signal;
      ChildExecutionStatus status;
      std::thread worker;
      bool joining = false;
    };
    ExecutionCancellation signal;
    std::shared_ptr<Capacity> budget;
    size_t limit;
    std::function<void (const ExecutionCancellation&)> enter;
    std::function<void ()> leave;
    mutable std::mutex mutex;
    std::mutex closingMutex;
    std::condition_variable changed;
    std::map<uint64_t, std::shared_ptr<Child>> children;
    uint64_t sequence = 0;
    bool closing = false;

    State (const ExecutionCancellation& signal, std::shared_ptr<Capacity> budget, size_t limit,
           std::function<void (const ExecutionCancellation&)> enter, std::function<void ()> leave)
        : signal (signal), budget (std::move (budget)), limit (limit), enter (std::move (enter)), leave (std::move (leave)) {
    }
    void cancel () {
      std::lock_guard lock (mutex);
      for (const auto& [id, child] : children)
        child->signal.cancel ();
    }
    void caller () const {
      for (const auto& [id, child] : children) {
        if (child->worker.get_id () == std::this_thread::get_id ())
          throw StateError ("EDEADLK");
      }
    }
    void release () {
      std::lock_guard lock (budget->mutex);
      budget->reserved--;
    }
  };
  std::shared_ptr<ExecutionGroup::Capacity> ExecutionGroup::capacity () {
    return std::make_shared<Capacity> ();
  }
  ExecutionGroup::ExecutionGroup (const ExecutionCancellation& signal, std::shared_ptr<Capacity> budget, size_t limit,
                                  std::function<void (const ExecutionCancellation&)> enter, std::function<void ()> leave) {
    if (!budget || limit < 1 || limit > 8)
      throw StateError ("EINVAL");
    state = std::make_shared<State> (signal, std::move (budget), limit, std::move (enter), std::move (leave));
    parent = signal.onCancel ([weak = std::weak_ptr<State> (state)] {
      if (const auto value = weak.lock ())
        value->cancel ();
    });
  }
  ExecutionGroup::~ExecutionGroup () {
    close ();
  }
  uint64_t ExecutionGroup::start (Work work) {
    if (!work)
      throw StateError ("EINVAL");
    std::lock_guard lock (state->mutex);
    if (state->closing)
      throw StateError ("ESHUTDOWN");
    if (state->signal.cancelled ())
      throw StateError ("ABORT_ERR");
    if (state->children.size () >= state->limit)
      throw StateError ("EBUSY");
    if (state->sequence == INT64_MAX)
      throw StateError ("EOVERFLOW");
    auto child = std::make_shared<State::Child> ();
    const auto id = ++state->sequence;
    child->status.handle = id;
    {
      std::lock_guard budget (state->budget->mutex);
      if (state->budget->reserved >= 8)
        throw StateError ("EBUSY");
      state->budget->reserved++;
    }
    try {
      state->children.emplace (id, child);
      child->worker = std::thread ([state = state, child, work = std::move (work)] () mutable {
        bool entered = false;
        std::string error;
        try {
          // Synchronize with assignment of worker/entry before granting owned access.
          {
            std::lock_guard lock (state->mutex);
            if (state->signal.cancelled ())
              child->signal.cancel ();
          }
          if (child->signal.cancelled ())
            throw StateError ("ABORT_ERR");
          state->enter (child->signal);
          entered = true;
          work (child->signal);
        } catch (const StateError& failure) {
          error = failure.code;
        } catch (...) {
          error = "EIO";
        }
        work = {};
        if (entered)
          state->leave ();
        std::lock_guard lock (state->mutex);
        child->status.finished = true;
        child->status.error = std::move (error);
        state->changed.notify_all ();
      });
    } catch (...) {
      state->children.erase (id);
      state->release ();
      throw;
    }
    return id;
  }
  ChildExecutionStatus ExecutionGroup::status (uint64_t id) const {
    std::lock_guard lock (state->mutex);
    const auto found = state->children.find (id);
    if (found == state->children.end ())
      throw StateError ("ENOENT");
    return found->second->status;
  }
  void ExecutionGroup::cancel (uint64_t id) {
    std::lock_guard lock (state->mutex);
    const auto found = state->children.find (id);
    if (found == state->children.end ())
      throw StateError ("ENOENT");
    found->second->signal.cancel ();
  }
  void ExecutionGroup::cancel () {
    state->cancel ();
  }
  ChildExecutionStatus ExecutionGroup::join (uint64_t id) {
    std::shared_ptr<State::Child> child;
    {
      std::lock_guard lock (state->mutex);
      state->caller ();
      const auto found = state->children.find (id);
      if (found == state->children.end ())
        throw StateError ("ENOENT");
      child = found->second;
      if (child->joining || state->closing)
        throw StateError ("EBUSY");
      child->joining = true;
    }
    child->worker.join ();
    state->release ();
    std::lock_guard lock (state->mutex);
    state->children.erase (id);
    state->changed.notify_all ();
    return child->status;
  }
  void ExecutionGroup::close () {
    {
      std::lock_guard lock (state->mutex);
      state->caller ();
    }
    std::lock_guard closing (state->closingMutex);
    std::vector<std::shared_ptr<State::Child>> children;
    {
      std::unique_lock lock (state->mutex);
      state->caller ();
      state->closing = true;
      for (const auto& [id, child] : state->children)
        child->signal.cancel ();
      state->changed.wait (lock, [&] {
        return std::none_of (state->children.begin (), state->children.end (), [] (const auto& entry) { return entry.second->joining; });
      });
      for (const auto& [id, child] : state->children) {
        child->joining = true;
        children.push_back (child);
      }
    }
    for (const auto& child : children) {
      child->worker.join ();
      state->release ();
    }
    {
      std::lock_guard lock (state->mutex);
      state->children.clear ();
      state->changed.notify_all ();
    }
    parent.reset ();
  }
}  // namespace oro::runtime::native
