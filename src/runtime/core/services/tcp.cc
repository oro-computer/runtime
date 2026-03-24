#include <algorithm>

#include "tcp.hh"
#include "../../http.hh"

using oro::runtime::crypto::rand64;

namespace oro::runtime::core::services {
  void TCP::create (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      Lock lock(this->mutex);
      if (manager.get(id)) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.create"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", "Socket already exists"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      int err = 0;
      auto s = manager.create(id, err);
      if (err || !s) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.create"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", String(uv_strerror(err))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{
        "source", "tcp.create"
      }, {
        "data",
        JSON::Object::Entries{{
          "id", std::to_string(id)
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TCP::close (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = manager.get(id);
      if (!s) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.close"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", "NotFound"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      // If this id corresponds to a previously accepted client, make sure
      // it is unregistered from any server tracking to avoid leaks.
      {
        Lock lock(this->mutex);
        Vector<ID> toErase;
        for (auto &entry : serverClients) {
          auto sid = entry.first;
          auto &vec = entry.second;
          auto before = vec.size();
          vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
          if (vec.empty()) {
            toErase.push_back(sid);
          }
          // no else: even if not empty, we removed client id if present
          (void) before;
        }
        for (auto sid : toErase) {
          serverClients.erase(sid);
        }
      }
      // Coordinated shutdown: close accepted clients first if this is a server
      Vector<ID> clients;
      {
        Lock lock(this->mutex);
        if (serverClients.contains(id)) {
          clients = serverClients.at(id);
        }
      }
      if (!clients.empty()) {
        auto counter = std::make_shared<std::atomic<size_t>>(clients.size());
        for (auto cid : clients) {
          auto c = manager.get(cid);
          if (c) {
            c->close([this, id, counter, cid]() {
              unregisterClient(id, cid);
              manager.remove(cid);
              if (--(*counter) == 0) {
                auto srv = manager.get(id);
                if (srv) {
                  srv->close([=, this] {
                    // Remove server mapping once server is closed
                    {
                      Lock lock(this->mutex);
                      serverClients.erase(id);
                    }
                    manager.remove(id);
                  });
                }
              }
            });
          } else {
            // Client already gone; ensure mapping is cleaned up and countdown continues
            unregisterClient(id, cid);
            manager.remove(cid);
            if (--(*counter) == 0) {
              auto srv = manager.get(id);
              if (srv) {
                srv->close([=, this] {
                  Lock lock(this->mutex);
                  serverClients.erase(id);
                  manager.remove(id);
                });
              }
            }
          }
        }
      } else {
        s->close([=, this] {
          // No clients tracked; ensure server mapping cleared and remove
          {
            Lock lock(this->mutex);
            serverClients.erase(id);
          }
          manager.remove(id);
        });
      }
      auto json = JSON::Object::Entries{{
        "source", "tcp.close"
      }, {
        "data",
        JSON::Object::Entries{{
          "id", std::to_string(id)
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TCP::bind (const String& seq, ID id, const String& address, int port, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = manager.get(id);
      if (!s) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.bind"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", "NotFound"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      const int err = s->bind(address, port);
      if (err) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.bind"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", String(uv_strerror(err))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{
        "source", "tcp.bind"
      }, {
        "data",
        JSON::Object::Entries{{
          "id", std::to_string(id)
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TCP::listen (const String& seq, ID id, int backlog, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = manager.get(id);
      if (!s) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.listen"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", "NotFound"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      const int err = s->listen(backlog, [=](int status) {
        if (status >= 0) {
          const auto json = JSON::Object::Entries{{
            "source", "tcp.connection"
          }, {
            "data",
            JSON::Object::Entries{{
              "id", std::to_string(id)
            }}
          }};
          cb("-1", json, QueuedResponse{});
        } else {
          const auto json = JSON::Object::Entries{{
            "source", "tcp.connection"
          }, {
            "err",
            JSON::Object::Entries{{
              "id", std::to_string(id)
            }, {
              "message", String(uv_strerror(status))
            }}
          }};
          cb("-1", json, QueuedResponse{});
        }
      });
      if (err) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.listen"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", String(uv_strerror(err))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{
        "source", "tcp.listen"
      }, {
        "data",
        JSON::Object::Entries{{
          "id", std::to_string(id)
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TCP::accept (const String& seq, ID serverId, ID clientId, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto server = manager.get(serverId);
      if (!server) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.accept"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(serverId)
          }, {
            "message", "ServerNotFound"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      int createErr = 0;
      auto client = manager.create(clientId, createErr);
      if (createErr || !client) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.accept"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(serverId)
          }, {
            "message", String(uv_strerror(createErr))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      const int err = server->accept(client.get());
      if (err) {
        client->close([=, this] {
          manager.remove(clientId);
        });
        auto json = JSON::Object::Entries{{
          "source", "tcp.accept"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(serverId)
          }, {
            "message", String(uv_strerror(err))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      registerClient(serverId, clientId);
      auto json = JSON::Object::Entries{{
        "source", "tcp.accept"
      }, {
        "data",
        JSON::Object::Entries{{
          "serverId", std::to_string(serverId)
        }, {
          "clientId", std::to_string(clientId)
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TCP::connect (const String& seq, ID id, const String& address, int port, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = manager.get(id);
      if (!s) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.connect"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", "NotFound"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      const int err = s->connect(address, port, [=](int status) {
        if (status >= 0) {
          const auto json = JSON::Object::Entries{{
            "source", "tcp.connect"
          }, {
            "data",
            JSON::Object::Entries{{
              "id", std::to_string(id)
            }}
          }};
          cb("-1", json, QueuedResponse{});
        } else {
          const auto json = JSON::Object::Entries{{
            "source", "tcp.connect"
          }, {
            "err",
            JSON::Object::Entries{{
              "id", std::to_string(id)
            }, {
              "message", String(uv_strerror(status))
            }}
          }};
          cb("-1", json, QueuedResponse{});
        }
      });
      if (err) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.connect"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", String(uv_strerror(err))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{
        "source", "tcp.connect"
      }, {
        "data",
        JSON::Object::Entries{{
          "id", std::to_string(id)
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TCP::write (const String& seq, ID id, SharedPointer<unsigned char[]> body, size_t len, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = manager.get(id);
      if (!s) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.write"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", "NotFound"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      const int err = s->write(reinterpret_cast<const char*>(body.get()), len, [=](int status) {
        if (status >= 0) {
          const auto json = JSON::Object::Entries{{
            "source", "tcp.write"
          }, {
            "data",
            JSON::Object::Entries{{
              "id", std::to_string(id)
            }, {
              "bytes", (int) len
            }}
          }};
          cb("-1", json, QueuedResponse{});
        } else {
          const auto json = JSON::Object::Entries{{
            "source", "tcp.write"
          }, {
            "err",
            JSON::Object::Entries{{
              "id", std::to_string(id)
            }, {
              "message", String(uv_strerror(status))
            }}
          }};
          cb("-1", json, QueuedResponse{});
        }
      });
      if (err) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.write"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", String(uv_strerror(err))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{
        "source", "tcp.write"
      }, {
        "data",
        JSON::Object::Entries{{
          "id", std::to_string(id)
        }, {
          "bytes", (int) len
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TCP::readStart (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = manager.get(id);
      if (!s) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.readStart"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", "NotFound"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      const int err = s->readStart([=](auto nread, auto buf) {
        if (nread > 0 && buf && buf->base) {
          http::Headers headers{{
            {"content-type", "application/octet-stream"},
            {"content-length", (int) nread}
          }};
          QueuedResponse post;
          post.id = rand64();
          post.body.reset(reinterpret_cast<unsigned char*>(buf->base));
          post.length = (int) nread;
          post.headers = headers.str();
          const auto json = JSON::Object::Entries{{
            "source", "tcp.read"
          }, {
            "data",
            JSON::Object::Entries{{
              "id", std::to_string(id)
            }, {
              "bytes", (int) nread
            }}
          }};
          cb("-1", json, post);
        } else if (nread == 0 && buf == nullptr) {
          // EOF signaled from socket: emit an end event
          const auto json = JSON::Object::Entries{{
            "source", "tcp.read"
          }, {
            "data",
            JSON::Object::Entries{{
              "id", std::to_string(id)
            }, {
              "EOF", true
            }}
          }};
          cb("-1", json, QueuedResponse{});
        } else if (nread < 0) {
          // Error; publish error event for this socket id
          const auto json = JSON::Object::Entries{{
            "source", "tcp.read"
          }, {
            "err",
            JSON::Object::Entries{{
              "id", std::to_string(id)
            }, {
              "message", String(uv_strerror((int) nread))
            }}
          }};
          cb("-1", json, QueuedResponse{});
        } else if (buf && buf->base) {
          delete[] buf->base;
        }
      });
      if (err && err != UV_EALREADY) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.readStart"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", String(uv_strerror(err))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{
        "source", "tcp.readStart"
      }, {
        "data",
        JSON::Object::Entries{{
          "id", std::to_string(id)
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TCP::readStop (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = manager.get(id);
      if (!s) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.readStop"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", "NotFound"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      const int err = s->readStop();
      if (err) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.readStop"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", String(uv_strerror(err))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{
        "source", "tcp.readStop"
      }, {
        "data",
        JSON::Object::Entries{{
          "id", std::to_string(id)
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TCP::getSockName (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = manager.get(id);
      if (!s) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.getSockName"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", "NotFound"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      struct sockaddr_storage ss{};
      int len = sizeof(ss);
      const int err = s->getsockname(reinterpret_cast<struct sockaddr*>(&ss), &len);
      if (err) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.getSockName"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", String(uv_strerror(err))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      char host[INET6_ADDRSTRLEN] = {0};
      int port = 0;
      if (ss.ss_family == AF_INET) {
        auto* a = reinterpret_cast<struct sockaddr_in*>(&ss);
        uv_ip4_name(a, host, sizeof(host));
        port = ntohs(a->sin_port);
      } else if (ss.ss_family == AF_INET6) {
        auto* a = reinterpret_cast<struct sockaddr_in6*>(&ss);
        uv_ip6_name(a, host, sizeof(host));
        port = ntohs(a->sin6_port);
      }
      auto json = JSON::Object::Entries{{
        "source", "tcp.getSockName"
      }, {
        "data",
        JSON::Object::Entries{{
          "id", std::to_string(id)
        }, {
          "address", String(host)
        }, {
          "port", port
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TCP::getPeerName (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = manager.get(id);
      if (!s) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.getPeerName"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", "NotFound"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      struct sockaddr_storage ss{};
      int len = sizeof(ss);
      const int err = s->getpeername(reinterpret_cast<struct sockaddr*>(&ss), &len);
      if (err) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.getPeerName"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", String(uv_strerror(err))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      char host[INET6_ADDRSTRLEN] = {0};
      int port = 0;
      if (ss.ss_family == AF_INET) {
        auto* a = reinterpret_cast<struct sockaddr_in*>(&ss);
        uv_ip4_name(a, host, sizeof(host));
        port = ntohs(a->sin_port);
      } else if (ss.ss_family == AF_INET6) {
        auto* a = reinterpret_cast<struct sockaddr_in6*>(&ss);
        uv_ip6_name(a, host, sizeof(host));
        port = ntohs(a->sin6_port);
      }
      auto json = JSON::Object::Entries{{
        "source", "tcp.getPeerName"
      }, {
        "data",
        JSON::Object::Entries{{
          "id", std::to_string(id)
        }, {
          "address", String(host)
        }, {
          "port", port
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TCP::registerClient (ID serverId, ID clientId) {
    Lock lock(this->mutex);
    auto& vec = this->serverClients[serverId];
    vec.push_back(clientId);
  }

  void TCP::unregisterClient (ID serverId, ID clientId) {
    Lock lock(this->mutex);
    if (!this->serverClients.contains(serverId)) return;
    auto& vec = this->serverClients.at(serverId);
    vec.erase(std::remove(vec.begin(), vec.end(), clientId), vec.end());
    if (vec.empty()) {
      this->serverClients.erase(serverId);
    }
  }

  void TCP::shutdown (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = manager.get(id);
      if (!s) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.shutdown"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", "NotFound"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      const int err = s->shutdown([=](int status) {
        // Publish an async completion event for shutdown
        if (status >= 0) {
          const auto json = JSON::Object::Entries{{
            "source", "tcp.shutdown"
          }, {
            "data",
            JSON::Object::Entries{{
              "id", std::to_string(id)
            }}
          }};
          cb("-1", json, QueuedResponse{});
        } else {
          const auto json = JSON::Object::Entries{{
            "source", "tcp.shutdown"
          }, {
            "err",
            JSON::Object::Entries{{
              "id", std::to_string(id)
            }, {
              "message", String(uv_strerror(status))
            }}
          }};
          cb("-1", json, QueuedResponse{});
        }
      });
      if (err) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.shutdown"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", String(uv_strerror(err))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{
        "source", "tcp.shutdown"
      }, {
        "data",
        JSON::Object::Entries{{
          "id", std::to_string(id)
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TCP::setNoDelay (const String& seq, ID id, bool on, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = manager.get(id);
      if (!s) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.setNoDelay"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", "NotFound"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      const int err = s->setNoDelay(on);
      if (err) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.setNoDelay"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", String(uv_strerror(err))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{
        "source", "tcp.setNoDelay"
      }, {
        "data",
        JSON::Object::Entries{{
          "id", std::to_string(id)
        }, {
          "on", on
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TCP::setKeepAlive (const String& seq, ID id, bool on, unsigned int delaySec, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = manager.get(id);
      if (!s) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.setKeepAlive"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", "NotFound"
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      const int err = s->setKeepAlive(on, delaySec);
      if (err) {
        auto json = JSON::Object::Entries{{
          "source", "tcp.setKeepAlive"
        }, {
          "err",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "message", String(uv_strerror(err))
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      auto json = JSON::Object::Entries{{
        "source", "tcp.setKeepAlive"
      }, {
        "data",
        JSON::Object::Entries{{
          "id", std::to_string(id)
        }, {
          "on", on
        }, {
          "delay", (int) delaySec
        }}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }
}
