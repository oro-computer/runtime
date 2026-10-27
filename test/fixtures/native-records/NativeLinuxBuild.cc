#include "src/cli/native-linux-build.hh"
#include "src/cli/native-android-build.hh"
#include <cassert>
#include <iostream>
#include <map>

int main (int argc, char** argv) {
  assert (argc == 2);
  namespace fs = std::filesystem;
  const fs::path directory (argv[1]);
  const auto project = directory / "project";
  fs::create_directories (project / "native");
  std::ofstream (project / "native/public.hh") << "#pragma once\n#include \"../shared.hh\"\n";
  std::ofstream (project / "shared.hh") << "#pragma once\n";
  std::ofstream (project / "native/startup.cc") << "#include \"public.hh\"\nnamespace oro::runtime { class Runtime; }\n"
                                                << "int public_initializers = 0;\nextern \"C\" void "
                                                   "public_init(oro::runtime::Runtime*) { public_initializers++; }\n";
  std::ofstream (directory / "outside.cc") << "// public test\n";
  fs::create_symlink (directory / "outside.cc", project / "native/escape.cc");
  std::map<std::string, std::string> settings{{"linux_native_service_enabled", "true"},
                                              {"linux_native_sources", "native/startup.cc"},
                                              {"linux_native_initializers", "public_init"}};
  const auto result = oro::cli::prepareLinuxNativeSources (settings, project, directory / "output");
  assert (result.size () == 2 && result.front () == fs::canonical (project / "native/startup.cc"));
  std::map<std::string, std::string> android{{"android_native_callbacks_enabled", "true"},
                                             {"android_native_callback_sources", "native/startup.cc"},
                                             {"android_native_initializers", "public_init"}};
  const auto androidSources = oro::cli::prepareAndroidNativeSources (android, project, directory / "android");
  assert (androidSources == (std::vector<std::string>{"native-source-0.cc", "native-initializers.cc"}));
  assert (fs::exists (directory / "android/project/native/startup.cc"));
  assert (fs::exists (directory / "android/project/native/public.hh"));
  assert (fs::exists (directory / "android/project/shared.hh"));
  std::ifstream captured (directory / "android/project/native/startup.cc");
  const std::string original ((std::istreambuf_iterator<char> (captured)), std::istreambuf_iterator<char> ());
  std::ofstream (project / "native/startup.cc", std::ios::app) << "\n// public edit after capture\n";
  std::ifstream same (directory / "android/project/native/startup.cc");
  assert (std::string ((std::istreambuf_iterator<char> (same)), std::istreambuf_iterator<char> ()) == original);
  android["android_native_callbacks_enabled"] = "false";
  try {
    oro::cli::prepareAndroidNativeSources (android, project, directory / "android-disabled");
    assert (false);
  } catch (const std::runtime_error&) {
  }
  android["android_native_callbacks_enabled"] = "true";
  android["android_native_callback_sources"] = "native/escape.cc";
  try {
    oro::cli::prepareAndroidNativeSources (android, project, directory / "android-escape");
    assert (false);
  } catch (const std::runtime_error&) {
  }
  android["android_native_callback_sources"] = "native/startup.cc";
  std::ofstream (project / "native/public.hh", std::ios::app) << "#include \"../../outside.cc\"\n";
  try {
    oro::cli::prepareAndroidNativeSources (android, project, directory / "android-header-escape");
    assert (false);
  } catch (const std::runtime_error&) {
  }
  const auto reject = [&] (const std::string& key, const std::string& value) {
    auto invalid = settings;
    invalid[key] = value;
    bool rejected = false;
    try {
      oro::cli::prepareLinuxNativeSources (invalid, project, directory / "rejected");
    } catch (const std::exception&) {
      rejected = true;
    }
    assert (rejected);
  };
  reject ("linux_native_sources", "../outside.cc");
  reject ("linux_native_sources", "native/escape.cc");
  reject ("linux_native_sources", (project / "native/startup.cc").string ());
  reject ("linux_native_sources", "native/startup.cc native/startup.cc");
  reject ("linux_native_sources", "");
  reject ("linux_native_initializers", "public_init public_init");
  reject ("linux_native_initializers", "public_init;malformed");
  reject ("linux_native_initializers", "oro_runtime_native_initialize");
  oro::cli::prepareLinuxNativeService (directory / "package", "org.oro.PublicNativeLinux", "/usr/bin/true", true);
  bool rejected = false;
  try {
    oro::cli::prepareLinuxNativeService (directory / "unsafe", "org.oro.Bad/Service", "/usr/bin/true", true);
  } catch (const std::exception&) {
    rejected = true;
  }
  assert (rejected);
  const auto quoted = oro::cli::nativeExecutableArgument ("/public %test/$dollar/quote\"\\app", true);
  assert (quoted.find ("%%") != std::string::npos && quoted.find ("$$") != std::string::npos &&
          quoted.find ("\\\"") != std::string::npos);
  std::cout << "linux-native-source-boundaries-initializers-and-service-generation-passed" << std::endl;
}
