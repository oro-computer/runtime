#include "callbacks.hh"
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <thread>

namespace oro::runtime::native {
  namespace {
    bool opaque (const std::string& value) {
      return !value.empty () && value.size () <= 128 &&
             value.find_first_not_of ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
                 std::string::npos;
    }
    bool name (const std::string& value, bool extension = false) {
      return !value.empty () && value.size () <= 64 && value.front () >= 'a' && value.front () <= 'z' &&
             value.find_first_not_of (extension ? "abcdefghijklmnopqrstuvwxyz0123456789_.-"
                                                : "abcdefghijklmnopqrstuvwxyz0123456789.-") == std::string::npos;
    }
    void active (const ExecutionCancellation& signal) {
      if (signal.cancelled ())
        throw StateError ("ABORT_ERR");
    }
  }  // namespace
  struct CallbackRegistry::State {
    struct Registration {
      CallbackDescriptor descriptor;
      Authorizer authorize;
      CallbackPushHandler push;
      Extension extension;
    };
    struct Invocation {
      Registration registration;
      ExecutionTask task;
      ExecutionCancellation signal;
      std::unique_ptr<CallbackExecution> authorized;
      bool busy = false;
      bool attempted = false;
      bool ran = false;
      std::thread::id worker;
      std::vector<std::shared_ptr<ExecutionGroup>> groups;
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::map<std::pair<CallbackType, std::string>, Registration> registrations;
    std::map<uint64_t, std::shared_ptr<Invocation>> invocations;
    uint64_t sequence = 0;
    bool sealed = false;
    bool closing = false;
    std::shared_ptr<ExecutionGroup::Capacity> childCapacity = ExecutionGroup::capacity ();
    struct ChildOwner {
      std::weak_ptr<Invocation> owner;
      ExecutionCancellation signal;
    };
    std::map<std::thread::id, ChildOwner> childWorkers;
    void add (Registration value) {
      std::lock_guard lock (mutex);
      if (sealed || closing)
        throw StateError ("ESHUTDOWN");
      const auto& descriptor = value.descriptor;
      if (!name (descriptor.kind, descriptor.type == CallbackType::Extension))
        throw StateError ("EINVAL");
      const auto key = std::make_pair (descriptor.type, descriptor.kind);
      if (registrations.contains (key))
        throw StateError ("EEXIST");
      const auto count = std::count_if (registrations.begin (), registrations.end (),
                                        [&] (const auto& entry) { return entry.first.first == descriptor.type; });
      if (count >= (descriptor.type == CallbackType::Extension ? 32 : 16))
        throw StateError ("ENOSPC");
      registrations.emplace (key, std::move (value));
    }
    struct Work {
      State& state;
      std::shared_ptr<Invocation> invocation;
      Work (State& state, uint64_t id) : state (state) {
        std::lock_guard lock (state.mutex);
        const auto found = state.invocations.find (id);
        if (found == state.invocations.end ())
          throw StateError ("ENOENT");
        invocation = found->second;
        if (invocation->busy)
          throw StateError ("EBUSY");
        invocation->busy = true;
        invocation->worker = std::this_thread::get_id ();
      }
      ~Work () {
        std::lock_guard lock (state.mutex);
        invocation->busy = false;
        invocation->worker = {};
        state.changed.notify_all ();
      }
    };
  };
  CallbackRegistry::CallbackRegistry () : state (std::make_shared<State> ()) {
  }
  CallbackRegistry::~CallbackRegistry () {
    shutdown ();
  }
  void CallbackRegistry::registerExecution (CallbackType type, const std::string& kind, Authorizer authorizer,
                                            const std::set<std::string>& operations, bool retrySafe) {
    if (type != CallbackType::Foreground && type != CallbackType::Continuous && type != CallbackType::DeferredRead &&
        type != CallbackType::Maintenance)
      throw StateError ("EINVAL");
    if (!authorizer ||
        (type == CallbackType::Maintenance &&
         (!retrySafe || operations.empty () ||
          std::any_of (operations.begin (), operations.end (),
                       [] (const auto& operation) { return operation != "cleanup" && operation != "revoke"; }))) ||
        (type != CallbackType::Maintenance && !operations.empty ()))
      throw StateError ("EINVAL");
    state->add ({{type, kind, operations}, std::move (authorizer), {}, {}});
  }
  void CallbackRegistry::registerPush (const std::string& kind, CallbackPushHandler handler) {
    if (!handler.authorize || !handler.message)
      throw StateError ("EINVAL");
    state->add ({{CallbackType::Push, kind, {}}, {}, std::move (handler), {}});
  }
  void CallbackRegistry::registerExtension (const std::string& kind, Extension extension) {
    if (!extension)
      throw StateError ("EINVAL");
    state->add ({{CallbackType::Extension, kind, {}}, {}, {}, std::move (extension)});
  }
  std::vector<CallbackDescriptor> CallbackRegistry::seal () {
    std::lock_guard lock (state->mutex);
    if (state->closing)
      throw StateError ("ESHUTDOWN");
    state->sealed = true;
    std::vector<CallbackDescriptor> result;
    for (const auto& [key, value] : state->registrations)
      result.push_back (value.descriptor);
    return result;
  }
  uint64_t CallbackRegistry::begin (CallbackType type, const ExecutionTask& task) {
    if (!name (task.kind, type == CallbackType::Extension))
      throw StateError ("EINVAL");
    if (type != CallbackType::Extension &&
        (!opaque (task.id) || !opaque (task.authorityRef) || !opaque (task.accountRef) || !opaque (task.targetRef) ||
         (type != CallbackType::Push && !opaque (task.operationRef))))
      throw StateError ("EINVAL");
    std::lock_guard lock (state->mutex);
    if (!state->sealed || state->closing)
      throw StateError ("ESHUTDOWN");
    const auto found = state->registrations.find ({type, task.kind});
    if (found == state->registrations.end ())
      throw StateError ("ENOSYS");
    if (type == CallbackType::Maintenance && !found->second.descriptor.operations.contains (task.operationRef))
      throw StateError ("EACCES");
    if (state->invocations.size () >= 64)
      throw StateError ("EBUSY");
    if (state->sequence == INT64_MAX)
      throw StateError ("EOVERFLOW");
    auto invocation = std::make_shared<State::Invocation> ();
    invocation->registration = found->second;
    invocation->task = task;
    const auto id = ++state->sequence;
    state->invocations.emplace (id, std::move (invocation));
    return id;
  }
  bool CallbackRegistry::authorize (uint64_t id) {
    State::Work work (*state, id);
    auto& invocation = *work.invocation;
    active (invocation.signal);
    if (!invocation.registration.authorize || invocation.attempted)
      throw StateError ("EINVAL");
    invocation.attempted = true;
    invocation.authorized = invocation.registration.authorize (invocation.task, invocation.signal);
    active (invocation.signal);
    return invocation.authorized != nullptr;
  }
  CallbackOutcome CallbackRegistry::run (uint64_t id) {
    State::Work work (*state, id);
    auto& invocation = *work.invocation;
    active (invocation.signal);
    if (!invocation.attempted || invocation.ran || !invocation.authorized)
      throw StateError ("EACCES");
    invocation.ran = true;
    const auto result = invocation.authorized->run (invocation.signal);
    active (invocation.signal);
    return result;
  }
  bool CallbackRegistry::push (uint64_t id, const std::string& stage, std::span<const unsigned char> bytes,
                               const std::string& enrollment) {
    State::Work work (*state, id);
    auto& invocation = *work.invocation;
    active (invocation.signal);
    if (invocation.registration.descriptor.type != CallbackType::Push || invocation.ran || bytes.size () > 4096 ||
        enrollment.size () > 16384 || (stage != "authorize" && stage != "message" && stage != "enrollment"))
      throw StateError ("EINVAL");
    invocation.ran = true;
    auto& handler = invocation.registration.push;
    if (!handler.authorize (invocation.task, invocation.signal))
      return false;
    active (invocation.signal);
    const auto result = stage == "authorize" ? true
                        : stage == "message"
                            ? handler.message (invocation.task, bytes, invocation.signal)
                            : handler.enrollment && handler.enrollment (invocation.task, enrollment, invocation.signal);
    active (invocation.signal);
    return result;
  }
  std::string CallbackRegistry::extension (uint64_t id, const std::string& action, const std::string& payload) {
    State::Work work (*state, id);
    auto& invocation = *work.invocation;
    active (invocation.signal);
    if (invocation.registration.descriptor.type != CallbackType::Extension || invocation.ran ||
        payload.size () > 196608 || action.empty () || action.size () > 64 ||
        action.find_first_not_of ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") !=
            std::string::npos)
      throw StateError ("EINVAL");
    invocation.ran = true;
    const auto result = invocation.registration.extension (action, payload, invocation.signal);
    active (invocation.signal);
    if (result.size () > 524288)
      throw StateError ("E2BIG");
    return result;
  }
  void CallbackRegistry::cancel (uint64_t id) {
    std::lock_guard lock (state->mutex);
    const auto found = state->invocations.find (id);
    if (found != state->invocations.end ())
      found->second->signal.cancel ();
  }
  void CallbackRegistry::close (uint64_t id) {
    State::Work work (*state, id);
    work.invocation->signal.cancel ();
    std::vector<std::shared_ptr<ExecutionGroup>> groups;
    {
      std::lock_guard lock (state->mutex);
      groups = work.invocation->groups;
    }
    for (const auto& group : groups)
      group->close ();
    work.invocation->authorized.reset ();
    std::lock_guard lock (state->mutex);
    state->invocations.erase (id);
    state->changed.notify_all ();
  }
  void CallbackRegistry::shutdown () {
    std::lock_guard lock (state->mutex);
    state->closing = true;
    for (const auto& [id, invocation] : state->invocations)
      invocation->signal.cancel ();
  }
  void CallbackRegistry::drain () {
    std::unique_lock lock (state->mutex);
    if (state->childWorkers.contains (std::this_thread::get_id ()))
      throw StateError ("EDEADLK");
    for (const auto& [id, invocation] : state->invocations)
      if (invocation->worker == std::this_thread::get_id ())
        throw StateError ("EDEADLK");
    state->changed.wait (lock, [&] { return state->invocations.empty (); });
  }
  bool CallbackRegistry::ownsInvocationThread () const {
    return invocationGuard () ();
  }
  std::function<bool ()> CallbackRegistry::invocationGuard () const {
    return [weak = std::weak_ptr<State> (state)] {
      const auto state = weak.lock ();
      if (!state)
        return false;
      std::lock_guard lock (state->mutex);
      return state->childWorkers.contains (std::this_thread::get_id ()) ||
        std::any_of (state->invocations.begin (), state->invocations.end (), [] (const auto& entry) {
      return entry.second->busy && entry.second->worker == std::this_thread::get_id ();
    });
    };
  }
  ExecutionCancellation CallbackRegistry::invocationSignal () const {
    std::lock_guard lock (state->mutex);
    const auto thread = std::this_thread::get_id ();
    if (const auto found = state->childWorkers.find (thread); found != state->childWorkers.end ()) {
      auto signal = found->second.signal;
      const auto owner = found->second.owner.lock ();
      if (!owner || owner->signal.cancelled ())
        signal.cancel ();
      return signal;
    }
    for (const auto& [id, invocation] : state->invocations)
      if (invocation->busy && invocation->worker == thread)
        return invocation->signal;
    throw StateError ("EACCES");
  }
  std::shared_ptr<ExecutionGroup> CallbackRegistry::children (const ExecutionCancellation& signal, size_t limit) {
    std::lock_guard lock (state->mutex);
    if (state->closing || signal.cancelled ())
      throw StateError ("ESHUTDOWN");
    for (const auto& [id, invocation] : state->invocations) {
      if (&invocation->signal != &signal || !invocation->busy || invocation->worker != std::this_thread::get_id ())
        continue;
      if (invocation->groups.size () >= 8)
        throw StateError ("ENOSPC");
      const auto weak = std::weak_ptr<State> (state);
      const auto owner = std::weak_ptr<State::Invocation> (invocation);
      auto group = std::shared_ptr<ExecutionGroup> (new ExecutionGroup (signal, state->childCapacity, limit,
        [weak, owner] (const ExecutionCancellation& signal) {
          const auto state = weak.lock ();
          const auto invocation = owner.lock ();
          if (!state || !invocation)
            throw StateError ("ESHUTDOWN");
          std::lock_guard lock (state->mutex);
          if (state->closing || invocation->signal.cancelled ())
            throw StateError ("ABORT_ERR");
          state->childWorkers.emplace (std::this_thread::get_id (), State::ChildOwner{invocation, signal});
        }, [weak] {
          if (const auto state = weak.lock ()) {
            std::lock_guard lock (state->mutex);
            state->childWorkers.erase (std::this_thread::get_id ());
            state->changed.notify_all ();
          }
        }));
      invocation->groups.push_back (group);
      return group;
    }
    throw StateError ("EACCES");
  }
}  // namespace oro::runtime::native
