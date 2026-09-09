#include <algorithm>
#include <bitset>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#if !defined(__ANDROID__)
#include <spawn.h>
#endif
#include <set>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#include <unistd.h>

#include "../debug.hh"
#include "../string.hh"
#include "../process.hh"

using oro::runtime::string::splitc;

extern char **environ;

namespace oro::runtime::process {
  static StringStream initial;

  static void clearProcessEnvironment () noexcept {
    #if defined(__APPLE__)
      while (environ != nullptr && environ[0] != nullptr) {
        const char* entry = environ[0];
        const char* separator = strchr(entry, '=');
        if (separator == nullptr || separator == entry) {
          _exit(EXIT_FAILURE);
        }

        const String name(entry, static_cast<size_t>(separator - entry));
        if (unsetenv(name.c_str()) != 0) {
          _exit(EXIT_FAILURE);
        }
      }
    #else
      if (clearenv() != 0) {
        _exit(EXIT_FAILURE);
      }
    #endif
  }

  Process::Data::Data () noexcept
    : id(-1)
  {}

  Process::Process (
    const String &command,
    const String &argv,
    const Vector<String> &env,
    const String &path,
    MessageCallback readStdout,
    MessageCallback readStderr,
    MessageCallback onExit,
    bool openStdin,
    const ProcessConfig &config
  ) noexcept
    : openStdin(openStdin),
      readStdout(std::move(readStdout)),
      readStderr(std::move(readStderr)),
      onExit(std::move(onExit)),
      env(env),
      command(command),
      argv(argv),
      path(path),
      config(config)
  {}

  Process::Process (
    const String &command,
    const String &argv,
    const String &path,
    MessageCallback readStdout,
    MessageCallback readStderr,
    MessageCallback onExit,
    bool openStdin,
    const ProcessConfig &config
  ) noexcept
    : openStdin(openStdin),
      readStdout(std::move(readStdout)),
      readStderr(std::move(readStderr)),
      onExit(std::move(onExit)),
      command(command),
      argv(argv),
      path(path),
      config(config)
  {}

  Process::Process (
    const Function<int()> &function,
    MessageCallback readStdout,
    MessageCallback readStderr,
    MessageCallback onExit,
    bool openStdin,
    const ProcessConfig &config
  ) noexcept
    : readStdout(std::move(readStdout)),
      readStderr(std::move(readStderr)),
      onExit(std::move(onExit)),
      openStdin(openStdin),
      config(config) {
    #if !ORO_RUNTIME_PLATFORM_IOS
      open(function);
      read();
    #endif
  }

  Process::PID Process::open (const Function<int()> &function) noexcept {
    #if ORO_RUNTIME_PLATFORM_IOS
      return -1; // -EPERM
    #else

    if (openStdin) {
      stdinFD = UniquePointer<FD>(new FD);
    }

    if (readStdout) {
      stdoutFD = UniquePointer<FD>(new FD);
    }

    if (readStderr) {
      stderrFD = UniquePointer<FD>(new FD);
    }

    int stdin_p[2];
    int stdout_p[2];
    int stderr_p[2];

    if (stdinFD && pipe(stdin_p) != 0) {
      return -1;
    }

    if (stdoutFD && pipe(stdout_p) != 0) {
      if (stdinFD) {
        close(stdin_p[0]);
        close(stdin_p[1]);
      }
      return -1;
    }

    if (stderrFD && pipe(stderr_p) != 0) {
      if (stdinFD) {
        close(stdin_p[0]);
        close(stdin_p[1]);
      }

      if (stdoutFD) {
        close(stdout_p[0]);
        close(stdout_p[1]);
      }

      return -1;
    }

    PID pid = fork();

    if (pid < 0) {
      if (stdinFD) {
        close(stdin_p[0]);
        close(stdin_p[1]);
      }

      if (stdoutFD) {
        close(stdout_p[0]);
        close(stdout_p[1]);
      }

      if (stderrFD) {
        close(stderr_p[0]);
        close(stderr_p[1]);
      }

      return pid;
    }

    closed = false;
    id = pid;

    if (pid > 0) {
    #if ORO_RUNTIME_PLATFORM_APPLE
      // On macOS, keep the child in a controllable process group
      setpgid(pid, getpgid(0));
    #endif

      auto thread = Thread([this] {
        int code = 0;
        PID waited = -1;
        do {
          waited = waitpid(this->id, &code, 0);
        } while (waited < 0 && errno == EINTR);

        if (waited < 0) {
          this->status = -1;
        } else if (WIFEXITED(code)) {
          this->status = WEXITSTATUS(code);
        } else if (WIFSIGNALED(code)) {
          this->status = 128 + WTERMSIG(code);
        } else {
          this->status = -1;
        }

        this->closeFDs();
        this->closed = true;

        if (this->onExit != nullptr) {
          try {
            this->onExit(std::to_string(status));
          } catch (const std::exception& exception) {
            std::cerr << "Process exit callback exception: " << exception.what() << std::endl;
          }
        }
      });

      thread.detach();
    } else if (pid == 0) {
      if (stdinFD) {
        dup2(stdin_p[0], 0);
      }

      if (stdoutFD) {
        dup2(stdout_p[1], 1);
      }

      if (stderrFD) {
        dup2(stderr_p[1], 2);
      }

      if (stdinFD) {
        close(stdin_p[0]);
        close(stdin_p[1]);
      }

      if (stdoutFD) {
        close(stdout_p[0]);
        close(stdout_p[1]);
      }

      if (stderrFD) {
        close(stderr_p[0]);
        close(stderr_p[1]);
      }

    #if !ORO_RUNTIME_PLATFORM_IOS
      // Put the child in its own process group so the CLI can signal the group (-pid)
      // This enables robust termination of the subtree via Process::kill()
      setpgid(0, 0);
      #if defined(__linux__)
        // If the parent (CLI) dies unexpectedly, deliver SIGTERM to this child
        prctl(PR_SET_PDEATHSIG, SIGTERM);
      #endif
    #endif

      if (function) {
        function();
      }

      _exit(EXIT_FAILURE);
    }

    if (stdinFD) {
      close(stdin_p[0]);
    }

    if (stdoutFD) {
      close(stdout_p[1]);
    }

    if (stderrFD) {
      close(stderr_p[1]);
    }

    if (stdinFD) {
      *stdinFD = stdin_p[1];
    }

    if (stdoutFD) {
      *stdoutFD = stdout_p[0];
    }

    if (stderrFD) {
      *stderrFD = stderr_p[0];
    }

    data.id = pid;
    return pid;
  #endif
  }

  Process::PID Process::open (const String &command, const String &path) noexcept {
    #if ORO_RUNTIME_PLATFORM_IOS
      return -1; // -EPERM
    #elif defined(__ANDROID__)
      return open([&command, &path, this] () -> int {
        if (this->config.useDirectArguments) {
          if (!path.empty() && chdir(path.c_str()) != 0) {
            _exit(EXIT_FAILURE);
          }

          Vector<String> arguments {command};
          if (this->config.argumentCount > 0) {
            auto encoded = splitc(this->argv, static_cast<char>(0x01));
            if (encoded.size() != this->config.argumentCount) {
              _exit(EXIT_FAILURE);
            }
            arguments.insert(arguments.end(), encoded.begin(), encoded.end());
          }

          Vector<char*> argumentPointers;
          argumentPointers.reserve(arguments.size() + 1);
          for (auto& argument : arguments) {
            argumentPointers.push_back(argument.data());
          }
          argumentPointers.push_back(nullptr);

          if (this->config.replaceEnvironment) {
            clearProcessEnvironment();
          }
          for (const auto& kv : this->env) {
            putenv(const_cast<char*>(kv.c_str()));
          }

          setpgid(0, 0);
          prctl(PR_SET_PDEATHSIG, SIGTERM);
          execvp(command.c_str(), argumentPointers.data());
          _exit(EXIT_FAILURE);
        }

        String cmdline = command;
        if (!this->argv.empty()) {
          cmdline += this->argv;
        }

        String cdAndCommand = cmdline;
        if (!path.empty()) {
          auto escapedPath = path;
          size_t position = 0;
          while ((position = escapedPath.find('\'', position)) != String::npos) {
            escapedPath.replace(position, 1, "'\\''");
            position += 4;
          }
          cdAndCommand = String("cd '") + escapedPath + String("' && ") + cmdline;
        }

        if (this->config.replaceEnvironment) {
          clearProcessEnvironment();
        }
        for (const auto& kv : this->env) {
          putenv(const_cast<char*>(kv.c_str()));
        }

        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        execl("/bin/sh", "/bin/sh", "-c", cdAndCommand.c_str(), (char*)nullptr);
        _exit(EXIT_FAILURE);
        return 0;
      });
    #else
      int stdinPipe[2] = {-1, -1};
      int stdoutPipe[2] = {-1, -1};
      int stderrPipe[2] = {-1, -1};

      const auto closePipe = [](int descriptors[2]) {
        for (size_t i = 0; i < 2; ++i) {
          if (descriptors[i] >= 0) {
            close(descriptors[i]);
            descriptors[i] = -1;
          }
        }
      };

      const auto fail = [&](int error) -> PID {
        closePipe(stdinPipe);
        closePipe(stdoutPipe);
        closePipe(stderrPipe);
        stdinFD.reset();
        stdoutFD.reset();
        stderrFD.reset();
        this->lastWriteStatus = error;
        errno = error;
        return -1;
      };

      if (openStdin) {
        stdinFD = UniquePointer<FD>(new FD);
        if (pipe(stdinPipe) != 0) {
          return fail(errno);
        }
      }

      if (readStdout) {
        stdoutFD = UniquePointer<FD>(new FD);
        if (pipe(stdoutPipe) != 0) {
          return fail(errno);
        }
      }

      if (readStderr) {
        stderrFD = UniquePointer<FD>(new FD);
        if (pipe(stderrPipe) != 0) {
          return fail(errno);
        }
      }

      posix_spawn_file_actions_t fileActions;
      auto error = posix_spawn_file_actions_init(&fileActions);
      if (error != 0) {
        return fail(error);
      }

      const auto addPipeActions = [&] (
        int descriptors[2],
        size_t childIndex,
        int standardDescriptor
      ) {
        const auto childDescriptor = descriptors[childIndex];
        if (childDescriptor != standardDescriptor) {
          auto status = posix_spawn_file_actions_adddup2(
            &fileActions,
            childDescriptor,
            standardDescriptor
          );
          if (status != 0) {
            return status;
          }

          status = posix_spawn_file_actions_addclose(&fileActions, childDescriptor);
          if (status != 0) {
            return status;
          }
        }

        const auto parentDescriptor = descriptors[childIndex == 0 ? 1 : 0];
        if (parentDescriptor != standardDescriptor) {
          return posix_spawn_file_actions_addclose(&fileActions, parentDescriptor);
        }
        return 0;
      };

      if (stdinFD) {
        error = addPipeActions(stdinPipe, 0, STDIN_FILENO);
      }
      if (error == 0 && stdoutFD) {
        error = addPipeActions(stdoutPipe, 1, STDOUT_FILENO);
      }
      if (error == 0 && stderrFD) {
        error = addPipeActions(stderrPipe, 1, STDERR_FILENO);
      }
      if (error == 0 && !path.empty()) {
        error = posix_spawn_file_actions_addchdir_np(&fileActions, path.c_str());
      }
      if (error != 0) {
        posix_spawn_file_actions_destroy(&fileActions);
        return fail(error);
      }

      posix_spawnattr_t attributes;
      error = posix_spawnattr_init(&attributes);
      if (error != 0) {
        posix_spawn_file_actions_destroy(&fileActions);
        return fail(error);
      }

      error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
      if (error == 0) {
        error = posix_spawnattr_setpgroup(&attributes, 0);
      }
      if (error != 0) {
        posix_spawnattr_destroy(&attributes);
        posix_spawn_file_actions_destroy(&fileActions);
        return fail(error);
      }

      Vector<String> arguments;
      String executable;
      if (this->config.useDirectArguments) {
        executable = command;
        arguments.push_back(command);
        if (this->config.argumentCount > 0) {
          auto encoded = splitc(this->argv, static_cast<char>(0x01));
          if (encoded.size() != this->config.argumentCount) {
            posix_spawnattr_destroy(&attributes);
            posix_spawn_file_actions_destroy(&fileActions);
            return fail(EINVAL);
          }
          arguments.insert(arguments.end(), encoded.begin(), encoded.end());
        }
      } else {
        executable = this->shell.empty() ? String("/bin/sh") : this->shell;
        auto cmdline = command;
        if (!this->argv.empty()) {
          cmdline += this->argv;
        }
        arguments = {executable, "-c", cmdline};
      }

      Vector<char*> argumentPointers;
      argumentPointers.reserve(arguments.size() + 1);
      for (auto& argument : arguments) {
        argumentPointers.push_back(argument.data());
      }
      argumentPointers.push_back(nullptr);

      Vector<String> environment;
      if (!this->config.replaceEnvironment) {
        for (auto entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
          environment.push_back(*entry);
        }
      }

      for (const auto& override : this->env) {
        const auto separator = override.find('=');
        bool replaced = false;
        if (separator != String::npos) {
          const auto prefix = override.substr(0, separator + 1);
          for (auto& entry : environment) {
            if (entry.starts_with(prefix)) {
              entry = override;
              replaced = true;
              break;
            }
          }
        }
        if (!replaced) {
          environment.push_back(override);
        }
      }

      Vector<char*> environmentPointers;
      environmentPointers.reserve(environment.size() + 1);
      for (auto& entry : environment) {
        environmentPointers.push_back(entry.data());
      }
      environmentPointers.push_back(nullptr);

      PID pid = -1;
      error = posix_spawnp(
        &pid,
        executable.c_str(),
        &fileActions,
        &attributes,
        argumentPointers.data(),
        environmentPointers.data()
      );
      posix_spawnattr_destroy(&attributes);
      posix_spawn_file_actions_destroy(&fileActions);

      if (error != 0) {
        return fail(error);
      }

      if (stdinFD) {
        close(stdinPipe[0]);
        stdinPipe[0] = -1;
        *stdinFD = stdinPipe[1];
        stdinPipe[1] = -1;
      }
      if (stdoutFD) {
        close(stdoutPipe[1]);
        stdoutPipe[1] = -1;
        *stdoutFD = stdoutPipe[0];
        stdoutPipe[0] = -1;
      }
      if (stderrFD) {
        close(stderrPipe[1]);
        stderrPipe[1] = -1;
        *stderrFD = stderrPipe[0];
        stderrPipe[0] = -1;
      }

      closed = false;
      id = pid;
      data.id = pid;

      // posix_spawnp avoids pthread_atfork handlers that can deadlock after
      // GTK and WebKit have started worker threads.
      auto thread = Thread([this] {
        int code = 0;
        PID waited = -1;
        do {
          waited = waitpid(this->id, &code, 0);
        } while (waited < 0 && errno == EINTR);

        if (waited < 0) {
          this->status = -1;
        } else if (WIFEXITED(code)) {
          this->status = WEXITSTATUS(code);
        } else if (WIFSIGNALED(code)) {
          this->status = 128 + WTERMSIG(code);
        } else {
          this->status = -1;
        }

        this->closeFDs();
        this->closed = true;

        if (this->onExit != nullptr) {
          try {
            this->onExit(std::to_string(status));
          } catch (const std::exception& exception) {
            std::cerr << "Process exit callback exception: " << exception.what() << std::endl;
          }
        }
      });

      thread.detach();
      return pid;
    #endif
  }

  int Process::wait () {
  #if ORO_RUNTIME_PLATFORM_IOS
    return -1; // -EPERM
  #else
    do {
      msleep(Process::PROCESS_WAIT_TIMEOUT);
    } while (this->closed == false);

    return this->status;
  #endif
  }

  void Process::read() noexcept {
  #if !ORO_RUNTIME_PLATFORM_IOS
    if (data.id <= 0 || (!stdoutFD && !stderrFD)) {
      return;
    }

    stdoutAndStderrThread = Thread([this] {
      Vector<pollfd> pollfds;
      std::bitset<2> fd_is_stdout;

      if (stdoutFD) {
        fd_is_stdout.set(pollfds.size());
        pollfds.emplace_back();
        pollfds.back().fd = fcntl(*stdoutFD, F_SETFL, fcntl(*stdoutFD, F_GETFL) | O_NONBLOCK) == 0 ? *stdoutFD : -1;
        pollfds.back().events = POLLIN;
      }

      if (stderrFD) {
        pollfds.emplace_back();
        pollfds.back().fd = fcntl(*stderrFD, F_SETFL, fcntl(*stderrFD, F_GETFL) | O_NONBLOCK) == 0 ? *stderrFD : -1;
        pollfds.back().events = POLLIN;
      }

      auto buffer = UniquePointer<unsigned char[]>(new unsigned char[config.bufferSize]);
      bool any_open = !pollfds.empty();
      StringStream ss;

      while (any_open && (poll(pollfds.data(), static_cast<nfds_t>(pollfds.size()), -1) > 0 || errno == EINTR)) {
        any_open = false;

        for (size_t i = 0; i < pollfds.size(); ++i) {
          if (!(pollfds[i].fd >= 0)) continue;
          if (pollfds[i].revents & POLLIN) {
            memset(buffer.get(), 0, config.bufferSize);
            const ssize_t n = ::read(pollfds[i].fd, buffer.get(), config.bufferSize);

            if (n > 0) {
              if (fd_is_stdout[i]) {
                Lock lock(stdoutMutex);
                auto output = String(
                  reinterpret_cast<char*>(buffer.get()),
                  static_cast<size_t>(n)
                );
                if (config.rawOutput) {
                  readStdout(output);
                } else {
                  auto parts = splitc(output, '\n');

                  if (parts.size() > 1) {
                    for (size_t part = 0; part + 1 < parts.size(); ++part) {
                      ss << parts[part];

                      String line(ss.str());
                      readStdout(line);

                      ss.str(String());
                      ss.clear();
                      ss.copyfmt(initial);
                    }
                    ss << parts.back();
                  } else {
                    ss << output;
                  }
                }
              } else {
                Lock lock(stderrMutex);
                readStderr(String(
                  reinterpret_cast<char*>(buffer.get()),
                  static_cast<size_t>(n)
                ));
              }
              // POLLHUP can arrive with more buffered output than one read.
              // Keep polling until every byte has been consumed.
              any_open = true;
              continue;
            } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
              pollfds[i].fd = -1;
              continue;
            }
          }

          if (pollfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            pollfds[i].fd = -1;
            continue;
          }

          any_open = true;
        }
      }

      if (!config.rawOutput && ss.tellp() > 0) {
        Lock lock(stdoutMutex);
        readStdout(ss.str());
      }
    });
  #endif
  }

  void Process::closeFDs () noexcept {
  #if !ORO_RUNTIME_PLATFORM_IOS
    if (stdoutAndStderrThread.joinable()) {
      stdoutAndStderrThread.join();
    }

    if (stdinFD) {
      closeStdin();
    }

    if (stdoutFD) {
      if (data.id > 0) {
        close(*stdoutFD);
      }

      stdoutFD.reset();
    }

    if (stderrFD) {
      if (data.id > 0) {
        close(*stderrFD);
      }

      stderrFD.reset();
    }
  #endif
  }

  bool Process::write (const char *bytes, size_t n) {
#if ORO_RUNTIME_PLATFORM_IOS
    return false;
#else
    Lock lock(stdinMutex);

    this->lastWriteStatus = 0;

    if (stdinFD) {
      if (n == 0) {
        return true;
      }

      const char* cursor = bytes;
      size_t remaining = n;

      while (remaining > 0) {
        const ssize_t bytesWritten = ::write(*stdinFD, cursor, remaining);

        if (bytesWritten == -1) {
          if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
          }
          this->lastWriteStatus = errno;
          return false;
        }

        if (bytesWritten == 0) {
          this->lastWriteStatus = EPIPE;
          return false;
        }

        cursor += bytesWritten;
        remaining -= static_cast<size_t>(bytesWritten);
      }

      return true;
    }

    return false;
#endif
  }

  void Process::closeStdin () noexcept {
  #if !ORO_RUNTIME_PLATFORM_IOS
    Lock lock(stdinMutex);

    if (stdinFD) {
      if (data.id > 0) {
        close(*stdinFD);
      }

      stdinFD.reset();
    }
  #endif
  }

  void Process::kill (PID id) noexcept {
  #if !ORO_RUNTIME_PLATFORM_IOS
    if (id <= 0) {
      return;
    }

    // Try to signal the entire process group first; if that fails (ESRCH),
    // fall back to signaling the individual process by PID. Escalate
    // TERM -> INT -> KILL to guarantee teardown.
    auto send_group_or_pid = [&](int sig) {
      if (::kill(-id, sig) != 0) {
        // If group doesn't exist, try the single PID
        ::kill(id, sig);
      }
    };

    send_group_or_pid(SIGTERM);
    // If still alive, escalate
    if (::kill(-id, 0) == 0 || ::kill(id, 0) == 0) {
      send_group_or_pid(SIGINT);
    }
    if (::kill(-id, 0) == 0 || ::kill(id, 0) == 0) {
      send_group_or_pid(SIGKILL);
    }
  #endif
  }
}
