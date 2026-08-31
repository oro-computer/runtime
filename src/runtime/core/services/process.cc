#include "../../process.hh"
#include "../../string.hh"
#include "../../http.hh"
#include "../../url.hh"

#include "process.hh"

using namespace oro::runtime::string;
using oro::runtime::http::Headers;
using oro::runtime::url::encodeURIComponent;
using oro::runtime::crypto::rand64;

namespace oro::runtime::core::services {
  namespace {
    Mutex processStoppingStatesMutex;
    Map<const Process*, SharedPointer<Atomic<bool>>> processStoppingStates;

    SharedPointer<Atomic<bool>> processStoppingStateFor (const Process* process) {
      Lock lock(processStoppingStatesMutex);
      auto iterator = processStoppingStates.find(process);
      if (iterator != processStoppingStates.end()) {
        return iterator->second;
      }

      auto state = std::make_shared<Atomic<bool>>(true);
      processStoppingStates.emplace(process, state);
      return state;
    }

    void setProcessStopping (const Process* process, bool stopping) {
      processStoppingStateFor(process)->store(stopping, std::memory_order_release);
    }

    String encodeProcessArguments (const Vector<String>& args, size_t offset) {
      String encoded;
      for (size_t index = offset; index < args.size(); ++index) {
        if (index > offset) {
          encoded.push_back(static_cast<char>(0x01));
        }
        encoded += args[index];
      }
      return encoded;
    }
  }

  bool Process::start () {
    setProcessStopping(this, false);
    return core::Service::start();
  }

  bool Process::stop () {
    setProcessStopping(this, true);
  #if !ORO_RUNTIME_PLATFORM_IOS
    // Terminate any outstanding processes and wait for them
    this->shutdown();
  #endif
    // Cancel any outstanding timers used for exec timeouts
    this->timers.stop();
    return true;
  }

  void Process::shutdown () {
  #if !ORO_RUNTIME_PLATFORM_IOS
    Lock lock(this->mutex);
    for (const auto& entry : this->handles) {
      auto process = entry.second;
      process->kill();
      process->wait();
    }
  #endif

    this->handles.clear();
  }

  void Process::kill (
    const String& seq,
    ID id,
    int signal,
    const Callback callback
  ) {
  #if ORO_RUNTIME_PLATFORM_IOS
    const auto json = JSON::Object::Entries {
      {"err", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"type", "NotSupportedError"},
        {"message", "kill() is not supported"}
      }}
    };
    return callback(seq, json, QueuedResponse{});
  #else
    this->loop.dispatch([=, this] {
      Lock lock(this->mutex);

      if (!this->handles.contains(id)) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"type", "NotFoundError"},
            {"message", "A process with that id does not exist"}
          }}
        };

        return this->loop.dispatch([=, this] () {
          callback(seq, json, QueuedResponse{});
        });
      }

      auto process = this->handles.at(id);

      #if ORO_RUNTIME_PLATFORM_WINDOWS
        process->kill();
      #else
        ::kill(-process->id, signal);
      #endif

      return this->loop.dispatch([=, this] () {
        callback(seq, JSON::Object{}, QueuedResponse{});
      });
    });
  #endif
  }

  void Process::exec (
    const String& seq,
    ID id,
    const Vector<String> args,
    const ExecOptions options,
    const Callback callback
  ) {
  #if ORO_RUNTIME_PLATFORM_IOS
    const auto json = JSON::Object::Entries {
      {"err", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"type", "NotSupportedError"},
        {"message", "exec() is not supported"}
      }}
    };
    return callback(seq, json, QueuedResponse{});
  #else
    auto stopping = processStoppingStateFor(this);
    this->loop.dispatch([=, this] {
      Lock lock(this->mutex);

      if (this->handles.contains(id)) {
        auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"message", "A process with that id already exists"}
          }}
        };

        this->loop.dispatch([=, this] () {
          callback(seq, json, QueuedResponse{});
        });
        return;
      }

      SharedPointer<runtime::Process> process = nullptr;
      Timers::ID timer = 0;

      const auto command = args.size() > 0 ? args.at(0) : String("");
      const auto argv = encodeProcessArguments(args, 1);

      auto stdoutBuffer = std::make_shared<StringStream>();
      auto stderrBuffer = std::make_shared<StringStream>();
      auto outputMutex = std::make_shared<Mutex>();
      auto completed = std::make_shared<Atomic<bool>>(false);
      auto timeoutId = std::make_shared<Atomic<Timers::ID>>(0);

      const auto onStdout = [=](const String& output) mutable {
        if (!options.allowStdout || output.size() == 0) {
          return;
        }

        if (stdoutBuffer != nullptr) {
          Lock lock(*outputMutex);
          *stdoutBuffer << String(output);
        }
      };

      const auto onStderr = [=](const String& output) mutable {
        if (!options.allowStderr || output.size() == 0) {
          return;
        }

        if (stderrBuffer != nullptr) {
          Lock lock(*outputMutex);
          *stderrBuffer << String(output);
        }
      };

      const auto onExit = [=, this](const String& output) mutable {
        if (completed->exchange(true, std::memory_order_acq_rel)) {
          return;
        }

        const auto idToClear = timeoutId->exchange(0, std::memory_order_acq_rel);
        if (idToClear > 0) {
          this->timers.clearTimeout(idToClear);
        }

        this->loop.dispatch([=, this] () mutable {
          if (stopping->load(std::memory_order_acquire)) {
            return;
          }

          SharedPointer<runtime::Process> process = nullptr;
          String stdoutText;
          String stderrText;
          int pid = 0;
          int code = 0;

          Lock lock(this->mutex);
          if (!this->handles.contains(id)) {
            return;
          }

          process = this->handles.at(id);
          if (process != nullptr) {
            pid = process->id;
            code = process->wait();
          }

          {
            Lock outputLock(*outputMutex);
            stdoutText = stdoutBuffer != nullptr ? stdoutBuffer->str() : "";
            stderrText = stderrBuffer != nullptr ? stderrBuffer->str() : "";
          }

          this->handles.erase(id);

          const auto json = JSON::Object::Entries {
            {"source", "child_process.exec"},
            {"data", JSON::Object::Entries {
              {"id", std::to_string(id)},
              {"pid", std::to_string(pid)},
              {"stdout", encodeURIComponent(stdoutText)},
              {"stderr", encodeURIComponent(stderrText)},
              {"code", code}
            }}
          };

          this->loop.dispatch([=, this] () {
            if (!stopping->load(std::memory_order_acquire)) {
              callback(seq, json, QueuedResponse{});
            }
          });
        });
      };

      runtime::process::ProcessConfig processConfig;
      processConfig.rawOutput = true;
      processConfig.replaceEnvironment = options.replaceEnvironment;
      process.reset(new runtime::Process(
        command,
        argv,
        options.env,
        options.cwd,
        onStdout,
        onStderr,
        onExit,
        false,
        processConfig
      ));

      #if ORO_RUNTIME_PLATFORM_WINDOWS
        process->shell = "cmd.exe";
      #endif

      this->handles.insert_or_assign(id, process);

      const auto pid = process->open();
      if (static_cast<int64_t>(pid) <= 0) {
        completed->store(true, std::memory_order_release);
        this->handles.erase(id);
        const auto json = JSON::Object::Entries {
          {"source", "child_process.exec"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"pid", std::to_string(pid)},
            {"code", "ESPAWN"},
            {"message", "Unable to start child process"}
          }}
        };
        this->loop.dispatch([=, this] () {
          if (!stopping->load(std::memory_order_acquire)) {
            callback(seq, json, QueuedResponse{});
          }
        });
        return;
      }

      if (options.timeout > 0) {
        timer = this->timers.setTimeout(options.timeout, [=, this] () mutable {
          if (completed->exchange(true, std::memory_order_acq_rel)) {
            return;
          }

          timeoutId->store(0, std::memory_order_release);

          SharedPointer<runtime::Process> processToKill = nullptr;
          String stdout;
          String stderr;

          {
            Lock lock(this->mutex);
            if (this->handles.contains(id)) {
              processToKill = this->handles.at(id);
              this->handles.erase(id);
            }
          }

          {
            Lock outputLock(*outputMutex);
            stdout = stdoutBuffer != nullptr ? stdoutBuffer->str() : "";
            stderr = stderrBuffer != nullptr ? stderrBuffer->str() : "";
          }

          const auto json = JSON::Object::Entries {
            {"source", "child_process.exec"},
            {"err", JSON::Object::Entries {
              {"id", std::to_string(id)},
              {"pid", std::to_string(pid)},
              {"stdout", encodeURIComponent(stdout)},
              {"stderr", encodeURIComponent(stderr)},
              {"code", "ETIMEDOUT"}
            }}
          };

          this->loop.dispatch([=, this] {
            if (!stopping->load(std::memory_order_acquire)) {
              callback(seq, json, QueuedResponse{});
            }
          });

          if (processToKill == nullptr) {
            return;
          }

        #if ORO_RUNTIME_PLATFORM_WINDOWS
          processToKill->kill();
        #else
          const auto killSignal = options.killSignal != 0 ? options.killSignal : SIGTERM;
          ::kill(-processToKill->id, killSignal);
        #endif

          processToKill->wait();
        });
        timeoutId->store(timer, std::memory_order_release);
      }
    });
  #endif
  }

  void Process::spawn (
    const String& seq,
    ID id,
    const Vector<String> args,
    const SpawnOptions options,
    const Callback callback
  ) {
  #if ORO_RUNTIME_PLATFORM_IOS
    const auto json = JSON::Object::Entries {
      {"err", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"type", "NotSupportedError"},
        {"message", "spawn() is not supported"}
      }}
    };
    return callback(seq, json, QueuedResponse{});
  #else
    auto stopping = processStoppingStateFor(this);
    this->loop.dispatch([=, this] {
      Lock lock(this->mutex);

      if (this->handles.contains(id)) {
        auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"message", "A process with that id already exists"}
          }}
        };

        return this->loop.dispatch([=, this] () {
          callback(seq, json, QueuedResponse{});
        });
      }

      SharedPointer<runtime::Process> process = nullptr;
      auto outputCount = std::make_shared<Atomic<uint64_t>>(0);

      const auto command = args.size() > 0 ? args.at(0) : String("");
      const auto argv = encodeProcessArguments(args, 1);

      const auto onStdout = [=, this](const String& output) {
        if (
          stopping->load(std::memory_order_acquire) ||
          !options.allowStdout ||
          output.size() == 0
        ) {
          return;
        }

        const auto bytes = new unsigned char[output.size()]{0};
        const auto headers = Headers {{
          {"content-type", "application/octet-stream"},
          {"content-length", (int) output.size()}
        }};

        memcpy(bytes, output.c_str(), output.size());
        outputCount->fetch_add(1, std::memory_order_acq_rel);

        QueuedResponse post;
        post.id = rand64();
        post.body.reset(bytes);
        post.length = (int) output.size();
        post.headers = headers.str();

        const auto json = JSON::Object::Entries {
          {"source", "child_process.spawn"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"source", "stdout"}
          }}
        };

        callback("-1", json, post);
      };

      const auto onStderr = [=, this](const String& output) {
        if (
          stopping->load(std::memory_order_acquire) ||
          !options.allowStderr ||
          output.size() == 0
        ) {
          return;
        }

        const auto bytes = new unsigned char[output.size()]{0};
        const auto headers = Headers {{
          {"content-type", "application/octet-stream"},
          {"content-length", (int) output.size()}
        }};

        memcpy(bytes, output.c_str(), output.size());
        outputCount->fetch_add(1, std::memory_order_acq_rel);

        QueuedResponse post;
        post.id = rand64();
        post.body.reset(bytes);
        post.length = (int) output.size();
        post.headers = headers.str();

        const auto json = JSON::Object::Entries {
          {"source", "child_process.spawn"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"source", "stderr"}
          }}
        };

        callback("-1", json, post);
      };

      const auto onExit = [=, this](const String& output) {
        if (stopping->load(std::memory_order_acquire)) {
          return;
        }

        const auto code = output.size() > 0 ? std::stoi(output) : 0;
        const auto json = JSON::Object::Entries {
          {"source", "child_process.spawn"},
          {"data", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"status", "exit"},
            {"code", code}
          }}
        };

        callback("-1", json, QueuedResponse{});

        this->loop.dispatch([=, this] {
          if (stopping->load(std::memory_order_acquire)) {
            return;
          }

          SharedPointer<runtime::Process> process = nullptr;
          do {
            Lock lock(this->mutex);
            if (!this->handles.contains(id)) {
              return;
            }

            process = this->handles.at(id);
          } while (0);

          const auto code = process->wait();

          this->loop.dispatch([=, this] {
            if (stopping->load(std::memory_order_acquire)) {
              return;
            }

            const auto json = JSON::Object::Entries {
              {"source", "child_process.spawn"},
              {"data", JSON::Object::Entries {
                {"id", std::to_string(id)},
                {"status", "close"},
                {"code", code},
                {"outputCount", outputCount->load(std::memory_order_acquire)}
              }}
            };

            callback("-1", json, QueuedResponse{});

            Lock lock(this->mutex);
            this->handles.erase(id);
          });
        });
      };

      runtime::process::ProcessConfig processConfig;
      processConfig.useDirectArguments = true;
      processConfig.argumentCount = args.size() > 0 ? args.size() - 1 : 0;
      processConfig.rawOutput = true;
      processConfig.replaceEnvironment = options.replaceEnvironment;
      process.reset(new runtime::Process(
        command,
        argv,
        options.env,
        options.cwd,
        onStdout,
        onStderr,
        onExit,
        options.allowStdin,
        processConfig
      ));

      this->handles.insert_or_assign(id, process);

      const auto pid = process->open();
      if (static_cast<int64_t>(pid) <= 0) {
        this->handles.erase(id);
        const auto json = JSON::Object::Entries {
          {"source", "child_process.spawn"},
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"pid", std::to_string(pid)},
            {"code", "ESPAWN"},
            {"message", "Unable to start child process"}
          }}
        };
        return this->loop.dispatch([=, this] () {
          if (!stopping->load(std::memory_order_acquire)) {
            callback(seq, json, QueuedResponse{});
          }
        });
      }
      const auto json = JSON::Object::Entries {
        {"source", "child_process.spawn"},
        {"data", JSON::Object::Entries {
          {"id", std::to_string(id)},
          {"pid", std::to_string(pid)}
        }}
      };

      return this->loop.dispatch([=, this] () {
        if (!stopping->load(std::memory_order_acquire)) {
          callback(seq, json, QueuedResponse{});
        }
      });
    });
  #endif
  }

  void Process::write (
    const String& seq,
    ID id,
    SharedPointer<unsigned char[]> buffer,
    size_t size,
    const Callback callback
  ) {
  #if ORO_RUNTIME_PLATFORM_IOS
    const auto json = JSON::Object::Entries {
      {"err", JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"type", "NotSupportedError"},
        {"message", "write() is not supported"}
      }}
    };
    return callback(seq, json, QueuedResponse{});
  #else
    this->loop.dispatch([=, this] {
      Lock lock(this->mutex);

      if (!this->handles.contains(id)) {
        auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"type", "NotFoundError"},
            {"message", "A process with that id does not exist"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      bool didWrite = false;

      auto process = this->handles.at(id);

      if (!process->openStdin) {
        auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"type", "NotSupportedError"},
            {"message", "Child process stdin is not opened"}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      try {
        didWrite = process->write(buffer, size);
      } catch (std::exception& e) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"type", "InternalError"},
            {"message", e.what()}
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      if (!didWrite) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"id", std::to_string(id)},
            {"type", process->lastWriteStatus != 0
              ? "ErrnoError"
              : "InternalError"
            },
            {"message", process->lastWriteStatus != 0
              ? strerror(process->lastWriteStatus)
              : "Failed to write to child process"
            }
          }}
        };

        callback(seq, json, QueuedResponse{});
        return;
      }

      callback(seq, JSON::Object{}, QueuedResponse{});
      return;
    });
  #endif
  }
}
