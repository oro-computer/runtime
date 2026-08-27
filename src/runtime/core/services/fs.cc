#include "../../string.hh"
#include "../../http.hh"
#include <future>
#include <thread>
#include "../../json.hh"
#include "../../config.hh"
#include "../../url.hh"
#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#  include <errno.h>
#  include <string.h>
#elif defined(__APPLE__) || defined(__MACH__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#  include <sys/time.h>
#  include <errno.h>
#  include <string.h>
#elif defined(__linux__)
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <string.h>
#endif

#include "fs.hh"

using oro::runtime::url::encodeURIComponent;
using oro::runtime::crypto::rand64;
using oro::runtime::string::join;
using oro::runtime::string::split;

namespace oro::runtime::core::services {
  bool FS::stop () {
  #if ORO_RUNTIME_PLATFORM_ANDROID
    return true;
  #elif ORO_RUNTIME_PLATFORM_LINUX
    // Non-blocking on Linux to avoid deadlocks with GTK-driven pumping.
    Vector<SharedPointer<filesystem::Watcher>> list;
    {
      Lock lock(this->mutex);
      for (auto &entry : this->watchers) {
        if (entry.second) list.push_back(entry.second);
      }
      this->watchers.clear();
    }
    if (!list.empty()) {
      #if defined(DEBUG)
        debug("FS::stop(): Linux scheduling %zu watcher(s) to stop; returning immediately", list.size());
      #endif
      this->loop.dispatch([list = std::move(list)]() mutable {
        for (const auto &w : list) {
          if (w) w->stop(w);
        }
      });
    } else {
      #if defined(DEBUG)
        debug("FS::stop(): Linux no watchers; returning immediately");
      #endif
    }
    return true;
  #else
    // Stop all active watchers on the loop thread before pausing the loop
    auto done = std::make_shared<std::promise<void>>();
    auto fut = done->get_future();
    this->loop.dispatch([=, this]() {
      Vector<SharedPointer<filesystem::Watcher>> list;
      {
        Lock lock(this->mutex);
        for (auto &entry : this->watchers) {
          if (entry.second) list.push_back(entry.second);
        }
        this->watchers.clear();
      }

      for (const auto &w : list) {
        if (w) w->stop(w);
      }
      done->set_value();
    });
    fut.wait();
    return true;
  #endif
  }
  JSON::Object getStatsJSON (const String& source, uv_stat_t* stats) {
    return JSON::Object::Entries {
      {"source", source},
      {"data", JSON::Object::Entries {
        {"st_dev", std::to_string(stats->st_dev)},
        {"st_mode", std::to_string(stats->st_mode)},
        {"st_nlink", std::to_string(stats->st_nlink)},
        {"st_uid", std::to_string(stats->st_uid)},
        {"st_gid", std::to_string(stats->st_gid)},
        {"st_rdev", std::to_string(stats->st_rdev)},
        {"st_ino", std::to_string(stats->st_ino)},
        {"st_size", std::to_string(stats->st_size)},
        {"st_blksize", std::to_string(stats->st_blksize)},
        {"st_blocks", std::to_string(stats->st_blocks)},
        {"st_flags", std::to_string(stats->st_flags)},
        {"st_gen", std::to_string(stats->st_gen)},
        {"st_atim", JSON::Object::Entries {
          {"tv_sec", std::to_string(stats->st_atim.tv_sec)},
          {"tv_nsec", std::to_string(stats->st_atim.tv_nsec)},
        }},
        {"st_mtim", JSON::Object::Entries {
          {"tv_sec", std::to_string(stats->st_mtim.tv_sec)},
          {"tv_nsec", std::to_string(stats->st_mtim.tv_nsec)}
        }},
        {"st_ctim", JSON::Object::Entries {
          {"tv_sec", std::to_string(stats->st_ctim.tv_sec)},
          {"tv_nsec", std::to_string(stats->st_ctim.tv_nsec)}
        }},
        {"st_birthtim", JSON::Object::Entries {
          {"tv_sec", std::to_string(stats->st_birthtim.tv_sec)},
          {"tv_nsec", std::to_string(stats->st_birthtim.tv_nsec)}
        }}
      }}
    };
  }

  void FS::RequestContext::setBuffer (SharedPointer<unsigned char[]> base, uint32_t len) {
    this->buffer = base;
    this->buf.base = reinterpret_cast<char*>(base.get());
    this->buf.len = len;
  }

  FS::Descriptor::Descriptor (FS* fs, ID id, const String& filename)
  #if ORO_RUNTIME_PLATFORM_ANDROID
    : resource(filename, { false, &fs->context.android.contentResolver}),
  #else
    : resource(filename, { false }),
  #endif
      fs(fs),
      id(id)
  {}

  bool FS::Descriptor::isDirectory () const {
  #if ORO_RUNTIME_PLATFORM_ANDROID
    if (this->isAndroidAssetDirectory) {
      return true;
    }
  #endif
    return this->dir != nullptr;
  }

  bool FS::Descriptor::isFile () const {
  #if ORO_RUNTIME_PLATFORM_ANDROID
    if (this->androidAsset != nullptr) {
      return true;
    }
  #endif
    return this->fd > 0 && this->dir == nullptr;
  }

  bool FS::Descriptor::isRetained () const {
    return this->retained;
  }

  bool FS::Descriptor::isStale () const {
    return this->stale;
  }

  SharedPointer<FS::Descriptor> FS::getDescriptor (ID id) {
    Lock lock(this->mutex);
    if (descriptors.find(id) != descriptors.end()) {
      return descriptors.at(id);
    }
    return nullptr;
  }

  SharedPointer<FS::Descriptor> FS::getDescriptor (ID id) const {
    if (descriptors.find(id) != descriptors.end()) {
      return descriptors.at(id);
    }
    return nullptr;
  }

  void FS::removeDescriptor (ID id) {
    Lock lock(this->mutex);
    if (descriptors.find(id) != descriptors.end()) {
      descriptors.erase(id);
    }
  }

  bool FS::hasDescriptor (ID id) const {
    return descriptors.find(id) != descriptors.end();
  }

  void FS::retainOpenDescriptor (
    const String& seq,
    ID id,
    const Callback callback
  ) {
    auto desc = getDescriptor(id);

    if (desc == nullptr) {
      auto json = JSON::Object::Entries {
        {"source", "fs.retainOpenDescriptor"},
        {"err", JSON::Object::Entries {
          {"id", std::to_string(id)},
          {"code", "ENOTOPEN"},
          {"type", "NotFoundError"},
          {"message", "No file descriptor found with that id"}
        }}
      };

      return callback(seq, json, QueuedResponse{});
    }

    desc->retained = true;
    auto json = JSON::Object::Entries {
      {"source", "fs.retainOpenDescriptor"},
      {"data", JSON::Object::Entries {
        {"id", std::to_string(desc->id)}
      }}
    };

    callback(seq, json, QueuedResponse{});
  }

  void FS::access (
    const String& seq,
    const String& path,
    int mode,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() mutable {
      auto loop = this->loop.get();
      auto desc = std::make_shared<Descriptor>(this, 0, path);

  #if ORO_RUNTIME_PLATFORM_ANDROID
      if (desc->resource.url.scheme == "oro") {
        if (desc->resource.access(mode) == mode) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.access"},
            {"data", JSON::Object::Entries {
              {"mode", mode},
            }}
          };

          return callback(seq, json, QueuedResponse{});
        } else if (mode == R_OK || mode == F_OK) {
          auto name = desc->resource.name;

          if (name.starts_with("/")) {
            name = name.substr(1);
          } else if (name.starts_with("./")) {
            name = name.substr(2);
          }

          const auto attachment = android::JNIEnvironmentAttachment(desc->fs->context.android.jvm);
          const auto assetManager = CallObjectClassMethodFromAndroidEnvironment(
            attachment.env,
            this->context.android.activity,
            "getAssetManager",
            "()Landroid/content/res/AssetManager;"
          );

          const auto entries = (jobjectArray) CallObjectClassMethodFromAndroidEnvironment(
            attachment.env,
            assetManager,
            "list",
            "(Ljava/lang/String;)[Ljava/lang/String;",
            attachment.env->NewStringUTF(name.c_str())
          );

          const auto length = attachment.env->GetArrayLength(entries);
          if (length > 0) {
            const auto json = JSON::Object::Entries {
              {"source", "fs.access"},
              {"data", JSON::Object::Entries {
                {"mode", mode},
              }}
            };

            return callback(seq, json, QueuedResponse{});
          }
        }
      } else if (
        desc->resource.url.scheme == "content" ||
        desc->resource.url.scheme == "android.resource"
      ) {
        if (this->context.android.contentResolver.hasAccess(desc->resource.url.str())) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.access"},
            {"data", JSON::Object::Entries {
              {"mode", mode},
            }}
          };

          return callback(seq, json, QueuedResponse{});
        }
      }
  #else
      if (desc->resource.url.scheme == "oro") {
        if (desc->resource.access(mode) == mode) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.access"},
            {"data", JSON::Object::Entries {
              {"mode", mode},
            }}
          };

          return callback(seq, json, QueuedResponse{});
        }
      }
  #endif

      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_access(loop, req, desc->resource.path.string().c_str(), mode, [](uv_fs_t* req) {
        auto ctx = (RequestContext *) req->data;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          bool handled = false;
        #if ORO_RUNTIME_PLATFORM_ANDROID
          if (ctx->descriptor->resource.access(req->flags) == req->flags) {
            json = JSON::Object::Entries {
              {"source", "fs.access"},
              {"data", JSON::Object::Entries {
                {"mode", req->flags},
              }}
            };
            handled = true;
          }
        #endif

          if (!handled) {
            json = JSON::Object::Entries {
              {"source", "fs.access"},
              {"err", JSON::Object::Entries {
                {"code", req->result},
                {"message", String(uv_strerror((int) req->result))}
              }}
            };
          }
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.access"},
            {"data", JSON::Object::Entries {
              {"mode", req->flags},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse {});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.access"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::chmod (
    const String& seq,
    const String& path,
    int mode,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      auto filename = path.c_str();
      auto loop = this->loop.get();
      auto ctx = new RequestContext(seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_chmod(loop, req, filename, mode, [](uv_fs_t* req) {
        auto ctx = (RequestContext *) req->data;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.chmod"},
            {"err", JSON::Object::Entries {
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.chmod"},
            {"data", JSON::Object::Entries {
              {"mode", req->flags},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse {});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.chmod"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::chown (
    const String& seq,
    const String& path,
    uv_uid_t uid,
    uv_gid_t gid,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      const auto ctx = new RequestContext(seq, callback);
      const auto err = uv_fs_chown(this->loop.get(), &ctx->req, path.c_str(), uid, gid, [](uv_fs_t* req) {
        auto ctx = static_cast<RequestContext*>(req->data);
        auto json = JSON::Object{};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.chown"},
            {"err", JSON::Object::Entries {
              {"code", uv_fs_get_result(req)},
              {"message", String(uv_strerror(uv_fs_get_result(req)))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.chown"},
            {"data", JSON::Object::Entries {
              {"mode", req->flags},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse {});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.chown"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };
        ctx->callback(seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::lchown (
    const String& seq,
    const String& path,
    uv_uid_t uid,
    uv_gid_t gid,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      const auto ctx = new RequestContext(seq, callback);
      const auto err = uv_fs_lchown(this->loop.get(), &ctx->req, path.c_str(), uid, gid, [](uv_fs_t* req) {
        auto ctx = static_cast<RequestContext*>(req->data);
        auto json = JSON::Object{};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.lchown"},
            {"err", JSON::Object::Entries {
              {"code", uv_fs_get_result(req)},
              {"message", String(uv_strerror(uv_fs_get_result(req)))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.lchown"},
            {"data", JSON::Object::Entries {
              {"mode", req->flags},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse {});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.lchown"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };
        ctx->callback(seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::lchmod (
    const String& seq,
    const String& path,
    int mode,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
    #if defined(_WIN32)
      auto json = JSON::Object::Entries {
        {"source", "fs.lchmod"},
        {"err", JSON::Object::Entries {{"message", "Not supported"}}}
      };
      return callback(seq, json, QueuedResponse{});
    #else
      // Sandbox check
      {
        auto d = std::make_shared<Descriptor>(const_cast<FS*>(this), 0, path);
        if (!d->resource.startAccessing()) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.lchmod"},
            {"err", JSON::Object::Entries {
              {"type", "SecurityError"},
              {"code", "EPERM"},
              {"message", "Filesystem sandbox denied access"}
            }}
          };
          return callback(seq, json, QueuedResponse{});
        }
      }
      auto ctx = new RequestContext(seq, callback);
    #  if defined(__APPLE__) || defined(__MACH__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
      int r = ::lchmod(path.c_str(), mode);
      auto json = JSON::Object::Entries {
        {"source", "fs.lchmod"},
        { r < 0 ? "err" : "data", JSON::Object::Entries {
          { r < 0 ? "message" : "result", r < 0 ? String(strerror(errno)) : String("0") }
        }}
      };
      callback(seq, json, QueuedResponse{});
      delete ctx;
    #  elif defined(__linux__)
      int r = fchmodat(AT_FDCWD, path.c_str(), mode, AT_SYMLINK_NOFOLLOW);
      auto json = JSON::Object::Entries {
        {"source", "fs.lchmod"},
        { r < 0 ? "err" : "data", JSON::Object::Entries {
          { r < 0 ? "message" : "result", r < 0 ? String(strerror(errno)) : String("0") }
        }}
      };
      callback(seq, json, QueuedResponse{});
      delete ctx;
    #  else
      auto json = JSON::Object::Entries {
        {"source", "fs.lchmod"},
        {"err", JSON::Object::Entries {{"message", "Not supported"}}}
      };
      callback(seq, json, QueuedResponse{});
      delete ctx;
    #  endif
    #endif
    });
  }

  void FS::close (
    const String& seq,
    ID id,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      auto desc = getDescriptor(id);

      if (desc == nullptr) {
        auto json = JSON::Object::Entries {
          {"source", "fs.close"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "ENOTOPEN"},
            {"type", "NotFoundError"},
            {"message", "No file descriptor found with that id"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

    #if ORO_RUNTIME_PLATFORM_ANDROID
      if (desc->androidAsset != nullptr) {
        Lock lock(this->mutex);
        AAsset_close(desc->androidAsset);
        desc->androidAsset = nullptr;
        const auto json = JSON::Object::Entries {
          {"source", "fs.close"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(desc->id)},
            {"fd", desc->fd}
          }}
        };

        this->removeDescriptor(desc->id);
        return callback(seq, json, QueuedResponse{});
      } else if (
        desc->resource.url.scheme == "content" ||
        desc->resource.url.scheme == "android.resource"
      ) {
        Lock lock(this->mutex);
        this->context.android.contentResolver.closeFileDescriptor(
          desc->androidContent
        );

        desc->androidContent = nullptr;
        const auto json = JSON::Object::Entries {
          {"source", "fs.close"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(desc->id)},
            {"fd", desc->fd}
          }}
        };

        this->removeDescriptor(desc->id);
        return callback(seq, json, QueuedResponse{});
      }
    #endif

      auto loop = this->loop.get();
      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_close(loop, req, desc->fd, [](uv_fs_t* req) {
        auto ctx = (RequestContext *) req->data;
        auto desc = ctx->descriptor;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.close"},
            {"err", JSON::Object::Entries {
              {"id", std::to_string(desc->id)},
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.close"},
            {"data", JSON::Object::Entries {
              {"id", std::to_string(desc->id)},
              {"fd", desc->fd}
            }}
          };

          desc->fs->removeDescriptor(desc->id);
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.close"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(desc->id)},
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::open (
    const String& seq,
    ID id,
    const String& path,
    int flags,
    int mode,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      auto desc = std::make_shared<Descriptor>(this, id, path);

    #if ORO_RUNTIME_PLATFORM_ANDROID
      if (desc->resource.url.scheme == "oro") {
        auto assetManager = filesystem::Resource::getSharedAndroidAssetManager();
        desc->androidAsset = AAssetManager_open(
          assetManager,
          desc->resource.name.c_str(),
          AASSET_MODE_RANDOM
        );

        desc->fd = AAsset_openFileDescriptor(
          desc->androidAsset,
          &desc->androidAssetOffset,
          &desc->androidAssetLength
        );

        const auto json = JSON::Object::Entries {
          {"source", "fs.open"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(desc->id)},
            {"fd", desc->fd}
          }}
        };

        // insert into `descriptors` map
        Lock lock(desc->fs->mutex);
        this->descriptors.insert_or_assign(desc->id, desc);
        return callback(seq, json, QueuedResponse{});
      } else if (
        desc->resource.url.scheme == "content" ||
        desc->resource.url.scheme == "android.resource"
      ) {
        auto fileDescriptor = this->context.android.contentResolver.openFileDescriptor(
          desc->resource.url.str(),
          &desc->androidContentOffset,
          &desc->androidContentLength
        );

        if (fileDescriptor == nullptr) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.open"},
            {"err", JSON::Object::Entries {
              {"id", std::to_string(desc->id)},
              {"type", "NotFoundError"},
              {"message", "Content does not exist at given URI"}
            }}
          };

          return callback(seq, json, QueuedResponse{});
        }

        desc->fd = this->context.android.contentResolver.getFileDescriptorFD(
          fileDescriptor
        );

        desc->androidContent = fileDescriptor;

        const auto json = JSON::Object::Entries {
          {"source", "fs.open"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(desc->id)},
            {"fd", desc->fd}
          }}
        };

        // insert into `descriptors` map
        Lock lock(desc->fs->mutex);
        this->descriptors.insert_or_assign(desc->id, desc);
        return callback(seq, json, QueuedResponse{});
      }
    #endif

      auto loop = this->loop.get();
      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_open(loop, req, desc->resource.path.string().c_str(), flags, mode, [](uv_fs_t* req) {
        auto ctx = (RequestContext *) req->data;
        auto desc = ctx->descriptor;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          bool handled = false;
        #if ORO_RUNTIME_PLATFORM_ANDROID
          if (ctx->descriptor->resource.isAndroidLocalAsset()) {
            auto assetManager = filesystem::Resource::getSharedAndroidAssetManager();
            desc->androidAsset = AAssetManager_open(
              assetManager,
              ctx->descriptor->resource.name.c_str(),
              AASSET_MODE_RANDOM
            );

            desc->fd = AAsset_openFileDescriptor(
              desc->androidAsset,
              &desc->androidAssetOffset,
              &desc->androidAssetLength
            );
            json = JSON::Object::Entries {
              {"source", "fs.open"},
              {"data", JSON::Object::Entries {
                {"id", std::to_string(desc->id)},
                {"fd", desc->fd}
              }}
            };

            // insert into `descriptors` map
            Lock lock(desc->fs->mutex);
            desc->fs->descriptors.insert_or_assign(desc->id, desc);
            handled = true;
          }
        #endif

          if (!handled) {
            json = JSON::Object::Entries {
              {"source", "fs.open"},
              {"err", JSON::Object::Entries {
                {"id", std::to_string(desc->id)},
                {"code", req->result},
                {"message", String(uv_strerror((int) req->result))}
              }}
            };
          }
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.open"},
            {"data", JSON::Object::Entries {
              {"id", std::to_string(desc->id)},
              {"fd", (int) req->result}
            }}
          };

          desc->fd = (int) req->result;
          // insert into `descriptors` map
          Lock lock(desc->fs->mutex);
          desc->fs->descriptors.insert_or_assign(desc->id, desc);
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.open"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(desc->id)},
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::opendir (
    const String& seq,
    ID id,
    const String& path,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      auto desc =  std::make_shared<Descriptor>(this, id, path);

  #if ORO_RUNTIME_PLATFORM_ANDROID
      if (desc->resource.url.scheme == "oro") {
        auto name = desc->resource.name;

        if (name.starts_with("/")) {
          name = name.substr(1);
        } else if (name.starts_with("./")) {
          name = name.substr(2);
        }

        const auto attachment = android::JNIEnvironmentAttachment(desc->fs->context.android.jvm);
        const auto assetManager = CallObjectClassMethodFromAndroidEnvironment(
          attachment.env,
          desc->fs->context.android.activity,
          "getAssetManager",
          "()Landroid/content/res/AssetManager;"
        );

        const auto entries = (jobjectArray) CallObjectClassMethodFromAndroidEnvironment(
          attachment.env,
          assetManager,
          "list",
          "(Ljava/lang/String;)[Ljava/lang/String;",
          attachment.env->NewStringUTF(name.c_str())
        );

        const auto length = attachment.env->GetArrayLength(entries);

        for (int i = 0; i < length; ++i) {
          const auto entry = (jstring) attachment.env->GetObjectArrayElement(entries, i);
          const auto filename = attachment.env->GetStringUTFChars(entry, nullptr);
          if (filename != nullptr) {
            desc->androidAssetDirectoryEntries.push(filename);
            attachment.env->ReleaseStringUTFChars(entry, filename);
          }
        }

        if (length > 0) {
          desc->isAndroidAssetDirectory = true;
          const auto json = JSON::Object::Entries {
            {"source", "fs.opendir"},
            {"data", JSON::Object::Entries {
              {"id", std::to_string(desc->id)}
            }}
          };

          // insert into `descriptors` map
          Lock lock(desc->fs->mutex);
          desc->fs->descriptors.insert_or_assign(desc->id, desc);
          return callback(seq, json, QueuedResponse{});
        }
      } else if (
        desc->resource.url.scheme == "content" ||
        desc->resource.url.scheme == "android.resource"
      ) {
        const auto entries = this->context.android.contentResolver.getPathnameEntriesFromContentURI(
          desc->resource.url.str()
        );

        for (const auto& entry : entries) {
          desc->androidContentDirectoryEntries.push(entry);
        }

        if (entries.size() > 0) {
          desc->isAndroidContentDirectory = true;
          const auto json = JSON::Object::Entries {
            {"source", "fs.opendir"},
            {"data", JSON::Object::Entries {
              {"id", std::to_string(desc->id)}
            }}
          };

          // insert into `descriptors` map
          Lock lock(desc->fs->mutex);
          desc->fs->descriptors.insert_or_assign(desc->id, desc);
          return callback(seq, json, QueuedResponse{});
        }
      }
    #endif

      auto loop = this->loop.get();
      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_opendir(loop, req, desc->resource.path.string().c_str(), [](uv_fs_t *req) {
        auto ctx = (RequestContext *) req->data;
        auto desc = ctx->descriptor;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          bool handled = false;

        #if ORO_RUNTIME_PLATFORM_ANDROID
          auto name = ctx->descriptor->resource.name;

          if (name.starts_with("/")) {
            name = name.substr(1);
          } else if (name.starts_with("./")) {
            name = name.substr(2);
          }

          const auto attachment = android::JNIEnvironmentAttachment(ctx->descriptor->fs->context.android.jvm);
          const auto assetManager = CallObjectClassMethodFromAndroidEnvironment(
            attachment.env,
            ctx->descriptor->fs->context.android.activity,
            "getAssetManager",
            "()Landroid/content/res/AssetManager;"
          );

          const auto entries = (jobjectArray) CallObjectClassMethodFromAndroidEnvironment(
            attachment.env,
            assetManager,
            "list",
            "(Ljava/lang/String;)[Ljava/lang/String;",
            attachment.env->NewStringUTF(name.c_str())
          );

          const auto length = attachment.env->GetArrayLength(entries);

          for (int i = 0; i < length; ++i) {
            const auto entry = (jstring) attachment.env->GetObjectArrayElement(entries, i);
            const auto filename = attachment.env->GetStringUTFChars(entry, nullptr);
            if (filename != nullptr) {
              desc->androidAssetDirectoryEntries.push(filename);
              attachment.env->ReleaseStringUTFChars(entry, filename);
            }
          }

          if (length > 0) {
            desc->isAndroidAssetDirectory = true;
          }

          if (desc->isAndroidAssetDirectory) {
            json = JSON::Object::Entries {
              {"source", "fs.opendir"},
              {"data", JSON::Object::Entries {
                {"id", std::to_string(desc->id)}
              }}
            };

            // insert into `descriptors` map
            Lock lock(desc->fs->mutex);
            desc->fs->descriptors.insert_or_assign(desc->id, desc);
            handled = true;
          }
        #endif

          if (!handled) {
            json = JSON::Object::Entries {
              {"source", "fs.opendir"},
              {"err", JSON::Object::Entries {
                {"id", std::to_string(desc->id)},
                {"code", req->result},
                {"message", String(uv_strerror((int) req->result))}
              }}
            };
          }
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.opendir"},
            {"data", JSON::Object::Entries {
              {"id", std::to_string(desc->id)}
            }}
          };

          desc->dir = (uv_dir_t *) req->ptr;
          // insert into `descriptors` map
          Lock lock(desc->fs->mutex);
          desc->fs->descriptors.insert_or_assign(desc->id, desc);
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.opendir"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(desc->id)},
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::readdir (
    const String& seq,
    ID id,
    size_t nentries,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      auto desc = getDescriptor(id);

      if (desc == nullptr) {
        auto json = JSON::Object::Entries {
          {"source", "fs.readdir"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "ENOTOPEN"},
            {"type", "NotFoundError"},
            {"message", "No directory descriptor found with that id"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      if (!desc->resource.startAccessing()) {
        auto json = JSON::Object::Entries {
          {"source", "fs.readdir"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"type", "SecurityError"},
            {"code", "EPERM"},
            {"message", "Filesystem sandbox denied access"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

    #if ORO_RUNTIME_PLATFORM_ANDROID
      if (desc->isAndroidAssetDirectory) {
        Vector<JSON::Any> entries;

        for (
          int i = 0;
          i < nentries && i < desc->androidAssetDirectoryEntries.size();
          ++i
        ) {
          const auto name = desc->androidAssetDirectoryEntries.front();
          const auto encodedName = name.ends_with("/")
            ? encodeURIComponent(name.substr(0, name.size() - 1))
            : encodeURIComponent(name);

          const auto entry = JSON::Object::Entries {
            {"type", name.ends_with("/") ? 2 : 1},
            {"name", encodedName}
          };

          entries.push_back(entry);
          desc->androidAssetDirectoryEntries.pop();
        }

        const auto json = JSON::Object::Entries {
          {"source", "fs.readdir"},
          {"data", entries}
        };

        return callback(seq, json, QueuedResponse{});
      } else if (desc->isAndroidContentDirectory) {
        Vector<JSON::Any> entries;

        for (
          int i = 0;
          i < nentries && i < desc->androidContentDirectoryEntries.size();
          ++i
        ) {
          const auto name = desc->androidContentDirectoryEntries.front();
          const auto encodedName = name.ends_with("/")
            ? encodeURIComponent(name.substr(0, name.size() - 1))
            : encodeURIComponent(name);

          const auto entry = JSON::Object::Entries {
            {"type", name.ends_with("/") ? 2 : 1},
            {"name", encodedName}
          };

          entries.push_back(entry);
          desc->androidContentDirectoryEntries.pop();
        }

        const auto json = JSON::Object::Entries {
          {"source", "fs.readdir"},
          {"data", entries}
        };

        return callback(seq, json, QueuedResponse{});
      }
    #endif

      if (!desc->isDirectory()) {
        auto json = JSON::Object::Entries {
          {"source", "fs.readdir"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "ENOTOPEN"},
            {"message", "No directory descriptor found with that id"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      Lock lock(desc->mutex);
      auto loop = this->loop.get();
      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;

      desc->dir->dirents = ctx->dirents;
      desc->dir->nentries = nentries;

      auto err = uv_fs_readdir(loop, req, desc->dir, [](uv_fs_t *req) {
        auto ctx = (RequestContext *) req->data;
        auto desc = ctx->descriptor;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.readdir"},
            {"err", JSON::Object::Entries {
              {"id", std::to_string(desc->id)},
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          Vector<JSON::Any> entries;

          for (int i = 0; i < req->result; ++i) {
            auto entry = JSON::Object::Entries {
              {"type", desc->dir->dirents[i].type},
              {"name", encodeURIComponent(desc->dir->dirents[i].name)}
            };

            entries.push_back(entry);
          }

          json = JSON::Object::Entries {
            {"source", "fs.readdir"},
            {"data", entries}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.readdir"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(desc->id)},
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::closedir (
    const String& seq,
    ID id,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      auto desc = getDescriptor(id);

      if (desc == nullptr) {
        auto json = JSON::Object::Entries {
          {"source", "fs.closedir"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "ENOTOPEN"},
            {"type", "NotFoundError"},
            {"message", "No directory descriptor found with that id"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

    #if ORO_RUNTIME_PLATFORM_ANDROID
      if (desc->isAndroidAssetDirectory) {
        Lock lock(this->mutex);
        desc->isAndroidAssetDirectory = false;
        desc->androidAssetDirectoryEntries = {};
        const auto json = JSON::Object::Entries {
          {"source", "fs.closedir"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(desc->id)}
          }}
        };

        this->removeDescriptor(desc->id);
        return callback(seq, json, QueuedResponse{});
      } else if (desc->isAndroidContentDirectory) {
        Lock lock(this->mutex);
        desc->isAndroidContentDirectory = false;
        desc->androidContentDirectoryEntries = {};
        const auto json = JSON::Object::Entries {
          {"source", "fs.closedir"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(desc->id)}
          }}
        };

        this->removeDescriptor(desc->id);
        return callback(seq, json, QueuedResponse{});
      }
    #endif

      if (!desc->isDirectory()) {
        auto json = JSON::Object::Entries {
          {"source", "fs.closedir"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "ENOTOPEN"},
            {"message", "The descriptor found with was not a directory"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      auto loop = this->loop.get();
      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_closedir(loop, req, desc->dir, [](uv_fs_t* req) {
        auto ctx = (RequestContext *) req->data;
        auto desc = ctx->descriptor;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.closedir"},
            {"err", JSON::Object::Entries {
              {"id", std::to_string(desc->id)},
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.closedir"},
            {"data", JSON::Object::Entries {
              {"id", std::to_string(desc->id)},
              {"fd", desc->fd}
            }}
          };

          desc->fs->removeDescriptor(desc->id);
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.closedir"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(desc->id)},
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::closeOpenDescriptor (
    const String& seq,
    ID id,
    const Callback callback
  ) {
    auto desc = getDescriptor(id);

    if (desc == nullptr) {
      auto json = JSON::Object::Entries {
        {"source", "fs.closeOpenDescriptor"},
        {"err", JSON::Object::Entries {
          {"id", std::to_string(id)},
          {"code", "ENOTOPEN"},
          {"type", "NotFoundError"},
          {"message", "No descriptor found with that id"}
        }}
      };

      return callback(seq, json, QueuedResponse{});
    }

    if (desc->isDirectory()) {
      this->closedir(seq, id, callback);
    } else if (desc->isFile()) {
      this->close(seq, id, callback);
    }
  }

  void FS::closeOpenDescriptors (const String& seq, const Callback callback) {
    return this->closeOpenDescriptors(seq, false, callback);
  }

  void FS::closeOpenDescriptors (
    const String& seq,
    bool preserveRetained,
    const Callback callback
  ) {
    Lock lock(this->mutex);

    auto pending = descriptors.size();
    int queued = 0;
    auto json = JSON::Object {};
    auto ids = Vector<uint64_t> {};

    for (auto const &tuple : descriptors) {
      ids.push_back(tuple.first);
    }

    for (auto const id : ids) {
      auto desc = descriptors[id];
      pending--;

      if (desc == nullptr) {
        descriptors.erase(id);
        continue;
      }

      if (preserveRetained && desc->isRetained()) {
        continue;
      }

      if (desc->isDirectory()) {
        queued++;
        this->closedir(seq, id, [pending, callback](auto seq, auto json, auto queuedResponse) {
          if (pending == 0) {
            callback(seq, json, queuedResponse);
          }
        });
      } else if (desc->isFile()) {
        queued++;
        this->close(seq, id, [pending, callback](auto seq, auto json, auto queuedResponse) {
          if (pending == 0) {
            callback(seq, json, queuedResponse);
          }
        });
      }
    }

    if (queued == 0) {
      callback(seq, json, QueuedResponse{});
    }
  }

  void FS::read (
    const String& seq,
    ID id,
    size_t size,
    size_t offset,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() mutable {
      auto desc = getDescriptor(id);

      if (desc == nullptr) {
        auto json = JSON::Object::Entries {
          {"source", "fs.read"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "ENOTOPEN"},
            {"type", "NotFoundError"},
            {"message", "No file descriptor found with that id"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

    #if ORO_RUNTIME_PLATFORM_ANDROID
      if (desc->androidAsset != nullptr) {
        const auto length = AAsset_getLength(desc->androidAsset);

        if (offset >= static_cast<size_t>(length)) {
          const auto headers = http::Headers {{
            {"content-type", "application/octet-stream"},
            {"content-length", 0}
          }};

          QueuedResponse queuedResponse {0};
          queuedResponse.id = rand64();
          queuedResponse.body = std::make_shared<unsigned char[]>(size);
          queuedResponse.length = 0;
          queuedResponse.headers = headers;
          return callback(seq, JSON::Object{}, queuedResponse);
        } else {
          if (size > length - offset) {
            size = length - offset;
          }

          offset += desc->androidAssetOffset;
        }
      } else if (desc->androidContent != nullptr) {
        const auto length = desc->androidContentLength;

        if (offset >= static_cast<size_t>(length)) {
          const auto headers = http::Headers {{
            {"content-type", "application/octet-stream"},
            {"content-length", 0}
          }};

          QueuedResponse queuedResponse {0};
          queuedResponse.id = rand64();
          queuedResponse.body = std::make_shared<unsigned char[]>(size);
          queuedResponse.length = 0;
          queuedResponse.headers = headers;
          return callback(seq, JSON::Object{}, queuedResponse);
        } else {
          if (size > length - offset) {
            size = length - offset;
          }

          offset += desc->androidContentOffset;
        }
      }
    #endif

      auto bytes = std::make_shared<unsigned char[]>(size);
      auto loop = this->loop.get();
      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;

      ctx->setBuffer(bytes, size);

      auto err = uv_fs_read(loop, req, desc->fd, &ctx->buf, 1, offset, [](uv_fs_t* req) {
        auto ctx = static_cast<RequestContext*>(req->data);
        auto desc = ctx->descriptor;
        auto json = JSON::Object {};
        QueuedResponse queuedResponse = {0};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.read"},
            {"err", JSON::Object::Entries {
              {"id", std::to_string(desc->id)},
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          auto headers = http::Headers {{
            {"content-type", "application/octet-stream"},
            {"content-length", req->result}
          }};

          queuedResponse.id = rand64();
          queuedResponse.body = ctx->buffer;
          queuedResponse.length = (int) req->result;
          queuedResponse.headers = headers.str();
        }

        ctx->callback(ctx->seq, json, queuedResponse);
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.read"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(desc->id)},
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::watch (
    const String& seq,
    ID id,
    const String& path,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
    #if ORO_RUNTIME_PLATFORM_ANDROID
      auto json = JSON::Object::Entries {
        {"source", "fs.watch"},
        {"err", JSON::Object::Entries {
          {"message", "Not supported"}
        }}
      };

      callback(seq, json, QueuedResponse{});
      return;
    #else
      SharedPointer<filesystem::Watcher> watcher;
      {
        Lock lock(this->mutex);
        watcher = this->watchers[id];
      }

      if (watcher == nullptr) {
        watcher.reset(new filesystem::Watcher(path));
        watcher->loop = &this->loop;
        watcher->ownsLoop = false;
        const auto started = watcher->start([=, this](
          const auto& changed,
          const auto& events,
          const auto& context
        ) mutable {
          JSON::Array::Entries eventNames;

          if (std::find(events.begin(), events.end(), filesystem::Watcher::Event::RENAME) != events.end()) {
            eventNames.push_back("rename");
          }

          if (std::find(events.begin(), events.end(), filesystem::Watcher::Event::CHANGE) != events.end()) {
            eventNames.push_back("change");
          }

          auto json = JSON::Object::Entries {
            {"source", "fs.watch"},
            {"data", JSON::Object::Entries {
              {"id", std::to_string(id)},
              {"events", eventNames},
              {"path", std::filesystem::relative(changed, path).string()}
            }}
          };

          callback("-1", json, QueuedResponse{});
        });

        if (!started) {
          auto json = JSON::Object::Entries {
            {"source", "fs.watch"},
            {"err", JSON::Object::Entries {
              {"message", "Failed to start 'fs.Watcher'"}
            }}
          };

          callback(seq, json, QueuedResponse{});
          return;
        }

        {
          Lock lock(this->mutex);
          this->watchers.insert_or_assign(id, watcher);
        }
      }

      auto json = JSON::Object::Entries {
        {"source", "fs.watch"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(id)}
        }}
      };

      callback(seq, json, QueuedResponse{});
    #endif
    });
  }

  void FS::write (
    const String& seq,
    ID id,
    SharedPointer<unsigned char[]> bytes,
    size_t size,
    size_t offset,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      auto desc = getDescriptor(id);

      if (desc == nullptr) {
        auto json = JSON::Object::Entries {
          {"source", "fs.write"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "ENOTOPEN"},
            {"type", "NotFoundError"},
            {"message", "No file descriptor found with that id"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

    #if ORO_RUNTIME_PLATFORM_ANDROID
      if (desc->androidAsset != nullptr) {
        auto json = JSON::Object::Entries {
          {"source", "fs.write"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "EPERM"},
            {"type", "NotAllowedError"},
            {"message", "Cannot write to an Android Asset file descriptor."}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }
    #endif

      auto loop = this->loop.get();
      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;

      ctx->setBuffer(bytes, size);
      auto err = uv_fs_write(loop, req, desc->fd, &ctx->buf, 1, offset, [](uv_fs_t* req) {
        auto ctx = static_cast<RequestContext*>(req->data);
        auto desc = ctx->descriptor;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.write"},
            {"err", JSON::Object::Entries {
              {"id", std::to_string(desc->id)},
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.write"},
            {"data", JSON::Object::Entries {
              {"id", std::to_string(desc->id)},
              {"result", req->result}
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.write"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(desc->id)},
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::stat (
    const String& seq,
    const String& path,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      auto desc = std::make_shared<Descriptor>(this, 0, path);

      if (!desc->resource.startAccessing()) {
        const auto json = JSON::Object::Entries {
          {"source", "fs.stat"},
          {"err", JSON::Object::Entries {
            {"type", "SecurityError"},
            {"code", "EPERM"},
            {"message", "Filesystem sandbox denied access"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

    #if ORO_RUNTIME_PLATFORM_ANDROID
      if (desc->resource.isAndroidLocalAsset()) {
        const auto json = JSON::Object::Entries {
          {"source", "fs.stat"},
          {"data", JSON::Object::Entries {
            {"st_mode", R_OK},
            {"st_size", desc->resource.size()}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      } else if (
        desc->resource.url.scheme == "content" ||
        desc->resource.url.scheme == "android.resource"
      ) {
        const auto json = JSON::Object::Entries {
          {"source", "fs.stat"},
          {"data", JSON::Object::Entries {
            {"st_mode", R_OK},
            {"st_size", desc->resource.size()}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }
    #endif

      auto loop = this->loop.get();
      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_stat(loop, req, desc->resource.path.string().c_str(), [](uv_fs_t *req) {
        auto ctx = (RequestContext *) req->data;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.stat"},
            {"err", JSON::Object::Entries {
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = getStatsJSON("fs.stat", uv_fs_get_statbuf(req));
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.stat"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::stopWatch (
    const String& seq,
    ID id,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
    #if ORO_RUNTIME_PLATFORM_ANDROID
      auto json = JSON::Object::Entries {
        {"source", "fs.stopWatch"},
        {"err", JSON::Object::Entries {
          {"message", "Not supported"}
        }}
      };

      callback(seq, json, QueuedResponse{});
      return;
    #else
      auto watcher = this->watchers[id];
      if (watcher != nullptr) {
        watcher->stop(watcher);
        this->watchers.erase(id);
        auto json = JSON::Object::Entries {
          {"source", "fs.stopWatch"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(id)},
          }}
        };
        callback(seq, json, QueuedResponse{});
      } else {
        auto json = JSON::Object::Entries {
          {"source", "fs.stat"},
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "fs.Watcher does not exist"}
          }}
        };

        callback(seq, json, QueuedResponse{});
      }
    #endif
    });
  }

  void FS::fsync (
    const String& seq,
    ID id,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      auto desc = getDescriptor(id);

      if (desc == nullptr) {
        auto json = JSON::Object::Entries {
          {"source", "fs.fsync"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "ENOTOPEN"},
            {"type", "NotFoundError"},
            {"message", "No file descriptor found with that id"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      auto loop = this->loop.get();
      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_fsync(loop, req, desc->fd, [](uv_fs_t *req) {
        auto ctx = (RequestContext *) req->data;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.fsync"},
            {"err", JSON::Object::Entries {
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.fsync"},
            {"data", JSON::Object::Entries {
              {"result", req->result},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.fsync"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::ftruncate (
    const String& seq,
    ID id,
    int64_t offset,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      auto desc = getDescriptor(id);

      if (desc == nullptr) {
        auto json = JSON::Object::Entries {
          {"source", "fs.ftruncate"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "ENOTOPEN"},
            {"type", "NotFoundError"},
            {"message", "No file descriptor found with that id"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      auto loop = this->loop.get();
      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_ftruncate(loop, req, desc->fd, offset, [](uv_fs_t *req) {
        auto ctx = (RequestContext *) req->data;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.ftruncate"},
            {"err", JSON::Object::Entries {
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.ftruncate"},
            {"data", JSON::Object::Entries {
              {"result", req->result},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.ftruncate"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::fdatasync (
    const String& seq,
    ID id,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      auto desc = getDescriptor(id);

      if (desc == nullptr) {
        auto json = JSON::Object::Entries {
          {"source", "fs.fdatasync"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "ENOTOPEN"},
            {"type", "NotFoundError"},
            {"message", "No file descriptor found with that id"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      auto loop = this->loop.get();
      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_fdatasync(loop, req, desc->fd, [](uv_fs_t *req) {
        auto ctx = (RequestContext *) req->data;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.fdatasync"},
            {"err", JSON::Object::Entries {
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.fdatasync"},
            {"data", JSON::Object::Entries {
              {"result", req->result},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.fdatasync"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::fstat (
    const String& seq,
    ID id,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      auto desc = getDescriptor(id);

      if (desc == nullptr) {
        auto json = JSON::Object::Entries {
          {"source", "fs.fstat"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "ENOTOPEN"},
            {"type", "NotFoundError"},
            {"message", "No file descriptor found with that id"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

    #if ORO_RUNTIME_PLATFORM_ANDROID
      if (desc->androidAsset != nullptr) {
        const auto json = JSON::Object::Entries {
          {"source", "fs.stat"},
          {"data", JSON::Object::Entries {
            {"st_mode", R_OK},
            {"st_size", desc->resource.size()}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }
    #endif

      auto loop = this->loop.get();
      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_fstat(loop, req, desc->fd, [](uv_fs_t *req) {
        auto ctx = (RequestContext *) req->data;
        auto desc = ctx->descriptor;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.fstat"},
            {"err", JSON::Object::Entries {
              {"id", std::to_string(desc->id)},
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = getStatsJSON("fs.fstat", uv_fs_get_statbuf(req));
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.fstat"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::getOpenDescriptors (
    const String& seq,
    const Callback callback
  ) const {
    auto entries = Vector<JSON::Any> {};
    auto snapshot = Vector<SharedPointer<Descriptor>> {};

    {
      Lock lock(this->mutex);
      snapshot.reserve(this->descriptors.size());

      for (auto const &tuple : this->descriptors) {
        auto desc = tuple.second;

        if (!desc || (desc->isStale() && !desc->isRetained())) {
          continue;
        }

        snapshot.push_back(desc);
      }
    }

    entries.reserve(snapshot.size());

    for (const auto& desc : snapshot) {
      auto entry = JSON::Object::Entries {
        {"id",  std::to_string(desc->id)},
        {"fd", std::to_string(desc->isDirectory() ? desc->id : desc->fd)},
        {"type", desc->dir ? "directory" : "file"}
      };

      entries.push_back(entry);
    }

    auto json = JSON::Object::Entries {
      {"source", "fs.getOpenDescriptors"},
      {"data", entries}
    };

    callback(seq, json, QueuedResponse{});
  }

  void FS::lstat (
    const String& seq,
    const String& path,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      auto desc = std::make_shared<Descriptor>(this, 0, path);
      auto loop = this->loop.get();
      auto ctx = new RequestContext(desc, seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_lstat(loop, req, desc->resource.path.string().c_str(), [](uv_fs_t* req) {
        auto ctx = (RequestContext *) req->data;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.stat"},
            {"err", JSON::Object::Entries {
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = getStatsJSON("fs.lstat", uv_fs_get_statbuf(req));
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.stat"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::link (
    const String& seq,
    const String& src,
    const String& dest,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      // Policy: optionally deny hard link creation when no-follow is enabled
      {
        const auto cfg = oro::runtime::config::getUserConfig();
        const bool noFollow = (
          cfg.contains("filesystem_no_follow_symlinks") &&
          (cfg.at("filesystem_no_follow_symlinks") == "1" || cfg.at("filesystem_no_follow_symlinks") == "true")
        );
        const bool disableLinks = (
          cfg.contains("filesystem_disable_links") &&
          (cfg.at("filesystem_disable_links") == "1" || cfg.at("filesystem_disable_links") == "true")
        );
        if (noFollow || disableLinks) {
          auto json = JSON::Object::Entries {
            {"source", "fs.link"},
            {"err", JSON::Object::Entries {
              {"type", "SecurityError"},
              {"code", "EPERM"},
              {"message", noFollow
                ? "Hard link creation is disabled by no-follow policy"
                : "Hard link creation is disabled by policy"
              }
            }}
          };
          return callback(seq, json, QueuedResponse{});
        }
      }
      // Sandbox check for source and destination
      {
        auto s = std::make_shared<Descriptor>(const_cast<FS*>(this), 0, src);
        auto d = std::make_shared<Descriptor>(const_cast<FS*>(this), 0, dest);
        if (!s->resource.startAccessing() || !d->resource.startAccessing()) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.link"},
            {"err", JSON::Object::Entries {
              {"type", "SecurityError"},
              {"code", "EPERM"},
              {"message", "Filesystem sandbox denied access"}
            }}
          };
          return callback(seq, json, QueuedResponse{});
        }
      }
      auto ctx = new RequestContext(seq, callback);
      auto err = uv_fs_link(this->loop.get(), &ctx->req, src.c_str(), dest.c_str(), [](uv_fs_t* req) {
        auto ctx = static_cast<RequestContext*>(req->data);
        auto json = JSON::Object{};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.link"},
            {"err", JSON::Object::Entries {
              {"code", uv_fs_get_result(req)},
              {"message", String(uv_strerror(uv_fs_get_result(req)))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.link"},
            {"data", JSON::Object::Entries {
              {"mode", req->flags},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse {});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.link"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };
        ctx->callback(seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::utimes (
    const String& seq,
    const String& path,
    double atime,
    double mtime,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      // Sandbox check
      {
        auto d = std::make_shared<Descriptor>(const_cast<FS*>(this), 0, path);
        if (!d->resource.startAccessing()) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.utimes"},
            {"err", JSON::Object::Entries {
              {"type", "SecurityError"},
              {"code", "EPERM"},
              {"message", "Filesystem sandbox denied access"}
            }}
          };
          return callback(seq, json, QueuedResponse{});
        }
      }
      auto ctx = new RequestContext(seq, callback);
      auto err = uv_fs_utime(this->loop.get(), &ctx->req, path.c_str(), atime, mtime, [](uv_fs_t* req) {
        auto ctx = static_cast<RequestContext*>(req->data);
        auto json = JSON::Object{};
        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.utimes"},
            {"err", JSON::Object::Entries {
              {"code", uv_fs_get_result(req)},
              {"message", String(uv_strerror(uv_fs_get_result(req)))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.utimes"},
            {"data", JSON::Object::Entries {
              {"result", uv_fs_get_result(req)}
            }}
          };
        }
        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });
      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.utimes"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };
        ctx->callback(seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::futimes (
    const String& seq,
    ID id,
    double atime,
    double mtime,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      auto desc = getDescriptor(id);
      if (desc == nullptr) {
        auto json = JSON::Object::Entries {
          {"source", "fs.futimes"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "ENOTOPEN"},
            {"type", "NotFoundError"},
            {"message", "No file descriptor found with that id"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

    #if ORO_RUNTIME_PLATFORM_ANDROID
      if (desc->androidAsset != nullptr) {
        auto json = JSON::Object::Entries {
          {"source", "fs.futimes"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"code", "EPERM"},
            {"type", "NotAllowedError"},
            {"message", "Cannot utime an Android Asset file descriptor."}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }
    #endif

      auto ctx = new RequestContext(seq, callback);
      auto err = uv_fs_futime(this->loop.get(), &ctx->req, desc->fd, atime, mtime, [](uv_fs_t* req) {
        auto ctx = static_cast<RequestContext*>(req->data);
        auto json = JSON::Object{};
        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.futimes"},
            {"err", JSON::Object::Entries {
              {"code", uv_fs_get_result(req)},
              {"message", String(uv_strerror(uv_fs_get_result(req)))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.futimes"},
            {"data", JSON::Object::Entries {
              {"result", uv_fs_get_result(req)}
            }}
          };
        }
        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });
      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.futimes"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };
        ctx->callback(seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::symlink (
    const String& seq,
    const String& src,
    const String& dest,
    int flags,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      // Policy: deny creating symlinks when no-follow is enabled
      {
        const auto cfg = oro::runtime::config::getUserConfig();
        const bool noFollow = (
          cfg.contains("filesystem_no_follow_symlinks") &&
          (cfg.at("filesystem_no_follow_symlinks") == "1" || cfg.at("filesystem_no_follow_symlinks") == "true")
        );
        if (noFollow) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.symlink"},
            {"err", JSON::Object::Entries {
              {"type", "SecurityError"},
              {"code", "EPERM"},
              {"message", "Symlink creation is disabled by policy"}
            }}
          };
          return callback(seq, json, QueuedResponse{});
        }
      }
      // Sandbox check for source and destination paths
      {
        auto s = std::make_shared<Descriptor>(const_cast<FS*>(this), 0, src);
        auto d = std::make_shared<Descriptor>(const_cast<FS*>(this), 0, dest);
        if (!s->resource.startAccessing() || !d->resource.startAccessing()) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.symlink"},
            {"err", JSON::Object::Entries {
              {"type", "SecurityError"},
              {"code", "EPERM"},
              {"message", "Filesystem sandbox denied access"}
            }}
          };
          return callback(seq, json, QueuedResponse{});
        }
      }
      auto ctx = new RequestContext(seq, callback);
      auto err = uv_fs_symlink(this->loop.get(), &ctx->req, src.c_str(), dest.c_str(), flags, [](uv_fs_t* req) {
        auto ctx = static_cast<RequestContext*>(req->data);
        auto json = JSON::Object{};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.symlink"},
            {"err", JSON::Object::Entries {
              {"code", uv_fs_get_result(req)},
              {"message", String(uv_strerror(uv_fs_get_result(req)))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.symlink"},
            {"data", JSON::Object::Entries {
              {"mode", req->flags},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse {});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.symlink"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };
        ctx->callback(seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::lutimes (
    const String& seq,
    const String& path,
    double atime,
    double mtime,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      // Sandbox check
      {
        auto d = std::make_shared<Descriptor>(const_cast<FS*>(this), 0, path);
        if (!d->resource.startAccessing()) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.lutimes"},
            {"err", JSON::Object::Entries {
              {"type", "SecurityError"},
              {"code", "EPERM"},
              {"message", "Filesystem sandbox denied access"}
            }}
          };
          return callback(seq, json, QueuedResponse{});
        }
      }

      int r = -1;
      String errMsg = "Not supported";

    #if defined(_WIN32)
      auto toFileTime = [] (double secs) -> FILETIME {
        const double EPOCH_DIFF = 11644473600.0; // seconds between 1601 and 1970
        double total100ns = (secs + EPOCH_DIFF) * 10000000.0;
        ULONGLONG t = static_cast<ULONGLONG>(total100ns + 0.5);
        FILETIME ft;
        ft.dwLowDateTime = static_cast<DWORD>(t & 0xFFFFFFFF);
        ft.dwHighDateTime = static_cast<DWORD>(t >> 32);
        return ft;
      };

      // Convert UTF-8 path to UTF-16
      int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
      std::wstring wpath;
      wpath.resize(wlen > 0 ? (wlen - 1) : 0);
      if (wlen > 0) {
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wlen);
      }

      HANDLE h = CreateFileW(
        wpath.c_str(),
        FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
      );

      if (h == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        char* msg = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
          nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, nullptr);
        errMsg = msg ? String(msg) : String("CreateFileW failed");
        if (msg) LocalFree(msg);
      } else {
        FILETIME at = toFileTime(atime);
        FILETIME mt = toFileTime(mtime);
        if (!SetFileTime(h, nullptr, &at, &mt)) {
          DWORD code = GetLastError();
          char* msg = nullptr;
          FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, nullptr);
          errMsg = msg ? String(msg) : String("SetFileTime failed");
          if (msg) LocalFree(msg);
          r = -1;
        } else {
          r = 0;
        }
        CloseHandle(h);
      }
    #elif defined(__APPLE__) || defined(__MACH__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
      struct timeval tv[2];
      tv[0].tv_sec = static_cast<time_t>(atime);
      tv[0].tv_usec = static_cast<suseconds_t>((atime - static_cast<time_t>(atime)) * 1000000.0);
      tv[1].tv_sec = static_cast<time_t>(mtime);
      tv[1].tv_usec = static_cast<suseconds_t>((mtime - static_cast<time_t>(mtime)) * 1000000.0);
      r = ::lutimes(path.c_str(), tv);
      if (r < 0) { errMsg = String(strerror(errno)); }
    #elif defined(__linux__)
      struct timespec ts[2];
      ts[0].tv_sec = static_cast<time_t>(atime);
      ts[0].tv_nsec = static_cast<long>((atime - static_cast<time_t>(atime)) * 1000000000.0);
      ts[1].tv_sec = static_cast<time_t>(mtime);
      ts[1].tv_nsec = static_cast<long>((mtime - static_cast<time_t>(mtime)) * 1000000000.0);
      r = ::utimensat(AT_FDCWD, path.c_str(), ts, AT_SYMLINK_NOFOLLOW);
      if (r < 0) { errMsg = String(strerror(errno)); }
    #else
      r = -1;
      errMsg = "Not supported on this platform";
    #endif

      if (r < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.lutimes"},
          {"err", JSON::Object::Entries {
            {"code", -1},
            {"message", errMsg}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      auto json = JSON::Object::Entries {
        {"source", "fs.lutimes"},
        {"data", JSON::Object::Entries {{"result", 0}}}
      };
      return callback(seq, json, QueuedResponse{});
    });
  }

  void FS::unlink (
    const String& seq,
    const String& path,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      // Sandbox check
      {
        auto d = std::make_shared<Descriptor>(const_cast<FS*>(this), 0, path);
        if (!d->resource.startAccessing()) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.unlink"},
            {"err", JSON::Object::Entries {
              {"type", "SecurityError"},
              {"code", "EPERM"},
              {"message", "Filesystem sandbox denied access"}
            }}
          };
          return callback(seq, json, QueuedResponse{});
        }
      }
      auto filename = path.c_str();
      auto loop = this->loop.get();
      auto ctx = new RequestContext(seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_unlink(loop, req, filename, [](uv_fs_t* req) {
        auto ctx = (RequestContext *) req->data;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.unlink"},
            {"err", JSON::Object::Entries {
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.unlink"},
            {"data", JSON::Object::Entries {
              {"result", req->result},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.unlink"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

	void FS::readlink (
    const String& seq,
    const String& path,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      // Sandbox check
      {
        auto d = std::make_shared<Descriptor>(const_cast<FS*>(this), 0, path);
        if (!d->resource.startAccessing()) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.readlink"},
            {"err", JSON::Object::Entries {
              {"type", "SecurityError"},
              {"code", "EPERM"},
              {"message", "Filesystem sandbox denied access"}
            }}
          };
          return callback(seq, json, QueuedResponse{});
        }
      }
      auto ctx = new RequestContext(seq, callback);
      auto err = uv_fs_readlink(this->loop.get(), &ctx->req, path.c_str(), [](uv_fs_t* req) {
        auto ctx = static_cast<RequestContext*>(req->data);
        auto json = JSON::Object{};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.readlink"},
            {"err", JSON::Object::Entries {
              {"code", uv_fs_get_result(req)},
              {"message", String(uv_strerror(uv_fs_get_result(req)))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.readlink"},
            {"data", JSON::Object::Entries {
              {"path", String((char*) uv_fs_get_ptr(req))}
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse {});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.readlink"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };
        ctx->callback(seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::realpath (
    const String& seq,
    const String& path,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      auto filename = path.c_str(); auto desc = std::make_shared<Descriptor>(this, 0, filename);
      if (!desc->resource.startAccessing()) {
        const auto json = JSON::Object::Entries {
          {"source", "fs.realpath"},
          {"err", JSON::Object::Entries {
            {"type", "SecurityError"},
            {"code", "EPERM"},
            {"message", "Filesystem sandbox denied access"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }
      auto ctx = new RequestContext(desc, seq, callback);
      auto err = uv_fs_realpath(this->loop.get(), &ctx->req, path.c_str(), [](uv_fs_t* req) {
        auto ctx = static_cast<RequestContext*>(req->data);
        auto json = JSON::Object{};

        if (uv_fs_get_result(req) < 0) {
          bool handled = false;

        #if ORO_RUNTIME_PLATFORM_ANDROID
          if (ctx->descriptor->resource.isAndroidLocalAsset()) {
            json = JSON::Object::Entries {
              {"source", "fs.realpath"},
              {"data", JSON::Object::Entries {
                {"path", ctx->descriptor->resource.path}
              }}
            };
            handled = true;
          } else if (ctx->descriptor->resource.isAndroidContent()) {
            json = JSON::Object::Entries {
              {"source", "fs.realpath"},
              {"data", JSON::Object::Entries {
                {"path", ctx->descriptor->resource.url.str()}
              }}
            };
            handled = true;
          }
        #endif

          if (!handled) {
            json = JSON::Object::Entries {
              {"source", "fs.realpath"},
              {"err", JSON::Object::Entries {
                {"code", uv_fs_get_result(req)},
                {"message", String(uv_strerror(uv_fs_get_result(req)))}
              }}
            };
          }
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.realpath"},
            {"data", JSON::Object::Entries {
              {"path", String((char*) uv_fs_get_ptr(req))}
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse {});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.realpath"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };
        ctx->callback(seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::rename (
    const String& seq,
    const String& pathA,
    const String& pathB,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      // Sandbox check for both paths
      {
        auto sa = std::make_shared<Descriptor>(const_cast<FS*>(this), 0, pathA);
        auto sb = std::make_shared<Descriptor>(const_cast<FS*>(this), 0, pathB);
        if (!sa->resource.startAccessing() || !sb->resource.startAccessing()) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.rename"},
            {"err", JSON::Object::Entries {
              {"type", "SecurityError"},
              {"code", "EPERM"},
              {"message", "Filesystem sandbox denied access"}
            }}
          };
          return callback(seq, json, QueuedResponse{});
        }
      }
      auto loop = this->loop.get();
      auto ctx = new RequestContext(seq, callback);
      auto req = &ctx->req;
      auto src = pathA.c_str();
      auto dst = pathB.c_str();
      auto err = uv_fs_rename(loop, req, src, dst, [](uv_fs_t* req) {
        auto ctx = (RequestContext *) req->data;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.rename"},
            {"err", JSON::Object::Entries {
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.rename"},
            {"data", JSON::Object::Entries {
              {"result", req->result},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.rename"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::copyFile (
    const String& seq,
    const String& pathA,
    const String& pathB,
    int flags,
    const Callback callback
  ) {
    this->loop.dispatch([=, this]() {
      auto src = std::make_shared<Descriptor>(this, 0, pathA);
      auto dst = std::make_shared<Descriptor>(this, 0, pathB);

      if (!src->resource.startAccessing() || !dst->resource.startAccessing()) {
        const auto json = JSON::Object::Entries {
          {"source", "fs.copyFile"},
          {"err", JSON::Object::Entries {
            {"type", "SecurityError"},
            {"code", "EPERM"},
            {"message", "Filesystem sandbox denied access"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

    #if ORO_RUNTIME_PLATFORM_ANDROID
      if (src->resource.isAndroidLocalAsset() || src->resource.isAndroidContent()) {
        const auto bytes = src->resource.read();
        const auto size = src->resource.size();
        if (!bytes || size == 0) {
          auto json = JSON::Object::Entries {
            {"source", "fs.copyFile"},
            {"err", JSON::Object::Entries {
              {"code", "ENOENT"},
              {"message", "Android asset/content empty or not found"}
            }}
          };
          return callback(seq, json, QueuedResponse{});
        }
        fs::remove(dst->resource.path.string());
        std::ofstream stream(dst->resource.path.string(), std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(size));
        stream.close();

        auto json = JSON::Object::Entries {
          {"source", "fs.copyFile"},
          {"data", JSON::Object::Entries {
            {"result", 0}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }
    #endif

      auto loop = this->loop.get();
      auto ctx = new RequestContext(seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_copyfile(loop, req, src->resource.path.string().c_str(), dst->resource.path.string().c_str(), flags, [](uv_fs_t* req) {
        auto ctx = (RequestContext *) req->data;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.copyFile"},
            {"err", JSON::Object::Entries {
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.copyFile"},
            {"data", JSON::Object::Entries {
              {"result", req->result},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.copyFile"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::rmdir (
    const String& seq,
    const String& path,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      // Sandbox check
      {
        auto d = std::make_shared<Descriptor>(const_cast<FS*>(this), 0, path);
        if (!d->resource.startAccessing()) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.rmdir"},
            {"err", JSON::Object::Entries {
              {"type", "SecurityError"},
              {"code", "EPERM"},
              {"message", "Filesystem sandbox denied access"}
            }}
          };
          return callback(seq, json, QueuedResponse{});
        }
      }
      auto filename = path.c_str();
      auto loop = this->loop.get();
      auto ctx = new RequestContext(seq, callback);
      auto req = &ctx->req;
      auto err = uv_fs_rmdir(loop, req, filename, [](uv_fs_t* req) {
        auto ctx = (RequestContext *) req->data;
        auto json = JSON::Object {};

        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.rmdir"},
            {"err", JSON::Object::Entries {
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.rmdir"},
            {"data", JSON::Object::Entries {
              {"result", req->result},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.rmdir"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::mkdir (
    const String& seq,
    const String& path,
    int mode,
    bool recursive,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      // Sandbox check
      {
        auto d = std::make_shared<Descriptor>(const_cast<FS*>(this), 0, path);
        if (!d->resource.startAccessing()) {
          const auto json = JSON::Object::Entries {
            {"source", "fs.mkdir"},
            {"err", JSON::Object::Entries {
              {"type", "SecurityError"},
              {"code", "EPERM"},
              {"message", "Filesystem sandbox denied access"}
            }}
          };
          return callback(seq, json, QueuedResponse{});
        }
      }
      int err = 0;
      auto filename = path.c_str();
      auto loop = this->loop.get();
      auto ctx = new RequestContext(seq, callback);
      ctx->recursive = recursive;
      auto req = &ctx->req;
      const auto callback = [](uv_fs_t* req) {
        auto ctx = (RequestContext *) req->data;
        auto json = JSON::Object {};

        int result = uv_fs_get_result(req);

        // Report recursive errors only when they are not EEXIST
        if (result < 0 && (!ctx->recursive || (result != -EEXIST && result != -4075))) {
          json = JSON::Object::Entries {
            {"source", "fs.mkdir"},
            {"err", JSON::Object::Entries {
              {"code", req->result},
              {"message", String(uv_strerror((int) req->result))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.mkdir"},
            {"data", JSON::Object::Entries {
              {"result", req->result},
            }}
          };
        }

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      };

      if (!recursive) {
        err = uv_fs_mkdir(loop, req, filename, mode, callback);
      } else {
        const auto sep = String(1, std::filesystem::path::preferred_separator);
        const auto components = split(path, sep);
        auto queue = std::queue(std::deque(components.begin(), components.end()));
        auto currentComponents = Vector<String>();
        while (queue.size() > 0) {
          uv_fs_t req;
          const auto currentComponent = queue.front();
          queue.pop();
          currentComponents.push_back(currentComponent);
          const auto joinedComponents = join(currentComponents, sep);
          const auto currentPath = joinedComponents.empty() ? sep : joinedComponents;
          if (queue.size() == 0) {
            err = uv_fs_mkdir(loop, &ctx->req, currentPath.c_str(), mode, callback);
          } else {
            err = uv_fs_mkdir(loop, &req, currentPath.c_str(), mode, nullptr);
          }

          if (err == 0 || err == -EEXIST || err == -4075) { // '4075' is EEXIST on Windows
            err = 0;
            continue;
          } else {
            break;
          }
        }
      }

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.mkdir"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };

        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::mkdtemp (
    const String& seq,
    const String& prefix,
    const Callback callback
  ) const {
    this->loop.dispatch([=, this]() {
      auto ctx = new RequestContext(seq, callback);
      auto req = &ctx->req;
      String pattern = prefix + "XXXXXX";
      auto err = uv_fs_mkdtemp(this->loop.get(), req, pattern.c_str(), [](uv_fs_t* req) {
        auto ctx = static_cast<RequestContext*>(req->data);
        auto json = JSON::Object{};
        if (uv_fs_get_result(req) < 0) {
          json = JSON::Object::Entries {
            {"source", "fs.mkdtemp"},
            {"err", JSON::Object::Entries {
              {"code", uv_fs_get_result(req)},
              {"message", String(uv_strerror(uv_fs_get_result(req)))}
            }}
          };
        } else {
          json = JSON::Object::Entries {
            {"source", "fs.mkdtemp"},
            {"data", JSON::Object::Entries {
              {"path", String(req->path ? req->path : "")}
            }}
          };
        }
        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete ctx;
      });

      if (err < 0) {
        auto json = JSON::Object::Entries {
          {"source", "fs.mkdtemp"},
          {"err", JSON::Object::Entries {
            {"code", err},
            {"message", String(uv_strerror(err))}
          }}
        };
        ctx->callback(seq, json, QueuedResponse{});
        delete ctx;
      }
    });
  }

  void FS::constants (
    const String& seq,
    const Callback callback
  ) const {
    static const auto data = JSON::Object(filesystem::constants());
    static const auto json = JSON::Object::Entries {
      {"source", "fs.constants"},
      {"data", data}
    };

    callback(seq, json, QueuedResponse {});
  }
}
