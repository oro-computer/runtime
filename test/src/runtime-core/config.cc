#include "./tests.hh"
#include "src/runtime/config.hh"
#include "src/cli/test-config.hh"

namespace oro::Tests {
  using runtime::config::Config;

  void config (Harness& t) {
    t.test("Mobile test entry survives embedded configuration serialization", [](auto t) {
      using namespace runtime::config;
      for (const auto format : {UserConfigFormat::Ini, UserConfigFormat::Toml}) {
        const String source = R"CONFIG(
[application]
argv = "--headless,--test=old.js,--allow-exec"
[meta]
title = "A quoted \"title\""
[webview]
service_worker_mode = "native"
)CONFIG";
        auto expected = parseUserConfigSource(source, format);
        expected["application_argv"] = "--headless,--allow-exec,--test=tests/new entry.js";
        const auto embedded = cli::configureMobileTestEntry(source, format, "tests/new entry.js");
        const auto actual = parseUserConfigSource(embedded, format);
        t.assert(actual == expected, "all configuration values survive with the requested test entry");

        const auto replaced = parseUserConfigSource(
          cli::configureMobileTestEntry(embedded, format, "next.js"), format
        );
        t.equals(replaced.at("application_argv"), "--headless,--allow-exec,--test=next.js",
          "a later test entry replaces the previous one");
      }
    });

    t.test("oro::Config::get()", [](auto t) {
      const auto config = Config(R"INI(
      [a]
      key = "value"

      [b]
      key = "value"
      )INI");

      const auto a = config.get("a.key");
      const auto b = config.get("b.key");

      t.equals(a, "value", "a.key == value");
      t.equals(b, "value", "b.key == value");
    });

    t.test("oro::Config::set()", [](auto t) {
      Config config;
      config.set("a.key", "value");
      config.set("b.key", "value");
      t.equals(config.get("a.key"), "value", "a.key == value");
      t.equals(config.get("b.key"), "value", "b.key == value");
    });

    t.test("oro::Config::contains()", [](auto t) {
      Config config;
      config.set("a.key", "value");
      config.set("b.key", "value");
      t.assert(config.contains("a.key"), "contains a.key");
      t.assert(config.contains("b.key"), "contains b.key");
    });

    t.test("oro::Config::query()", [](auto t) {
      const auto config = Config(R"INI(
      [simple]
      key = "value"

      [first.class-a]
      key = "class a"

      [second.class-a]
      key = "class a"

      [third.class-a]
      key = "class a"
      )INI");

      auto simple = config.query("[simple]");
      auto first = config.query("[first]");
      auto second = config.query("[second]");
      auto third = config.query("[third]");

      auto classA = config.query("[.class-a]");

      t.equals(simple.get("simple.key"), "value", "simple.key == value");
      t.equals(first.get("first.class-a.key"), "class a", "first.class-a.key == class a");
      t.equals(second.get("second.class-a.key"), "class a", "second.class-a.key == class a");
      t.equals(third.get("third.class-a.key"), "class a", "third.class-a.key == class a");

      for (const auto& tuple : classA) {
        t.equals(tuple.second, "class a", tuple.first + "contains [.class-a]");
      }
    });

    t.test("oro::Config::erase()", [](auto t) {
      Config config;
      config.set("a.key", "value");
      config.set("b.key", "value");

      config.erase("a.key");
      t.assert(!config.contains("a.key"), "does not contain a.key");

      config.erase("b");
      t.assert(!config.contains("b.key"), "does not contain b.key");
    });

    t.test("oro::Config::clear()", [](auto t) {
      Config config;
      config.set("a.key", "value");
      config.set("b.key", "value");

      config.clear();
      t.assert(!config.contains("a.key"), "does not contain a.key");
      t.assert(!config.contains("b.key"), "does not contain b.key");
    });

    t.test("oro::Config::size()", [](auto t) {
      Config config;
      config.set("a", "value");
      t.equals(config.size(), 1, "config.size() == 1");
      config.set("b", "value");
      t.equals(config.size(), 2, "config.size() == 2");
      config.set("c", "value");
      t.equals(config.size(), 3, "config.size() == 3");
      config.set("c", "value");
      t.equals(config.size(), 3, "config.size() == 3");
      config.erase("c");
      t.equals(config.size(), 2, "config.size() == 2");
    });

    t.test("oro::Config::slice()", [](auto t) {
      const auto config = Config(R"INI(
      [meta]
      title = "my application"
      version = "1.2.3"

      [build]
      script = "build.sh"

      [build.extensions.my-extension]
      source = "extension/"

      [build.extensions.my-other-extension]
      source = "other-extension/"
      )INI");

      const auto meta = config.slice("meta");
      t.equals(meta.get("title"), "my application", "meta.title = 'my application'");
      t.equals(meta.get("version"), "1.2.3", "meta.version = '1.2.3'");

      const auto build = config.slice("build");
      t.equals(build.get("script"), "build.sh", "build.script == 'build.sh'");

      const auto extensions = build.slice("extensions");
      t.equals(extensions.get("my-extension.source"), "extension/", "build.extensions.my-extension.source = 'extension/'");
      t.equals(extensions.get("my-other-extension.source"), "other-extension/", "build.extensions.my-other-extension.source = 'other-extension/'");
    });

    t.test("oro::Config::children()", [](auto t) {
      const auto config = Config(R"INI(
      [0]
      leaf = 0
        [0.1]
        leaf = 01
          [0.1.0]
          leaf = 010
          [0.1.1]
          leaf = 011
          [0.1.2]
          leaf = 012
        [0.2]
        leaf = 02
        [0.3]
        leaf = 03

      [1]
      leaf = 1

      [2]
      leaf = 2
      )INI");

      const auto children = config.children();
      t.equals(children[0].prefix, "0", "children[0].prefix == 0");
      t.equals(children[1].prefix, "1", "children[1].prefix == 1");
      t.equals(children[2].prefix, "2", "children[2].prefix == 2");

      t.equals(children[0].children()[0].prefix, "1", "children[0].children[0].prefix == 1");
      t.equals(children[0].children()[1].prefix, "2", "children[0].children[1].prefix == 2");
      t.equals(children[0].children()[2].prefix, "3", "children[0].children[2].prefix == 3");
    });

    t.test("config::parseUserConfigSource() flattens TOML", [](auto t) {
      const auto source = R"TOML(
[meta]
title = "Example"

[build]
env = ["USER", "PWD"]

[[windows]]
title = "Main"

[[windows]]
title = "Secondary"
      )TOML";

      const auto flattened = oro::runtime::config::parseUserConfigSource(
        source,
        oro::runtime::config::UserConfigFormat::Toml
      );

      t.equals(flattened.at("meta_title"), "Example", "meta_title parsed from TOML");
      t.equals(flattened.at("build_env"), "USER PWD", "arrays collapse to space separated scalars");
      t.equals(flattened.at("windows_0_title"), "Main", "first table array entry flattened");
      t.equals(flattened.at("windows_1_title"), "Secondary", "second table array entry flattened");
    });

    t.test("config::normalizeKeySegment() normalizes non-alphanumeric characters", [](auto t) {
      t.equals(
        oro::runtime::config::normalizeKeySegment("Qwen 2.5/7B-Instruct"),
        "Qwen_2_5_7B_Instruct",
        "dynamic model names normalize to flattened lookup keys"
      );
    });

    t.test("config::key() joins normalized segments with underscores", [](auto t) {
      t.equals(
        oro::runtime::config::key({
          "ai_llm_model",
          "Qwen 2.5/7B-Instruct",
          "pool_prewarm"
        }),
        "ai_llm_model_Qwen_2_5_7B_Instruct_pool_prewarm",
        "config::key joins normalized path segments"
      );
    });

    t.test("config::parseUserConfigSource() flattens INI sections for per-model overrides", [](auto t) {
      const auto source = R"INI(
[ai.llm.model.Qwen 2.5/7B-Instruct]
pool_prewarm = 2
pool_prewarm_size = 4096
      )INI";

      const auto flattened = oro::runtime::config::parseUserConfigSource(
        source,
        oro::runtime::config::UserConfigFormat::Ini
      );

      const auto prewarm = flattened.find(
        "ai_llm_model_Qwen_2_5_7B_Instruct_pool_prewarm"
      );
      t.assert(
        prewarm != flattened.end(),
        "per-model prewarm count uses the normalized flattened key"
      );
      if (prewarm != flattened.end()) {
        t.equals(prewarm->second, "2", "per-model prewarm count is preserved");
      }

      const auto prewarmSize = flattened.find(
        "ai_llm_model_Qwen_2_5_7B_Instruct_pool_prewarm_size"
      );
      t.assert(
        prewarmSize != flattened.end(),
        "per-model prewarm size uses the normalized flattened key"
      );
      if (prewarmSize != flattened.end()) {
        t.equals(prewarmSize->second, "4096", "per-model prewarm size is preserved");
      }
    });
  }
}
