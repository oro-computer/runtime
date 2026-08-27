#ifndef ORO_RUNTIME_PLATFORM_HH
#define ORO_RUNTIME_PLATFORM_HH

#include "platform/system.hh"
#include "platform/types.hh"

namespace oro::runtime {
  using types::Array;
  using types::Atomic;
  using types::AtomicBool;
  using types::AtomicInt;
  using types::BinarySemaphore;
  using types::ConditionVariable;
  using types::ConditionVariableAny;
  using types::Error;
  using types::Exception;
  using types::ExitCallback;
  using types::Function;
  using types::InputFileStream;
  using types::InputStreamBufferIterator;
  using types::Lock;
  using types::Map;
  using types::MessageCallback;
  using types::Mutex;
  using types::OutputFileStream;
  using types::Path;
  using types::Promise;
  using types::Queue;
  using types::ScopedLock;
  using types::Semaphore;
  using types::Set;
  using types::SharedPointer;
  using types::String;
  using types::StringStream;
  using types::Thread;
  using types::Tuple;
  using types::UniqueLock;
  using types::UniquePointer;
  using types::UnorderedMap;
  using types::UnorderedSet;
  using types::Vector;
  using types::WString;
  using types::WStringStream;

  struct RuntimePlatform {
    const String arch;
    const String os;
    bool mac = false;
    bool ios = false;
    bool win = false;
    bool android = false;
    bool linux = false;
    bool unix = false;
  };

  extern const RuntimePlatform platform;
  void msleep (uint64_t ms);
}
#endif
