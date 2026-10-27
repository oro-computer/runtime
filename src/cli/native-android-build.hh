#ifndef ORO_CLI_NATIVE_ANDROID_BUILD_H
#define ORO_CLI_NATIVE_ANDROID_BUILD_H

#include "native-linux-build.hh"
#include <map>
#include <deque>

namespace oro::cli {
  template <typename Settings>
  inline std::vector<std::string> prepareAndroidNativeSources (Settings& settings, const std::filesystem::path& project,
                                                               const std::filesystem::path& output) {
    const bool enabled = settings["android_native_callbacks_enabled"] == "true";
    if (!enabled) {
      if (!settings["android_native_callback_sources"].empty () || !settings["android_native_initializers"].empty ())
        throw std::runtime_error ("Enable android.native.callbacks_enabled before configuring C++ native callbacks");
      return {};
    }
    std::map<std::string, std::string> input{{"linux_native_service_enabled", "true"},
                                             {"linux_native_sources", settings["android_native_callback_sources"]},
                                             {"linux_native_initializers", settings["android_native_initializers"]}};
    std::vector<std::filesystem::path> sources;
    try {
      sources = prepareLinuxNativeSources (input, project, output);
    } catch (const std::exception&) {
      throw std::runtime_error (
          "Android native callbacks require paired project-contained C++ sources and distinct C initializer names");
    }
    const auto root = std::filesystem::canonical (project);
    std::deque<std::filesystem::path> pending;
    for (size_t i = 0; i + 1 < sources.size (); i++)
      pending.push_back (sources[i]);
    std::set<std::string> captured;
    uint64_t totalBytes = 0;
    const std::regex localInclude (R"INCLUDE(^[ \t]*#[ \t]*include[ \t]*"([^"\r\n]{1,4096})")INCLUDE");
    while (!pending.empty ()) {
      const auto source = std::filesystem::canonical (pending.front ());
      pending.pop_front ();
      const auto relative = source.lexically_relative (root);
      const auto name = relative.generic_string ();
      if (relative.empty () || *relative.begin () == ".." || name.find_first_of (":\"\\\n\r") != std::string::npos)
        throw std::runtime_error (
            "Android native sources and local headers must remain inside the project with portable names");
      if (!captured.insert (name).second)
        continue;
      const auto size = std::filesystem::file_size (source);
      totalBytes += size;
      if (captured.size () > 4096 || size > 16777216 || totalBytes > 67108864)
        throw std::runtime_error ("Android native source/header snapshot exceeds its 4096-file/64 MiB limit");
      const auto destination = output / "project" / relative;
      std::filesystem::create_directories (destination.parent_path ());
      std::filesystem::copy_file (source, destination, std::filesystem::copy_options::overwrite_existing);
      std::ifstream file (destination);
      if (!file)
        throw std::runtime_error ("Unable to read captured Android native source");
      std::string line;
      while (std::getline (file, line)) {
        std::smatch match;
        if (!std::regex_search (line, match, localInclude))
          continue;
        const std::filesystem::path include (match[1].str ());
        if (include.is_absolute () || include.string ().find ('\\') != std::string::npos)
          throw std::runtime_error ("Use project-relative quoted includes for Android native local headers");
        for (const auto& candidate : {source.parent_path () / include, root / include}) {
          if (!std::filesystem::exists (candidate))
            continue;
          pending.push_back (candidate);
          break;
        }
      }
    }
    std::vector<std::string> result;
    for (size_t i = 0; i + 1 < sources.size (); i++) {
      const auto source = sources[i].lexically_relative (root).generic_string ();
      const auto name = "native-source-" + std::to_string (i) + ".cc";
      std::ofstream file (output / name, std::ios::trunc);
      // Compile captured bytes, never live project files outside the bound JNI input inventory.
      file << "#include \"project/" << source << "\"\n";
      file.close ();
      if (!file)
        throw std::runtime_error ("Unable to prepare Android native source wrapper");
      result.push_back (name);
    }
    result.push_back (sources.back ().filename ().string ());
    return result;
  }
}  // namespace oro::cli
#endif
