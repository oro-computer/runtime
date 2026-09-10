#include "../bridge.hh"
#include "../crypto.hh"
#include "../string.hh"
#include "../ipc.hh"

using oro::runtime::string::toLowerCase;
using oro::runtime::crypto::rand64;

namespace oro::runtime::ipc {
  Router::Router (bridge::Bridge& bridge)
    : dispatcher(bridge.dispatcher),
      bridge(bridge)
  {}

  void Router::init () {
    this->mapRoutes();
    this->preserveCurrentTable();
  }

  void Router::preserveCurrentTable () {
    Lock lock(this->mutex);
    this->preserved = this->table;
  }

  uint64_t Router::listen (
    const String& name,
    const MessageCallback callback
  ) {
    const auto key = toLowerCase(name);

    Lock lock(this->mutex);
    if (!this->listeners.contains(key)) {
      this->listeners[key] = Vector<MessageCallbackListenerContext>();
    }

    auto& listeners = this->listeners.at(key);
    const auto token = rand64();
    listeners.push_back(MessageCallbackListenerContext { token , callback });
    return token;
  }

  bool Router::unlisten (const String& name, uint64_t token) {
    const auto key = toLowerCase(name);
    Lock lock(this->mutex);
    if (!this->listeners.contains(key)) {
      return false;
    }

    auto& listeners = this->listeners.at(key);
    for (int i = 0; i < listeners.size(); ++i) {
      const auto& listener = listeners[i];
      if (listener.token == token) {
        listeners.erase(listeners.begin() + i);
        return true;
      }
    }

    return false;
  }

  void Router::map (const String& name, const MessageCallback callback) {
    return this->map(name, true, std::move(callback));
  }

  void Router::map (
    const String& name,
    bool async,
    const MessageCallback callback
  ) {
    if (callback != nullptr) {
      const auto key = toLowerCase(name);
      Lock lock(this->mutex);
      this->table.insert_or_assign(key, MessageCallbackContext {
        async,
        callback
      });
    }
  }

  void Router::unmap (const String& name) {
    Lock lock(this->mutex);
    this->table.erase(toLowerCase(name));
  }

  bool Router::invoke (
    const String& uri,
    SharedPointer<unsigned char[]> bytes,
    size_t size
  ) {
    const auto weak = this->bridge.weak_from_this();
    return this->invoke(uri, bytes, size, [weak](auto result) {
      if (const auto bridge = weak.lock()) {
        bridge->dispatcher.dispatch([weak, result] () {
          if (const auto bridge = weak.lock()) {
            bridge->send(result.seq, result.str(), result.queuedResponse);
          }
        });
      }
    });
  }

  bool Router::invoke (const String& uri, const ResultCallback callback) {
    return this->invoke(uri, nullptr, 0, callback);
  }

  bool Router::invoke (
    const String& uri,
    SharedPointer<unsigned char[]> bytes,
    size_t size,
    const ResultCallback callback
  ) {
    if (!this->bridge.active()) {
      return false;
    }

    const auto message = Message(uri, true);
    return this->invoke(std::move(message), bytes, size, std::move(callback));
  }

  bool Router::invoke (
    const Message& message,
    SharedPointer<unsigned char[]> bytes,
    size_t size,
    const ResultCallback callback
  ) {
    if (!this->bridge.active()) {
      return false;
    }

    const auto name = toLowerCase(message.name);
    MessageCallbackContext context;
    Vector<MessageCallbackListenerContext> namedListeners;
    Vector<MessageCallbackListenerContext> wildcardListeners;

    {
      Lock lock(this->mutex);
      // lookup router function in the preserved table,
      // then the public table, return if unable to determine a context
      if (this->preserved.contains(name)) {
        context = this->preserved.at(name);
      } else if (this->table.contains(name)) {
        context = this->table.at(name);
      } else {
        return false;
      }

      if (this->listeners.contains(name)) {
        namedListeners = this->listeners.at(name);
      }

      if (this->listeners.contains("*")) {
        wildcardListeners = this->listeners.at("*");
      }
    }

    if (context.callback == nullptr) {
      return false;
    }

    auto incomingMessage = Message(message);

    if (bytes != nullptr && size > 0) {
      incomingMessage.buffer = bytes::ArrayBuffer(size, bytes);
    }

    // named listeners
    for (const auto& listener : namedListeners) {
      listener.callback(incomingMessage, this, [](const auto& _) {});
    }

    // wild card (*) listeners
    for (const auto& listener : wildcardListeners) {
      listener.callback(incomingMessage, this, [](const auto& _) {});
    }

    const auto weak = this->bridge.weak_from_this();
    const auto reply = [weak, callback](const auto result) {
      const auto bridge = weak.lock();
      if (bridge == nullptr) {
        return;
      }

      if (result.seq == "-1") {
        bridge->dispatcher.dispatch([weak, result] {
          if (const auto bridge = weak.lock()) {
            bridge->send(result.seq, result.str(), result.queuedResponse);
          }
        });
      } else {
        callback(result);
      }
    };

    if (context.async) {
      // A window can close before the UI queue reaches this route. Resolve its
      // bridge at execution time and keep it alive while calling the handler.
      auto invokeRoute = [weak, context, reply, incomingMessage]() mutable {
        if (const auto bridge = weak.lock()) {
          context.callback(incomingMessage, &bridge->router, reply);
        }
      };

      if (this->bridge.dispatchRouterCallbacksWithBridge) {
        return this->bridge.dispatch(std::move(invokeRoute));
      }

      return this->dispatcher.dispatch(std::move(invokeRoute));
    }

    context.callback(incomingMessage, this, reply);

    return true;
  }
}
