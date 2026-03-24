#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <limits.h>

#include "../process.hh"
#include "../env.hh"
#include "../string.hh"

namespace oro::runtime::process {

const static StringStream initial;

namespace {
  int mapWinErrorToErrno (DWORD error) noexcept {
    _dosmaperr(static_cast<unsigned long>(error));
    return errno;
  }
}

Process::Data::Data() noexcept : id(0) {}

Process::Process(
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
    config(config) {
  this->command = command;
  this->argv = argv;
  this->path = path;
}

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

int Process::wait () {
  do {
    msleep(Process::PROCESS_WAIT_TIMEOUT);
  } while (this->closed == false);

  return this->status;
}

// Simple HANDLE wrapper to close it automatically from the destructor.
class Handle {
  public:
    Handle() noexcept : handle(INVALID_HANDLE_VALUE) {}

    ~Handle() noexcept {
      close();
    }

    void close() noexcept {
      if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
      }
    }

    HANDLE detach() noexcept {
      HANDLE old_handle = handle;
      handle = INVALID_HANDLE_VALUE;
      return old_handle;
    }

    operator HANDLE() const noexcept { return handle; }
    HANDLE *operator&() noexcept { return &handle; }

  private:
    HANDLE handle;
};

//Based on the discussion thread: https://www.reddit.com/r/cpp/comments/3vpjqg/a_new_platform_independent_process_library_for_c11/cxq1wsj
std::recursive_mutex create_processMutex;

//Based on the example at https://msdn.microsoft.com/en-us/library/windows/desktop/ms682499(v=vs.85).aspx.
Process::PID Process::open (const String &command, const String &path) noexcept {
  if (openStdin) {
    stdinFD = UniquePointer<Process::FD>(new Process::FD(nullptr));
  }

  if (readStdout) {
    stdoutFD = UniquePointer<Process::FD>(new Process::FD(nullptr));
  }

  if (readStderr) {
    stderrFD = UniquePointer<Process::FD>(new Process::FD(nullptr));
  }

  Handle stdin_rd_p;
  Handle stdin_wr_p;
  Handle stdout_rd_p;
  Handle stdout_wr_p;
  Handle stderr_rd_p;
  Handle stderr_wr_p;

  SECURITY_ATTRIBUTES security_attributes;

  security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
  security_attributes.bInheritHandle = TRUE;
  security_attributes.lpSecurityDescriptor = nullptr;

  Lock lock(create_processMutex);

  if (stdinFD) {
    if (!CreatePipe(&stdin_rd_p, &stdin_wr_p, &security_attributes, 0) ||
       !SetHandleInformation(stdin_wr_p, HANDLE_FLAG_INHERIT, 0))
      return 0;
  }

  if (stdoutFD) {
    if (!CreatePipe(&stdout_rd_p, &stdout_wr_p, &security_attributes, 0) ||
       !SetHandleInformation(stdout_rd_p, HANDLE_FLAG_INHERIT, 0)) {
      return 0;
    }
  }

  if (stderrFD) {
    if (!CreatePipe(&stderr_rd_p, &stderr_wr_p, &security_attributes, 0) ||
       !SetHandleInformation(stderr_rd_p, HANDLE_FLAG_INHERIT, 0)) {
      return 0;
    }
  }

  PROCESS_INFORMATION process_info;
  STARTUPINFO startup_info;

  ZeroMemory(&process_info, sizeof(PROCESS_INFORMATION));
  ZeroMemory(&startup_info, sizeof(STARTUPINFO));

  startup_info.cb = sizeof(STARTUPINFO);
  startup_info.hStdInput = stdin_rd_p;
  startup_info.hStdOutput = stdout_wr_p;
  startup_info.hStdError = stderr_wr_p;

  if (stdinFD || stdoutFD || stderrFD)
    startup_info.dwFlags |= STARTF_USESTDHANDLES;

  if (config.show_window != ProcessConfig::ShowWindow::show_default) {
    startup_info.dwFlags |= STARTF_USESHOWWINDOW;
    startup_info.wShowWindow = static_cast<WORD>(config.show_window);
  }

  auto process_command = command;
#ifdef MSYS_PROCESS_USE_SH
  size_t pos = 0;
  while((pos = process_command.find('\\', pos)) != String::npos) {
    process_command.replace(pos, 1, "\\\\\\\\");
    pos += 4;
  }
  pos = 0;
  while((pos = process_command.find('\"', pos)) != String::npos) {
    process_command.replace(pos, 1, "\\\"");
    pos += 2;
  }
  process_command.insert(0, "sh -c \"");
  process_command += "\"";
#endif

  auto comspec = Env::get("COMSPEC");
  auto shell = this->shell;

  if (shell == "cmd.exe" && comspec.size() > 0) {
    shell = comspec;
  }

  auto needs_quote = [](const String& s) {
    for (char c : s) {
      if (c == ' ' || c == '\t' || c == '"') return true;
    }
    return s.empty();
  };

  auto quote_arg = [&](const String& s) {
    if (!needs_quote(s)) return s;
    String out = "\"";
    size_t bs_count = 0;
    for (char c : s) {
      if (c == '\\') {
        bs_count++;
        out.push_back('\\');
      } else if (c == '"') {
        out.append(String(bs_count, '\\'));
        out.push_back('\\');
        out.push_back('"');
        bs_count = 0;
      } else {
        bs_count = 0;
        out.push_back(c);
      }
    }
    out.push_back('"');
    return out;
  };

  // Build command line safely without the shell when possible
  String cmdline;
  if (shell == "cmd.exe") {
    cmdline = String("/d /s /c ") + (process_command.empty() ? String("") : process_command);
  } else {
    // Program path and arguments
    Vector<String> args;
    if (this->argv.size() > 0) {
      for (const auto& token : oro::runtime::string::splitc(this->argv, (char) 0x01)) {
        if (!token.empty()) args.push_back(token);
      }
    }
    // Quote each argument as needed
    for (size_t i = 0; i < args.size(); ++i) {
      if (i > 0) cmdline.push_back(' ');
      cmdline += quote_arg(args[i]);
    }
  }

  // If not using shell, set application name to program path and pass only args
  const char* applicationName = shell.size() > 0 ? shell.c_str() : (process_command.empty() ? nullptr : process_command.c_str());
  // CreateProcess may modify the command line buffer; ensure mutable
  std::string mutableCmd = cmdline;

  BOOL bSuccess = CreateProcess(
    applicationName,
    mutableCmd.size() > 0 ? mutableCmd.data() : nullptr,
    nullptr,
    nullptr,
    stdinFD || stdoutFD || stderrFD || config.inheritFDs, // Cannot be false when stdout, stderr or stdin is used
    stdinFD || stdoutFD || stderrFD ? CREATE_NO_WINDOW : 0,             // CREATE_NO_WINDOW cannot be used when stdout or stderr is redirected to parent process
    nullptr,
    path.empty() ? nullptr : path.c_str(),
    &startup_info,
    &process_info
  );

  if (!bSuccess) {
    auto msg = String("Unable to execute: " + process_command);
    MessageBoxA(nullptr, &msg[0], "Alert", MB_OK | MB_ICONSTOP);
    return 0;
  } else {
    CloseHandle(process_info.hThread);
  }

  if (stdinFD) {
    *stdinFD = stdin_wr_p.detach();
  }

  if (stdoutFD) {
    *stdoutFD = stdout_rd_p.detach();
  }

  if (stderrFD) {
    *stderrFD = stderr_rd_p.detach();
  }

  auto processHandle = process_info.hProcess;
  auto t = Thread([&](HANDLE _processHandle) {
    DWORD exitCode = 0;
    try {
      WaitForSingleObject(_processHandle, INFINITE);

      if (GetExitCodeProcess(_processHandle, &exitCode) == 0) {
        std::cerr << formatWindowsError(GetLastError(), "Process::open() GetExitCodeProcess()") << std::endl;
        exitCode = -1;
      }

      if (this->closed) {
        std::cout << "Process killed. " << exitCode << std::endl;
        return;
      }

      this->status = (exitCode <= UINT_MAX ? exitCode : WEXITSTATUS(exitCode));
      this->closed = true;
      if (this->onExit != nullptr)
        this->onExit(std::to_string(this->status));
    } catch (const std::exception& e) {
      std::cerr << "Process thread exception: " << e.what() << std::endl;
      this->closed = true;
    }
  }, processHandle);

  t.detach();

  closed = false;
  id = process_info.dwProcessId;

  data.id = process_info.dwProcessId;
  data.handle = process_info.hProcess;

  return process_info.dwProcessId;
}

void Process::read() noexcept {
  if (data.id == 0) {
    return;
  }

  if (stdoutFD) {
    stdoutThread = Thread([this]() {
      DWORD n;

      UniquePointer<unsigned char[]> buffer(new unsigned char[config.bufferSize]);
      StringStream ss;

      for (;;) {
        memset(buffer.get(), 0, config.bufferSize);
        BOOL bSuccess = ReadFile(*stdoutFD, static_cast<CHAR *>(buffer.get()), static_cast<DWORD>(config.bufferSize), &n, nullptr);

        if (!bSuccess || n == 0) {
          break;
        }

        auto b = String(buffer.get());
        auto parts = splitc(b, '\n');

        if (parts.size() > 1) {
          Lock lock(stdoutMutex);

          for (int i = 0; i < parts.size() - 1; i++) {
            ss << parts[i];
            String s(ss.str());
            readStdout(s);
            ss.str(String());
            ss.clear();
            ss.copyfmt(initial);
          }
          ss << parts[parts.size() - 1];
        } else {
          ss << b;
        }
      }
    });
  }

  if (stderrFD) {
    stderrThread = Thread([this]() {
      DWORD n;
      auto buffer = std::make_unique<unsigned char[]>(config.bufferSize);

      for (;;) {
        BOOL bSuccess = ReadFile(*stderrFD, static_cast<CHAR *>(buffer.get()), static_cast<DWORD>(config.bufferSize), &n, nullptr);
        if (!bSuccess || n == 0) break;
        Lock lock(stderrMutex);
        readStderr(String(buffer.get()));
      }
    });
  }
}

void Process::closeFDs() noexcept {
  if (stdoutThread.joinable()) {
    stdoutThread.join();
  }

  if (stderrThread.joinable()) {
    stderrThread.join();
  }

  if (stdinFD) {
    closeStdin();
  }

  if (stdoutFD) {
    if (*stdoutFD != nullptr) {
      CloseHandle(*stdoutFD);
    }

    stdoutFD.reset();
  }

  if (stderrFD) {
    if (*stderrFD != nullptr) {
      CloseHandle(*stderrFD);
    }

    stderrFD.reset();
  }
}

bool Process::write(const char *bytes, size_t n) {
  if (!openStdin) {
    throw std::invalid_argument("Can't write to an unopened stdin pipe. Please set openStdin=true when constructing the process.");
  }

  Lock lock(stdinMutex);
  if (stdinFD) {
    this->lastWriteStatus = 0;

    if (n == 0) {
      return true;
    }

    const char* cursor = bytes;
    size_t remaining = n;

    while (remaining > 0) {
      DWORD bytesWritten = 0;
      DWORD chunk = remaining > static_cast<size_t>(UINT_MAX)
        ? static_cast<DWORD>(UINT_MAX)
        : static_cast<DWORD>(remaining);

      BOOL bSuccess = WriteFile(*stdinFD, cursor, chunk, &bytesWritten, nullptr);

      if (!bSuccess) {
        this->lastWriteStatus = mapWinErrorToErrno(GetLastError());
        return false;
      }

      if (bytesWritten == 0) {
        this->lastWriteStatus = mapWinErrorToErrno(ERROR_BROKEN_PIPE);
        return false;
      }

      cursor += bytesWritten;
      remaining -= static_cast<size_t>(bytesWritten);
    }

    return true;
  }

  return false;
}

void Process::closeStdin () noexcept {
  Lock lock(stdinMutex);

  if (stdinFD) {
    if (*stdinFD != nullptr) {
      CloseHandle(*stdinFD);
    }

    stdinFD.reset();
  }
}

//Based on http://stackoverflow.com/a/1173396
void Process::kill (PID id) noexcept {
  if (id == 0) {
    return;
  }

  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

  if (snapshot) {
    PROCESSENTRY32 process;
    ZeroMemory(&process, sizeof(process));
    process.dwSize = sizeof(process);

    if (Process32First(snapshot, &process)) {
      do {
        if (process.th32ParentProcessID == id) {
          HANDLE process_handle = OpenProcess(PROCESS_TERMINATE, FALSE, process.th32ProcessID);

          if (process_handle) {
            TerminateProcess(process_handle, 2);
            CloseHandle(process_handle);
          }
        }
      } while (Process32Next(snapshot, &process));
    }

    CloseHandle(snapshot);

    this->closed = true;
  }

  HANDLE process_handle = OpenProcess(PROCESS_TERMINATE, FALSE, id);

  if (process_handle) {
    TerminateProcess(process_handle, 2);
  }
}
} // namespace oro::runtime::process
