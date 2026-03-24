#ifndef ORO_RUNTIME_TCP_H
#define ORO_RUNTIME_TCP_H

#include "platform.hh"
#include "loop.hh"
#include "queued_response.hh"

struct addrinfo;

namespace oro::runtime::tcp {
  class SocketManager;

  enum socket_state_t {
    TCP_STATE_NONE = 0,
    TCP_STATE_BOUND = 1 << 1,
    TCP_STATE_LISTENING = 1 << 2,
    TCP_STATE_CONNECTED = 1 << 3,
    TCP_STATE_RECV_STARTED = 1 << 4,
    TCP_STATE_PAUSED = 1 << 5,
    TCP_STATE_CONNECTING = 1 << 6
  };

  class Socket {
    public:
      using ReadCallback = Function<void(ssize_t, const uv_buf_t*)>;
      using ConnectionCallback = Function<void(int)>;
      using ConnectCallback = Function<void(int)>;

      union { uv_tcp_t tcp; } handle;
      Mutex mutex;
      SocketManager* manager = nullptr;
      uint64_t id = 0;
      socket_state_t state = TCP_STATE_NONE;
      ReadCallback onread = nullptr;
      ConnectionCallback onconnection = nullptr;
      ConnectCallback onconnect = nullptr;
      Function<void()> onclose_cb = nullptr;
      bool nodelay = false;
      bool keepalive = false;
      unsigned int keepaliveDelay = 0;

      Socket (SocketManager* manager, uint64_t id);
      ~Socket ();

      int init ();
      bool has (socket_state_t s);
      void add (socket_state_t s);
      void remove (socket_state_t s);
      bool isClosing ();
      bool isActive ();

      int bind (const String& address, int port);
      int listen (int backlog, ConnectionCallback cb);
      int accept (Socket* client);
      int connect (const String& address, int port, Function<void(int)> done);
      int write (const char* data, size_t len, Function<void(int)> done);
      int setNoDelay (bool on);
      int setKeepAlive (bool on, unsigned int delaySec);
      int readStart (ReadCallback cb);
      int readStop ();
      int pause ();
      int resume ();
      int shutdown (Function<void(int)> done);
      void close (Function<void()> onclose = nullptr);

      // Address info helpers
      int getsockname (struct sockaddr* addr, int* len) { return uv_tcp_getsockname(&this->handle.tcp, addr, len); }
      int getpeername (struct sockaddr* addr, int* len) { return uv_tcp_getpeername(&this->handle.tcp, addr, len); }

    private:
      struct ConnectContext;

      void cleanupConnectContext (ConnectContext* ctx);
      void handleResolveResult (ConnectContext* ctx, int status, struct addrinfo* res);
      void handleConnectResult (ConnectContext* ctx, int status);
      void cancelPendingConnect ();
      void cancelPendingConnectLocked ();

      static void onAddrinfoResolved (uv_getaddrinfo_t* req, int status, struct addrinfo* res);
      static void onConnectCompleted (uv_connect_t* req, int status);

      ConnectContext* connectCtx = nullptr;
  };

  class SocketManager {
    public:
      using ID = uint64_t;
      using Map = Map<ID, SharedPointer<Socket>>;

      loop::Loop& loop;
      Mutex mutex;
      Map sockets;

      explicit SocketManager (loop::Loop& loop) : loop(loop) {}
      ~SocketManager () {}

      SharedPointer<Socket> create (ID id, int& err);
      SharedPointer<Socket> get (ID id);
      void remove (ID id);
      void pauseOnLoopThread ();
  };
}

#endif
