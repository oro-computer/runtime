#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
#include <cassert>
#include <atomic>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <thread>

static std::atomic<int> destroyedTasks = 0;
static std::atomic<int> deliveredEvents = 0;
template <typename T> using SharedPointer = std::shared_ptr<T>;

// Model WebKit's synchronous hop to the main run loop. The main thread waits
// for native I/O in the replay, just as it does while pausing the runtime.
@interface Task : NSObject
@property (retain) NSMutableArray* events;
- (void) didReceiveResponse: (NSHTTPURLResponse*) response;
- (void) didReceiveData: (NSData*) data;
- (void) didFinish;
- (void) didFailWithError: (NSError*) error;
@end

@implementation Task
- (instancetype) init {
  self = [super init];
  if (self) self.events = [NSMutableArray array];
  return self;
}
- (void) record: (NSString*) event {
  if (![NSThread isMainThread]) {
    dispatch_sync(dispatch_get_main_queue(), ^{ [self record: event]; });
    return;
  }
  [self.events addObject: event];
  ++deliveredEvents;
}
- (void) didReceiveResponse: (NSHTTPURLResponse*) response {
  assert(response.statusCode == 200);
  [self record: @"head"];
}
- (void) didReceiveData: (NSData*) data {
  assert(data.length == 4 && memcmp(data.bytes, "body", 4) == 0);
  [self record: @"data"];
}
- (void) didFinish { [self record: @"finish"]; }
- (void) didFailWithError: (NSError*) error {
  assert(error.code == 1);
  [self record: @"fail"];
}
- (void) dealloc {
  assert([NSThread isMainThread]);
  ++destroyedTasks;
#if !__has_feature(objc_arc)
  [_events release];
  [super dealloc];
#endif
}
@end

// The production helper only needs task registration and the handler's liveness.
@interface OROInternalWKURLSchemeHandler : NSObject
@property void* handlers;
@property (retain) NSMutableSet* tasks;
- (bool) waitingForTask: (Task*) task;
- (void) finalizeTask: (Task*) task;
@end

@implementation OROInternalWKURLSchemeHandler
- (instancetype) init {
  self = [super init];
  if (self) self.tasks = [NSMutableSet set];
  return self;
}
- (bool) waitingForTask: (Task*) task { return [self.tasks containsObject: task]; }
- (void) finalizeTask: (Task*) task { [self.tasks removeObject: task]; }
#if !__has_feature(objc_arc)
- (void) dealloc {
  [_tasks release];
  [super dealloc];
}
#endif
@end

// Match the production Task alias while keeping the replay independent of WebKit.
using NativeTask = Task*;
#define Task NativeTask
#include "task-dispatch.hh"
#undef Task

struct Internals { OROInternalWKURLSchemeHandler* schemeHandler; };
struct Handlers { Internals* internals; };
struct Request {
  Task* platformRequest;
  SharedPointer<void> platformRequestOwner;
  explicit Request (Task* task) : platformRequest(task) { this->retainTask(); }
  void retainTask ();
};
struct Response {
  Request* request;
  Handlers* handlers;
  Task* platformRequest;
  NSHTTPURLResponse* platformResponse;
  void head ();
  void data ();
  void finish ();
  void fail ();
};

#define Task NativeTask
#include "task-delivery.hh"
#undef Task

static void drainMainQueue () {
  __block bool drained = false;
  dispatch_async(dispatch_get_main_queue(), ^{ drained = true; });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!drained && std::chrono::steady_clock::now() < deadline) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
  }
  assert(drained);
}

static void replyFromNativeIO (Task* task, OROInternalWKURLSchemeHandler* handler, bool fail) {
  std::promise<void> returned;
  auto completed = returned.get_future();
  std::thread worker([&]() {
    @autoreleasepool {
      Internals internals { handler };
      Handlers handlers { &internals };
      Request request { task };
      auto head = [[NSHTTPURLResponse alloc] initWithURL:[NSURL URLWithString:@"ipc://fs.access"]
        statusCode:200 HTTPVersion:@"HTTP/1.1" headerFields:@{}];
      Response response { &request, &handlers, task, head };
      response.head();
      response.data();
      if (fail) response.fail();
      else response.finish();
#if !__has_feature(objc_arc)
      [head release];
#endif
    }
    returned.set_value();
  });
  // Do not pump the main queue until I/O returns. Direct WebKit calls would
  // deadlock here, before a queued Conduit shutdown could run on the I/O loop.
  assert(completed.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
  worker.join();
  assert(task.events.count == 0);
}

static void completeAfterCancellation (OROInternalWKURLSchemeHandler* handler) {
  const auto destroyedBefore = destroyedTasks.load();
  const auto deliveredBefore = deliveredEvents.load();
  auto task = [Task new];
  auto request = std::make_shared<Request>(task);
  [handler.tasks addObject: task];
  // The real completion callback retains the C++ request, but captures the
  // Objective-C task as a borrowed pointer when ARC is disabled.
  std::function<void()> complete = [request, task, handler]() {
    assert(request->platformRequestOwner != nullptr);
    Internals internals { handler };
    Handlers handlers { &internals };
    Response response { request.get(), &handlers, task, nullptr };
    response.data();
    response.finish();
    finalizeCompletedTask(handler, task);
  };
  [handler finalizeTask: task];
#if !__has_feature(objc_arc)
  [task release];
#endif
  task = nil;
  // Window closure also clears the public platform request before native
  // callbacks disappear. Its ownership must survive that field being cleared.
  request->platformRequest = nullptr;
  assert(destroyedTasks == destroyedBefore);
  std::thread worker([&]() {
    @autoreleasepool { complete(); }
  });
  worker.join();
  complete = nullptr;
  request.reset();
  drainMainQueue();
  assert(deliveredEvents == deliveredBefore);
  assert(destroyedTasks == destroyedBefore + 1);
}

int main () {
  @autoreleasepool {
    auto handler = [OROInternalWKURLSchemeHandler new];
    handler.handlers = &handler;
    completeAfterCancellation(handler);
    auto task = [Task new];
    [handler.tasks addObject: task];
    replyFromNativeIO(task, handler, false);
    finalizeCompletedTask(handler, task);
    drainMainQueue();
    assert([task.events isEqualToArray:(@[@"head", @"data", @"finish"])]);
    assert(![handler waitingForTask: task]);

    [task.events removeAllObjects];
    [handler.tasks addObject: task];
    replyFromNativeIO(task, handler, true);
    drainMainQueue();
    assert([task.events isEqualToArray:(@[@"head", @"data", @"fail"])]);

    [task.events removeAllObjects];
    replyFromNativeIO(task, handler, false);
    [handler finalizeTask: task];
    drainMainQueue();
    assert(task.events.count == 0);

    [handler.tasks addObject: task];
    replyFromNativeIO(task, handler, false);
    handler.handlers = nullptr;
    drainMainQueue();
    assert(task.events.count == 0);
#if !__has_feature(objc_arc)
    [task release];
    [handler release];
#endif
  }
}
