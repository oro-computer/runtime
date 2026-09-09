#ifndef ORO_CLI_TEST_CONFIG_H
#define ORO_CLI_TEST_CONFIG_H

#include "../runtime/config.hh"
#include "../runtime/ini.hh"
#include "../runtime/json.hh"
#include "../runtime/string.hh"

namespace oro::cli {
  inline runtime::String configureMobileTestEntry (
    const runtime::String& source,
    runtime::config::UserConfigFormat format,
    const runtime::String& filename
  ) {
    using namespace runtime;
    if (filename.find(',') != String::npos) {
      throw std::invalid_argument("Mobile test entry paths cannot contain commas");
    }
    auto values = config::parseUserConfigSource(source, format);
    Vector<String> arguments;
    for (const auto& argument : string::split(values["application_argv"], ',')) {
      const auto value = string::trim(argument);
      if (!value.empty() && value != "--test" && !value.starts_with("--test=")) {
        arguments.push_back(value);
      }
    }
    arguments.push_back("--test=" + filename);
    values["application_argv"] = string::join(arguments, ',');

    // Embed the flattened values consumed by the mobile launcher. Serializing
    // them avoids duplicate TOML tables and preserves quoted option values.
    if (format == config::UserConfigFormat::Toml) {
      String output;
      for (const auto& [key, value] : values) {
        output += JSON::stringify(JSON::String(key)) + " = " +
          JSON::stringify(JSON::String(value)) + "\n";
      }
      return output;
    }

    INI::Document document;
    for (const auto& [key, value] : values) {
      document.section("").set(key, value, false, false);
    }
    return INI::serialize(document);
  }
}

#endif
