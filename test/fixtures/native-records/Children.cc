#include "src/runtime/native/callbacks.hh"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>

using namespace oro::runtime::native;
using namespace std::chrono_literals;
template <class F>
void fails (const std::string& code, F work) {
  try {
    work ();
    assert (false);
  } catch (const StateError& error) {
    assert (error.code == code);
  }
}
struct Event {
  std::mutex mutex;
  std::condition_variable changed;
  bool value = false;
  void set () {
    std::lock_guard lock (mutex);
    value = true;
    changed.notify_all ();
  }
  void wait () {
    std::unique_lock lock (mutex);
    assert (changed.wait_for (lock, 5s, [&] { return value; }));
  }
};
int main () {
  ExecutionCancellation cancelled;
  cancelled.cancel ();
  Event observed;
  auto early = cancelled.onCancel ([&] { observed.set (); });
  observed.wait ();
  Event expired;
  ExecutionCancellation deadline (20);
  auto timed = deadline.onCancel ([&] { expired.set (); });
  expired.wait ();
  assert (deadline.cancelled ());
  std::atomic<int> removed = 0;
  ExecutionCancellation removedSignal;
  { auto registration = removedSignal.onCancel ([&] { removed++; }); }
  removedSignal.cancel ();
  assert (removed == 0);
  ExecutionCancellation callbacks;
  Event listenerEntered;
  Event releaseListener;
  auto listener = callbacks.onCancel ([&] { listenerEntered.set (); releaseListener.wait (); });
  callbacks.cancel ();
  listenerEntered.wait ();
  auto resetting = std::async (std::launch::async, [&] { listener.reset (); });
  assert (resetting.wait_for (30ms) == std::future_status::timeout);
  releaseListener.set ();
  resetting.get ();

  CallbackRegistry registry;
  std::vector<std::shared_ptr<ExecutionGroup>> groups;
  registry.registerExtension ("public.children", [&] (const auto&, const auto&, const auto& signal) {
    groups.push_back (registry.children (signal, 8));
    return "{}";
  });
  registry.seal ();
  const auto first = registry.begin (CallbackType::Extension, {"", "public.children"});
  registry.extension (first, "open", "{}");
  const auto second = registry.begin (CallbackType::Extension, {"", "public.children"});
  registry.extension (second, "open", "{}");
  ExecutionCancellation unowned;
  fails ("EACCES", [&] { registry.children (unowned); });
  Event blocked;
  Event entered;
  Event cancellation;
  const auto stubborn = groups[0]->start ([&] (const auto& signal) {
    assert (registry.ownsInvocationThread ());
    fails ("EDEADLK", [&] { registry.drain (); });
    auto hook = signal.onCancel ([&] { cancellation.set (); });
    entered.set ();
    blocked.wait ();
  });
  entered.wait ();
  groups[0]->cancel (stubborn);
  cancellation.wait ();
  assert (!groups[0]->status (stubborn).finished);
  std::vector<uint64_t> others;
  for (int index = 0; index < 7; index++)
    others.push_back (groups[1]->start ([] (const auto&) {}));
  // Finished but unjoined children and a cancelled uncooperative child retain all eight slots.
  fails ("EBUSY", [&] { groups[1]->start ([] (const auto&) {}); });
  groups[1]->join (others.back ());
  groups[1]->start ([] (const auto&) {});
  fails ("ENOENT", [&] { groups[1]->cancel (others.back ()); });
  auto closing = std::async (std::launch::async, [&] { registry.close (first); });
  assert (closing.wait_for (30ms) == std::future_status::timeout);
  fails ("EBUSY", [&] { groups[1]->start ([] (const auto&) {}); });
  blocked.set ();
  closing.get ();
  assert (!registry.ownsInvocationThread ());
  fails ("ESHUTDOWN", [&] { groups[0]->start ([] (const auto&) {}); });
  registry.cancel (second);
  fails ("ABORT_ERR", [&] { groups[1]->start ([] (const auto&) {}); });
  registry.close (second);
  registry.shutdown ();
  registry.drain ();
  std::cout << "native-event-cancellation-owned-children-shared-capacity-joined-cleanup-and-no-replay-passed\n";
}
