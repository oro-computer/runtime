#include "execution.hh"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace oro::runtime::native {
  namespace {
    /** Uses existing mutex/CV primitives; no newer standard-library stop-token runtime dependency. */
    struct CancellationListener {
      std::mutex mutex;
      std::condition_variable changed;
      std::function<void ()> work;
      std::thread::id worker;
      bool removed = false;
      bool complete = false;
      explicit CancellationListener (std::function<void ()> work) : work (std::move (work)) {}
      void invoke () {
        std::function<void ()> operation;
        {
          std::lock_guard lock (mutex);
          if (removed || complete || worker != std::thread::id{})
            return;
          worker = std::this_thread::get_id ();
          operation = std::move (work);
        }
        try {
          operation ();
        } catch (...) {
        }
        operation = {};
        {
          std::lock_guard lock (mutex);
          worker = {};
          complete = true;
          changed.notify_all ();
        }
      }
      void remove () {
        std::function<void ()> discarded;
        {
          std::unique_lock lock (mutex);
          removed = true;
          if (worker != std::this_thread::get_id ())
            changed.wait (lock, [&] { return worker == std::thread::id{}; });
          discarded = std::move (work);
        }
      }
    };
  }
  struct ExecutionCancellation::State {
    using Clock = std::chrono::steady_clock;
    std::atomic<bool> stopped = false;
    std::atomic<int64_t> deadline = INT64_MAX;
    std::atomic<size_t> listeners = 0;
    std::mutex listenersMutex;
    std::vector<std::weak_ptr<CancellationListener>> callbacks;
    bool delivered = false;
    bool registered = false;
    bool dispatched = false;

    void deliver () {
      std::vector<std::weak_ptr<CancellationListener>> pending;
      {
        std::lock_guard lock (listenersMutex);
        delivered = true;
        pending = std::move (callbacks);
      }
      for (const auto& weak : pending)
        if (const auto listener = weak.lock ())
          listener->invoke ();
    }
    struct Hub {
      std::mutex mutex;
      std::condition_variable changed;
      std::condition_variable ready;
      std::vector<std::weak_ptr<State>> signals;
      std::array<std::shared_ptr<State>, 256> pending;
      size_t head = 0;
      size_t count = 0;
      uint64_t revision = 0;
      bool closing = false;
      std::thread timer;
      std::vector<std::thread> workers;

      Hub () {
        try {
          signals.reserve (256);
          workers.reserve (4);
          for (int index = 0; index < 4; index++) {
            workers.emplace_back ([this] {
              while (true) {
                std::shared_ptr<State> signal;
                {
                  std::unique_lock lock (mutex);
                  ready.wait (lock, [&] { return closing || count; });
                  if (!count)
                    return;
                  signal = std::move (pending[head]);
                  head = (head + 1) % pending.size ();
                  count--;
                }
                signal->deliver ();
              }
            });
          }
          timer = std::thread ([this] {
            std::unique_lock lock (mutex);
            while (!closing) {
              auto wake = Clock::time_point::max ();
              for (auto iterator = signals.begin (); iterator != signals.end ();) {
                auto signal = iterator->lock ();
                if (!signal) {
                  iterator = signals.erase (iterator);
                  continue;
                }
                iterator++;
                if (signal->dispatched)
                  continue;
                const auto deadline = Clock::time_point (Clock::duration (signal->deadline.load ()));
                if (signal->stopped.load () || Clock::now () >= deadline) {
                  signal->stopped.store (true);
                  signal->dispatched = true;
                  // Each of the at most256 observed live signals enters this fixed queue only once.
                  pending[(head + count) % pending.size ()] = std::move (signal);
                  count++;
                  ready.notify_one ();
                } else if (deadline < wake) {
                  wake = deadline;
                }
              }
              const auto observed = revision;
              if (wake == Clock::time_point::max ()) {
                changed.wait (lock, [&] { return closing || revision != observed; });
              } else {
                changed.wait_until (lock, wake, [&] { return closing || revision != observed; });
              }
            }
          });
        } catch (...) {
          {
            std::lock_guard lock (mutex);
            closing = true;
            ready.notify_all ();
          }
          for (auto& worker : workers)
            worker.join ();
          throw;
        }
      }
      ~Hub () {
        {
          std::lock_guard lock (mutex);
          closing = true;
          changed.notify_all ();
          ready.notify_all ();
        }
        timer.join ();
        for (auto& worker : workers)
          worker.join ();
      }
      void observe (const std::shared_ptr<State>& signal) {
        std::lock_guard lock (mutex);
        if (!signal->registered) {
          std::erase_if (signals, [] (const auto& entry) { return entry.expired (); });
          if (signals.size () >= 256 || closing)
            throw StateError ("EBUSY");
          signals.push_back (signal);
          signal->registered = true;
        }
        revision++;
        changed.notify_all ();
      }
      void notify () {
        std::lock_guard lock (mutex);
        revision++;
        changed.notify_all ();
      }
    };
    static Hub& hub () {
      static Hub value;
      return value;
    }
  };

  struct CancellationSubscription::State {
    std::function<void ()> release;
    std::shared_ptr<CancellationListener> callback;
    ~State () {
      if (callback)
        callback->remove ();
      if (release)
        release ();
    }
  };
  CancellationSubscription::CancellationSubscription () = default;
  CancellationSubscription::~CancellationSubscription () = default;
  CancellationSubscription::CancellationSubscription (CancellationSubscription&&) noexcept = default;
  CancellationSubscription& CancellationSubscription::operator= (CancellationSubscription&&) noexcept = default;
  void CancellationSubscription::reset () {
    state.reset ();
  }
  ExecutionCancellation::ExecutionCancellation (int64_t durationMs) : state (std::make_shared<State> ()) {
    expireAfter (durationMs);
  }
  bool ExecutionCancellation::cancelled () const {
    return state->stopped.load () || State::Clock::now ().time_since_epoch ().count () >= state->deadline.load ();
  }
  void ExecutionCancellation::cancel () {
    state->stopped.store (true);
    if (state->listeners.load ())
      State::hub ().notify ();
  }
  void ExecutionCancellation::expireAfter (int64_t durationMs) {
    if (durationMs < 0 || durationMs > 86400000)
      throw StateError ("EINVAL");
    state->deadline.store (durationMs ? (State::Clock::now () + std::chrono::milliseconds (durationMs)).time_since_epoch ().count ()
                                     : INT64_MAX);
    if (state->listeners.load ())
      State::hub ().notify ();
  }
  CancellationSubscription ExecutionCancellation::onCancel (std::function<void ()> work) const {
    if (!work)
      throw StateError ("EINVAL");
    if (state->listeners.fetch_add (1) >= 32) {
      state->listeners.fetch_sub (1);
      throw StateError ("ENOSPC");
    }
    bool owned = false;
    try {
      State::hub ().observe (state);
      CancellationSubscription result;
      result.state = std::make_unique<CancellationSubscription::State> ();
      result.state->release = [signal = state] { signal->listeners.fetch_sub (1); };
      owned = true;
      const auto listener = std::make_shared<CancellationListener> (std::move (work));
      result.state->callback = listener;
      bool delivered = false;
      {
        std::lock_guard lock (state->listenersMutex);
        delivered = state->delivered;
        if (!delivered) {
          std::erase_if (state->callbacks, [] (const auto& entry) { return entry.expired (); });
          state->callbacks.push_back (listener);
        }
      }
      if (delivered)
        listener->invoke ();
      return result;
    } catch (...) {
      if (!owned)
        state->listeners.fetch_sub (1);
      throw;
    }
  }
}  // namespace oro::runtime::native
