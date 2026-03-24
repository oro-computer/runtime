#include "hci.hh"

#include "../../crypto.hh"
#include "../../http.hh"
#include "../../string.hh"
#include "../../debug.hh"

#include <optional>
#include <cstring>
#include <cstdlib>

#if defined(__linux__) && !defined(__ANDROID__)
#  if defined(__has_include)
#    if __has_include(<bluetooth/bluetooth.h>) && __has_include(<bluetooth/hci.h>)
#      define ORO_RUNTIME_HAVE_HCI 1
#    else
#      define ORO_RUNTIME_HAVE_HCI 0
#    endif
#  else
#    define ORO_RUNTIME_HAVE_HCI 1
#  endif
#else
#  define ORO_RUNTIME_HAVE_HCI 0
#endif

#if ORO_RUNTIME_HAVE_HCI
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <cerrno>
#include <memory>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
  inline oro::runtime::JSON::Object::Entries notSupported (const char* source) {
    return oro::runtime::JSON::Object::Entries {
      {"source", source},
      {"err", oro::runtime::JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", "HCI is not supported on this platform"}
      }}
    };
  }
}

namespace oro::runtime::core::services {

#if ORO_RUNTIME_HAVE_HCI

using oro::runtime::String;
using oro::runtime::crypto::rand64;

namespace {
  struct ErrnoEntry {
    int code;
    const char* name;
  };

  constexpr ErrnoEntry kErrnoEntries[] = {
    {EPERM, "EPERM"}, {ENOENT, "ENOENT"}, {ESRCH, "ESRCH"}, {EINTR, "EINTR"},
    {EIO, "EIO"}, {ENXIO, "ENXIO"}, {E2BIG, "E2BIG"}, {ENOEXEC, "ENOEXEC"},
    {EBADF, "EBADF"}, {ECHILD, "ECHILD"}, {EAGAIN, "EAGAIN"}, {ENOMEM, "ENOMEM"},
    {EACCES, "EACCES"}, {EFAULT, "EFAULT"}, {ENOTBLK, "ENOTBLK"}, {EBUSY, "EBUSY"},
    {EEXIST, "EEXIST"}, {EXDEV, "EXDEV"}, {ENODEV, "ENODEV"}, {ENOTDIR, "ENOTDIR"},
    {EISDIR, "EISDIR"}, {EINVAL, "EINVAL"}, {ENFILE, "ENFILE"}, {EMFILE, "EMFILE"},
    {ENOTTY, "ENOTTY"}, {ETXTBSY, "ETXTBSY"}, {EFBIG, "EFBIG"}, {ENOSPC, "ENOSPC"},
    {ESPIPE, "ESPIPE"}, {EROFS, "EROFS"}, {EMLINK, "EMLINK"}, {EPIPE, "EPIPE"},
    {EDOM, "EDOM"}, {ERANGE, "ERANGE"}
  };

  inline const char* errnoName (int err) {
  #ifdef __GLIBC__
    if (const char* name = ::strerrorname_np(err)) {
      return name;
    }
  #endif
    for (const auto& entry : kErrnoEntries) {
      if (entry.code == err) {
        return entry.name;
      }
    }
    return "UNKNOWN";
  }

  inline oro::runtime::JSON::Object::Entries makeErr (
    const char* source,
    int err,
    const char* context = nullptr,
    std::optional<uint64_t> id = std::nullopt
  ) {
    const auto message = context ? context : ::strerror(err);
    oro::runtime::JSON::Object::Entries payload {
      {"type", "ErrnoError"},
      {"message", String(message)},
      {"code", String(errnoName(err))},
      {"errno", err}
    };
    if (id.has_value()) {
      payload.push_back({"id", std::to_string(*id)});
    }
    return oro::runtime::JSON::Object::Entries {
      {"source", source},
      {"err", payload}
    };
  }

  inline oro::runtime::JSON::Object::Entries adapterToJson (const hci_dev_info& info) {
    char bdaddr[18] = {0};
    ::ba2str(&info.bdaddr, bdaddr);

    constexpr const char* kDeviceTypes[] = {"PRIMARY", "AMP"};
    constexpr const char* kBusTypes[] = {
      "VIRTUAL", "USB", "PCCARD", "UART", "RS232",
      "PCI", "SDIO", "SPI", "I2C", "SMD", "VIRTIO"
    };
    constexpr size_t deviceTypeCount = sizeof(kDeviceTypes) / sizeof(kDeviceTypes[0]);
    constexpr size_t busTypeCount = sizeof(kBusTypes) / sizeof(kBusTypes[0]);

    const size_t devType = (info.type >> 4) & 0x03;
    const size_t busType = info.type & 0x0f;

    oro::runtime::JSON::Object::Entries json {
      {"devId", static_cast<int>(info.dev_id)},
      {"name", String(info.name)},
      {"bdaddr", String(bdaddr)},
      {"flags", static_cast<uint32_t>(info.flags)},
      {"powered", (info.flags & (1u << HCI_UP)) != 0}
    };

    if (devType < deviceTypeCount) {
      json.push_back({"type", String(kDeviceTypes[devType])});
    } else {
      json.push_back({"type", static_cast<int>(devType)});
    }

    if (busType < busTypeCount) {
      json.push_back({"bus", String(kBusTypes[busType])});
    } else {
      json.push_back({"bus", static_cast<int>(busType)});
    }

    return json;
  }

  struct AutoFd {
    int fd;
    explicit AutoFd (int value) : fd(value) {}
    ~AutoFd () {
      if (fd >= 0) {
        ::close(fd);
      }
    }
    AutoFd (const AutoFd&) = delete;
    AutoFd& operator = (const AutoFd&) = delete;
    void release () { fd = -1; }
  };

  inline int openControlSocket () {
    return ::socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, BTPROTO_HCI);
  }

  inline std::unique_ptr<hci_dev_list_req, void(*)(void*)> makeDeviceList () {
    const size_t count = HCI_MAX_DEV > 0 ? static_cast<size_t>(HCI_MAX_DEV) : static_cast<size_t>(16);
    auto alloc = static_cast<hci_dev_list_req*>(
      std::calloc(1, sizeof(hci_dev_list_req) + count * sizeof(hci_dev_req))
    );
    return std::unique_ptr<hci_dev_list_req, void(*)(void*)>(alloc, [](void* ptr) {
      std::free(ptr);
    });
  }

  inline int ensureAdapterInfo (int controlFd, uint16_t& devId, hci_dev_info& info) {
    std::memset(&info, 0, sizeof(info));
    info.dev_id = devId;

    if (::ioctl(controlFd, HCIGETDEVINFO, &info) == 0) {
      devId = info.dev_id;
      return 0;
    }

    if (devId != 0 || errno != ENODEV) {
      return -errno;
    }

    const auto list = makeDeviceList();
    if (!list) {
      return -ENOMEM;
    }

    list->dev_num = static_cast<uint16_t>(HCI_MAX_DEV);
    if (::ioctl(controlFd, HCIGETDEVLIST, list.get()) < 0) {
      return -errno;
    }

    for (int i = 0; i < list->dev_num; ++i) {
      hci_dev_info candidate{};
      candidate.dev_id = list->dev_req[i].dev_id;
      if (::ioctl(controlFd, HCIGETDEVINFO, &candidate) == 0) {
        info = candidate;
        devId = candidate.dev_id;
        return 0;
      }
    }

    return -ENODEV;
  }

  inline int setAdapterPowerState (int controlFd, uint16_t devId, bool up) {
    if (::ioctl(controlFd, up ? HCIDEVUP : HCIDEVDOWN, static_cast<int>(devId)) < 0) {
      return -errno;
    }
    return 0;
  }

  inline int blockingWrite (int fd, const unsigned char* data, size_t len) {
    size_t offset = 0;
    while (offset < len) {
      const ssize_t written = ::write(fd, data + offset, len - offset);
      if (written < 0) {
        if (errno == EINTR) {
          continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          struct pollfd pfd;
          pfd.fd = fd;
          pfd.events = POLLOUT;
          pfd.revents = 0;
          int pollRes;
          do {
            pollRes = ::poll(&pfd, 1, -1);
          } while (pollRes < 0 && errno == EINTR);
          if (pollRes <= 0) {
            return pollRes < 0 ? -errno : -EIO;
          }
          continue;
        }
        return -errno;
      }
      offset += static_cast<size_t>(written);
    }
    return static_cast<int>(len);
  }

  inline void copyBufferToResponse (
    const unsigned char* data,
    size_t length,
    oro::runtime::QueuedResponse& response
  ) {
    if (!data || length == 0) {
      response.body.reset();
      response.length = 0;
      response.headers.clear();
      return;
    }
    auto body = SharedPointer<unsigned char[]>(new unsigned char[length]);
    std::memcpy(body.get(), data, length);
    response.body = std::move(body);
    response.length = static_cast<int>(length);

    oro::runtime::http::Headers headers{
      {"content-type", "application/octet-stream"},
      {"content-length", static_cast<int>(length)}
    };
    response.headers = headers.str();
    response.id = rand64();
  }
}

struct HCI::Handle : public std::enable_shared_from_this<HCI::Handle> {
  HCI& service;
  ID id = 0;
  uint16_t devId = 0;
  int fd = -1;
  uv_poll_t* poll = nullptr;
  bool pollActive = false;
  bool reading = false;
  bool closing = false;
  Callback eventCb;

  Handle (HCI& svc, ID handleId, uint16_t adapterId)
    : service(svc),
      id(handleId),
      devId(adapterId)
  {}

  ~Handle () {
    close();
  }

  void close () {
    if (closing) {
      return;
    }
    closing = true;

    if (poll) {
      if (pollActive) {
        uv_poll_stop(poll);
        pollActive = false;
      }
      uv_close(reinterpret_cast<uv_handle_t*>(poll), [](uv_handle_t* handle) {
        delete reinterpret_cast<uv_poll_t*>(handle);
      });
      poll = nullptr;
    }

    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }

    reading = false;
  }
};

SharedPointer<HCI::Handle> HCI::getHandle (ID id) {
  Lock lock(this->handlesMutex);
  auto it = this->handles.find(id);
  if (it != this->handles.end()) {
    return it->second;
  }
  return nullptr;
}

void HCI::removeHandle (ID id) {
  Lock lock(this->handlesMutex);
  this->handles.erase(id);
}

void HCI::onPoll (Handle& handle, int status, int events) {
  (void) events;

  if (handle.fd < 0) {
    return;
  }

  auto callback = handle.eventCb;
  if (!callback) {
    return;
  }

  unsigned char buffer[HCI_MAX_FRAME_SIZE];
  ssize_t nread = ::read(handle.fd, buffer, sizeof(buffer));
  int err = 0;

  if (nread > 0) {
    oro::runtime::QueuedResponse response;
    copyBufferToResponse(buffer, static_cast<size_t>(nread), response);
    const auto json = JSON::Object::Entries {
      {"source", "hci.data"},
      {"data", JSON::Object::Entries {
        {"id", std::to_string(handle.id)},
        {"bytes", static_cast<int>(nread)}
      }}
    };
    callback("-1", json, response);
    return;
  }

  if (nread == 0) {
    err = 0;
  } else {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return;
    }
    err = errno;
  }

  if (status < 0 && err == 0) {
    err = -status;
  }

  handle.reading = false;
  handle.eventCb = nullptr;

  JSON::Object::Entries payload {
    {"source", "hci.close"}
  };
  JSON::Object::Entries dataEntries {
    {"id", std::to_string(handle.id)}
  };
  payload.push_back({"data", dataEntries});

  if (err) {
    payload.push_back({"err", JSON::Object::Entries {
      {"type", "ErrnoError"},
      {"message", String(::strerror(err))},
      {"code", String(errnoName(err))},
      {"errno", err},
      {"id", std::to_string(handle.id)}
    }});
  }

  callback("-1", payload, QueuedResponse{});
  handle.close();
  this->removeHandle(handle.id);
}

void HCI::listAdapters (const String& seq, const Callback cb) {
  this->loop.dispatch([=, this]() {
    const int controlFd = openControlSocket();
    if (controlFd < 0) {
      cb(seq, makeErr("hci.listAdapters", errno), QueuedResponse{});
      return;
    }
    AutoFd ctrl(controlFd);

    const auto list = makeDeviceList();
    if (!list) {
      cb(seq, makeErr("hci.listAdapters", ENOMEM), QueuedResponse{});
      return;
    }

    list->dev_num = static_cast<uint16_t>(HCI_MAX_DEV);
    if (::ioctl(controlFd, HCIGETDEVLIST, list.get()) < 0) {
      cb(seq, makeErr("hci.listAdapters", errno), QueuedResponse{});
      return;
    }

    JSON::Array::Entries devices;
    for (int i = 0; i < list->dev_num; ++i) {
      hci_dev_info info{};
      info.dev_id = list->dev_req[i].dev_id;
      if (::ioctl(controlFd, HCIGETDEVINFO, &info) == 0) {
        devices.push_back(adapterToJson(info));
      }
    }

    const auto json = JSON::Object::Entries {
      {"source", "hci.listAdapters"},
      {"data", JSON::Object::Entries {
        {"devices", devices}
      }}
    };
    cb(seq, json, QueuedResponse{});
  });
}

void HCI::getAdapter (const String& seq, uint16_t devId, const Callback cb) {
  this->loop.dispatch([=, this]() {
    const int controlFd = openControlSocket();
    if (controlFd < 0) {
      cb(seq, makeErr("hci.getAdapterInfo", errno), QueuedResponse{});
      return;
    }
    AutoFd ctrl(controlFd);

    hci_dev_info info{};
    int err = ensureAdapterInfo(controlFd, devId, info);
    if (err < 0) {
      cb(seq, makeErr("hci.getAdapterInfo", -err), QueuedResponse{});
      return;
    }

    const auto json = JSON::Object::Entries {
      {"source", "hci.getAdapterInfo"},
      {"data", adapterToJson(info)}
    };
    cb(seq, json, QueuedResponse{});
  });
}

void HCI::setAdapterState (const String& seq, uint16_t devId, bool up, const Callback cb) {
  this->loop.dispatch([=, this]() {
    const int controlFd = openControlSocket();
    if (controlFd < 0) {
      cb(seq, makeErr("hci.setAdapterState", errno), QueuedResponse{});
      return;
    }
    AutoFd ctrl(controlFd);

    hci_dev_info info{};
    int err = ensureAdapterInfo(controlFd, devId, info);
    if (err < 0) {
      cb(seq, makeErr("hci.setAdapterState", -err), QueuedResponse{});
      return;
    }

    err = setAdapterPowerState(controlFd, info.dev_id, up);
    if (err < 0) {
      cb(seq, makeErr("hci.setAdapterState", -err), QueuedResponse{});
      return;
    }

    const auto json = JSON::Object::Entries {
      {"source", "hci.setAdapterState"},
      {"data", JSON::Object::Entries {
        {"devId", static_cast<int>(info.dev_id)},
        {"up", up}
      }}
    };
    cb(seq, json, QueuedResponse{});
  });
}

void HCI::open (const String& seq, ID id, uint16_t devId, const Callback cb) {
  this->loop.dispatch([=, this]() {
    if (!this->enabled.load()) {
      cb(seq, JSON::Object::Entries {
        {"source", "hci.open"},
        {"err", JSON::Object::Entries {
          {"type", "InvalidStateError"},
          {"message", "HCI service is disabled"}
        }}
      }, QueuedResponse{});
      return;
    }

    {
      Lock lock(this->handlesMutex);
      if (this->handles.contains(id)) {
        cb(seq, JSON::Object::Entries {
          {"source", "hci.open"},
          {"err", JSON::Object::Entries {
            {"type", "InvalidStateError"},
            {"message", "Handle already exists"},
            {"id", std::to_string(id)}
          }}
        }, QueuedResponse{});
        return;
      }
    }

    const int controlFd = openControlSocket();
    if (controlFd < 0) {
      cb(seq, makeErr("hci.open", errno, nullptr, id), QueuedResponse{});
      return;
    }
    AutoFd ctrl(controlFd);

    hci_dev_info info{};
    int err = ensureAdapterInfo(controlFd, devId, info);
    if (err < 0) {
      cb(seq, makeErr("hci.open", -err, nullptr, id), QueuedResponse{});
      return;
    }

    if (info.flags & (1u << HCI_UP)) {
      err = setAdapterPowerState(controlFd, info.dev_id, false);
      if (err < 0 && err != -EALREADY) {
        cb(seq, makeErr("hci.open", -err, "Failed to bring adapter down", id), QueuedResponse{});
        return;
      }
    }

    const int sock = ::socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, BTPROTO_HCI);
    if (sock < 0) {
      cb(seq, makeErr("hci.open", errno, nullptr, id), QueuedResponse{});
      return;
    }
    AutoFd sockGuard(sock);

    sockaddr_hci addr{};
    addr.hci_family = AF_BLUETOOTH;
    addr.hci_dev = info.dev_id;
    addr.hci_channel = HCI_CHANNEL_USER;

    if (::bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
      const int bindErr = errno;
      cb(seq, makeErr("hci.open", bindErr, nullptr, id), QueuedResponse{});
      return;
    }

    auto handle = std::make_shared<Handle>(*this, id, info.dev_id);
    handle->fd = sock;
    handle->closing = false;
    handle->reading = false;

    auto* poll = new uv_poll_t();
    const int uvErr = uv_poll_init(this->loop.get(), poll, sock);
    if (uvErr != 0) {
      delete poll;
      handle->close();
      cb(seq, makeErr("hci.open", -uvErr, uv_strerror(uvErr), id), QueuedResponse{});
      return;
    }

    poll->data = handle.get();
    handle->poll = poll;
    sockGuard.release();

    {
      Lock lock(this->handlesMutex);
      this->handles.insert_or_assign(id, handle);
    }

    const auto json = JSON::Object::Entries {
      {"source", "hci.open"},
      {"data", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"devId", static_cast<int>(info.dev_id)}
      }}
    };
    cb(seq, json, QueuedResponse{});
  });
}

void HCI::close (const String& seq, ID id, const Callback cb) {
  this->loop.dispatch([=, this]() {
    auto handle = this->getHandle(id);
    if (!handle) {
      cb(seq, JSON::Object::Entries {
        {"source", "hci.close"},
        {"err", JSON::Object::Entries {
          {"type", "NotFoundError"},
          {"message", "Handle not found"},
          {"id", std::to_string(id)}
        }}
      }, QueuedResponse{});
      return;
    }

    handle->eventCb = nullptr;
    handle->reading = false;
    handle->close();
    this->removeHandle(id);

    const auto json = JSON::Object::Entries {
      {"source", "hci.close"},
      {"data", JSON::Object::Entries {
        {"id", std::to_string(id)}
      }}
    };
    cb(seq, json, QueuedResponse{});
  });
}

void HCI::write (const String& seq, ID id, SharedPointer<unsigned char[]> body, size_t length, const Callback cb) {
  this->loop.dispatch([=, this]() {
    auto handle = this->getHandle(id);
    if (!handle || handle->fd < 0) {
      cb(seq, JSON::Object::Entries {
        {"source", "hci.write"},
        {"err", JSON::Object::Entries {
          {"type", "NotFoundError"},
          {"message", "Handle not found"},
          {"id", std::to_string(id)}
        }}
      }, QueuedResponse{});
      return;
    }

    if (!body || length == 0 || length > HCI_MAX_FRAME_SIZE) {
      cb(seq, JSON::Object::Entries {
        {"source", "hci.write"},
        {"err", JSON::Object::Entries {
          {"type", "RangeError"},
          {"message", "Buffer length must be between 1 and 1028 bytes"},
          {"id", std::to_string(id)}
        }}
      }, QueuedResponse{});
      return;
    }

    const int result = blockingWrite(handle->fd, body.get(), length);
    if (result < 0) {
      cb(seq, makeErr("hci.write", -result, nullptr, id), QueuedResponse{});
      return;
    }

    const auto json = JSON::Object::Entries {
      {"source", "hci.write"},
      {"data", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"bytes", result}
      }}
    };
    cb(seq, json, QueuedResponse{});
  });
}

void HCI::readStart (const String& seq, ID id, const Callback cb) {
  this->loop.dispatch([=, this]() {
    auto handle = this->getHandle(id);
    if (!handle || handle->fd < 0) {
      cb(seq, JSON::Object::Entries {
        {"source", "hci.readStart"},
        {"err", JSON::Object::Entries {
          {"type", "NotFoundError"},
          {"message", "Handle not found"},
          {"id", std::to_string(id)}
        }}
      }, QueuedResponse{});
      return;
    }

    if (handle->reading) {
      cb(seq, JSON::Object::Entries {
        {"source", "hci.readStart"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(id)},
          {"active", true}
        }}
      }, QueuedResponse{});
      return;
    }

    if (!handle->poll) {
      cb(seq, JSON::Object::Entries {
        {"source", "hci.readStart"},
        {"err", JSON::Object::Entries {
          {"type", "InternalError"},
          {"message", "Poll handle is not initialized"},
          {"id", std::to_string(id)}
        }}
      }, QueuedResponse{});
      return;
    }

    handle->eventCb = cb;
    const int uvErr = uv_poll_start(handle->poll, UV_READABLE | UV_DISCONNECT, [](uv_poll_t* poll, int status, int events) {
      auto* self = static_cast<HCI::Handle*>(poll->data);
      if (!self) {
        return;
      }
      self->service.onPoll(*self, status, events);
    });

    if (uvErr != 0) {
      handle->eventCb = nullptr;
      cb(seq, makeErr("hci.readStart", -uvErr, uv_strerror(uvErr), id), QueuedResponse{});
      return;
    }

    handle->pollActive = true;
    handle->reading = true;

    cb(seq, JSON::Object::Entries {
      {"source", "hci.readStart"},
      {"data", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"active", true}
      }}
    }, QueuedResponse{});
  });
}

void HCI::readStop (const String& seq, ID id, const Callback cb) {
  this->loop.dispatch([=, this]() {
    auto handle = this->getHandle(id);
    if (!handle || handle->fd < 0) {
      cb(seq, JSON::Object::Entries {
        {"source", "hci.readStop"},
        {"err", JSON::Object::Entries {
          {"type", "NotFoundError"},
          {"message", "Handle not found"},
          {"id", std::to_string(id)}
        }}
      }, QueuedResponse{});
      return;
    }

    if (handle->pollActive && handle->poll) {
      uv_poll_stop(handle->poll);
      handle->pollActive = false;
    }

    handle->eventCb = nullptr;
    handle->reading = false;

    cb(seq, JSON::Object::Entries {
      {"source", "hci.readStop"},
      {"data", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"active", false}
      }}
    }, QueuedResponse{});
  });
}

#else

void HCI::listAdapters (const String& seq, const Callback cb) {
  cb(seq, notSupported("hci.listAdapters"), QueuedResponse{});
}

void HCI::getAdapter (const String& seq, uint16_t, const Callback cb) {
  cb(seq, notSupported("hci.getAdapterInfo"), QueuedResponse{});
}

void HCI::setAdapterState (const String& seq, uint16_t, bool, const Callback cb) {
  cb(seq, notSupported("hci.setAdapterState"), QueuedResponse{});
}

void HCI::open (const String& seq, ID, uint16_t, const Callback cb) {
  cb(seq, notSupported("hci.open"), QueuedResponse{});
}

void HCI::close (const String& seq, ID, const Callback cb) {
  cb(seq, notSupported("hci.close"), QueuedResponse{});
}

void HCI::write (const String& seq, ID, SharedPointer<unsigned char[]>, size_t, const Callback cb) {
  cb(seq, notSupported("hci.write"), QueuedResponse{});
}

void HCI::readStart (const String& seq, ID, const Callback cb) {
  cb(seq, notSupported("hci.readStart"), QueuedResponse{});
}

void HCI::readStop (const String& seq, ID, const Callback cb) {
  cb(seq, notSupported("hci.readStop"), QueuedResponse{});
}

#endif

}
