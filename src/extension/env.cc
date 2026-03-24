#include "extension.hh"

using oro::runtime::config::getUserConfig;
using oro::runtime::string::split;

const char* oapi_env_get (
  oapi_context_t* ctx,
  const char* name
) {
  if (ctx == nullptr || name == nullptr)  return nullptr;
  if (!ctx->isAllowed("env_get")) {
    oapi_debug(ctx, "'env_get' is not allowed.");
    return nullptr;
  }

  static const auto userConfig = getUserConfig();

  if (!userConfig.contains("build_env")) {
    return nullptr;
  }

  static const auto allowed = split(userConfig.at("build_env"), ' ');

  if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
    return nullptr;
  }

  auto value = oro::runtime::env::get(name);
  if (value.size() == 0) {
    return nullptr;
  }

  auto pointer = ctx->memory.alloc<char>(value.size() + 1);
  return reinterpret_cast<const char*>(
    memcpy(pointer, value.c_str(), value.size() + 1)
  );
}

bool oapi_env_has (
  oapi_context_t* ctx,
  const char* name
) {
  if (ctx == nullptr || name == nullptr) return false;
  if (!ctx->isAllowed("env_has")) {
    oapi_debug(ctx, "'env_has' is not allowed.");
    return false;
  }

  static const auto userConfig = getUserConfig();

  if (!userConfig.contains("build_env")) {
    return false;
  }

  static const auto allowed = split(userConfig.at("build_env"), ' ');

  if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
    return false;
  }

  return oro::runtime::env::has(name);
}
