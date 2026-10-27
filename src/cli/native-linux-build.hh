#ifndef ORO_CLI_NATIVE_LINUX_BUILD_H
#define ORO_CLI_NATIVE_LINUX_BUILD_H

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace oro::cli {
  template <typename Settings>
  inline std::vector<std::filesystem::path> prepareLinuxNativeSources (Settings& settings,
                                                                       const std::filesystem::path& project,
                                                                       const std::filesystem::path& output) {
    const bool enabled = settings["linux_native_service_enabled"] == "true";
    std::istringstream sourceInput(settings["linux_native_sources"]);
    std::istringstream initializerInput(settings["linux_native_initializers"]);
    std::vector<std::filesystem::path> sources;
    const auto root = std::filesystem::canonical(project);
    std::string value;
    std::set<std::string> seen;
    while (sourceInput >> value) {
      if (sources.size() >= 16)
        throw std::runtime_error("linux.native.sources permits at most 16 C++ files");
      const std::filesystem::path relative(value);
      if (relative.is_absolute())
        throw std::runtime_error("linux.native.sources paths must be relative to oro.toml");
      const auto path = std::filesystem::canonical(root / relative);
      const auto within = path.lexically_relative(root);
      if (within.empty() || *within.begin() == ".." || !std::filesystem::is_regular_file(path) ||
          (path.extension() != ".cc" && path.extension() != ".cpp" && path.extension() != ".cxx") ||
          !seen.insert(path.string()).second)
        throw std::runtime_error("linux.native.sources must name distinct C++ files inside the project");
      sources.push_back(path);
    }
    std::vector<std::string> initializers;
    seen.clear();
    while (initializerInput >> value) {
      if (initializers.size() >= 16 || value.size() > 64 ||
          !std::regex_match(value, std::regex("[A-Za-z_][A-Za-z0-9_]*")) || value == "oro_runtime_native_initialize" ||
          !seen.insert(value).second)
        throw std::runtime_error("linux.native.initializers must contain at most 16 distinct C function names");
      initializers.push_back(value);
    }
    if ((sources.empty() != initializers.empty()) || (enabled && sources.empty()))
      throw std::runtime_error("Linux native hosting requires both linux.native.sources and linux.native.initializers");
    if (sources.empty())
      return sources;
    const auto wrapper = output / "native-initializers.cc";
    std::filesystem::create_directories(output);
    std::ofstream file(wrapper, std::ios::trunc);
    file << "namespace oro::runtime { class Runtime; }\n";
    for (const auto& name : initializers)
      file << "extern \"C\" void " << name << "(oro::runtime::Runtime*);\n";
    file << "extern \"C\" void oro_runtime_native_initialize(oro::runtime::Runtime* runtime) {\n";
    for (const auto& name : initializers)
      file << "  " << name << "(runtime);\n";
    file << "}\n";
    file.close();
    if (!file)
      throw std::runtime_error("Unable to write Linux native initializers");
    sources.push_back(wrapper);
    return sources;
  }

  inline std::string nativeExecutableArgument (const std::string& value, bool systemd) {
    std::string result = "\"";
    for (const auto ch : value) {
      if (ch < 32 || ch == 127)
        throw std::runtime_error("Invalid native service executable path");
      if (ch == '\\' || ch == '"')
        result += '\\';
      if (systemd && ch == '%')
        result += '%';
      if (systemd && ch == '$')
        result += '$';
      result += ch;
    }
    return result + '"';
  }

  inline void prepareLinuxNativeService (const std::filesystem::path& package, const std::string& bundle,
                                         const std::string& executable, bool enabled) {
    if (!enabled)
      return;
    if (!std::regex_match(bundle, std::regex("[a-zA-Z_][a-zA-Z0-9_]*(\\.[a-zA-Z_][a-zA-Z0-9_]*)+")) ||
        bundle.size() > 200)
      throw std::runtime_error("Native Linux hosting requires a D-Bus compatible bundle identifier");
    const auto unit = bundle + "-native.service";
    const auto services = package / "usr/share/dbus-1/services";
    const auto units = package / "usr/lib/systemd/user";
    std::filesystem::create_directories(services);
    std::filesystem::create_directories(units);
    for (const auto& suffix : {".NativeRuntime", ".NativePush"}) {
      std::ofstream file(services / (bundle + suffix + ".service"), std::ios::trunc);
      file << "[D-BUS Service]\nName=" << bundle << suffix << "\nExec=" << nativeExecutableArgument(executable, false)
           << " --oro-native-service\nSystemdService=" << unit << '\n';
      file.close();
      if (!file)
        throw std::runtime_error("Unable to write native D-Bus activation file");
    }
    std::ofstream file(units / unit, std::ios::trunc);
    file
      << "[Unit]\nDescription=Oro native application service\nAfter=graphical-session-pre.target\n\n"
      << "[Service]\nType=dbus\nBusName=" << bundle
      << ".NativeRuntime\nExecStart=" << nativeExecutableArgument(executable, true)
      << " --oro-native-service\nRestart=on-failure\nRestartSec=5\nTimeoutStopSec=35\nKillMode=process\nUMask=0077\n\n"
      << "[Install]\nWantedBy=default.target\n";
    file.close();
    if (!file)
      throw std::runtime_error("Unable to write native systemd user unit");
  }
}  // namespace oro::cli
#endif
