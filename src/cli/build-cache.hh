#ifndef ORO_CLI_BUILD_CACHE_H
#define ORO_CLI_BUILD_CACHE_H

#include <array>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace oro::cli {
  inline constexpr std::array<const char*, 5> buildCacheOptionNames = {
    "cache", "compiler-cache", "android-cache", "android-build-cache",
    "android-configuration-cache"
  };

  struct BuildCachePolicy {
    bool enabled = false;
    bool compiler = false;
    bool gradleBuild = false;
    bool gradleConfiguration = false;

    std::string gradleArguments () const {
      return std::string(gradleBuild ? " --build-cache" : " --no-build-cache") +
        (gradleConfiguration ? " --configuration-cache" : " --no-configuration-cache") +
        " --configuration-cache-problems=fail";
    }
  };

  inline BuildCachePolicy resolveBuildCachePolicy (
    const std::map<std::string, std::string>& settings,
    const std::unordered_set<std::string>& flags,
    std::string platform
  ) {
    if (platform == "android-emulator") platform = "android";
    if (platform == "ios-simulator") platform = "ios";
    if (platform == "darwin") platform = "mac";
    if (platform == "win32" || platform == "windows") platform = "win";

    const auto setting = [&](const std::string& key) -> std::optional<bool> {
      const auto entry = settings.find(key);
      if (entry == settings.end() || entry->second.empty()) return std::nullopt;
      if (entry->second == "true") return true;
      if (entry->second == "false") return false;
      throw std::invalid_argument("Build cache setting '" + key + "' must be true or false");
    };

    std::map<std::string, std::optional<bool>> options;
    for (const auto* name : buildCacheOptionNames) {
      const auto enabled = flags.contains(std::string("--") + name);
      const auto disabled = flags.contains(std::string("--no-") + name);
      if (enabled && disabled) {
        throw std::invalid_argument(std::string("Use only one of --") + name + " and --no-" + name);
      }
      options[name] = enabled ? std::optional(true) : disabled ? std::optional(false) : std::nullopt;
    }

    const auto resolve = [&](const std::string& kind, const std::string& flag) {
      // CLI choices precede configuration; within each source, specific choices win.
      const std::array<std::optional<bool>, 7> choices = {
        kind.empty() ? std::nullopt : options[flag],
        platform == "android" ? options["android-cache"] : std::nullopt,
        options["cache"],
        kind.empty() ? std::nullopt : setting(platform + "_" + kind),
        setting(platform + "_cache"),
        kind == "compiler_cache" ? setting("build_compiler_cache") : std::nullopt,
        setting("build_cache")
      };
      for (const auto& choice : choices) {
        if (choice.has_value()) return *choice;
      }
      return false;
    };

    BuildCachePolicy policy;
    policy.enabled = resolve("", "cache");
    policy.compiler = resolve("compiler_cache", "compiler-cache");
    if (platform == "android") {
      policy.gradleBuild = resolve("build_cache", "android-build-cache");
      policy.gradleConfiguration = resolve("configuration_cache", "android-configuration-cache");
    }
    return policy;
  }
} // namespace oro::cli

#endif
