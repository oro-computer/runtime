#include "../debug.hh"
#include "../concurrent.hh"

namespace oro::runtime::concurrent {
  static void runWorkerQueueThread (WorkerQueue* queue) {
    while (true) {
      SharedPointer<Queue::Entry> baseEntry;
      SharedPointer<WorkerQueue::Entry> entry;
      {
        UniqueLock lock(queue->mutex);
        queue->condition.wait(lock, [queue]() {
          return queue->destroyed() || !queue->empty();
        });

        if (queue->destroyed() && queue->entries.empty()) {
          return;
        }

        if (!queue->entries.empty()) {
          baseEntry = queue->entries.front();
          queue->entries.pop();
        }
      }
      if (baseEntry) {
        entry = std::static_pointer_cast<WorkerQueue::Entry>(baseEntry);
      }
      if (entry == nullptr || entry->work == nullptr) {
        continue;
      }

      try {
        entry->work();
      } catch (const Exception& e) {
        debug("WorkerQueue::Thread work handler exception: %s", e.what());
      }

      if (entry->callback != nullptr) {
        try {
          entry->callback();
        } catch (const Exception& e) {
          debug("WorkerQueue::Thread callback handler exception: %s", e.what());
        }
      }
    }
  }

  Queue::Queue (const Options& options)
    : limit(options.limit),
      semaphore(options.limit)
  {}

  Queue::~Queue () {
    this->destroy();
  }

  bool Queue::destroyed () const {
    return this->isDestroyed.load(std::memory_order_relaxed);
  }

  void Queue::destroy () {
    if (this->isDestroyed.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    Lock lock(this->mutex);
    this->condition.notify_all();
  }

  size_t Queue::size () const {
    Lock lock(this->mutex);
    return this->entries.size();
  }

  bool Queue::empty () const {
    return this->size () == 0;
  }

  size_t Queue::push (const Entry& entry) {
    Lock lock(this->mutex);
    if (this->destroyed()) {
      return this->entries.size();
    }
    this->entries.push(std::make_shared<Entry>(entry));
    return this->entries.size();
  }

  WorkerQueue::WorkerQueue (const Options& options)
    : Queue(options) {
    const size_t n = this->limit.load();
    const size_t poolSize = n > 0 ? n : 1;
    this->threads.reserve(poolSize);
    for (size_t i = 0; i < poolSize; ++i) {
      this->threads.emplace_back(&runWorkerQueueThread, this);
    }
  }

  WorkerQueue::~WorkerQueue () {
    this->destroy();
  }

  size_t WorkerQueue::push (const Entry& entry) {
    UniqueLock lock(this->mutex);
    if (this->destroyed()) {
      return this->entries.size();
    }
    this->entries.push(std::make_shared<Entry>(entry));
    const auto currentSize = this->entries.size();
    lock.unlock();
    this->condition.notify_one();
    return currentSize;
  }

  size_t WorkerQueue::push (const WorkHandler& work, const WorkCallback& callback) {
    return this->push(Entry { crypto::rand64(), work, callback });
  }

  void WorkerQueue::destroy () {
    Queue::destroy();
    Vector<Thread> threadsToJoin;
    {
      Lock lock(this->mutex);
      threadsToJoin.swap(this->threads);
    }
    for (auto &t : threadsToJoin) {
      if (t.joinable()) t.join();
    }
  }
}
