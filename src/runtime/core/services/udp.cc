#include "../../http.hh"
#include <future>
#include "../../core.hh"
#include "udp.hh"

using oro::runtime::crypto::rand64;

namespace oro::runtime::core::services {
  static JSON::Object::Entries ERR_SOCKET_ALREADY_BOUND (
    const String& source,
    UDP::ID id
  ) {
    return JSON::Object::Entries {
      {"source", source},
      {"err", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"type", "InternalError"},
        {"code", "ERR_SOCKET_ALREADY_BOUND"},
        {"message", "Socket is already bound"}
      }}
    };
  }

  static JSON::Object::Entries ERR_SOCKET_DGRAM_IS_CONNECTED (
    const String& source,
    UDP::ID id
  ) {
    return JSON::Object::Entries {
      {"source", source},
      {"err", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"type", "InternalError"},
        {"code", "ERR_SOCKET_DGRAM_IS_CONNECTED"},
        {"message", "Already connected"}
      }}
    };
  }

  static JSON::Object::Entries ERR_SOCKET_DGRAM_NOT_CONNECTED (
    const String& source,
    UDP::ID id
  ) {
    return JSON::Object::Entries {
      {"source", source},
      {"err", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"type", "InternalError"},
        {"code", "ERR_SOCKET_DGRAM_NOT_CONNECTED"},
        {"message", "Not connected"}
      }}
    };
  }

  static JSON::Object::Entries ERR_SOCKET_DGRAM_CLOSED (
    const String& source,
    UDP::ID id
  ) {
    return JSON::Object::Entries {
      {"source", source},
      {"err", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"type", "InternalError"},
        {"code", "ERR_SOCKET_DGRAM_CLOSED"},
        {"message", "Socket is closed"}
      }}
    };
  }

  static JSON::Object::Entries ERR_SOCKET_DGRAM_CLOSING (
    const String& source,
    UDP::ID id
  ) {
    return JSON::Object::Entries {
      {"source", source},
      {"err", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"type", "NotFoundError"},
        {"code", "ERR_SOCKET_DGRAM_CLOSING"},
        {"message", "Socket is closing"}
      }}
    };
  }

  static JSON::Object::Entries ERR_SOCKET_DGRAM_NOT_RUNNING (
    const String& source,
    UDP::ID id
  ) {
    return JSON::Object::Entries {
      {"source", source},
      {"err", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"type", "NotFoundError"},
        {"code", "ERR_SOCKET_DGRAM_NOT_RUNNING"},
        {"message", "Not running"}
      }}
    };
  }

  void UDP::resumeAllSockets () {
    this->manager.resume();
  }

  void UDP::pauseAllSockets () {
    this->manager.pause();
  }

  bool UDP::hasSocket (ID id) {
    return this->manager.has(id);
  }

  void UDP::removeSocket (ID id) {
    return this->manager.remove(id, false);
  }

  void UDP::removeSocket (ID id, bool autoClose) {
    return this->manager.remove(id, autoClose);
  }

  SharedPointer<udp::Socket> UDP::getSocket (ID id) {
    return this->manager.get(id);
  }

  SharedPointer<udp::Socket> UDP::createSocket (udp::socket_type_t socketType, ID id) {
    return this->createSocket(socketType, id, false);
  }

  SharedPointer<udp::Socket> UDP::createSocket (
    udp::socket_type_t socketType,
    ID id,
    bool isEphemeral
  ) {
    return this->manager.create(socketType, id, isEphemeral);
  }

  void UDP::bind (
    const String& seq,
    ID id,
    const UDP::BindOptions& options,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      if (this->hasSocket(id)) {
        if (this->getSocket(id)->isBound()) {
          auto json = ERR_SOCKET_ALREADY_BOUND("udp.bind", id);
          return callback(seq, json, QueuedResponse{});
        }
      }

      auto socket = this->createSocket(udp::SOCKET_TYPE_UDP, id);
      auto err = socket->bind(options.address, options.port, options.reuseAddr);

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "udp.bind"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"message", String(uv_strerror(err))}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      auto info = socket->getLocalPeerInfo();

      if (info->err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "udp.bind"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"message", String(uv_strerror(info->err))}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      auto json = JSON::Object::Entries {
        {"source", "udp.bind"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(id)},
          {"port", (int) info->port},
          {"event" , "listening"},
          {"family", info->family},
          {"address", info->address}
        }}
      };

      callback(seq, json, QueuedResponse{});
    });
  }

  void UDP::connect (
    const String& seq,
    ID id,
    const UDP::ConnectOptions& options,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      auto socket = this->createSocket(udp::SOCKET_TYPE_UDP, id);

      if (socket->isConnected()) {
        auto json = ERR_SOCKET_DGRAM_IS_CONNECTED("udp.connect", id);
        return callback(seq, json, QueuedResponse{});
      }

      auto err = socket->connect(options.address, options.port);

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "udp.connect"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"message", String(uv_strerror(err))}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      auto info = socket->getRemotePeerInfo();

      if (info->err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "udp.connect"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"message", String(uv_strerror(info->err))}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      auto json = JSON::Object::Entries {
        {"source", "udp.connect"},
        {"data", JSON::Object::Entries {
          {"address", info->address},
          {"family", info->family},
          {"port", (int) info->port},
          {"id", std::to_string(id)}
        }}
      };

      callback(seq, json, QueuedResponse{});
    });
  }

  void UDP::disconnect (
    const String& seq,
    ID id,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      if (!this->hasSocket(id)) {
        auto json = ERR_SOCKET_DGRAM_NOT_CONNECTED("udp.disconnect", id);
        return callback(seq, json, QueuedResponse{});
      }

      auto socket = this->getSocket(id);
      auto err = socket->disconnect();

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "udp.disconnect"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"message", String(uv_strerror(err))}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      auto json = JSON::Object::Entries {
        {"source", "udp.disconnect"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(id)}
        }}
      };

      callback(seq, json, QueuedResponse{});
    });
  }

  void UDP::getPeerName (
    const String& seq,
    ID id,
    const Callback callback
  ) {
    if (!this->hasSocket(id)) {
      auto json = ERR_SOCKET_DGRAM_NOT_CONNECTED("udp.getPeerName", id);
      return callback(seq, json, QueuedResponse{});
    }

    auto socket = this->getSocket(id);
    auto info = socket->getRemotePeerInfo();

    if (info->err < 0) {
      auto json = JSON::Object::Entries {
        {"source", "udp.getPeerName"},
        {"err", JSON::Object::Entries {
          {"id", std::to_string(id)},
          {"message", String(uv_strerror(info->err))}
        }}
      };

      return callback(seq, json, QueuedResponse{});
    }

    auto json = JSON::Object::Entries {
      {"source", "udp.getPeerName"},
      {"data", JSON::Object::Entries {
        {"address", info->address},
        {"family", info->family},
        {"port", (int) info->port},
        {"id", std::to_string(id)}
      }}
    };

    callback(seq, json, QueuedResponse{});
  }

  void UDP::getSockName (
    const String& seq,
    ID id,
    const Callback callback
  ) {
    if (!this->hasSocket(id)) {
      auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.getSockName", id);
      return callback(seq, json, QueuedResponse{});
    }

    auto socket = this->getSocket(id);
    auto info = socket->getLocalPeerInfo();

    if (info->err < 0) {
      auto json = JSON::Object::Entries {
        {"source", "udp.getSockName"},
        {"err", JSON::Object::Entries {
          {"id", std::to_string(id)},
          {"message", String(uv_strerror(info->err))}
        }}
      };

      return callback(seq, json, QueuedResponse{});
    }

    auto json = JSON::Object::Entries {
      {"source", "udp.getSockName"},
      {"data", JSON::Object::Entries {
        {"address", info->address},
        {"family", info->family},
        {"port", (int) info->port},
        {"id", std::to_string(id)}
      }}
    };

    callback(seq, json, QueuedResponse{});
  }

  void UDP::getState (
    const String& seq,
    ID id,
    const Callback callback
  ) {
    if (!this->hasSocket(id)) {
      auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.getState", id);
      return callback(seq, json, QueuedResponse{});
    }

    auto socket = this->getSocket(id);

    if (!socket->isUDP()) {
      auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.getState", id);
      return callback(seq, json, QueuedResponse{});
    }

    auto json = JSON::Object::Entries {
      {"source", "udp.getState"},
      {"data", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"type", "udp"},
        {"bound", socket->isBound()},
        {"active", socket->isActive()},
        {"closed", socket->isClosed()},
        {"closing", socket->isClosing()},
        {"connected", socket->isConnected()},
        {"ephemeral", socket->isEphemeral()}
      }}
    };

    callback(seq, json, QueuedResponse{});
  }

  void UDP::send (
    const String& seq,
    ID id,
    const UDP::SendOptions& options,
    const Callback callback
  ) {
    this->loop.dispatch([=, this] {
      auto socket = this->createSocket(udp::SOCKET_TYPE_UDP, id, options.ephemeral);
      auto size = options.size;
      auto port = options.port;
      auto address = options.address;
      // Enforce sane UDP payload size (approx. IPv4 max payload 65,507 bytes)
      static constexpr size_t kMaxUDPPayload = 65507;
      if (size > kMaxUDPPayload) {
        auto json = JSON::Object::Entries {
          {"source", "udp.send"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"message", "Payload too large for UDP"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }
      if ((port <= 0 || port > 65535) && !socket->isConnected()) {
        auto json = JSON::Object::Entries {
          {"source", "udp.send"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"message", "Invalid port"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }
      if (address.size() == 0 && !socket->isConnected()) {
        auto json = JSON::Object::Entries {
          {"source", "udp.send"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"message", "Invalid address"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }
      auto bytes = options.bytes;
      socket->send(bytes, size, port, address, [=](auto status, auto queuedResponse) {
        if (status < 0) {
          auto json = JSON::Object::Entries {
            {"source", "udp.send"},
            {"err", JSON::Object::Entries {
              {"id", std::to_string(id)},
              {"message", String(uv_strerror(status))}
            }}
          };

          return callback(seq, json, QueuedResponse{});
        }

        auto json = JSON::Object::Entries {
          {"source", "udp.send"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"status", status}
          }}
        };

        callback(seq, json, QueuedResponse{});
      });
    });
  }

  void UDP::readStart (const String& seq, ID id, const Callback callback) {
    if (!this->hasSocket(id)) {
      auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.readStart", id);
      return callback(seq, json, QueuedResponse{});
    }

    auto socket = this->getSocket(id);

    if (socket->isClosed()) {
      auto json = ERR_SOCKET_DGRAM_CLOSED("udp.readStart", id);
      return callback(seq, json, QueuedResponse{});
    }

    if (socket->isClosing()) {
      auto json = ERR_SOCKET_DGRAM_CLOSING("udp.readStart", id);
      return callback(seq, json, QueuedResponse{});
    }

    if (socket->hasState(udp::SOCKET_STATE_UDP_RECV_STARTED)) {
      auto json = JSON::Object::Entries {
        {"source", "udp.readStart"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(id)}
        }}
      };
      /* auto json = JSON::Object::Entries {
        {"source", "udp.readStart"},
        {"err", JSON::Object::Entries {
          {"id", std::to_string(id)},
          {"message", "Socket is already receiving"}
        }}
      }; */

      return callback(seq, json, QueuedResponse{});
    }

    if (socket->isActive()) {
      auto json = JSON::Object::Entries {
        {"source", "udp.readStart"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(id)}
        }}
      };

      return callback(seq, json, QueuedResponse{});
    }

    auto err = socket->recvstart([=](auto nread, auto buf, auto addr) {
      if (nread == UV_EOF) {
        auto json = JSON::Object::Entries {
          {"source", "udp.readStart"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"EOF", true}
          }}
        };

        // No payload consumed; free allocated buffer if present
        if (buf && buf->base) {
          delete[] buf->base;
        }

        callback("-1", json, QueuedResponse{});
      } else if (nread > 0 && buf && buf->base) {
        char address[INET6_ADDRSTRLEN] = {0};
        QueuedResponse queuedResponse {0};
        int port = 0;

        udp::ip::parseAddress((struct sockaddr *) addr, &port, address, sizeof(address));

        const auto headers = http::Headers {{
          {"content-type" ,"application/octet-stream"},
          {"content-length", nread}
        }};

        queuedResponse.id = rand64();
        queuedResponse.body.reset(reinterpret_cast<unsigned char*>(buf->base));
        queuedResponse.length = (int) nread;
        queuedResponse.headers = headers.str();

        const auto json = JSON::Object::Entries {
          {"source", "udp.readStart"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"port", port},
            {"bytes", std::to_string(queuedResponse.length)},
            {"address", address}
          }}
        };

        callback("-1", json, queuedResponse);
      } else {
        // No payload consumed; free allocated buffer if present
        if (buf && buf->base) {
          delete[] buf->base;
        }
      }
    });

    // `UV_EALREADY || UV_EBUSY` could mean there might be
    // active IO on the underlying handle
    if (err < 0 && err != UV_EALREADY && err != UV_EBUSY) {
      auto json = JSON::Object::Entries {
        {"source", "udp.readStart"},
        {"err", JSON::Object::Entries {
          {"id", std::to_string(id)},
          {"message", String(uv_strerror(err))}
        }}
      };

      return callback(seq, json, QueuedResponse{});
    }

    auto json = JSON::Object::Entries {
      {"source", "udp.readStart"},
      {"data", JSON::Object::Entries {
        {"id", std::to_string(id)}
      }}
    };

    callback(seq, json, QueuedResponse {});
  }

  void UDP::readStop (
    const String& seq,
    ID id,
    const Callback callback
  ) {
    this->loop.dispatch([=, this] {
      if (!this->hasSocket(id)) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.readStop", id);
        return callback(seq, json, QueuedResponse{});
      }

      auto socket = this->getSocket(id);

      if (socket->isClosed()) {
        auto json = ERR_SOCKET_DGRAM_CLOSED("udp.readStop", id);
        return callback(seq, json, QueuedResponse{});
      }

      if (socket->isClosing()) {
        auto json = ERR_SOCKET_DGRAM_CLOSING("udp.readStop", id);
        return callback(seq, json, QueuedResponse{});
      }

      if (!socket->hasState(udp::SOCKET_STATE_UDP_RECV_STARTED)) {
        auto json = JSON::Object::Entries {
          {"source", "udp.readStop"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"message", "Socket is not receiving"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      auto err = socket->recvstop();

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "udp.readStop"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"message", String(uv_strerror(err))}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      auto json = JSON::Object::Entries {
        {"source", "udp.readStop"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(id)}
        }}
      };

      callback(seq, json, QueuedResponse {});
    });
  }

  void UDP::close (
    const String& seq,
    ID id,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      if (!this->hasSocket(id)) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.close", id);
        return callback(seq, json, QueuedResponse{});
      }

      auto socket = this->getSocket(id);

      if (!socket->isUDP()) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.close", id);
        return callback(seq, json, QueuedResponse{});
      }

      if (socket->isClosed()) {
        auto json = ERR_SOCKET_DGRAM_CLOSED("udp.close", id);
        return callback(seq, json, QueuedResponse{});
      }

      if (socket->isClosing()) {
        auto json = ERR_SOCKET_DGRAM_CLOSING("udp.close", id);
        return callback(seq, json, QueuedResponse{});
      }

      socket->close([=, this]() {
        auto json = JSON::Object::Entries {
          {"source", "udp.close"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(id)}
          }}
        };

        callback(seq, json, QueuedResponse{});
      });
    });
  }

  void UDP::setBroadcast (const String& seq, ID id, bool on, const Callback callback) {
    this->loop.dispatch([=, this]() {
      if (!this->hasSocket(id)) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.setBroadcast", id);
        return callback(seq, json, QueuedResponse{});
      }
      auto socket = this->getSocket(id);
      if (!socket->isUDP()) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.setBroadcast", id);
        return callback(seq, json, QueuedResponse{});
      }
      const int err = uv_udp_set_broadcast(reinterpret_cast<uv_udp_t*>(&socket->handle), on ? 1 : 0);
      if (err < 0) {
        auto json = JSON::Object::Entries{{"source","udp.setBroadcast"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        return callback(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{"source","udp.setBroadcast"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      callback(seq, json, QueuedResponse{});
    });
  }

  void UDP::setTTL (const String& seq, ID id, int ttl, const Callback callback) {
    this->loop.dispatch([=, this]() {
      if (!this->hasSocket(id)) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.setTTL", id);
        return callback(seq, json, QueuedResponse{});
      }
      auto socket = this->getSocket(id);
      const int err = uv_udp_set_ttl(reinterpret_cast<uv_udp_t*>(&socket->handle), ttl);
      if (err < 0) {
        auto json = JSON::Object::Entries{{"source","udp.setTTL"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        return callback(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{"source","udp.setTTL"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      callback(seq, json, QueuedResponse{});
    });
  }

  void UDP::setMulticastTTL (const String& seq, ID id, int ttl, const Callback callback) {
    this->loop.dispatch([=, this]() {
      if (!this->hasSocket(id)) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.setMulticastTTL", id);
        return callback(seq, json, QueuedResponse{});
      }
      auto socket = this->getSocket(id);
      const int err = uv_udp_set_multicast_ttl(reinterpret_cast<uv_udp_t*>(&socket->handle), ttl);
      if (err < 0) {
        auto json = JSON::Object::Entries{{"source","udp.setMulticastTTL"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        return callback(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{"source","udp.setMulticastTTL"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      callback(seq, json, QueuedResponse{});
    });
  }

  void UDP::setMulticastLoopback (const String& seq, ID id, bool on, const Callback callback) {
    this->loop.dispatch([=, this]() {
      if (!this->hasSocket(id)) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.setMulticastLoopback", id);
        return callback(seq, json, QueuedResponse{});
      }
      auto socket = this->getSocket(id);
      const int err = uv_udp_set_multicast_loop(reinterpret_cast<uv_udp_t*>(&socket->handle), on ? 1 : 0);
      if (err < 0) {
        auto json = JSON::Object::Entries{{"source","udp.setMulticastLoopback"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        return callback(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{"source","udp.setMulticastLoopback"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      callback(seq, json, QueuedResponse{});
    });
  }

  void UDP::setMulticastInterface (const String& seq, ID id, const String& iface, const Callback callback) {
    this->loop.dispatch([=, this]() {
      if (!this->hasSocket(id)) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.setMulticastInterface", id);
        return callback(seq, json, QueuedResponse{});
      }
      auto socket = this->getSocket(id);
      const int err = uv_udp_set_multicast_interface(reinterpret_cast<uv_udp_t*>(&socket->handle), iface.c_str());
      if (err < 0) {
        auto json = JSON::Object::Entries{{"source","udp.setMulticastInterface"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        return callback(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{"source","udp.setMulticastInterface"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      callback(seq, json, QueuedResponse{});
    });
  }

  void UDP::setIPv6Only (const String& seq, ID id, bool on, const Callback callback) {
    this->loop.dispatch([=, this]() {
      if (!this->hasSocket(id)) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.setIPv6Only", id);
        return callback(seq, json, QueuedResponse{});
      }
      // Not supported by libuv at runtime; this flag is set at bind time via UV_UDP_IPV6ONLY
      auto json = JSON::Object::Entries{{"source","udp.setIPv6Only"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","ENOSYS"}}}};
      callback(seq, json, QueuedResponse{});
    });
  }

  void UDP::addMembership (const String& seq, ID id, const String& maddr, const String& iface, const Callback callback) {
    this->loop.dispatch([=, this]() {
      if (!this->hasSocket(id)) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.addMembership", id);
        return callback(seq, json, QueuedResponse{});
      }
      auto socket = this->getSocket(id);
      const char* ifc = iface.size() ? iface.c_str() : nullptr;
      const int err = uv_udp_set_membership(reinterpret_cast<uv_udp_t*>(&socket->handle), maddr.c_str(), ifc, UV_JOIN_GROUP);
      if (err < 0) {
        auto json = JSON::Object::Entries{{"source","udp.addMembership"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        return callback(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{"source","udp.addMembership"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      callback(seq, json, QueuedResponse{});
    });
  }

  void UDP::dropMembership (const String& seq, ID id, const String& maddr, const String& iface, const Callback callback) {
    this->loop.dispatch([=, this]() {
      if (!this->hasSocket(id)) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.dropMembership", id);
        return callback(seq, json, QueuedResponse{});
      }
      auto socket = this->getSocket(id);
      const char* ifc = iface.size() ? iface.c_str() : nullptr;
      const int err = uv_udp_set_membership(reinterpret_cast<uv_udp_t*>(&socket->handle), maddr.c_str(), ifc, UV_LEAVE_GROUP);
      if (err < 0) {
        auto json = JSON::Object::Entries{{"source","udp.dropMembership"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        return callback(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{"source","udp.dropMembership"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      callback(seq, json, QueuedResponse{});
    });
  }

  void UDP::addSourceSpecificMembership (const String& seq, ID id, const String& maddr, const String& source, const String& iface, const Callback callback) {
    this->loop.dispatch([=, this]() {
      if (!this->hasSocket(id)) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.addSourceSpecificMembership", id);
        return callback(seq, json, QueuedResponse{});
      }
      auto socket = this->getSocket(id);
      const char* ifc = iface.size() ? iface.c_str() : nullptr;
#if defined(uv_udp_set_source_membership)
      const int err = uv_udp_set_source_membership(reinterpret_cast<uv_udp_t*>(&socket->handle), maddr.c_str(), ifc, source.c_str(), UV_JOIN_GROUP);
#else
      const int err = UV_ENOSYS;
#endif
      if (err < 0) {
        auto json = JSON::Object::Entries{{"source","udp.addSourceSpecificMembership"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        return callback(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{"source","udp.addSourceSpecificMembership"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      callback(seq, json, QueuedResponse{});
    });
  }

  void UDP::dropSourceSpecificMembership (const String& seq, ID id, const String& maddr, const String& source, const String& iface, const Callback callback) {
    this->loop.dispatch([=, this]() {
      if (!this->hasSocket(id)) {
        auto json = ERR_SOCKET_DGRAM_NOT_RUNNING("udp.dropSourceSpecificMembership", id);
        return callback(seq, json, QueuedResponse{});
      }
      auto socket = this->getSocket(id);
      const char* ifc = iface.size() ? iface.c_str() : nullptr;
#if defined(uv_udp_set_source_membership)
      const int err = uv_udp_set_source_membership(reinterpret_cast<uv_udp_t*>(&socket->handle), maddr.c_str(), ifc, source.c_str(), UV_LEAVE_GROUP);
#else
      const int err = UV_ENOSYS;
#endif
      if (err < 0) {
        auto json = JSON::Object::Entries{{"source","udp.dropSourceSpecificMembership"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        return callback(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{"source","udp.dropSourceSpecificMembership"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      callback(seq, json, QueuedResponse{});
    });
  }

  bool UDP::start () {
    if (this->enabled) {
      this->resumeAllSockets();
      return true;
    }

    return false;
  }

  bool UDP::stop () {
    if (!this->enabled) {
      return false;
    }

  #if ORO_RUNTIME_PLATFORM_LINUX
    // Non-blocking on Linux to avoid deadlocks with GTK-driven loop pumping.
    this->loop.dispatch([this]() {
      this->manager.pauseOnLoopThread();
    });
    return true;
  #else
    // Pause all sockets on the loop thread and wait for the operation
    auto done = std::make_shared<std::promise<void>>();
    auto fut = done->get_future();
    this->loop.dispatch([=, this]() {
      this->manager.pauseOnLoopThread();
      done->set_value();
    });
    fut.wait();
    return true;
  #endif
  }

  void UDP::capabilities (const String& seq, const Callback callback) {
    JSON::Object::Entries caps = {
      {"multicast", true},
      {"broadcast", true},
      {"ipv6only", true}
    };
#if defined(uv_udp_set_source_membership)
    caps["ssm"] = true;
#else
    caps["ssm"] = false;
#endif
    auto json = JSON::Object::Entries{
      {"source", "udp.capabilities"},
      {"data", caps}
    };
    callback(seq, json, QueuedResponse{});
  }
}
