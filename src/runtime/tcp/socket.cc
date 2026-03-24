#include "../tcp.hh"

#include <cstdio>
#include <cstring>
#include <limits>

#if defined(_WIN32)
#  include <ws2tcpip.h>
#else
#  include <netdb.h>
#endif

namespace oro::runtime::tcp {
  struct Socket::ConnectContext {
    Socket* self = nullptr;
    uv_getaddrinfo_t addrreq{};
    uv_connect_t connreq{};
    sockaddr_storage addr{};
    int addrlen = 0;
    bool connectStarted = false;
    bool cancelled = false;
  };

  void Socket::onAddrinfoResolved (uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    auto ctx = static_cast<Socket::ConnectContext*>(req ? req->data : nullptr);
    if (!ctx) {
      if (res) uv_freeaddrinfo(res);
      return;
    }
    auto self = ctx->self;
    if (!self) {
      if (res) uv_freeaddrinfo(res);
      delete ctx;
      return;
    }
    self->handleResolveResult(ctx, status, res);
  }

  void Socket::onConnectCompleted (uv_connect_t* req, int status) {
    auto ctx = static_cast<Socket::ConnectContext*>(req ? req->data : nullptr);
    if (!ctx) return;
    auto self = ctx->self;
    if (!self) {
      delete ctx;
      return;
    }
    self->handleConnectResult(ctx, status);
  }

  Socket::Socket (SocketManager* manager, uint64_t id)
    : manager(manager), id(id) {
    memset(&this->handle, 0, sizeof(this->handle));
  }

  Socket::~Socket () {
    this->cancelPendingConnect();
  }

  int Socket::init () {
    Lock lock(this->mutex);
    auto loop = this->manager->loop.get();
    return uv_tcp_init(loop, &this->handle.tcp);
  }

  bool Socket::has (socket_state_t s) { return (this->state & s) == s; }
  void Socket::add (socket_state_t s) { this->state = (socket_state_t)(this->state | s); }
  void Socket::remove (socket_state_t s) { this->state = (socket_state_t)(this->state & ~s); }
  bool Socket::isClosing () { return uv_is_closing(reinterpret_cast<uv_handle_t*>(&this->handle.tcp)); }
  bool Socket::isActive () { return uv_is_active(reinterpret_cast<uv_handle_t*>(&this->handle.tcp)); }

  int Socket::bind (const String& address, int port) {
    Lock lock(this->mutex);
    int err = 0;
    bool needsResolve = false;
    String host = address.empty() ? String("0.0.0.0") : address;
    auto loop = this->manager ? this->manager->loop.get() : nullptr;

    if (host.find(':') != String::npos) {
      struct sockaddr_in6 addr6{};
      err = uv_ip6_addr(host.c_str(), port, &addr6);
      if (!err) {
        err = uv_tcp_bind(&this->handle.tcp, reinterpret_cast<const struct sockaddr*>(&addr6), 0);
        if (!err) { this->add(TCP_STATE_BOUND); return 0; }
        return err;
      }
      if (err == UV_EINVAL) {
        needsResolve = true;
      } else {
        return err;
      }
    } else {
      struct sockaddr_in addr4{};
      err = uv_ip4_addr(host.c_str(), port, &addr4);
      if (!err) {
        err = uv_tcp_bind(&this->handle.tcp, reinterpret_cast<const struct sockaddr*>(&addr4), 0);
        if (!err) { this->add(TCP_STATE_BOUND); return 0; }
        return err;
      }
      if (err == UV_EINVAL) {
        needsResolve = true;
      } else {
        return err;
      }
    }

    if (!needsResolve) return err;

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char service[16] = {0};
    std::snprintf(service, sizeof(service), "%d", port);

    if (!loop) return UV_EINVAL;

    uv_getaddrinfo_t req{};
    int aiErr = uv_getaddrinfo(loop, &req, nullptr, host.c_str(), service, &hints);
    if (aiErr) return aiErr;

    int bindErr = UV_EADDRNOTAVAIL;
    for (auto ai = req.addrinfo; ai; ai = ai->ai_next) {
      if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6) continue;
      bindErr = uv_tcp_bind(&this->handle.tcp, ai->ai_addr, 0);
      if (!bindErr) {
        this->add(TCP_STATE_BOUND);
        break;
      }
    }
    uv_freeaddrinfo(req.addrinfo);
    return bindErr;
  }

  int Socket::listen (int backlog, ConnectionCallback cb) {
    Lock lock(this->mutex);
    this->onconnection = cb;
    // Ensure handle->data points back to this socket
    uv_handle_set_data(reinterpret_cast<uv_handle_t*>(&this->handle.tcp), this);
    int err = uv_listen(
      reinterpret_cast<uv_stream_t*>(&this->handle.tcp),
      backlog,
      [](uv_stream_t* server, int status) {
        auto self = static_cast<Socket*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(server)));
        if (!self) return;
        if (status >= 0) {
          if (self->onconnection) self->onconnection(status);
        } else {
          if (self->onconnection) self->onconnection(status);
        }
      }
    );
    if (!err) this->add(TCP_STATE_LISTENING);
    return err;
  }

  int Socket::accept (Socket* client) {
    auto server = reinterpret_cast<uv_stream_t*>(&this->handle.tcp);
    auto cli = reinterpret_cast<uv_stream_t*>(&client->handle.tcp);
    int err = uv_accept(server, cli);
    if (!err) client->add(TCP_STATE_CONNECTED);
    return err;
  }

  int Socket::connect (const String& address, int port, Function<void(int)> done) {
    Lock lock(this->mutex);
    if (this->has(TCP_STATE_CONNECTING) || this->has(TCP_STATE_CONNECTED)) {
      return UV_EALREADY;
    }
    auto loop = this->manager->loop.get();
    auto ctx = new ConnectContext();
    ctx->self = this;
    ctx->addrreq.data = ctx;
    ctx->connreq.data = ctx;
    ctx->addrlen = 0;
    ctx->connectStarted = false;
    ctx->cancelled = false;

    this->onconnect = done;
    this->connectCtx = ctx;
    this->add(TCP_STATE_CONNECTING);

    char service[16] = {0};
    std::snprintf(service, sizeof(service), "%d", port);

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int err = uv_getaddrinfo(loop, &ctx->addrreq, Socket::onAddrinfoResolved, address.c_str(), service, &hints);
    if (err) {
      this->remove(TCP_STATE_CONNECTING);
      this->connectCtx = nullptr;
      this->onconnect = nullptr;
      delete ctx;
    }
    return err;
  }

  void Socket::cleanupConnectContext (ConnectContext* ctx) {
    if (this->connectCtx == ctx) {
      this->connectCtx = nullptr;
    }
    delete ctx;
  }

  void Socket::handleResolveResult (ConnectContext* ctx, int status, struct addrinfo* res) {
    Function<void(int)> cb = nullptr;
    int cbStatus = status;
    bool invoke = false;

    {
      Lock lock(this->mutex);
      if (this->connectCtx != ctx) {
        // Already cleaned elsewhere; nothing to do besides freeing addrinfo.
      } else if (ctx->cancelled) {
        this->remove(TCP_STATE_CONNECTING);
        this->onconnect = nullptr;
        this->cleanupConnectContext(ctx);
      } else if (status < 0) {
        this->remove(TCP_STATE_CONNECTING);
        cb = this->onconnect;
        this->onconnect = nullptr;
        this->cleanupConnectContext(ctx);
        invoke = cb != nullptr;
      } else {
        struct addrinfo* selected = nullptr;
        for (auto ai = res; ai; ai = ai->ai_next) {
          if (ai->ai_family == AF_INET || ai->ai_family == AF_INET6) {
            selected = ai;
            break;
          }
        }
        if (!selected) {
          this->remove(TCP_STATE_CONNECTING);
          cb = this->onconnect;
          this->onconnect = nullptr;
          this->cleanupConnectContext(ctx);
          cbStatus = UV_EADDRNOTAVAIL;
          invoke = cb != nullptr;
        } else {
          memcpy(&ctx->addr, selected->ai_addr, selected->ai_addrlen);
          ctx->addrlen = static_cast<int>(selected->ai_addrlen);
          ctx->connectStarted = true;
          ctx->connreq.data = ctx;
          int err = uv_tcp_connect(&ctx->connreq, &this->handle.tcp, reinterpret_cast<const struct sockaddr*>(&ctx->addr), Socket::onConnectCompleted);
          if (err) {
            this->remove(TCP_STATE_CONNECTING);
            cb = this->onconnect;
            this->onconnect = nullptr;
            this->cleanupConnectContext(ctx);
            cbStatus = err;
            invoke = cb != nullptr;
          }
        }
      }
    }

    if (res) uv_freeaddrinfo(res);
    if (invoke && cb) cb(cbStatus);
  }

  void Socket::handleConnectResult (ConnectContext* ctx, int status) {
    Function<void(int)> cb = nullptr;
    bool invoke = false;

    {
      Lock lock(this->mutex);
      if (this->connectCtx != ctx) {
        // Already cleaned.
      } else {
        bool cancelled = ctx->cancelled;
        this->remove(TCP_STATE_CONNECTING);
        if (status >= 0) this->add(TCP_STATE_CONNECTED);
        cb = this->onconnect;
        this->onconnect = nullptr;
        this->cleanupConnectContext(ctx);
        if (!cancelled && cb) {
          invoke = true;
        } else {
          status = cancelled ? UV_ECANCELED : status;
        }
      }
    }

    if (invoke && cb) cb(status);
  }

  void Socket::cancelPendingConnectLocked () {
    if (!this->connectCtx) return;
    auto ctx = this->connectCtx;
    ctx->cancelled = true;
    ctx->self = nullptr;
    this->connectCtx = nullptr;
    this->onconnect = nullptr;
    this->remove(TCP_STATE_CONNECTING);
    if (!ctx->connectStarted) {
      uv_cancel(reinterpret_cast<uv_req_t*>(&ctx->addrreq));
    }
  }

  void Socket::cancelPendingConnect () {
    Lock lock(this->mutex);
    this->cancelPendingConnectLocked();
  }

  int Socket::write (const char* data, size_t len, Function<void(int)> done) {
    Lock lock(this->mutex);
    if (this->isClosing()) {
      return UV_ECANCELED;
    }
    if (len > std::numeric_limits<unsigned int>::max()) {
      return UV_EINVAL;
    }
    struct WriteCtx { char* base; size_t len; Function<void(int)> cb; };
    auto ctx = new WriteCtx{ nullptr, len, done };
    ctx->base = new char[len];
    memcpy(ctx->base, data, len);
    auto req = new uv_write_t;
    req->data = ctx;
    uv_buf_t buf = uv_buf_init(ctx->base, static_cast<unsigned int>(len));
    int err = uv_write(
      req,
      reinterpret_cast<uv_stream_t*>(&this->handle.tcp),
      &buf,
      1,
      [](uv_write_t* r, int status){
        auto ctx = static_cast<WriteCtx*>(r->data);
        auto cb = ctx->cb;
        delete[] ctx->base;
        delete ctx;
        if (cb) cb(status);
        delete r;
      }
    );
    if (err < 0) {
      auto cb = ctx->cb;
      delete[] ctx->base;
      delete ctx;
      delete req;
      if (cb) cb(err);
    }
    return err;
  }

  int Socket::setNoDelay (bool on) {
    Lock lock(this->mutex);
    int err = uv_tcp_nodelay(&this->handle.tcp, on ? 1 : 0);
    if (!err) this->nodelay = on;
    return err;
  }

  int Socket::setKeepAlive (bool on, unsigned int delaySec) {
    Lock lock(this->mutex);
    int err = uv_tcp_keepalive(&this->handle.tcp, on ? 1 : 0, delaySec);
    if (!err) { this->keepalive = on; this->keepaliveDelay = delaySec; }
    return err;
  }

  int Socket::readStart (ReadCallback cb) {
    Lock lock(this->mutex);
    if (this->has(TCP_STATE_RECV_STARTED)) {
      return UV_EALREADY;
    }
    this->onread = cb;
    // Ensure handle->data points to this for read callbacks
    uv_handle_set_data(reinterpret_cast<uv_handle_t*>(&this->handle.tcp), this);
    int err = uv_read_start(
      reinterpret_cast<uv_stream_t*>(&this->handle.tcp),
      [](uv_handle_t* h, size_t size, uv_buf_t* buf) {
        if (buf && size > 0) {
          buf->base = new char[size]{0};
          buf->len = size;
        } else if (buf) {
          buf->base = nullptr;
          buf->len = 0;
        }
      },
      [](uv_stream_t* s, ssize_t nread, const uv_buf_t* buf) {
        auto self = static_cast<Socket*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(s)));
        auto base = buf ? buf->base : nullptr;
        if (!self) {
          if (base) {
            delete[] base;
          }
          return;
        }

        if (nread > 0) {
          if (self->onread) {
            self->onread(nread, buf);
          }
        } else if (nread == UV_EOF) {
          // Signal EOF: zero-length with no buffer transfer; free buffer
          if (self->onread) {
            self->onread(0, nullptr);
          }
          if (base) {
            delete[] base;
          }
          return;
        } else if (nread < 0) {
          // Non-EOF error: free buffer and signal with sentinel negative size
          if (self->onread) {
            self->onread(nread, nullptr);
          }
          if (base) {
            delete[] base;
          }
          return;
        }

        // Free when not transferred/consumed
        if (!(nread > 0)) {
          if (base) {
            delete[] base;
          }
        }
      }
    );
    if (!err) this->add(TCP_STATE_RECV_STARTED);
    return err;
  }

  int Socket::readStop () {
    Lock lock(this->mutex);
    if (!this->has(TCP_STATE_RECV_STARTED)) return 0;
    int err = uv_read_stop(reinterpret_cast<uv_stream_t*>(&this->handle.tcp));
    if (!err) this->remove(TCP_STATE_RECV_STARTED);
    return err;
  }

  int Socket::pause () {
    int err = this->readStop();
    this->add(TCP_STATE_PAUSED);
    return err;
  }

  int Socket::resume () {
    this->remove(TCP_STATE_PAUSED);
    return 0;
  }

  int Socket::shutdown (Function<void(int)> done) {
    Lock lock(this->mutex);
    struct ShutdownCtx { Function<void(int)> cb; };
    auto req = new uv_shutdown_t;
    req->data = new ShutdownCtx{ done };
    int err = uv_shutdown(
      req,
      reinterpret_cast<uv_stream_t*>(&this->handle.tcp),
      [](uv_shutdown_t* r, int status){
        auto ctx = static_cast<ShutdownCtx*>(r->data);
        Function<void(int)> cb;
        if (ctx) { cb = ctx->cb; delete ctx; }
        if (cb) cb(status);
        delete r;
      }
    );
    if (err < 0) {
      auto ctx = static_cast<ShutdownCtx*>(req->data);
      Function<void(int)> cb;
      if (ctx) { cb = ctx->cb; delete ctx; }
      if (cb) cb(err);
      delete req;
    }
    return err;
  }

  void Socket::close (Function<void()> onclose) {
    Lock lock(this->mutex);
    if (this->isClosing()) { if (onclose) onclose(); return; }
    // Stop reads to avoid callbacks racing while we close.
    uv_read_stop(reinterpret_cast<uv_stream_t*>(&this->handle.tcp));
    this->remove(TCP_STATE_RECV_STARTED);
    this->cancelPendingConnectLocked();
    this->onconnect = nullptr;
    this->onconnection = nullptr;
    this->onread = nullptr;
    this->onclose_cb = onclose;
    auto h = reinterpret_cast<uv_handle_t*>(&this->handle.tcp);
    // Preserve existing handle->data pointing to this Socket
    uv_close(h, [](uv_handle_t* hdl){
      auto self = static_cast<Socket*>(uv_handle_get_data(hdl));
      if (self) {
        uv_handle_set_data(hdl, nullptr);
        self->state = TCP_STATE_NONE;
        if (self->onclose_cb) {
          auto cb = self->onclose_cb;
          self->onclose_cb = nullptr;
          cb();
        }
      }
    });
  }

  SharedPointer<Socket> SocketManager::create (ID id, int& err) {
    Lock lock(this->mutex);
    auto s = std::make_shared<Socket>(this, id);
    err = s->init();
    if (err) return nullptr;
    uv_handle_set_data(reinterpret_cast<uv_handle_t*>(&s->handle.tcp), s.get());
    sockets[id] = s;
    return s;
  }

  SharedPointer<Socket> SocketManager::get (ID id) {
    Lock lock(this->mutex);
    if (!sockets.contains(id)) return nullptr;
    return sockets.at(id);
  }

  void SocketManager::remove (ID id) {
    Lock lock(this->mutex);
    sockets.erase(id);
  }

  void SocketManager::pauseOnLoopThread () {
    Lock lock(this->mutex);
    for (auto& entry : sockets) {
      auto s = entry.second;
      if (s) s->pause();
    }
  }
}
