#include "../udp.hh"

namespace oro::runtime::udp {
  SocketManager::SocketManager (const Options& options)
    : loop(options.loop)
  {}

  SocketManager::~SocketManager () {}

  void SocketManager::resume () {
    // Schedule resume on the loop thread and guard iteration with the mutex
    this->loop.dispatch([=, this]() {
      Lock lock(this->mutex);
      for (const auto& entry : this->sockets) {
        auto& socket = entry.second;
        if (socket != nullptr /* && (socket->isBound() || socket->isConnected()) */) {
          socket->resume();
        }
      }
    });
  }

  void SocketManager::pause () {
    // Ensure handle operations occur on the loop thread
    this->loop.dispatch([=, this]() {
      this->pauseOnLoopThread();
    });
  }

  void SocketManager::pauseOnLoopThread () {
    Lock lock(this->mutex);
    for (const auto& entry : this->sockets) {
      auto& socket = entry.second;
      if (socket != nullptr /* && (socket->isBound() || socket->isConnected()) */) {
        socket->pause();
      }
    }
  }

  bool SocketManager::has (ID id) {
    Lock lock(this->mutex);
    return this->sockets.contains(id);
  }

  void SocketManager::remove (ID id) {
    return this->remove(id, false);
  }

  void SocketManager::remove (ID id, bool autoClose) {
    if (this->has(id)) {
      if (autoClose) {
        auto socket = this->get(id);
        if (socket != nullptr) {
          socket->close();
        }
      }

      Lock lock(this->mutex);
      this->sockets.erase(id);
    }
  }

  SharedPointer<Socket> SocketManager::get (ID id) {
    Lock lock(this->mutex);
    if (this->sockets.contains(id)) {
      return this->sockets.at(id);
    }
    return nullptr;
  }

  SharedPointer<Socket> SocketManager::create (socket_type_t type, ID id) {
    return this->create(type, id, false);
  }

  SharedPointer<Socket> SocketManager::create (
    socket_type_t type,
    ID id,
    bool isEphemeral
  ) {
    if (this->has(id)) {
      auto socket = this->get(id);
      if (socket != nullptr) {
        if (isEphemeral) {
          Lock lock(socket->mutex);
          socket->flags = (udp::socket_flag_t) (socket->flags | udp::SOCKET_FLAG_EPHEMERAL);
        }
      }

      return socket;
    }

    Lock lock(this->mutex);
    this->sockets[id] = std::make_shared<Socket>(this, type, id, isEphemeral);
    return this->sockets.at(id);
  }
}
