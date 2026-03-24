#include "extension.hh"

#if !ORO_RUNTIME_PLATFORM_IOS
using oro::runtime::process::exec;
#endif

using oro::runtime::string::trim;

const oapi_process_exec_t* oapi_process_exec (
  oapi_context_t* ctx,
  const char* command
) {
#if ORO_RUNTIME_PLATFORM_IOS
  debug("oapi_process_exec is not supported on this platform");
  return nullptr;
#else

  if (ctx == nullptr) return nullptr;
  if (!ctx->isAllowed("process_exec")) {
    oapi_debug(ctx, "'process_exec' is not allowed.");
    return nullptr;
  }

  auto process = exec(command);
  process.output = trim(process.output);
  return ctx->memory.alloc<oapi_process_exec_t>(ctx, process);
#endif
}

int oapi_process_exec_get_exit_code (
  const oapi_process_exec_t* process
) {
#if ORO_RUNTIME_PLATFORM_IOS
  debug("oapi_process_exec_get_exit_code is not supported on this platform");
  return -1;
#else
  return process != nullptr ? process->exitCode : -1;
#endif
}

const char* oapi_process_exec_get_output (
  const oapi_process_exec_t* process
) {
#if ORO_RUNTIME_PLATFORM_IOS
  debug("oapi_process_exec_get_output is not supported on this platform");
  return nullptr;
#else
  return process != nullptr ? process->output.c_str() : nullptr;
#endif
}

oapi_process_spawn_t* oapi_process_spawn (
  oapi_context_t* ctx,
  const char* command,
  const char* argv,
  const char* path,
  oapi_process_spawn_stderr_callback_t onstdout,
  oapi_process_spawn_stderr_callback_t onstderr,
  oapi_process_spawn_exit_callback_t onexit
) {
#if ORO_RUNTIME_PLATFORM_IOS
  debug("oapi_process_spawn is not supported on this platform");
  return nullptr;
#else
  auto process = ctx->memory.alloc<oapi_process_spawn_t>(
    ctx,
    command,
    argv,
    oro::runtime::Vector<oro::runtime::String>{},
    path,
    onstdout,
    onstderr,
    onexit
  );
  process->open();
  return process;
#endif
}

int oapi_process_spawn_get_exit_code (
  const oapi_process_spawn_t* process
) {
#if ORO_RUNTIME_PLATFORM_IOS
  debug("oapi_process_spawn_get_exit_code is not supported on this platform");
  return -1;
#else
  return process != nullptr ? process->status.load() : -1;
#endif
}

unsigned long oapi_process_spawn_get_pid (
  const oapi_process_spawn_t* process
) {
#if ORO_RUNTIME_PLATFORM_IOS
  debug("oapi_process_spawn_get_pid is not supported on this platform");
  return 0;
#else
  return process != nullptr ? process->id : 0;
#endif
}

oapi_context_t* oapi_process_spawn_get_context (
  const oapi_process_spawn_t* process
) {
#if ORO_RUNTIME_PLATFORM_IOS
  debug("oapi_process_spawn_get_context is not supported on this platform");
  return nullptr;
#else
  return process != nullptr ? process->context : nullptr;
#endif
}

int oapi_process_spawn_wait (
  oapi_process_spawn_t* process
) {
#if ORO_RUNTIME_PLATFORM_IOS
  debug("oapi_process_spawn_wait is not supported on this platform");
  return -1;
#else
  return process != nullptr ? process->wait() : -1;
#endif
}

bool oapi_process_spawn_write (
  oapi_process_spawn_t* process,
  const char* data,
  const size_t size
) {
#if ORO_RUNTIME_PLATFORM_IOS
  debug("oapi_process_spawn_write is not supported on this platform");
  return false;
#else
  if (!process || process->closed) return false;
  process->write(data, size);
  return true;
#endif
}

bool oapi_process_spawn_close_stdin (
  oapi_process_spawn_t* process
) {
#if ORO_RUNTIME_PLATFORM_IOS
  debug("oapi_process_spawn_close_stdin is not supported on this platform");
  return false;
#else
  if (!process || process->closed) return false;
  process->closeStdin();
  return true;
#endif
}

bool oapi_process_spawn_kill (
  oapi_process_spawn_t* process,
  int code
) {
#if ORO_RUNTIME_PLATFORM_IOS
  debug("oapi_process_spawn_kill is not supported on this platform");
  return false;
#else
  if (!process || process->closed) return false;
  process->kill(code);
  return true;
#endif
}
