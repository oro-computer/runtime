#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <limits>
#include <limits.h>

#include "../process.hh"
#include "../env.hh"
#include "../string.hh"

namespace oro::runtime::process {

const static StringStream initial;

namespace {
  int mapWinErrorToErrno (DWORD error) noexcept {
    return std::error_code(
      static_cast<int>(error),
      std::system_category()
    ).default_error_condition().value();
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
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    ~Handle() noexcept {
      close();
    }

    void close() noexcept {
      if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
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
  if (processThread.joinable()) {
    if (!closed) {
      return 0;
    }
    processThread.join();
  }

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
  STARTUPINFOW startup_info;

  ZeroMemory(&process_info, sizeof(PROCESS_INFORMATION));
  ZeroMemory(&startup_info, sizeof(STARTUPINFOW));

  startup_info.cb = sizeof(STARTUPINFOW);
  startup_info.hStdInput = stdinFD ? static_cast<HANDLE>(stdin_rd_p) : GetStdHandle(STD_INPUT_HANDLE);
  startup_info.hStdOutput = stdoutFD ? static_cast<HANDLE>(stdout_wr_p) : GetStdHandle(STD_OUTPUT_HANDLE);
  startup_info.hStdError = stderrFD ? static_cast<HANDLE>(stderr_wr_p) : GetStdHandle(STD_ERROR_HANDLE);

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

  auto comspec = env::get("COMSPEC");
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
    out.append(String(bs_count, '\\'));
    out.push_back('"');
    return out;
  };

  // Build command line safely without the shell when possible
  String cmdline;
  if (this->shell == "cmd.exe") {
    cmdline = quote_arg(shell) + " /d /s /c \"" + process_command;
    if (!this->argv.empty()) {
      cmdline += " " + this->argv;
    }
    cmdline += "\"";
  } else if (this->config.useDirectArguments) {
    Vector<String> args {process_command};
    if (this->config.argumentCount > 0) {
      const auto encoded = oro::runtime::string::splitc(
        this->argv,
        static_cast<char>(0x01)
      );
      if (encoded.size() != this->config.argumentCount) {
        return 0;
      }
      args.insert(args.end(), encoded.begin(), encoded.end());
    }
    for (size_t i = 0; i < args.size(); ++i) {
      if (i > 0) cmdline.push_back(' ');
      cmdline += quote_arg(args[i]);
    }
  } else {
    // The CLI supplies an already formatted argument string. Include argv[0]
    // and preserve its argument boundaries and quoting for the child's CRT.
    cmdline = quote_arg(process_command);
    if (!this->argv.empty()) {
      cmdline += " " + this->argv;
    }
  }

  // CreateProcessW still requires argv[0] in cmdline when applicationName is set.
  const String applicationName = shell.size() > 0
    ? shell
    : process_command;
  // A null application name lets Windows search PATH for bare executable names.
  const bool searchPath = applicationName.find_first_of("/\\") == String::npos;
  const auto wideApplicationName = searchPath
    ? WString()
    : string::convertStringToWString(applicationName);
  if (!searchPath && !applicationName.empty() && wideApplicationName.empty()) {
    return 0;
  }

  // CreateProcess may modify the command line buffer; ensure mutable
  auto mutableCmd = string::convertStringToWString(cmdline);
  if (!cmdline.empty() && mutableCmd.empty()) {
    return 0;
  }

  Vector<wchar_t> environmentBlock;
  if (config.replaceEnvironment) {
    auto environment = this->env;
    std::sort(environment.begin(), environment.end(), [](const String& left, const String& right) {
      return oro::runtime::string::toLowerCase(left) <
        oro::runtime::string::toLowerCase(right);
    });
    for (const auto& entry : environment) {
      const auto wideEntry = string::convertStringToWString(entry);
      if (!entry.empty() && wideEntry.empty()) {
        return 0;
      }

      environmentBlock.insert(environmentBlock.end(), wideEntry.begin(), wideEntry.end());
      environmentBlock.push_back(L'\0');
    }
    environmentBlock.push_back(L'\0');
    if (environment.empty()) {
      environmentBlock.push_back(L'\0');
    }
  }

  const auto widePath = string::convertStringToWString(path);
  if (!path.empty() && widePath.empty()) {
    return 0;
  }

  DWORD creationFlags = stdinFD || stdoutFD || stderrFD ? CREATE_NO_WINDOW : 0;
  if (config.replaceEnvironment) {
    creationFlags |= CREATE_UNICODE_ENVIRONMENT;
  }

  BOOL bSuccess = CreateProcessW(
    wideApplicationName.empty() ? nullptr : wideApplicationName.c_str(),
    mutableCmd.size() > 0 ? mutableCmd.data() : nullptr,
    nullptr,
    nullptr,
    stdinFD || stdoutFD || stderrFD || config.inheritFDs, // Cannot be false when stdout, stderr or stdin is used
    creationFlags,
    config.replaceEnvironment ? environmentBlock.data() : nullptr,
    widePath.empty() ? nullptr : widePath.c_str(),
    &startup_info,
    &process_info
  );

  if (!bSuccess) {
    const auto error = GetLastError();
    errno = mapWinErrorToErrno(error);
    std::cerr << string::formatWindowsError(error, "Process::open() CreateProcessW") << std::endl;
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
  closed = false;
  id = process_info.dwProcessId;
  data.id = process_info.dwProcessId;
  data.handle = process_info.hProcess;

  // Initialize both readers before the waiter can close their handles.
  read();

  processThread = Thread([this](HANDLE _processHandle) {
    DWORD exitCode = 0;
    try {
      WaitForSingleObject(_processHandle, INFINITE);

      if (GetExitCodeProcess(_processHandle, &exitCode) == 0) {
        std::cerr << string::formatWindowsError(
          GetLastError(),
          "Process::open() GetExitCodeProcess()"
        ) << std::endl;
        exitCode = -1;
      }

      this->status = static_cast<int>(exitCode);
    } catch (const std::exception& e) {
      std::cerr << "Process thread exception: " << e.what() << std::endl;
      this->status = -1;
    }

    this->closeFDs();
    CloseHandle(_processHandle);
    this->data.handle = nullptr;
    this->closed = true;
    if (this->onExit != nullptr) {
      try {
        this->onExit(std::to_string(this->status));
      } catch (const std::exception& exception) {
        std::cerr << "Process exit callback exception: " << exception.what() << std::endl;
      }
    }
  }, processHandle);

  return process_info.dwProcessId;
}

void Process::read() noexcept {
  if (data.id == 0) {
    return;
  }

  if (stdoutFD) {
    stdoutThread = Thread([this]() {
      DWORD n;

      const auto bufferSize = std::min(
        config.bufferSize,
        static_cast<size_t>(std::numeric_limits<DWORD>::max())
      );
      UniquePointer<unsigned char[]> buffer(new unsigned char[bufferSize]);
      StringStream ss;

      for (;;) {
        memset(buffer.get(), 0, bufferSize);
        BOOL bSuccess = ReadFile(
          *stdoutFD,
          reinterpret_cast<CHAR *>(buffer.get()),
          static_cast<DWORD>(bufferSize),
          &n,
          nullptr
        );

        if (!bSuccess || n == 0) {
          break;
        }

        auto output = String(
          reinterpret_cast<char*>(buffer.get()),
          static_cast<size_t>(n)
        );
        Lock lock(stdoutMutex);
        if (config.rawOutput) {
          readStdout(output);
        } else {
          auto parts = string::splitc(output, '\n');

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
      }

      if (!config.rawOutput && ss.tellp() > 0) {
        Lock lock(stdoutMutex);
        readStdout(ss.str());
      }
    });
  }

  if (stderrFD) {
    stderrThread = Thread([this]() {
      DWORD n;
      const auto bufferSize = std::min(
        config.bufferSize,
        static_cast<size_t>(std::numeric_limits<DWORD>::max())
      );
      auto buffer = std::make_unique<unsigned char[]>(bufferSize);

      for (;;) {
        BOOL bSuccess = ReadFile(
          *stderrFD,
          reinterpret_cast<CHAR *>(buffer.get()),
          static_cast<DWORD>(bufferSize),
          &n,
          nullptr
        );
        if (!bSuccess || n == 0) break;
        Lock lock(stderrMutex);
        readStderr(String(
          reinterpret_cast<char*>(buffer.get()),
          static_cast<size_t>(n)
        ));
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

  if (snapshot != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32W process;
    ZeroMemory(&process, sizeof(process));
    process.dwSize = sizeof(process);

    if (Process32FirstW(snapshot, &process)) {
      do {
        if (process.th32ParentProcessID == id) {
          HANDLE process_handle = OpenProcess(PROCESS_TERMINATE, FALSE, process.th32ProcessID);

          if (process_handle) {
            TerminateProcess(process_handle, 2);
            CloseHandle(process_handle);
          }
        }
      } while (Process32NextW(snapshot, &process));
    }

    CloseHandle(snapshot);
  }

  HANDLE process_handle = OpenProcess(PROCESS_TERMINATE, FALSE, id);

  if (process_handle) {
    TerminateProcess(process_handle, 2);
    CloseHandle(process_handle);
  }
}
} // namespace oro::runtime::process
