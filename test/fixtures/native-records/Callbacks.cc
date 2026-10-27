#include "src/runtime/native/callbacks.hh"
#include <cassert>
#include <thread>
#include <iostream>

using namespace oro::runtime::native;
template <typename F>
void fails (const std::string& code, F action) {
  try {
    action ();
    throw std::runtime_error ("Expected " + code);
  } catch (const StateError& error) {
    assert (error.code == code);
  }
}
struct Resource : CallbackExecution {
  std::atomic<int>& closed;
  std::atomic<bool>& entered;
  std::atomic<bool>& cleanup;
  std::atomic<bool>& release;
  explicit Resource (std::atomic<int>& closed, std::atomic<bool>& entered, std::atomic<bool>& cleanup,
                     std::atomic<bool>& release)
      : closed (closed), entered (entered), cleanup (cleanup), release (release) {
  }
  ~Resource () override {
    while (cleanup)
      std::this_thread::sleep_for (std::chrono::milliseconds (1));
    closed++;
  }
  CallbackOutcome run (const ExecutionCancellation& signal) override {
    entered = true;
    while (!signal.cancelled ())
      std::this_thread::sleep_for (std::chrono::milliseconds (1));
    while (!release)
      std::this_thread::sleep_for (std::chrono::milliseconds (1));
    return CallbackOutcome::Unknown;
  }
};
int main () {
  CallbackRegistry registry;
  assert (!registry.ownsInvocationThread ());
  std::atomic<int> closed = 0;
  std::atomic<bool> entered = false;
  std::atomic<bool> cleanup = false;
  std::atomic<bool> release = false;
  auto authorize = [&] (const ExecutionTask& task, const ExecutionCancellation&) -> std::unique_ptr<CallbackExecution> {
    assert (registry.ownsInvocationThread ());
    if (task.accountRef != "account")
      return nullptr;
    return std::make_unique<Resource> (closed, entered, cleanup, release);
  };
  registry.registerExecution (CallbackType::Continuous, "public.monitor", authorize);
  fails ("EEXIST", [&] { registry.registerExecution (CallbackType::Continuous, "public.monitor", authorize); });
  fails ("EINVAL",
         [&] { registry.registerExecution (CallbackType::Maintenance, "public.cleanup", authorize, {"read"}, true); });
  fails ("EINVAL", [&] {
    registry.registerExecution (CallbackType::Maintenance, "public.cleanup", authorize, {"revoke"}, false);
  });
  registry.registerExecution (CallbackType::Maintenance, "public.cleanup", authorize, {"revoke"}, true);
  registry.registerExtension ("public.metadata", [] (const auto& action, const auto&, const auto&) {
    return action == "large" ? std::string (524289, 'a') : "{\"public\":true}";
  });
  std::atomic<bool> allowed = false;
  int messages = 0;
  registry.registerPush ("public.alert", {[&] (const auto&, const auto&) { return allowed.load (); },
                                          [&] (const auto&, auto bytes, const auto&) {
                                            messages++;
                                            return bytes.size () == 3;
                                          },
                                          {}});
  assert (registry.seal ().size () == 4);
  fails ("ESHUTDOWN", [&] {
    registry.registerExtension ("public.late", [] (const auto&, const auto&, const auto&) { return "null"; });
  });
  ExecutionTask task{"id", "public.monitor", "authority", "account", "target", "read"};
  auto wrong = task;
  wrong.accountRef = "different";
  auto denied = registry.begin (CallbackType::Continuous, wrong);
  assert (!registry.authorize (denied));
  fails ("EACCES", [&] { registry.run (denied); });
  registry.close (denied);
  const auto active = registry.begin (CallbackType::Continuous, task);
  assert (registry.authorize (active));
  fails ("EINVAL", [&] { registry.authorize (active); });
  std::string result;
  std::thread worker ([&] {
    try {
      registry.run (active);
    } catch (const StateError& error) {
      result = error.code;
    }
  });
  while (!entered)
    std::this_thread::sleep_for (std::chrono::milliseconds (1));
  registry.cancel (active);
  fails ("EBUSY", [&] { registry.close (active); });
  assert (closed == 0);
  release = true;
  worker.join ();
  assert (result == "ABORT_ERR");
  cleanup = true;
  std::thread closer ([&] { registry.close (active); });
  std::atomic<bool> drained = false;
  std::thread drain ([&] {
    registry.drain ();
    drained = true;
  });
  std::this_thread::sleep_for (std::chrono::milliseconds (30));
  assert (!drained && closed == 0);
  cleanup = false;
  closer.join ();
  drain.join ();
  assert (closed == 1);
  task.kind = "public.cleanup";
  fails ("EACCES", [&] { registry.begin (CallbackType::Maintenance, task); });
  task.operationRef = "revoke";
  auto maintenance = registry.begin (CallbackType::Maintenance, task);
  registry.cancel (maintenance);
  fails ("ABORT_ERR", [&] { registry.authorize (maintenance); });
  registry.close (maintenance);
  task.kind = "public.alert";
  auto push = registry.begin (CallbackType::Push, task);
  assert (!registry.push (push, "message", RecordBytes{1, 2, 3}) && messages == 0);
  registry.close (push);
  allowed = true;
  push = registry.begin (CallbackType::Push, task);
  assert (registry.push (push, "message", RecordBytes{1, 2, 3}) && messages == 1);
  fails ("EINVAL", [&] { registry.push (push, "message", RecordBytes{1, 2, 3}); });
  registry.close (push);
  task.kind = "public.metadata";
  auto extension = registry.begin (CallbackType::Extension, task);
  assert (registry.extension (extension, "read", "{}") == "{\"public\":true}");
  registry.close (extension);
  extension = registry.begin (CallbackType::Extension, task);
  fails ("E2BIG", [&] { registry.extension (extension, "large", "{}"); });
  registry.close (extension);
  std::vector<uint64_t> retained;
  for (int i = 0; i < 64; i++)
    retained.push_back (registry.begin (CallbackType::Extension, task));
  fails ("EBUSY", [&] { registry.begin (CallbackType::Extension, task); });
  registry.shutdown ();
  fails ("ESHUTDOWN", [&] { registry.begin (CallbackType::Extension, task); });
  for (auto id : retained)
    registry.close (id);
  registry.drain ();
  std::cout << "native-callback-authorization-capacity-cancellation-cleanup-and-no-replay-passed\n";
}
